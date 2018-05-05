/**
Copyright (c) 2016 Simon Zolin
*/

#include <FF/aformat/flac-fmt.h>
#include <FF/string.h>
#include <FF/number.h>
#include <FF/data/utf8.h>

#include <flac/FLAC-ff.h>


/** Process block header.
Return enum FLAC_TYPE;  -1 if more data is needed. */
int flac_hdr(const char *data, size_t size, uint *blocksize, uint *islast)
{
	if (size < sizeof(struct flac_hdr))
		return -1;
	const struct flac_hdr *hdr = (void*)data;
	*blocksize = ffint_ntoh24(hdr->size);
	*islast = hdr->last;
	FFDBG_PRINTLN(2, "meta block '%u' (%u)", hdr->type, *blocksize);
	return hdr->type;
}

/** Return the size required for INFO, TAGS and PADDING blocks. */
uint flac_hdrsize(uint tags_size, uint padding)
{
	return FLAC_MINSIZE
		+ sizeof(struct flac_hdr) + tags_size
		+ sizeof(struct flac_hdr) + padding;
}

void flac_sethdr(void *dst, uint type, uint islast, uint size)
{
	struct flac_hdr *hdr = dst;
	hdr->type = type;
	hdr->last = islast;
	ffint_hton24(hdr->size, size);
}

/** Process FLAC header and STREAMINFO block.
Return bytes processed;  0 if more data is needed;  -1 on error. */
int flac_info(const char *data, size_t size, ffflac_info *info, ffpcm *fmt, uint *islast)
{
	uint len;
	const char *end = data + size;
	if (size < FLAC_MINSIZE)
		return 0;

	if (0 != ffs_cmp(data, FLAC_SYNC, FLAC_SYNCLEN))
		return -1;
	data += FLAC_SYNCLEN;

	if (FLAC_TINFO != flac_hdr(data, end - data, &len, islast))
		return -1;
	if (len < sizeof(struct flac_streaminfo))
		return -1;
	data += sizeof(struct flac_hdr);
	if ((uint)(end - data) < len)
		return 0;

	const struct flac_streaminfo *sinfo = (void*)data;
	uint uinfo4 = ffint_ntoh32(sinfo->info);
	fmt->sample_rate = (uinfo4 & 0xfffff000) >> 12;
	fmt->channels = ((uinfo4 & 0x00000e00) >> 9) + 1;
	uint bpsample = ((uinfo4 & 0x000001f0) >> 4) + 1;
	switch (bpsample) {
	case 8:
	case 16:
	case 24:
		fmt->format = bpsample;
		break;
	default:
		return -1;
	}

	info->total_samples = (((uint64)(uinfo4 & 0x0000000f)) << 4) | ffint_ntoh32(sinfo->info + 4);

	info->minblock = ffint_ntoh16(sinfo->minblock);
	info->maxblock = ffint_ntoh16(sinfo->maxblock);
	info->minframe = ffint_ntoh24(sinfo->minframe);
	info->maxframe = ffint_ntoh24(sinfo->maxframe);
	ffmemcpy(info->md5, sinfo->md5, sizeof(sinfo->md5));

	return FLAC_SYNCLEN + sizeof(struct flac_hdr) + len;
}

/** Add sync-word and STREAMINFO block.
Return bytes written;  <0 on error. */
int flac_info_write(char *out, size_t cap, const ffflac_info *info)
{
	struct flac_streaminfo *sinfo;

	FF_ASSERT(cap >= FLAC_MINSIZE);

	ffmemcpy(out, FLAC_SYNC, FLAC_SYNCLEN);

	flac_sethdr(out + FLAC_SYNCLEN, FLAC_TINFO, 0, sizeof(struct flac_streaminfo));

	sinfo = (void*)(out + FLAC_SYNCLEN + sizeof(struct flac_hdr));
	ffint_hton16(sinfo->minblock, info->minblock);
	ffint_hton16(sinfo->maxblock, info->maxblock);
	ffint_hton24(sinfo->minframe, info->minframe);
	ffint_hton24(sinfo->maxframe, info->maxframe);

	sinfo->info[0] = (byte)(info->sample_rate >> 12);
	sinfo->info[1] = (byte)(info->sample_rate >> 4);
	sinfo->info[2] = (byte)((info->sample_rate << 4) & 0xf0);

	sinfo->info[2] |= (byte)(((info->channels - 1) << 1) & 0x0e);

	sinfo->info[2] |= (byte)(((info->bits - 1) >> 4) & 0x01);
	sinfo->info[3] = (byte)(((info->bits - 1) << 4) & 0xf0);

	if ((info->total_samples >> 32) & ~0x0000000f)
		return -FLAC_EBIGHDRSAMPLES;
	// 0x544332211 -> "?5 44 33 22 11"
	sinfo->info[3] |= (byte)((info->total_samples >> 32) & 0x0f);
	ffint_hton32(sinfo->info + 4, (uint)info->total_samples);

	ffmemcpy(sinfo->md5, info->md5, sizeof(sinfo->md5));

	return FLAC_MINSIZE;
}

/** Add padding block of the specified size. */
uint flac_padding_write(char *out, uint padding, uint last)
{
	flac_sethdr(out, FLAC_TPADDING, last, padding);
	ffmem_zero(out + sizeof(struct flac_hdr), padding);
	return sizeof(struct flac_hdr) + padding;
}


struct flac_seekpoint {
	byte sample_number[8];
	byte stream_offset[8];
	byte frame_samples[2];
};

#define FLAC_SEEKPT_PLACEHOLDER  ((uint64)-1)

/** Parse seek table.
Output table always has an entry for sample =0 and a reserved place for the last entry =total_samples.
The last entry can't be filled here because the total size of frames may not be known yet. */
int flac_seektab(const char *data, size_t len, _ffflac_seektab *sktab, uint64 total_samples)
{
	uint i, npts, have0pt = 0;
	const struct flac_seekpoint *st = (void*)data;
	uint64 prev_sample = 0, prev_off = 0;

	npts = len / sizeof(struct flac_seekpoint);

	for (i = 0;  i != npts;  i++) {
		uint64 samp = ffint_ntoh64(st[i].sample_number);
		uint64 off = ffint_ntoh64(st[i].stream_offset);

		if (prev_sample >= samp || prev_off >= off) {
			if (samp == FLAC_SEEKPT_PLACEHOLDER) {
				npts = i; //skip placeholders
				break;
			}
			if (i == 0) {
				have0pt = 1;
				continue;
			}
			return -1; //seek points must be sorted and unique
		}
		prev_sample = samp;
		prev_off = off;
	}

	if (have0pt) {
		st++;
		npts--;
	}

	if (npts == 0)
		return 0; //no useful seek points
	if (prev_sample >= total_samples)
		return -1; //seek point is too big

	ffpcm_seekpt *sp;
	if (NULL == (sp = ffmem_callocT(npts + 2, ffpcm_seekpt)))
		return -1;
	sktab->ptr = sp;
	sp++; //skip zero point

	for (i = 0;  i != npts;  i++) {
		sp->sample = ffint_ntoh64(st[i].sample_number);
		sp->off = ffint_ntoh64(st[i].stream_offset);
		FFDBG_PRINTLN(10, "seekpoint: sample:%U  off:%xU"
			, sp->sample, sp->off);
		sp++;
	}

	sp->sample = total_samples;
	// sp->off

	sktab->len = npts + 2;
	return sktab->len;
}

/** Validate file offset and complete the last seek point. */
int flac_seektab_finish(_ffflac_seektab *sktab, uint64 frames_size)
{
	FF_ASSERT(sktab->len >= 2);
	if (sktab->ptr[sktab->len - 2].off >= frames_size) {
		ffmem_free0(sktab->ptr);
		sktab->len = 0;
		return -1; //seek point is too big
	}

	sktab->ptr[sktab->len - 1].off = frames_size;
	return 0;
}

/**
Return the index of lower-bound seekpoint;  -1 on error. */
int flac_seektab_find(const ffpcm_seekpt *pts, size_t npts, uint64 sample)
{
	size_t n = npts;
	uint i = -1, start = 0;

	while (start != n) {
		i = start + (n - start) / 2;
		if (sample == pts[i].sample)
			return i;
		else if (sample < pts[i].sample)
			n = i--;
		else
			start = i + 1;
	}

	if (i == (uint)-1 || i == npts - 1)
		return -1;

	FF_ASSERT(sample > pts[i].sample && sample < pts[i + 1].sample);
	return i;
}

/* Initialize seek table.
Example for 1 sec interval: [0 1* 2* 3* 3.1] */
int flac_seektab_init(_ffflac_seektab *sktab, uint64 total_samples, uint interval)
{
	uint i, npts;
	uint64 pos = interval;

	npts = total_samples / interval - !(total_samples % interval);
	if ((int)npts <= 0)
		return 0;

	if (NULL == (sktab->ptr = ffmem_allocT(npts, ffpcm_seekpt)))
		return -1;
	sktab->len = npts;

	for (i = 0;  i != npts;  i++) {
		ffpcm_seekpt *sp = &sktab->ptr[i];
		sp->sample = pos;
		pos += interval;
	}

	return npts;
}

/** Return size for the whole seektable. */
uint flac_seektab_size(size_t npts)
{
	return sizeof(struct flac_hdr) + npts * sizeof(struct flac_seekpoint);
}

uint flac_seektab_add(ffpcm_seekpt *pts, size_t npts, uint idx, uint64 nsamps, uint frlen, uint blksize)
{
	for (;  idx != npts;  idx++) {
		ffpcm_seekpt *sp = &pts[idx];
		if (!(sp->sample >= nsamps && sp->sample < nsamps + blksize))
			break;
		sp->sample = nsamps;
		sp->off = frlen;
	}
	return idx;
}

/** Add seek table to the stream.
Move duplicate points to the right. */
uint flac_seektab_write(void *out, size_t cap, const ffpcm_seekpt *pts, size_t npts, uint blksize)
{
	uint i, uniq = 0;
	struct flac_seekpoint *skpt;
	uint len = npts * sizeof(struct flac_seekpoint);
	uint64 last_sample = (uint64)-1;

	FF_ASSERT(npts != 0);
	FF_ASSERT(cap >= sizeof(struct flac_hdr) + len);

	flac_sethdr(out, FLAC_TSEEKTABLE, 1, len);

	skpt = (void*)(out + sizeof(struct flac_hdr));
	for (i = 0;  i != npts;  i++) {
		const ffpcm_seekpt *sp = &pts[i];

		if (sp->sample == last_sample)
			continue;

		ffint_hton64(skpt[uniq].sample_number, sp->sample);
		ffint_hton64(skpt[uniq].stream_offset, sp->off);
		ffint_hton16(skpt[uniq++].frame_samples, blksize);
		last_sample = sp->sample;
	}

	for (;  uniq != npts;  uniq++) {
		ffint_hton64(skpt[uniq].sample_number, FLAC_SEEKPT_PLACEHOLDER);
		ffmem_zero(skpt[uniq].stream_offset, sizeof(skpt->stream_offset) + sizeof(skpt->frame_samples));
	}

	return sizeof(struct flac_hdr) + len;
}


/*
type[4] // 0:Other 3:FrontCover 4:BackCover
mime_len[4]
mime[]
desc_len[4]
desc[]
width[4]
height[4]
bpp[4]
ncolors[4]
data_len[4]
data[]
*/
/** Parse picture block and return picture data. */
int flac_meta_pic(const char *data, size_t len, ffstr *pic)
{
	const char *d = data, *end = data + len;
	if (end - d < 8)
		return FLAC_EPIC;
	d += 4;
	uint mime_len = ffint_ntoh32(d);
	d += 4;
	if (end - d < (int)mime_len)
		return FLAC_EPIC;
	d += mime_len;

	uint desc_len = ffint_ntoh32(d);
	d += 4;
	if (end - d < (int)desc_len + 4*4)
		return FLAC_EPIC;
	d += desc_len + 4*4;

	uint data_len = ffint_ntoh32(d);
	d += 4;
	if (end - d < (int)data_len)
		return FLAC_EPIC;

	FFDBG_PRINTLN(10, "mime:%u  desc:%u  data:%u"
		, mime_len, desc_len, data_len);

	ffstr_set(pic, d, data_len);
	return 0;
}

/** Write picture block data.
@data: if NULL:return the number of bytes needed.
Return -1 on error. */
int flac_pic_write(char *data, size_t cap, const struct flac_picinfo *info, const ffstr *pic, uint islast)
{
	ffstr mime, desc;
	ffstr_setz(&mime, info->mime);
	ffstr_setz(&desc, info->desc);
	size_t n = sizeof(struct flac_hdr) + 4 + 4+mime.len + 4+desc.len + 4*4 + 4+pic->len;

	if (data == NULL)
		return n;

	char *d = data, *end = data + cap;
	if (end - d < (ssize_t)n
		|| n < (mime.len | desc.len | pic->len))
		return -1;

	d += sizeof(struct flac_hdr);

	ffint_hton32(d, 3);  d += 4;

	ffint_hton32(d, mime.len);  d += 4;
	d = ffs_copy(d, end, mime.ptr, mime.len);

	ffint_hton32(d, desc.len);  d += 4;
	d = ffs_copy(d, end, desc.ptr, desc.len);

	ffint_hton32(d, info->width);  d += 4;
	ffint_hton32(d, info->height);  d += 4;
	ffint_hton32(d, info->bpp);  d += 4;
	ffint_hton32(d, 0);  d += 4;

	ffint_hton32(d, pic->len);  d += 4;
	d = ffs_copy(d, end, pic->ptr, pic->len);

	flac_sethdr(data, FLAC_TPIC, islast, (d - data) - sizeof(struct flac_hdr));
	return d - data;
}


static const char* flac_frame_samples(uint *psamples, const char *d, size_t len)
{
	uint samples = *psamples;
	switch (samples) {
	case 0:
		return NULL; //reserved
	case 1:
		samples = 192;
		break;
	case 6:
		if (len < 1)
			return NULL;
		samples = (byte)d[0] + 1;
		d += 1;
		break;
	case 7:
		if (len < 2)
			return NULL;
		samples = ffint_ntoh16(d) + 1;
		d += 2;
		break;
	default:
		if (samples & 0x08)
			samples = 256 << (samples & ~0x08);
		else
			samples = 576 << (samples - 2);
	}
	*psamples = samples;
	return d;
}

static const ushort flac_rates[] = {
	0, 88200/10, 176400/10, 192000/10, 8000/10, 16000/10, 22050/10, 24000/10, 32000/10, 44100/10, 48000/10, 96000/10
};

static const char* flac_frame_rate(uint *prate, const char *d, size_t len)
{
	uint rate = *prate;
	switch (rate) {
	case 0:
		break;
	case 0x0c:
		if (len < 1)
			return NULL;
		rate = (uint)(byte)d[0] * 1000;
		d += 1;
		break;
	case 0x0d:
	case 0x0e:
		if (len < 2)
			return NULL;
		if (rate == 0x0d)
			rate = ffint_ntoh16(d);
		else
			rate = (uint)ffint_ntoh16(d) * 10;
		d += 2;
		break;
	case 0x0f:
		return NULL; //invalid
	default:
		rate = flac_rates[rate] * 10;
	}
	*prate = rate;
	return d;
}

static const byte flac_bps[] = { 0, 8, 12, 0, 16, 20, 24 };

enum FR_HDR {
	FR_SYNC = 14, //=0x3ffe
	FR_RES = 1,
	FR_BLKSIZE_VAR = 1,

	FR_SAMPLES = 4,
	FR_RATE = 4,

	FR_CHAN = 4,
	FR_BPS = 3,
	FR_RES2 = 1,

	// frame_number[1..6] or sample_number[1..7]
	// samples[0..2] //=samples-1
	// rate[0..2]
	// crc8
};

#define FR_CONSTMASK  0xffff0f0f

/**
Return the position after the header;  0 on error. */
uint flac_frame_parse(ffflac_frame *fr, const char *data, size_t len)
{
	const char *d = data, *end = d + len;

	FF_ASSERT(len >= 4);
	uint v = ffint_ntoh32(d);
	uint sync = ffbit_read32(v, 0, FR_SYNC);
	ffbool res = ffbit_read32(v, FR_SYNC, FR_RES);
	ffbool res2 = ffbit_read32(v, 24 + FR_CHAN + FR_BPS, FR_RES2);
	if (!(sync == 0x3ffe && res == 0 && res2 == 0))
		return 0;
	d += 4;

	ffbool bsvar = ffbit_read32(v, FR_SYNC+FR_RES, FR_BLKSIZE_VAR);
	int r;
	if (!bsvar) {
		r = ffutf8_decode1(d, end - d, &fr->num);
		fr->pos = 0;
	} else {
		fr->num = -1;
		r = ffutf8_decode1_64(d, end - d, &fr->pos);
	}
	if (r <= 0)
		return 0;
	d += r;

	fr->samples = ffbit_read32(v, 16, FR_SAMPLES);
	if (NULL == (d = flac_frame_samples(&fr->samples, d, end - d)))
		return 0;

	fr->rate = ffbit_read32(v, 16 + FR_SAMPLES, FR_RATE);
	if (NULL == (d = flac_frame_rate(&fr->rate, d, end - d)))
		return 0;

	fr->channels = ffbit_read32(v, 24, FR_CHAN);
	if (fr->channels >= 0x0b)
		return 0; //reserved
	else if (fr->channels & 0x08)
		fr->channels = 2;
	else
		fr->channels = fr->channels + 1;

	uint bps = ffbit_read32(v, 24 + FR_CHAN, FR_BPS);
	if ((bps & 3) == 3)
		return 0; //reserved
	fr->bps = flac_bps[bps];

	if ((byte)*d != flac_crc8(data, d - data))
		return 0; //header CRC mismatch
	d++;
	return d - data;
}

/** Find a valid frame header.
Return header position;  <0 if not found. */
ssize_t flac_frame_find(const char *data, size_t len, ffflac_frame *fr, byte hdr[4])
{
	const char *d = data, *end = d + len;
	uint h = ffint_ntoh32(hdr) & FR_CONSTMASK;

	while (d != end) {
		if ((byte)d[0] != 0xff && NULL == (d = ffs_findc(d, end - d, 0xff)))
			break;

		if ((end - d) >= (ssize_t)FLAC_MINFRAMEHDR
			&& (h == 0 || (ffint_ntoh32(d) & FR_CONSTMASK) == h)) {

			uint r = flac_frame_parse(fr, d, end - d);
			if (r != 0)
				return d - data;
		}

		d++;
	}

	return -1;
}
