/**
Copyright (c) 2015 Simon Zolin
*/

#include <FF/audio/flac.h>
#include <FF/data/utf8.h>
#include <FF/number.h>
#include <FF/crc.h>
#include <FFOS/error.h>


enum FLAC_TYPE {
	FLAC_TINFO,
	FLAC_TPADDING,
	FLAC_TSEEKTABLE = 3,
	FLAC_TTAGS,
};

struct flac_hdr {
	byte type :7 //enum FLAC_TYPE
		, last :1;
	byte size[3];
};

struct flac_streaminfo {
	byte minblock[2];
	byte maxblock[2];
	byte minframe[3];
	byte maxframe[3];
	// byte rate :20;
	// byte channels :3; //=channels-1
	// byte bps :5; //=bps-1
	// byte total_samples :36;
	byte info[8];
	byte md5[16];
};

struct flac_seekpoint {
	byte sample_number[8];
	byte stream_offset[8];
	byte frame_samples[2];
};

#define FLAC_SEEKPT_PLACEHOLDER  ((uint64)-1)

struct flac_frame {
	byte sync[2]; //FF F8
	byte rate :4
		, samples :4;
	byte reserved :1
		, bps :3
		, channels :4;
	//byte number[1..6];
	//byte samples[0..2]; //=samples-1
	//byte rate[0..2];
	//byte crc8;
};

#define FLAC_SYNC  "fLaC"

enum {
	FLAC_SYNCLEN = FFSLEN(FLAC_SYNC),
	FLAC_MINSIZE = FLAC_SYNCLEN + sizeof(struct flac_hdr) + sizeof(struct flac_streaminfo),
	FLAC_MAXFRAMEHDR = sizeof(struct flac_frame) + 6 + 2 + 2 + 1,
	FLAC_MAXFRAME = 1 * 1024 * 1024,
	MAX_META = 1 * 1024 * 1024,
	MAX_NOSYNC = 1 * 1024 * 1024,
};

enum FLAC_E {
	FLAC_EUKN
	, FLAC_ESYS
	, FLAC_EINIT
	, FLAC_EDEC
	, FLAC_ESTATE
	,
	FLAC_EINITENC,
	FLAC_EENC_STATE,

	FLAC_EFMT,
	FLAC_EHDR,
	FLAC_EBIGMETA,
	FLAC_ETAG,
	FLAC_ESEEKTAB,
	FLAC_ESEEK,
	FLAC_ESYNC,
	FLAC_ENOSYNC,
	FLAC_EHDRSAMPLES,
};


static int flac_hdr(const char *data, size_t size, uint *blocksize, uint *islast);
static void flac_sethdr(void *dst, uint type, uint islast, uint size);
static int flac_info(const char *data, size_t size, ffflac_info *info, ffpcm *fmt, uint *islast);
static int flac_info_write(char *out, size_t cap, const ffflac_info *info);
static uint flac_padding_write(char *out, size_t cap, uint padding, uint last);

static int flac_seektab(const char *data, size_t len, _ffflac_seektab *sktab, uint64 total_samples);
static int flac_seektab_finish(_ffflac_seektab *sktab, uint64 frames_size);
static int flac_seektab_find(const ffpcm_seekpt *pts, size_t npts, uint64 sample);
static int flac_seektab_init(_ffflac_seektab *sktab, uint64 total_samples, uint interval);
static uint flac_seektab_write(void *out, size_t cap, const ffpcm_seekpt *pts, size_t npts, uint blksize);

static uint flac_frame_parse(ffflac_frame *fr, const char *data, size_t len);
static uint flac_frame_find(const char *d, size_t *len, ffflac_frame *fr);


static int _ffflac_meta(ffflac *f);
static int _ffflac_init(ffflac *f);
static int _ffflac_seek(ffflac *f);
static int _ffflac_findhdr(ffflac *f, ffflac_frame *fr);
static int _ffflac_getframe(ffflac *f, ffstr *sframe);


/** Process block header.
Return enum FLAC_TYPE;  -1 if more data is needed. */
static int flac_hdr(const char *data, size_t size, uint *blocksize, uint *islast)
{
	if (size < sizeof(struct flac_hdr))
		return -1;
	const struct flac_hdr *hdr = (void*)data;
	*blocksize = ffint_ntoh24(hdr->size);
	*islast = hdr->last;
	FFDBG_PRINT(2, "%s(): meta block '%u' (%u)\n", FF_FUNC, hdr->type, *blocksize);
	return hdr->type;
}

static FFINL void flac_sethdr(void *dst, uint type, uint islast, uint size)
{
	struct flac_hdr *hdr = dst;
	hdr->type = type;
	hdr->last = islast;
	ffint_hton24(hdr->size, size);
}

/** Process FLAC header and STREAMINFO block.
Return bytes processed;  0 if more data is needed;  -1 on error. */
static int flac_info(const char *data, size_t size, ffflac_info *info, ffpcm *fmt, uint *islast)
{
	uint len;
	const char *end = data + size;
	if (size < FLAC_MINSIZE)
		return 0;

	if (0 != ffs_cmp(data, FLAC_SYNC, FLAC_SYNCLEN))
		return -1;
	data += FLAC_SYNCLEN;

	if (FLAC_TINFO != flac_hdr(data, end - data, &len, islast))
		return 0;
	if (len < sizeof(struct flac_streaminfo))
		return -1;
	data += sizeof(struct flac_hdr);
	if (end - data < len)
		return 0;

	const struct flac_streaminfo *sinfo = (void*)data;
	uint uinfo4 = ffint_ntoh32(sinfo->info);
	fmt->sample_rate = (uinfo4 & 0xfffff000) >> 12;
	fmt->channels = ((uinfo4 & 0x00000e00) >> 9) + 1;
	uint bpsample = ((uinfo4 & 0x000001f0) >> 4) + 1;
	switch (bpsample) {
	case 16:
	case 24:
	case 32:
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
Return bytes written;  0 on error. */
static int flac_info_write(char *out, size_t cap, const ffflac_info *info)
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
		return 0; //too large value
	// 0x544332211 -> "?5 44 33 22 11"
	sinfo->info[3] |= (byte)((info->total_samples >> 32) & 0x0f);
	ffint_hton32(sinfo->info + 4, (uint)info->total_samples);

	ffmemcpy(sinfo->md5, info->md5, sizeof(sinfo->md5));

	return FLAC_MINSIZE;
}

/** Add padding block of the specified size. */
static uint flac_padding_write(char *out, size_t cap, uint padding, uint last)
{
	FF_ASSERT(cap >= sizeof(struct flac_hdr) + padding);

	flac_sethdr(out, FLAC_TPADDING, last, padding);
	ffmem_zero(out + sizeof(struct flac_hdr), padding);
	return sizeof(struct flac_hdr) + padding;
}

/** Parse seek table.
Output table always has an entry for sample =0 and a reserved place for the last entry =total_samples.
The last entry can't be filled here because the total size of frames may not be known yet. */
static int flac_seektab(const char *data, size_t len, _ffflac_seektab *sktab, uint64 total_samples)
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
	if (NULL == (sp = ffmem_tcalloc(ffpcm_seekpt, npts + 2)))
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
static int flac_seektab_finish(_ffflac_seektab *sktab, uint64 frames_size)
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
static int flac_seektab_find(const ffpcm_seekpt *pts, size_t npts, uint64 sample)
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
static int flac_seektab_init(_ffflac_seektab *sktab, uint64 total_samples, uint interval)
{
	uint i, npts;
	uint64 pos = interval;

	npts = total_samples / interval - !(total_samples % interval);
	if ((int)npts <= 0)
		return 0;

	if (NULL == (sktab->ptr = ffmem_talloc(ffpcm_seekpt, npts)))
		return -1;
	sktab->len = npts;

	for (i = 0;  i != npts;  i++) {
		ffpcm_seekpt *sp = &sktab->ptr[i];
		sp->sample = pos;
		pos += interval;
	}

	return npts;
}

/** Add seek table to the stream.
Move duplicate points to the right. */
static uint flac_seektab_write(void *out, size_t cap, const ffpcm_seekpt *pts, size_t npts, uint blksize)
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

static const uint flac_rates[] = {
	0, 88200, 176400, 192000, 8000, 16000, 22050, 24000, 32000, 44100, 48000, 96000
};

static const char* flac_frame_rate(uint *prate, const char *d, size_t len)
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
static uint flac_frame_parse(ffflac_frame *fr, const char *data, size_t len)
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

	if ((byte)*d != FLAC__crc8((void*)data, d - data))
		return 0; //header CRC mismatch
	d++;
	return d - data;
}

/** Find a valid frame header.
Return header size, *len is set to the header position;  0 if not found. */
static uint flac_frame_find(const char *data, size_t *len, ffflac_frame *fr)
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


void ffflac_init(ffflac *f)
{
	ffmem_tzero(f);
}

static const char *const flac_errs[] = {
	"unsupported PCM format",
	"invalid header",
	"too large meta",
	"bad tags",
	"bad seek table",
	"seek error",
	"unrecognized data before frame header",
	"can't find sync",
	"invalid total samples value in FLAC header",
};

const char* ffflac_errstr(ffflac *f)
{
	switch (f->errtype) {
	case FLAC_ESYS:
		return fferr_strp(fferr_last());

	case FLAC_EINIT:
		return FLAC__StreamDecoderInitStatusString[f->err];

	case FLAC_EDEC:
		return FLAC__StreamDecoderErrorStatusString[f->err];

	case FLAC_ESTATE:
		return FLAC__StreamDecoderStateString[f->err];

	case FLAC_EINITENC:
		return FLAC__StreamEncoderInitStatusString[f->err];

	case FLAC_EENC_STATE:
		return FLAC__StreamEncoderStateString[f->err];
	}

	if (f->errtype >= FLAC_EFMT) {
		uint e = f->errtype - FLAC_EFMT;
		FF_ASSERT(e < FFCNT(flac_errs));
		return flac_errs[e];
	}

	return "unknown error";
}

void ffflac_close(ffflac *f)
{
	ffarr_free(&f->buf);
	ffmem_safefree(f->sktab.ptr);
	if (f->dec != NULL)
		FLAC__stream_decoder_delete(f->dec);
}

enum {
	I_INFO, I_META, I_SKIPMETA, I_TAG, I_TAG_PARSE, I_SEEKTBL, I_METALAST,
	I_INIT, I_DATA, I_FRHDR, I_FROK, I_SEEK, I_FIN
};

int ffflac_open(ffflac *f)
{
	return 0;
}

void ffflac_seek(ffflac *f, uint64 sample)
{
	if (f->st == I_INIT) {
		f->seeksample = sample;
		return;
	}

	int i;
	if (0 > (i = flac_seektab_find(f->sktab.ptr, f->sktab.len, sample)))
		return;
	f->seekpt[0] = f->sktab.ptr[i];
	f->seekpt[1] = f->sktab.ptr[i + 1];
	f->skoff = 0;
	f->seeksample = sample;
	f->st = I_SEEK;
}

/** Process header of meta block. */
static int _ffflac_meta(ffflac *f)
{
	uint islast;
	int r;

	r = ffarr_append_until(&f->buf, f->data, f->datalen, sizeof(struct flac_hdr));
	if (r == 0)
		return FFFLAC_RMORE;
	else if (r == -1) {
		f->errtype = FLAC_ESYS;
		return FFFLAC_RERR;
	}
	FFARR_SHIFT(f->data, f->datalen, r);
	f->off += sizeof(struct flac_hdr);
	f->buf.len = 0;

	r = flac_hdr(f->buf.ptr, sizeof(struct flac_hdr), &f->blksize, &islast);

	if (islast) {
		f->hdrlast = 1;
		f->st = I_METALAST;
	}

	if (f->off + f->blksize > MAX_META) {
		f->errtype = FLAC_EBIGMETA;
		return FFFLAC_RERR;
	}

	switch (r) {
	case FLAC_TTAGS:
		f->st = I_TAG;
		return 0;

	case FLAC_TSEEKTABLE:
		if (f->sktab.len != 0)
			break; //take only the first seek table

		if (f->total_size == 0 || f->info.total_samples == 0
			|| f->info.minblock == 0 || f->info.minblock != f->info.maxblock)
			break; //seeking not supported

		f->st = I_SEEKTBL;
		return 0;
	}

	f->off += f->blksize;
	return FFFLAC_RSEEK;
}

static int _ffflac_init(ffflac *f)
{
	int r;
	FLAC__StreamMetadata_StreamInfo si = {0};
	si.min_blocksize = f->info.minblock;
	si.max_blocksize = f->info.maxblock;
	si.sample_rate = f->fmt.sample_rate;
	si.channels = f->fmt.channels;
	si.bits_per_sample = ffpcm_bits(f->fmt.format);
	if (0 != (r = FLAC__decode_init(&f->dec, &si))) {
		f->errtype = FLAC_EINIT;
		f->err = r;
		return FFFLAC_RERR;
	}

	if (f->total_size != 0) {
		if (f->sktab.len == 0 && f->info.total_samples != 0) {
			if (NULL == (f->sktab.ptr = ffmem_tcalloc(ffpcm_seekpt, 2))) {
				f->errtype = FLAC_ESYS;
				return FFFLAC_RERR;
			}
			f->sktab.ptr[1].sample = f->info.total_samples;
			f->sktab.len = 2;
		}

		if (f->sktab.len != 0)
			flac_seektab_finish(&f->sktab, f->total_size - f->framesoff);
	}

	return 0;
}

static int _ffflac_seek(ffflac *f)
{
	int r;
	struct ffpcm_seek sk;

	r = _ffflac_findhdr(f, &f->frame);
	if (r != 0 && !(r == FFFLAC_RWARN && f->errtype == FLAC_ESYNC))
		return r;

	sk.target = f->seeksample;
	sk.off = f->off - f->framesoff - (f->buf.len - f->bufoff);
	sk.lastoff = f->skoff;
	sk.pt = f->seekpt;
	sk.fr_index = f->frame.num * f->frame.samples;
	sk.fr_samples = f->frame.samples;
	sk.avg_fr_samples = f->info.minblock;
	sk.fr_size = sizeof(struct flac_frame); //note: frame body size is not known, this may slightly reduce efficiency of ffpcm_seek()
	sk.flags = FFPCM_SEEK_ALLOW_BINSCH;

	r = ffpcm_seek(&sk);
	if (r == 1) {
		f->skoff = sk.off;
		f->off = f->framesoff + sk.off;
		f->datalen = 0;
		f->buf.len = 0;
		f->bufoff = 0;
		return FFFLAC_RSEEK;

	} else if (r == -1) {
		f->errtype = FLAC_ESEEK;
		return FFFLAC_RERR;
	}

	return 0;
}

/** Find frame header.
Return 0 on success. */
static int _ffflac_findhdr(ffflac *f, ffflac_frame *fr)
{
	size_t hdroff;
	uint hdrlen;

	fr->channels = f->fmt.channels;
	fr->rate = f->fmt.sample_rate;
	fr->bps = ffpcm_bits(f->fmt.format);

	if (NULL == ffarr_append(&f->buf, f->data, f->datalen)) {
		f->errtype = FLAC_ESYS;
		return FFFLAC_RERR;
	}

	hdroff = f->buf.len - f->bufoff;
	hdrlen = flac_frame_find(f->buf.ptr + f->bufoff, &hdroff, fr);

	if (hdrlen == 0) {
		if (f->fin)
			return FFFLAC_RDONE;

		f->bytes_skipped += f->datalen;
		if (f->bytes_skipped > MAX_NOSYNC) {
			f->errtype = FLAC_ENOSYNC;
			return FFFLAC_RERR;
		}

		_ffarr_rmleft(&f->buf, f->bufoff, sizeof(char));
		f->bufoff = 0;
		f->off += f->datalen;
		return FFFLAC_RMORE;
	}

	if (f->bytes_skipped != 0)
		f->bytes_skipped = 0;

	f->off += f->datalen;
	FFARR_SHIFT(f->data, f->datalen, f->datalen);

	if (hdroff != 0) {
		f->bufoff += hdroff;
		f->errtype = FLAC_ESYNC;
		return FFFLAC_RWARN;
	}
	return 0;
}

/** Get frame header and body.
Frame length becomes known only after the next frame is found. */
static int _ffflac_getframe(ffflac *f, ffstr *sframe)
{
	ffflac_frame fr;
	size_t frlen;
	uint hdrlen;

	fr.channels = f->fmt.channels;
	fr.rate = f->fmt.sample_rate;
	fr.bps = ffpcm_bits(f->fmt.format);

	if (NULL == ffarr_append(&f->buf, f->data, f->datalen)) {
		f->errtype = FLAC_ESYS;
		return FFFLAC_RERR;
	}

	frlen = f->buf.len - f->bufoff - sizeof(struct flac_frame);
	hdrlen = flac_frame_find(f->buf.ptr + f->bufoff + sizeof(struct flac_frame), &frlen, &fr);
	frlen += sizeof(struct flac_frame);

	if (hdrlen == 0 && !f->fin) {

		f->bytes_skipped += f->datalen;
		if (f->bytes_skipped > FLAC_MAXFRAME) {
			f->errtype = FLAC_ENOSYNC;
			return FFFLAC_RERR;
		}

		_ffarr_rmleft(&f->buf, f->bufoff, sizeof(char));
		f->bufoff = 0;
		f->off += f->datalen;
		return FFFLAC_RMORE;
	}

	if (f->bytes_skipped != 0)
		f->bytes_skipped = 0;

	ffstr_set(sframe, f->buf.ptr + f->bufoff, frlen);
	f->bufoff += frlen;
	f->off += f->datalen;
	FFARR_SHIFT(f->data, f->datalen, f->datalen);
	return 0;
}

int ffflac_decode(ffflac *f)
{
	int r;
	uint isrc, lastblk, i, ich;
	const int **out;
	ffstr sframe;

	for (;;) {
	switch (f->st) {

	case I_INFO:
		r = ffarr_append_until(&f->buf, f->data, f->datalen, FLAC_MINSIZE);
		if (r == 0)
			return FFFLAC_RMORE;
		else if (r == -1) {
			f->errtype = FLAC_ESYS;
			return FFFLAC_RERR;
		}
		FFARR_SHIFT(f->data, f->datalen, r);

		r = flac_info(f->buf.ptr, f->buf.len, &f->info, &f->fmt, &lastblk);
		if (r <= 0) {
			f->errtype = FLAC_EHDR;
			return FFFLAC_RERR;
		}
		f->buf.len = 0;
		f->off += r;

		f->st = lastblk ? I_METALAST : I_META;
		return FFFLAC_RHDR;

	case I_SKIPMETA:
		f->st = (f->hdrlast) ? I_METALAST : I_META;
		f->off += f->blksize;
		return FFFLAC_RSEEK;

	case I_META:
		r = _ffflac_meta(f);
		if (r != 0)
			return r;
		break;

	case I_TAG:
		r = ffarr_append_until(&f->buf, f->data, f->datalen, f->blksize);
		if (r == 0)
			return FFFLAC_RMORE;
		else if (r == -1) {
			f->errtype = FLAC_ESYS;
			return FFFLAC_RERR;
		}

		FFARR_SHIFT(f->data, f->datalen, r);
		f->buf.len = 0;
		f->vtag.data = f->buf.ptr;
		f->vtag.datalen = f->blksize;
		f->st = I_TAG_PARSE;
		// break

	case I_TAG_PARSE:
		r = ffvorbtag_parse(&f->vtag);

		if (r == FFVORBTAG_OK)
			return FFFLAC_RTAG;

		else if (r == FFVORBTAG_ERR) {
			f->st = I_SKIPMETA;
			f->errtype = FLAC_ETAG;
			return FFFLAC_RWARN;
		}

		//FFVORBTAG_DONE
		f->off += f->blksize;
		f->st = (f->hdrlast) ? I_METALAST : I_META;
		break;

	case I_SEEKTBL:
		r = ffarr_append_until(&f->buf, f->data, f->datalen, f->blksize);
		if (r == 0)
			return FFFLAC_RMORE;
		else if (r == -1) {
			f->errtype = FLAC_ESYS;
			return FFFLAC_RERR;
		}

		FFARR_SHIFT(f->data, f->datalen, r);
		f->buf.len = 0;
		if (0 > flac_seektab(f->buf.ptr, f->blksize, &f->sktab, f->info.total_samples)) {
			f->st = I_SKIPMETA;
			f->errtype = FLAC_ESEEKTAB;
			return FFFLAC_RWARN;
		}
		f->off += f->blksize;
		f->st = (f->hdrlast) ? I_METALAST : I_META;
		break;

	case I_METALAST:
		f->st = I_INIT;
		f->framesoff = (uint)f->off;
		f->buf.len = 0;
		return FFFLAC_RHDRFIN;

	case I_INIT:
		if (0 != (r = _ffflac_init(f)))
			return r;
		f->st = I_FRHDR;
		if (f->seeksample != 0)
			ffflac_seek(f, f->seeksample);
		break;

	case I_FROK:
		f->st = I_FRHDR;
		// break

	case I_FRHDR:
		r = _ffflac_findhdr(f, &f->frame);
		if (r != 0) {
			if (r == FFFLAC_RWARN)
				f->st = I_DATA;
			else if (r == FFFLAC_RDONE) {
				f->st = I_FIN;
				continue;
			}
			return r;
		}

		f->st = I_DATA;
		// break

	case I_DATA:
		if (0 != (r = _ffflac_getframe(f, &sframe)))
			return r;

		FFDBG_PRINT(10, "%s(): frame #%u: size:%L, samples:%u\n"
			, FF_FUNC, f->frame.num, sframe.len, f->frame.samples);

		r = FLAC__decode(f->dec, sframe.ptr, sframe.len, &out);
		if (r != 0) {
			if (r < 0) {
				f->errtype = FLAC_ESTATE;
				f->err = -r;
			} else {
				f->errtype = FLAC_EDEC;
				f->err = r;
			}
			f->st = I_FROK;
			return FFFLAC_RWARN;
		}

		f->pcm = (void**)f->out32;
		f->pcmlen = f->frame.samples;
		f->frsample = f->frame.num * f->info.minblock;
		isrc = 0;
		if (f->seek_ok) {
			f->seek_ok = 0;
			FF_ASSERT(f->seeksample >= f->frsample);
			isrc = f->seeksample - f->frsample;
			f->pcmlen -= isrc;
			f->frsample = f->seeksample;
			f->seeksample = 0;
		}

		for (ich = 0;  ich != f->fmt.channels;  ich++) {
			f->out32[ich] = out[ich];
		}

		if (f->fmt.format == FFPCM_16LE) {
			//in-place conversion: int[] -> short[]
			for (ich = 0;  ich != f->fmt.channels;  ich++) {
				uint j = isrc;
				for (i = 0;  i != f->pcmlen;  i++) {
					f->out16[ich][i] = (short)f->out32[ich][j++];
				}
			}

		} else if (f->fmt.format == FFPCM_24) {
			// 24/32 -> 24/24
			char **out8 = (char**)f->out32;
			for (ich = 0;  ich != f->fmt.channels;  ich++) {
				uint j = isrc;
				for (i = 0;  i != f->pcmlen;  i++) {
					ffmemcpy(&out8[ich][i * 3], &f->out32[ich][j++], 3);
				}
			}
		}

		f->pcmlen *= ffpcm_size1(&f->fmt);
		f->st = I_FROK;
		return FFFLAC_RDATA;

	case I_SEEK:
		if (0 != (r = _ffflac_seek(f)))
			return r;
		f->seek_ok = 1;
		f->st = I_DATA;
		break;


	case I_FIN:
		if (f->datalen != 0) {
			f->st = I_FIN + 1;
			f->errtype = FLAC_ESYNC;
			return FFFLAC_RWARN;
		}

	case I_FIN + 1:
		if (f->info.total_samples != 0 && f->info.total_samples != f->frsample + f->frame.samples) {
			f->st = I_FIN + 2;
			f->errtype = FLAC_EHDRSAMPLES;
			return FFFLAC_RWARN;
		}

	case I_FIN + 2:
		return FFFLAC_RDONE;
	}
	}
	//unreachable
}


const char* ffflac_enc_errstr(ffflac_enc *f)
{
	ffflac fl;
	fl.errtype = f->errtype;
	fl.err = f->err;
	return ffflac_errstr(&fl);
}

int ffflac_addtag(ffflac_enc *f, const char *name, const char *val, size_t vallen)
{
	if (0 != ffvorbtag_add(&f->vtag, name, val, vallen)) {
		f->errtype = FLAC_EHDR;
		return FFFLAC_RERR;
	}
	return 0;
}

enum ENC_STATE {
	ENC_HDR, ENC_FRAMES,
	ENC_SEEK0, ENC_INFO_WRITE, ENC_SEEKTAB_SEEK, ENC_SEEKTAB_WRITE,
};

void ffflac_enc_init(ffflac_enc *f)
{
	ffmem_tzero(f);
	f->min_meta = 1000;
	f->seektable_int = (uint)-1;
	f->level = 5;
}

void ffflac_enc_close(ffflac_enc *f)
{
	ffvorbtag_destroy(&f->vtag);
	ffarr_free(&f->outbuf);
	ffmem_safefree(f->sktab.ptr);
	FLAC__stream_encoder_delete(f->enc);
}

int ffflac_create(ffflac_enc *f, const ffpcm *pcm)
{
	int r;
	if (pcm->format != FFPCM_16LE) {
		f->errtype = FLAC_EFMT;
		return -1;
	}

	FLAC__StreamEncoderConf conf;
	conf.bps = ffpcm_bits(pcm->format);
	conf.channels = pcm->channels;
	conf.rate = pcm->sample_rate;
	conf.level = f->level;
	conf.nomd5 = !!(f->opts & FFFLAC_ENC_NOMD5);

	if (0 != (r = FLAC__encode_init(&f->enc, &conf))) {
		if (r > 0) {
			f->err = r;
			f->errtype = FLAC_EINITENC;
		} else {
			f->err = -r;
			f->errtype = FLAC_EENC_STATE;
		}
		return FFFLAC_RERR;
	}

	if (f->seektable_int != 0 && f->total_samples != 0) {
		uint interval = (f->seektable_int == (uint)-1) ? pcm->sample_rate : f->seektable_int;
		if (0 > flac_seektab_init(&f->sktab, f->total_samples, interval)) {
			f->errtype = FLAC_ESYS;
			return FFFLAC_RERR;
		}
	}

	if (NULL == ffarr_alloc(&f->outbuf, FLAC_MINSIZE
		+ sizeof(struct flac_hdr) * 2 + ffmax(4096, f->min_meta)
		+ sizeof(struct flac_hdr) + f->sktab.len * sizeof(struct flac_seekpoint))) {
		f->errtype = FLAC_ESYS;
		return FFFLAC_RERR;
	}

	f->info.minblock = f->enc->info->min_blocksize;
	f->info.maxblock = f->enc->info->max_blocksize;
	f->info.channels = pcm->channels;
	f->info.sample_rate = pcm->sample_rate;
	f->info.bits = ffpcm_bits(pcm->format);
	if (0 == (r = flac_info_write(f->outbuf.ptr, f->outbuf.cap, &f->info))) {
		f->errtype = FLAC_EHDR;
		return FFFLAC_RERR;
	}
	f->outbuf.len += r;

	ffarr_acq(&f->vtag.out, &f->outbuf);
	f->vtag.out.len += sizeof(struct flac_hdr);
	if (0 != ffvorbtag_add(&f->vtag, NULL, FLAC__VENDOR_STRING, ffsz_len(FLAC__VENDOR_STRING))) {
		f->errtype = FLAC_EHDR;
		return FFFLAC_RERR;
	}

	return 0;
}

static int _ffflac_enc_hdr(ffflac_enc *f)
{
	ffvorbtag_fin(&f->vtag);
	uint tagoff = f->outbuf.len;
	uint taglen = f->vtag.out.len - sizeof(struct flac_hdr) - tagoff;
	ffarr_acq(&f->outbuf, &f->vtag.out);
	uint have_padding = (f->min_meta > taglen);

	flac_sethdr(f->outbuf.ptr + tagoff, FLAC_TTAGS, !have_padding && (f->sktab.len == 0), taglen);

	if (have_padding)
		f->outbuf.len += flac_padding_write(ffarr_end(&f->outbuf), f->outbuf.cap, f->min_meta - taglen, f->sktab.len == 0);

	if (f->sktab.len != 0) {
		// write header with empty body
		f->seektab_off = f->outbuf.len;
		uint len = sizeof(struct flac_hdr) + f->sktab.len * sizeof(struct flac_seekpoint);
		FF_ASSERT(ffarr_unused(&f->outbuf) >= len);
		flac_sethdr(ffarr_end(&f->outbuf), FLAC_TSEEKTABLE, 1, len - sizeof(struct flac_hdr));
		ffmem_zero(ffarr_end(&f->outbuf) + sizeof(struct flac_hdr), len - sizeof(struct flac_hdr));
		f->outbuf.len += len;
	}

	f->metalen = f->outbuf.len;
	return 0;
}

int ffflac_encode(ffflac_enc *f)
{
	uint samples, sampsize;
	int r;

	switch (f->state) {

	case ENC_HDR:
		if (0 != (r = _ffflac_enc_hdr(f)))
			return r;

		f->state = ENC_FRAMES;
		f->data = (void*)f->outbuf.ptr;
		f->datalen = f->outbuf.len;
		f->outbuf.len = 0;
		return FFFLAC_RDATA;

	case ENC_FRAMES:
		break;

	case ENC_SEEK0:
		f->state = ENC_INFO_WRITE;
		f->seekoff = 0;
		return FFFLAC_RSEEK;

	case ENC_INFO_WRITE:
		f->info.minblock = f->enc->info->min_blocksize;
		f->info.maxblock = f->enc->info->max_blocksize;
		f->info.minframe = f->enc->info->min_framesize;
		f->info.maxframe = f->enc->info->max_framesize;
		f->info.total_samples = f->nsamps;
		ffmemcpy(f->info.md5, f->enc->info->md5sum, sizeof(f->info.md5));
		r = flac_info_write(f->outbuf.ptr, f->outbuf.cap, &f->info);
		if (r < 0) {
			f->errtype = -r;
			return FFFLAC_RERR;
		}
		f->data = (void*)f->outbuf.ptr;
		f->datalen = r;
		if (f->sktab.len == 0)
			return FFFLAC_RDONE;
		f->state = ENC_SEEKTAB_SEEK;
		return FFFLAC_RDATA;

	case ENC_SEEKTAB_SEEK:
		f->state = ENC_SEEKTAB_WRITE;
		f->seekoff = f->seektab_off;
		return FFFLAC_RSEEK;

	case ENC_SEEKTAB_WRITE:
		r = flac_seektab_write(f->outbuf.ptr, f->outbuf.cap, f->sktab.ptr, f->sktab.len, f->enc->info->min_blocksize);
		f->data = (void*)f->outbuf.ptr;
		f->datalen = r;
		return FFFLAC_RDONE;
	}

	sampsize = sizeof(int) * f->info.channels;
	for (;;) {
		samples = f->pcmlen / sampsize - f->off;

		f->datalen = 0;
		if (samples != 0 || f->fin) {
			const int *src[FLAC__MAX_CHANNELS];
			uint i;
			for (i = 0;  i != f->info.channels;  i++) {
				src[i] = f->pcm[i] + f->off;
			}
			r = FLAC__encode(f->enc, src, samples, (char**)&f->data, &f->datalen);
			if (r < 0) {
				f->err = -r;
				f->errtype = FLAC_EENC_STATE;
				return FFFLAC_RERR;
			}
			f->off += r;
		}

		if (f->datalen == 0) {
			if (f->fin && samples != 0)
				continue;
			f->off = 0;
			return FFFLAC_RMORE;
		}
		break;
	}

	uint blksize = f->enc->info->min_blocksize;
	if (samples == 0 && f->fin)
		blksize = r;

	while (f->iskpt != f->sktab.len) {
		ffpcm_seekpt *sp = &f->sktab.ptr[f->iskpt];
		if (!(sp->sample >= f->nsamps && sp->sample < f->nsamps + blksize))
			break;
		sp->sample = f->nsamps;
		sp->off = f->frlen;
		f->iskpt++;
	}
	f->frlen += f->datalen;

	f->nsamps += blksize;

	if (samples == 0 && f->fin) {
		f->state = ENC_SEEK0;
		return FFFLAC_RDATA;
	}

	return FFFLAC_RDATA;
}
