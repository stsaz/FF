/**
Copyright (c) 2015 Simon Zolin
*/

#include <FF/audio/ogg.h>
#include <FF/string.h>
#include <FFOS/mem.h>
#include <FFOS/random.h>

#include <vorbis/vorbisenc.h>


enum {
	SYNC_BUF = 4096
};

static int _ffogg_pageread(ffogg *o);
static int _ffogg_open(ffogg *o);
static int _ffogg_pageseek(ffogg *o);

enum { I_HDRPG, I_HDRPKT, I_COMM, I_HDRDONE
	, I_SEEK_EOS, I_SEEK_EOS2, I_SEEK_EOS_THIS, I_SEEK_EOS3
	, I_SEEK, I_SEEKDATA, I_SEEK2
	, I_PAGE, I_PKT, I_SYNTH, I_DATA };

enum OGG_E {
	OGG_EOK
	, OGG_ESYNC
	, OGG_EBADPAGE
	, OGG_EGAP
};

static const char* const ogg_errstr[] = {
	""
	, "stream has not yet captured sync (bytes were skipped)"
	, "the serial number of the page did not match the serial number of the bitstream, the page version was incorrect, or an internal error occurred"
	, "out of sync and there is a gap in the data"
};

static const char* const vorb_errstr[] = {
	"" /*OV_EREAD*/
	, "internal error; indicates a bug or memory corruption" /*OV_EFAULT*/
	, "unimplemented; not supported by this version of the library" /*OV_EIMPL*/
	, "invalid parameter" /*OV_EINVAL*/
	, "the packet is not a Vorbis header packet" /*OV_ENOTVORBIS*/
	, "error interpreting the packet" /*OV_EBADHEADER*/
	, "" /*OV_EVERSION*/
	, "the packet is not an audio packet" /*OV_ENOTAUDIO*/
	, "there was an error in the packet" /*OV_EBADPACKET*/
	, "" /*OV_EBADLINK*/
	, "" /*OV_ENOSEEK*/
};

const char* ffogg_errstr(int e)
{
	if (e >= 0)
		return ogg_errstr[e];
	e = -(e - (OV_EREAD));
	if ((uint)e < FFCNT(vorb_errstr))
		return vorb_errstr[e];
	return "";
}

void ffogg_init(ffogg *o)
{
	ffmem_tzero(o);
	ogg_sync_init(&o->osync);
	vorbis_info_init(&o->vinfo);
	vorbis_comment_init(&o->vcmt);
}

uint ffogg_bitrate(ffogg *o)
{
	uint64 dur_ms;

	if (o->total_samples == 0 || o->total_size == 0)
		return o->vinfo.bitrate_nominal;

	dur_ms = o->total_samples * 1000 / o->vinfo.rate;
	return (uint)((o->total_size - o->off_data) * 8 * 1000 / dur_ms);
}

/* Find the next page. */
static int _ffogg_pageseek(ffogg *o)
{
	char *buf;
	uint n;

	for (;;) {
		int r = ogg_sync_pageseek(&o->osync, &o->opg);
		if (r > 0)
			break;

		else if (r == 0) {
			if (o->datalen == 0)
				return FFOGG_RMORE;

			buf = ogg_sync_buffer(&o->osync, SYNC_BUF);
			n = (uint)ffmin(o->datalen, SYNC_BUF);
			ffmemcpy(buf, o->data, n);
			ogg_sync_wrote(&o->osync, n);
			o->data += n;
			o->datalen -= n;
		}
	}

	return FFOGG_RDONE;
}

static int _ffogg_pageread(ffogg *o)
{
	char *buf;
	uint n;

	for (;;) {
		int r = ogg_sync_pageout(&o->osync, &o->opg);
		if (r > 0)
			break;

		else if (r == 0) {
			if (o->datalen == 0)
				return FFOGG_RMORE;

			buf = ogg_sync_buffer(&o->osync, SYNC_BUF);
			n = (uint)ffmin(o->datalen, SYNC_BUF);
			ffmemcpy(buf, o->data, n);
			ogg_sync_wrote(&o->osync, n);
			o->data += n;
			o->datalen -= n;

		} else if (r < 0) {
			o->err = OGG_ESYNC;
			return FFOGG_RWARN;
		}
	}

	return FFOGG_RDONE;
}

static int _ffogg_open(ffogg *o)
{
	int r;
	size_t datalen2;
	ogg_packet opkt;

	for (;;) {

	switch (o->state) {

	case I_HDRPG:
		datalen2 = o->datalen;
		r = _ffogg_pageread(o);
		o->off_data += datalen2 - o->datalen;
		if (r != FFOGG_RDONE)
			return (r == FFOGG_RWARN) ? FFOGG_RERR : r;

		if (o->nhdr == 0) {
			ogg_stream_init(&o->ostm, ogg_page_serialno(&o->opg));
			o->ostm_valid = 1;
		}

		r = ogg_stream_pagein(&o->ostm, &o->opg);
		if (r < 0) {
			o->err = OGG_EBADPAGE;
			return FFOGG_RERR;
		}
		o->state = I_HDRPKT;
		// break;

	case I_HDRPKT:
		r = ogg_stream_packetout(&o->ostm, &opkt);
		if (r == 0) {
			o->state = I_HDRPG;
			break;

		} else if (r < 0) {
			o->err = OGG_EGAP;
			return FFOGG_RERR;
		}

		r = vorbis_synthesis_headerin(&o->vinfo, &o->vcmt, &opkt);
		if (r != 0) {
			o->err = r;
			return FFOGG_RERR;
		}

		switch (++o->nhdr) {
		case 1:
			return FFOGG_RHDR;

		case 2:
			o->ncomm = 0;
			o->state = I_COMM;
			break;

		case 3:
			o->state = I_HDRDONE;
			break;
		}
		break;

	case I_COMM:
		if (o->ncomm == (uint)o->vcmt.comments) {
			o->nhdr = 2;
			o->state = I_HDRPKT;
			break;
		}
		ffs_split2by(o->vcmt.user_comments[o->ncomm], o->vcmt.comment_lengths[o->ncomm], '=', &o->tagname, &o->tagval);
		o->ncomm++;
		o->tag = ffvorbtag_find(o->tagname.ptr, o->tagname.len);
		return FFOGG_RTAG;

	case I_HDRDONE:
		if (0 == vorbis_synthesis_init(&o->vds, &o->vinfo)) {
			vorbis_block_init(&o->vds, &o->vblk);
			o->vblk_valid = 1;
		}
		o->first_sample = ogg_page_granulepos(&o->opg);

		if (o->seekable && o->total_size != 0)
			o->state = I_SEEK_EOS;
		else
			o->state = I_PAGE;
		return FFOGG_RHDRFIN;

	case I_SEEK_EOS:
		o->off = o->total_size - ffmin(64 * 1024, o->total_size);
		o->state = I_SEEK_EOS2;
		ogg_sync_init(&o->osync_seek);
		return FFOGG_RSEEK;

	case I_SEEK_EOS2:
		{
		ffogg ogsk;
		ffogg_init(&ogsk);
		ogsk.data = o->data;
		ogsk.datalen = o->datalen;
		ogsk.osync = o->osync_seek;

		while (FFOGG_RDONE == _ffogg_pageseek(&ogsk)) {
			o->total_samples = ogg_page_granulepos(&ogsk.opg) - o->first_sample;
			if (ogg_page_eos(&ogsk.opg)) {
				o->osync_seek = ogsk.osync;
				// o->state = I_SEEK_EOS_THIS;
				goto eos_this;
			}
		}
		o->osync_seek = ogsk.osync;
		return FFOGG_RMORE;
		}

eos_this:
	case I_SEEK_EOS_THIS:
		ogg_sync_clear(&o->osync_seek);
		o->state = I_SEEK_EOS3;
		return FFOGG_RINFO;

	case I_SEEK_EOS3:
		o->off = o->off_data;
		o->state = I_PAGE;
		return FFOGG_RSEEK;
	}
	}
	//unreachable
}

int ffogg_nodata(ffogg *o)
{
	if (o->state != I_SEEK_EOS2)
		return -1;
	o->state = I_SEEK_EOS_THIS;
	return 0;
}

void ffogg_close(ffogg *o)
{
	if (o->vblk_valid) {
		vorbis_block_clear(&o->vblk);
		vorbis_dsp_clear(&o->vds);
	}
	if (o->ostm_valid)
		ogg_stream_clear(&o->ostm);
	vorbis_comment_clear(&o->vcmt);
	vorbis_info_clear(&o->vinfo);
	ogg_sync_clear(&o->osync_seek);
	ogg_sync_clear(&o->osync);
}

void ffogg_seek(ffogg *o, uint64 sample)
{
	if (o->total_samples == 0 || o->total_size == 0)
		return;
	o->seek_sample = sample;
	if (o->state == I_DATA)
		o->state = I_SEEKDATA;
	else
		o->state = I_SEEK;
}

/*
ogg_sync_state -> ogg_page -> ogg_stream_state -> ogg_packet -> vorbis_block -> vorbis_dsp_state
*/
int ffogg_decode(ffogg *o)
{
	int r;
	ogg_packet opkt;

	for (;;) {

	switch (o->state) {
	default:
		return _ffogg_open(o);

	case I_SEEKDATA:
		vorbis_synthesis_read(&o->vds, o->nsamples);
		//o->state = I_SEEK;
		//break;

	case I_SEEK:
		ogg_sync_reset(&o->osync);
		o->off = o->off_data + (o->total_size - o->off_data) * o->seek_sample / o->total_samples;
		if (o->off > 16 * 1024)
			o->off -= 16 * 1024;
		o->state = I_SEEK2;
		return FFOGG_RSEEK;

	case I_SEEK2:
		if (FFOGG_RDONE != (r = _ffogg_pageseek(o)))
			return r;
		o->cursample = ogg_page_granulepos(&o->opg) - o->first_sample;
		o->state = I_PAGE;
		//break;

	case I_PAGE:
		if (ogg_page_eos(&o->opg))
			return FFOGG_RDONE;

		if (FFOGG_RDONE != (r = _ffogg_pageread(o)))
			return r;

		if (0 > ogg_stream_pagein(&o->ostm, &o->opg)) {
			o->err = OGG_EBADPAGE;
			return FFOGG_RWARN;
		}
		o->state = I_PKT;
		// break;

	case I_PKT:
		r = ogg_stream_packetout(&o->ostm, &opkt);
		if (r == 0) {
			o->state = I_PAGE;
			break;
		} else if (r < 0) {
			o->err = OGG_EGAP;
			return FFOGG_RWARN;
		}

		if (0 != (r = vorbis_synthesis(&o->vblk, &opkt))) {
			o->err = r;
			return FFOGG_RWARN;
		}

		vorbis_synthesis_blockin(&o->vds, &o->vblk);
		// o->state = I_SYNTH;
		// break;

	case I_SYNTH:
		if (0 != (o->nsamples = vorbis_synthesis_pcmout(&o->vds, (float***)&o->pcm))) {
			o->pcmlen = o->nsamples * sizeof(float) * o->vinfo.channels;
			o->state = I_DATA;
			return FFOGG_RDATA;
		}
		o->state = I_PKT;
		break;

	case I_DATA:
		vorbis_synthesis_read(&o->vds, o->nsamples);
		o->cursample += o->nsamples;
		o->state = I_SYNTH;
		break;
	}
	}
	//unreachable
}


void ffogg_enc_init(ffogg_enc *o)
{
	ffmem_tzero(o);
	ogg_sync_init(&o->osync);
	vorbis_info_init(&o->vinfo);
	vorbis_comment_init(&o->vcmt);
}

void ffogg_enc_close(ffogg_enc *o)
{
	if (o->vblk_valid) {
		vorbis_block_clear(&o->vblk);
		vorbis_dsp_clear(&o->vds);
	}
	if (o->ostm_valid)
		ogg_stream_clear(&o->ostm);
	vorbis_comment_clear(&o->vcmt);
	vorbis_info_clear(&o->vinfo);
	ogg_sync_clear(&o->osync);
}

/*
HDR_STREAMID  HDR_COMMENTS  HDR_CODEBOOKS  DATA...
*/

int ffogg_create(ffogg_enc *o, ffpcm *pcm, int quality)
{
	int r, i;
	ogg_packet pkt[3];

	if (0 != (r = vorbis_encode_init_vbr(&o->vinfo, pcm->channels, pcm->sample_rate, (float)quality / 100)))
		return r;

	vorbis_analysis_init(&o->vds, &o->vinfo);
	vorbis_block_init(&o->vds, &o->vblk);
	o->vblk_valid = 1;

	ogg_stream_init(&o->ostm, ffrnd_get());
	o->ostm_valid = 1;

	if (0 != (r = vorbis_analysis_headerout(&o->vds, &o->vcmt, &pkt[0], &pkt[1], &pkt[2])))
		return r;
	vorbis_comment_clear(&o->vcmt);

	for (i = 0;  i < 3;  i++) {
		ogg_stream_packetin(&o->ostm, &pkt[i]);
	}

	return 0;
}

int ffogg_encode(ffogg_enc *o)
{
	enum { I_HDRFLUSH, I_INPUT, I_ENCODE, I_GETPAGE };
	int r;
	uint i, n;
	float **fpcm;
	ogg_packet opkt;

	for (;;) {

	switch (o->state) {
	case I_HDRFLUSH:
		if (0 != (r = ogg_stream_flush(&o->ostm, &o->opg))) {
			o->data = (void*)o->opg.body;
			o->datalen = o->opg.body_len;
			return FFOGG_RDATA;
		}
		o->state = I_INPUT;
		//break;

	case I_INPUT:
		if (o->pcmlen == 0 && !o->fin)
			return FFOGG_RMORE;

		n = (uint)(o->pcmlen / (sizeof(float) * o->vinfo.channels));
		fpcm = vorbis_analysis_buffer(&o->vds, n);
		if (o->pcmlen != 0) {
			for (i = 0;  i != (uint)o->vinfo.channels;  i++) {
				ffmemcpy(fpcm[i], o->pcm[i], n * sizeof(float));
			}
		}
		vorbis_analysis_wrote(&o->vds, n);
		o->pcmlen = 0;
		o->state = I_ENCODE;
		//break;

	case I_ENCODE:
		r = vorbis_analysis_blockout(&o->vds, &o->vblk);
		if (r < 0) {
			o->err = r;
			return FFOGG_RERR;
		} else if (r != 1) {
			o->state = I_INPUT;
			break;
		}

		if (0 != (r = vorbis_analysis(&o->vblk, &opkt))) {
			o->err = r;
			return FFOGG_RERR;
		}

		if (0 != (r = ogg_stream_packetin(&o->ostm, &opkt))) {
			o->err = r;
			return FFOGG_RERR;
		}
		o->state = I_GETPAGE;
		//break;

	case I_GETPAGE:
		if (ogg_page_eos(&o->opg))
			return FFOGG_RDONE;

		if (0 == (r = ogg_stream_pageout(&o->ostm, &o->opg))) {
			o->state = I_ENCODE;
			break;
		}

		o->data = (void*)o->opg.body;
		o->datalen = o->opg.body_len;
		return FFOGG_RDATA;
	}
	}

	//unreachable
}
