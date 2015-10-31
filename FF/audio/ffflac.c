/**
Copyright (c) 2015 Simon Zolin
*/

#include <FF/audio/flac.h>
#include <FF/audio/pcm.h>
#include <FFOS/error.h>
#include <FFOS/file.h>


enum FLAC_E {
	FLAC_EUKN
	, FLAC_ESYS
	, FLAC_EINIT
	, FLAC_EDEC
	, FLAC_ESTATE
	, FLAC_EFMT
};

static FLAC__StreamDecoderReadStatus _ffflac_read(const FLAC__StreamDecoder *decoder
	, FLAC__byte buffer[], size_t *bytes, void *client_data);
static FLAC__StreamDecoderWriteStatus _ffflac_write(const FLAC__StreamDecoder *decoder
	, const FLAC__Frame *frame, const FLAC__int32 *const buffer[], void *client_data);
static FLAC__StreamDecoderTellStatus _ffflac_tell(const FLAC__StreamDecoder *decoder, FLAC__uint64 *absolute_byte_offset, void *client_data);
static void _ffflac_meta(const FLAC__StreamDecoder *decoder, const FLAC__StreamMetadata *metadata, void *client_data);
static void _ffflac_error(const FLAC__StreamDecoder *decoder, FLAC__StreamDecoderErrorStatus status, void *client_data);

static FLAC__StreamEncoderWriteStatus _ffflac_ewrite(const FLAC__StreamEncoder *encoder
	, const FLAC__byte buffer[], size_t bytes, unsigned samples, unsigned current_frame, void *client_data);


void ffflac_init(ffflac *f)
{
	ffmem_tzero(f);
}

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

	case FLAC_EFMT:
		return "PCM format error";
	}

	return "unknown error";
}


const char *const ffflac_tagstr[] = {
	"ALBUM"
	, "ARTIST"
	, "COMMENT"
	, "DATE"
	, "GENRE"
	, "TITLE"
	, "TRACKNUMBER"
	, "TRACKTOTAL"
};

int ffflac_tag(const char *name, size_t len)
{
	return ffszarr_ifindsorted(ffflac_tagstr, FFCNT(ffflac_tagstr), name, len);
}

int ffflac_addtag(ffflac_enc *f, const char *name, const char *val)
{
	FLAC__StreamMetadata_VorbisComment_Entry entry;

	if (f->meta[0] == NULL
		&& NULL == (f->meta[0] = FLAC__metadata_object_new(FLAC__METADATA_TYPE_VORBIS_COMMENT)))
		return FFFLAC_RERR;

	if (!FLAC__metadata_object_vorbiscomment_entry_from_name_value_pair(&entry, name, val)
		|| !FLAC__metadata_object_vorbiscomment_append_comment(f->meta[0], entry, /*copy*/ 0))
		return FFFLAC_RERR;

	f->metasize += ffsz_len(name) + FFSLEN("=") + ffsz_len(val);
	return 0;
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
	int ich;

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
	switch (f->errtype) {
	case FLAC_ESYS:
		return fferr_strp(fferr_last());

	case FLAC_EINIT:
		return FLAC__StreamEncoderInitStatusString[f->err];

	case FLAC_EFMT:
		return "PCM format error";
	}

	return "unknown error";
}

static FLAC__StreamEncoderWriteStatus _ffflac_ewrite(const FLAC__StreamEncoder *encoder
	, const FLAC__byte buffer[], size_t bytes, unsigned samples, unsigned current_frame, void *client_data)
{
	ffflac_enc *f = client_data;
	if (NULL == ffarr_append(&f->outbuf, buffer, bytes))
		return FLAC__STREAM_ENCODER_WRITE_STATUS_FATAL_ERROR;
	return FLAC__STREAM_ENCODER_WRITE_STATUS_OK;
}

void ffflac_enc_init(ffflac_enc *f)
{
	ffmem_tzero(f);
	f->min_meta = 1000;
}

void ffflac_enc_close(ffflac_enc *f)
{
	ffarr_free(&f->data32);
	ffarr_free(&f->outbuf);

	if (f->meta[0] != NULL)
		FLAC__metadata_object_delete(f->meta[0]);
	if (f->meta[1] != NULL)
		FLAC__metadata_object_delete(f->meta[1]);
	FLAC__stream_encoder_delete(f->enc);
}

int ffflac_create(ffflac_enc *f, const ffpcm *pcm)
{
	if (NULL == (f->enc = FLAC__stream_encoder_new())) {
		f->errtype = FLAC_ESYS;
		return -1;
	}

	if (pcm->format != FFPCM_16LE) {
		f->errtype = FLAC_EFMT;
		return -1;
	}
	f->bpsample = ffpcm_bits[pcm->format];
	f->fmt = *pcm;
	FLAC__stream_encoder_set_bits_per_sample(f->enc, f->bpsample);
	FLAC__stream_encoder_set_channels(f->enc, pcm->channels);
	FLAC__stream_encoder_set_sample_rate(f->enc, pcm->sample_rate);

	FLAC__stream_encoder_set_compression_level(f->enc, f->level);
	FLAC__stream_encoder_set_total_samples_estimate(f->enc, f->total_samples);

	if (f->meta[0] != NULL || f->min_meta != 0) {
		uint n = (f->meta[0] != NULL) ? 1 : 0;

		if (f->min_meta > f->metasize) {
			if (NULL == (f->meta[n] = FLAC__metadata_object_new(FLAC__METADATA_TYPE_PADDING))) {
				f->errtype = FLAC_ESYS;
				return -1;
			}
			f->meta[n]->length = f->min_meta - f->metasize;
			n++;
		}

		if (!FLAC__stream_encoder_set_metadata(f->enc, f->meta, n)) {
			f->errtype = FLAC_ESYS;
			return -1;
		}
	}

	if (FLAC__STREAM_ENCODER_INIT_STATUS_OK != (f->err = FLAC__stream_encoder_init_stream(f->enc
		, &_ffflac_ewrite, NULL /*seek*/, NULL /*tell*/, NULL /*meta*/, f))) {
		f->errtype = FLAC_EINIT;
		return -1;
	}

	if (NULL == ffarr_alloc(&f->data32, 64 * 1024)) {
		f->errtype = FLAC_ESYS;
		return -1;
	}

	return 0;
}

int ffflac_encode(ffflac_enc *f)
{
	uint i, ich, j, samples, sampsize;
	int *dst = (void*)f->data32.ptr;
	const short *src16 = f->pcmi, **src16ni;

	if (f->outbuf.len != 0) {
		f->data = (void*)f->outbuf.ptr;
		f->datalen = f->outbuf.len;
		f->outbuf.len = 0;
		return FFFLAC_RDATA;
	}

	sampsize = f->bpsample / 8 * f->fmt.channels;
	samples = ffmin(f->data32.cap / (sizeof(int) * f->fmt.channels), f->pcmlen / sampsize);

	if (f->pcmi != NULL) {
		//short[] -> int[]
		for (i = 0;  i < samples * f->fmt.channels;  i++) {
			dst[i] = src16[i];
		}
		f->pcmlen -= samples * sampsize;
		f->pcmi = (byte*)f->pcmi + samples * sampsize;

	} else {
		src16ni = (const short**)f->pcm;
		//short[] -> int[]
		for (ich = 0;  ich < f->fmt.channels;  ich++) {
			j = ich;
			for (i = f->off;  i < samples;  i++) {
				dst[j] = src16ni[ich][i];
				j += f->fmt.channels;
			}
		}

		f->pcmlen -= samples * sampsize;
		f->off += samples;
		if (f->pcmlen == 0)
			f->off = 0;
	}

	if (!FLAC__stream_encoder_process_interleaved(f->enc, dst, samples)) {
		f->errtype = FLAC_EUKN;
		return FFFLAC_RERR;
	}

	if (f->fin) {
		if (!FLAC__stream_encoder_finish(f->enc))
			return FFFLAC_RERR;
		f->data = (void*)f->outbuf.ptr;
		f->datalen = f->outbuf.len;
		f->outbuf.len = 0;
		return FFFLAC_RDONE;
	}

	f->data = (void*)f->outbuf.ptr;
	f->datalen = f->outbuf.len;
	f->outbuf.len = 0;
	if (f->datalen == 0)
		return FFFLAC_RMORE;
	return FFFLAC_RDATA;
}
