/**
Copyright (c) 2015 Simon Zolin
*/

#include <FF/audio/ogg.h>
#include <FF/string.h>
#include <FFOS/mem.h>


enum {
	SYNC_BUF = 4096
};

void ffogg_init(ffogg *o)
{
	ffmem_tzero(o);
	ogg_sync_init(&o->osync);
	vorbis_info_init(&o->vinfo);
	vorbis_comment_init(&o->vcmt);
	o->nhdr = 0;
}

int ffogg_pageseek(ffogg *o)
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

	return FFOGG_ROK;
}

int ffogg_pageread(ffogg *o)
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
			return FFOGG_RERR;
		}
	}

	return FFOGG_ROK;
}

int ffogg_open(ffogg *o)
{
	int r;

	if (o->comments)
		goto cmts;

	for (;;) {

		r = ffogg_pageread(o);
		if (r != FFOGG_ROK)
			return r;

		if (o->nhdr == 0) {
			ogg_stream_init(&o->ostm, ogg_page_serialno(&o->opg));
			o->ostm_valid = 1;
		}

		r = ogg_stream_pagein(&o->ostm, &o->opg);
		if (r < 0) {
			return FFOGG_RERR;
		}

		for (;;) {
			ogg_packet opkt;

			r = ogg_stream_packetout(&o->ostm, &opkt);
			if (r == 0) {
				break;

			} else if (r < 0) {
				return FFOGG_RERR;
			}

			r = vorbis_synthesis_headerin(&o->vinfo, &o->vcmt, &opkt);
			if (r != 0) {
				return FFOGG_RERR;
			}

			if (++o->nhdr == 3) {
				o->nhdr = 0;
				o->comments = 1;
				goto cmts;
			}
		}
	}

cmts:
	if (o->ncomm == o->vcmt.comments) {
		o->ncomm = 0;
		goto done;
	}
	ffs_split2by(o->vcmt.user_comments[o->ncomm], o->vcmt.comment_lengths[o->ncomm], '=', &o->tagname, &o->tagval);
	o->ncomm++;
	return FFOGG_RTAG;

done:
	if (0 == vorbis_synthesis_init(&o->vds, &o->vinfo)) {
		vorbis_block_init(&o->vds, &o->vblk);
		o->vblk_valid = 1;
	}
	return FFOGG_RDONE;
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
	ogg_sync_clear(&o->osync);
}

static const char *const _ffogg_strcomment[] = {
	"album"
	, "artist"
	, "comment"
	, "date"
	, "genre"
	, "title"
	, "tracknumber"
};

uint ffogg_tag(const char *name, size_t len)
{
	uint i;
	for (i = 0;  i < FFCNT(_ffogg_strcomment);  i++) {
		if (!ffs_icmpz(name, len, _ffogg_strcomment[i]))
			return i;
	}
	return (uint)-1;
}

/*
ogg_sync_state -> ogg_page -> ogg_stream_state -> ogg_packet -> vorbis_block -> vorbis_dsp_state
*/
int ffogg_decode(ffogg *o)
{
	int r;

	o->pcmlen = 0;

	if (o->nsamples != 0) {
		vorbis_synthesis_read(&o->vds, o->nsamples);
		o->nsamples = 0;
		o->pcmlen = 0;
		goto next_pcm;
	}

	for (;;) {

		r = ffogg_pageread(o);
		if (r == FFOGG_RMORE)
			return FFOGG_RMORE;
		else if (r < 0)
			continue;

		r = ogg_stream_pagein(&o->ostm, &o->opg);
		if (r < 0)
			continue;

		for (;;) {
			ogg_packet opkt;
			float **fpcm;
			int samples;

			r = ogg_stream_packetout(&o->ostm, &opkt);
			if (r == 0)
				break;
			else if (r < 0)
				continue;

			if (0 == vorbis_synthesis(&o->vblk, &opkt))
				vorbis_synthesis_blockin(&o->vds, &o->vblk);

next_pcm:
			if (0 != (samples = vorbis_synthesis_pcmout(&o->vds, &fpcm))) {
				o->pcm = (void*)fpcm;
				o->pcmlen = samples * sizeof(float) * o->vinfo.channels;
				o->nsamples = samples;
				return FFOGG_ROK;
			}
		}

		if (ogg_page_eos(&o->opg))
			break;
	}

	return FFOGG_RDONE;
}
