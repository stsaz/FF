/**
Copyright (c) 2016 Simon Zolin
*/

#include <FF/audio/flac-fmt.h>
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

/** Return the size required for the whole header. */
uint flac_hdrsize(uint tags_size, uint padding, size_t seekpts)
{
	return FLAC_MINSIZE
		+ sizeof(struct flac_hdr) + tags_size
		+ sizeof(struct flac_hdr) + padding
		+ flac_seektab_size(seekpts);
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


const char* flac_frame_samples(uint *psamples, const char *d, size_t len)
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

static const uint flac_rates[] = {
	0, 88200, 176400, 192000, 8000, 16000, 22050, 24000, 32000, 44100, 48000, 96000
};

const char* flac_frame_rate(uint *prate, const char *d, size_t len)
{
	uint rate = *prate;
	switch (rate) {
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
		rate = flac_rates[rate];
	}
	*prate = rate;
	return d;
}

static const byte flac_bps[] = { 0, 8, 12, 0, 16, 20, 24, 0 };

/**
Return the position after the header;  0 on error. */
uint flac_frame_parse(ffflac_frame *fr, const char *data, size_t len)
{
	const char *d = data, *end = d + len;
	const struct flac_frame *f = (void*)d;

	if (!(f->sync[0] == 0xff && f->sync[1] == 0xf8 && f->reserved == 0))
		return 0;
	d += sizeof(struct flac_frame);

	int r = ffutf8_decode1(d, end - d, &fr->num);
	if (r <= 0)
		return 0;
	d += r;

	fr->samples = f->samples;
	if (NULL == (d = flac_frame_samples(&fr->samples, d, end - d)))
		return 0;

	fr->rate = f->rate;
	if (NULL == (d = flac_frame_rate(&fr->rate, d, end - d)))
		return 0;

	fr->channels = f->channels;
	if (fr->channels >= 0x0b)
		return 0; //reserved
	else if (fr->channels & 0x08)
		fr->channels = 2;
	else
		fr->channels = fr->channels + 1;

	if ((f->bps & 3) == 3)
		return 0; //reserved
	fr->bps = flac_bps[f->bps];

	if ((byte)*d != flac_crc8(data, d - data))
		return 0; //header CRC mismatch
	d++;
	return d - data;
}

/** Find a valid frame header.
Return header size, *len is set to the header position;  0 if not found. */
uint flac_frame_find(const char *data, size_t *len, ffflac_frame *fr)
{
	const char *d = data, *end = d + *len;
	ffflac_frame frame;

	while (d != end) {
		if ((byte)d[0] != 0xff && NULL == (d = ffs_findc(d, end - d, 0xff)))
			break;

		if ((end - d) >= (ssize_t)sizeof(struct flac_frame)) {
			uint r = flac_frame_parse(&frame, d, end - d);
			if (r != 0 && (fr->channels == 0
				|| (fr->channels == frame.channels
					&& fr->rate == frame.rate
					&& fr->bps == frame.bps))) {
				*fr = frame;
				*len = d - data;
				return r;
			}
		}

		d++;
	}

	return 0;
}
