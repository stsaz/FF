/**
Copyright (c) 2015 Simon Zolin
*/

#include <FF/audio/flac.h>
#include <FF/audio/pcm.h>
#include <FF/number.h>
#include <FFOS/error.h>
#include <FFOS/file.h>


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

#define FLAC_SYNC  "fLaC"

enum {
	FLAC_SYNCLEN = FFSLEN(FLAC_SYNC),
	FLAC_MINSIZE = FLAC_SYNCLEN + sizeof(struct flac_hdr) + sizeof(struct flac_streaminfo),
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
};


static void flac_sethdr(void *dst, uint type, uint islast, uint size);
static int flac_info_write(char *out, size_t cap, const ffflac_info *info);
static uint flac_padding_write(char *out, size_t cap, uint padding, uint last);

static int flac_seektab_init(_ffflac_seektab *sktab, uint64 total_samples, uint interval);
static uint flac_seektab_write(void *out, size_t cap, const ffpcm_seekpt *pts, size_t npts, uint blksize);


static FLAC__StreamDecoderReadStatus _ffflac_read(const FLAC__StreamDecoder *decoder
	, FLAC__byte buffer[], size_t *bytes, void *client_data);
static FLAC__StreamDecoderWriteStatus _ffflac_write(const FLAC__StreamDecoder *decoder
	, const FLAC__Frame *frame, const FLAC__int32 *const buffer[], void *client_data);
static FLAC__StreamDecoderTellStatus _ffflac_tell(const FLAC__StreamDecoder *decoder, FLAC__uint64 *absolute_byte_offset, void *client_data);
static void _ffflac_meta(const FLAC__StreamDecoder *decoder, const FLAC__StreamMetadata *metadata, void *client_data);
static void _ffflac_error(const FLAC__StreamDecoder *decoder, FLAC__StreamDecoderErrorStatus status, void *client_data);


static FFINL void flac_sethdr(void *dst, uint type, uint islast, uint size)
{
	struct flac_hdr *hdr = dst;
	hdr->type = type;
	hdr->last = islast;
	ffint_hton24(hdr->size, size);
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

/* Initialize seek table.
Example for 1 sec interval: [0 1* 2* 3* 3.1] */
static int flac_seektab_init(_ffflac_seektab *sktab, uint64 total_samples, uint interval)
{
	uint i, npts;
	uint64 pos = interval;

	npts = total_samples / interval - (total_samples % interval == 0);
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
	FLAC__stream_decoder_finish(f->dec);
	FLAC__stream_decoder_delete(f->dec);
}

static FLAC__StreamDecoderReadStatus _ffflac_read(const FLAC__StreamDecoder *decoder
	, FLAC__byte buffer[], size_t *bytes, void *client_data)
{
	ffflac *f = client_data;

	if (f->datalen == 0) {
		if (f->fin) {
			f->r = FFFLAC_RDONE;
			*bytes = 0;
			return FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM;
		}

		f->r = FFFLAC_RMORE;
		return FLAC__STREAM_DECODER_READ_STATUS_AGAIN;
	}

	if (f->nbuf != 0) {
		*bytes = ffmin(*bytes, f->nbuf);
		ffmemcpy(buffer, f->buf.ptr + f->buf.len - f->nbuf, *bytes);
		f->nbuf -= *bytes;

	} else {
		*bytes = ffmin(*bytes, f->datalen);
		ffmemcpy(buffer, f->data, *bytes);
		f->data += *bytes;
		f->datalen -= *bytes;
	}

	f->off += *bytes;
	return FLAC__STREAM_DECODER_READ_STATUS_CONTINUE;
}

static FLAC__StreamDecoderWriteStatus _ffflac_write(const FLAC__StreamDecoder *decoder
	, const FLAC__Frame *frame, const FLAC__int32 *const buffer[], void *client_data)
{
	ffflac *f = client_data;
	uint ich;

	if (frame->header.bits_per_sample != f->bpsample
		|| frame->header.channels != f->fmt.channels
		|| frame->header.sample_rate != f->fmt.sample_rate) {
		f->errtype = FLAC_EFMT;
		f->r = FFFLAC_RERR;
		return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
	}

	//'buffer' is an array on the stack when we're called after seeking
	for (ich = 0;  ich != f->fmt.channels;  ich++) {
		f->out32[ich] = buffer[ich];
	}

	f->pcm = (void**)f->out32;
	f->pcmlen = frame->header.blocksize;
	f->frsample = frame->header.number.sample_number;
	f->r = FFFLAC_RDATA;
	return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}

static FLAC__StreamDecoderTellStatus _ffflac_tell(const FLAC__StreamDecoder *decoder, FLAC__uint64 *absolute_byte_offset, void *client_data)
{
	ffflac *f = client_data;
	*absolute_byte_offset = f->off;
	return FLAC__STREAM_DECODER_TELL_STATUS_OK;
}

static void _ffflac_meta(const FLAC__StreamDecoder *decoder, const FLAC__StreamMetadata *metadata, void *client_data)
{
	ffflac *f = client_data;

	switch (metadata->type) {

	case FLAC__METADATA_TYPE_STREAMINFO:
		f->fmt.sample_rate = metadata->data.stream_info.sample_rate;
		f->fmt.channels = metadata->data.stream_info.channels;
		f->bpsample = metadata->data.stream_info.bits_per_sample;
		if (metadata->data.stream_info.bits_per_sample == 16)
			f->fmt.format = FFPCM_16LE;
		else {
			f->errtype = FLAC_EFMT;
			f->r = FFFLAC_RERR;
			return;
		}

		f->r = FFFLAC_RHDR;
		break;

	case FLAC__METADATA_TYPE_VORBIS_COMMENT:
		f->r = FFFLAC_RTAG;
		break;

	default:
		break;
	}
}

static void _ffflac_error(const FLAC__StreamDecoder *decoder, FLAC__StreamDecoderErrorStatus status, void *client_data)
{
	ffflac *f = client_data;
	f->err = status;
	f->errtype = FLAC_EDEC;
	f->r = FFFLAC_RERR;
}

int ffflac_open(ffflac *f)
{
	int r;

	if (NULL == (f->dec = FLAC__stream_decoder_new())) {
		f->errtype = FLAC_ESYS;
		return FFFLAC_RERR;
	}

	FLAC__stream_decoder_set_metadata_respond(f->dec, FLAC__METADATA_TYPE_VORBIS_COMMENT);

	if (FLAC__STREAM_DECODER_INIT_STATUS_OK != (r = FLAC__stream_decoder_init_stream(f->dec
		, &_ffflac_read, NULL /*seek*/, &_ffflac_tell, NULL /*length*/, NULL /*eof*/, &_ffflac_write, &_ffflac_meta, &_ffflac_error, f))) {

		f->err = r;
		f->errtype = FLAC_EINIT;
		return FFFLAC_RERR;
	}

	return 0;
}

enum { I_HDR, I_TAG, I_DATA, I_SEEK, I_SEEK2 };

void ffflac_seek(ffflac *f, uint64 sample)
{
	if (f->total_size == 0)
		return;
	f->st = I_SEEK;
	f->buf.len = 0;
	FLAC__seek_init(f->dec, sample, f->total_size);
}

// #define ffflac_skipframe(f)  FLAC__stream_decoder_skip_single_frame(f->dec)

int ffflac_decode(ffflac *f)
{
	int r;
	size_t datalen_last;
	uint64 off_last;
	const FLAC__StreamMetadata_VorbisComment *vcom;

	for (;;) {
	switch (f->st) {
	case I_TAG:
		vcom = &f->dec->meta.data.vorbis_comment;
		if (f->idx != vcom->num_comments) {
			ffs_split2by((char*)vcom->comments[f->idx].entry, vcom->comments[f->idx].length, '='
				, &f->tagname, &f->tagval);
			f->idx++;
			return FFFLAC_RTAG;
		}

		f->st = I_HDR;
		// break;

	case I_HDR:
		if (FLAC__STREAM_DECODER_SEARCH_FOR_FRAME_SYNC == FLAC__stream_decoder_get_state(f->dec)) {
			f->st = I_DATA;
			return FFFLAC_RHDRFIN;
		}
		// break;

	case I_SEEK2:
	case I_DATA:
		datalen_last = f->datalen;
		off_last = f->off;
		f->nbuf = f->buf.len; //number of valid bytes in buf
		f->r = FFFLAC_RNONE;
		r = FLAC__stream_decoder_process_single(f->dec);

		switch (f->r) {
		case FFFLAC_RMORE:
			// f->datalen == 0
			if (datalen_last != 0
				&& NULL == ffarr_append(&f->buf, f->data - datalen_last, datalen_last)) {
				f->errtype = FLAC_ESYS;
				return FFFLAC_RERR;
			}
			f->off = off_last;
			return FFFLAC_RMORE;

		case FFFLAC_RERR:
			return FFFLAC_RERR;
		}

		if (f->nbuf != f->buf.len) {
			//move unprocessed data to the beginning of the buffer
			if (f->nbuf != 0)
				ffmemcpy(f->buf.ptr, f->buf.ptr + f->buf.len - f->nbuf, f->nbuf);
			f->buf.len = f->nbuf;
		}

		switch (f->r) {
		case FFFLAC_RNONE:
			//something happened inside libflac
			if (!r) {
				f->err = FLAC__stream_decoder_get_state(f->dec);
				f->errtype = FLAC_ESTATE;
				return FFFLAC_RERR;
			}

			r = FLAC__stream_decoder_get_state(f->dec);
			switch (r) {
			case FLAC__STREAM_DECODER_END_OF_STREAM:
				return FFFLAC_RDONE;

			case FLAC__STREAM_DECODER_SEARCH_FOR_METADATA:
			case FLAC__STREAM_DECODER_READ_METADATA:
			case FLAC__STREAM_DECODER_SEARCH_FOR_FRAME_SYNC:
			case FLAC__STREAM_DECODER_READ_FRAME:
			case FLAC__STREAM_DECODER_READ_METADATA_FREE:
				break;

			default:
				f->err = r;
				f->errtype = FLAC_ESTATE;
				return FFFLAC_RERR;
			}
			break; //again

		case FFFLAC_RTAG:
			f->st = I_TAG;
			break; //again

		case FFFLAC_RDATA:
			{
			uint i, ich;
			//in-place conversion: int[] -> short[]
			for (ich = 0;  ich != f->fmt.channels;  ich++) {
				for (i = 0;  i != f->pcmlen;  i++) {
					f->out16[ich][i] = (short)f->out32[ich][i];
				}
			}

			f->pcmlen *= ffpcm_size1(&f->fmt);
			}

			if (f->st == I_SEEK2)
				f->st = I_DATA;
			// break;

		default:
			return f->r;
		}

		if (f->st == I_SEEK2)
			f->st = I_SEEK;
		break; //again

	case I_SEEK:
		r = FLAC__seek(f->dec, &f->off);
		if (r == -1) {
			f->err = FLAC__stream_decoder_get_state(f->dec);
			f->errtype = FLAC_ESTATE;
			return FFFLAC_RERR;
		}
		f->st = I_SEEK2;
		f->datalen = 0;
		return FFFLAC_RSEEK;
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

	f->vtag.out = ffarr_end(&f->outbuf) + sizeof(struct flac_hdr);
	f->vtag.outcap = ffarr_unused(&f->outbuf) - sizeof(struct flac_hdr);
	if (0 != ffvorbtag_add(&f->vtag, NULL, FLAC__VENDOR_STRING, ffsz_len(FLAC__VENDOR_STRING))) {
		f->errtype = FLAC_EHDR;
		return FFFLAC_RERR;
	}

	return 0;
}

static int _ffflac_enc_hdr(ffflac_enc *f)
{
	ffvorbtag_fin(&f->vtag);
	f->have_padding = (f->min_meta > f->vtag.outlen);

	flac_sethdr(ffarr_end(&f->outbuf), FLAC_TTAGS, !f->have_padding && (f->sktab.len == 0), f->vtag.outlen);
	f->outbuf.len += sizeof(struct flac_hdr) + f->vtag.outlen;

	if (f->have_padding)
		f->outbuf.len += flac_padding_write(ffarr_end(&f->outbuf), f->outbuf.cap, f->min_meta - f->vtag.outlen, f->sktab.len == 0);

	if (f->sktab.len != 0) {
		// write header with empty body
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
		f->seekoff = FLAC_MINSIZE + sizeof(struct flac_hdr) + f->vtag.outlen;
		if (f->have_padding)
			f->seekoff += sizeof(struct flac_hdr) + f->min_meta - f->vtag.outlen;
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
