/** OGG Vorbis.
Copyright (c) 2015 Simon Zolin
*/

#pragma once

#include <FF/array.h>

#include <vorbis/codec.h>


typedef struct ffogg {
	ogg_sync_state osync;
	ogg_page opg;
	ogg_stream_state ostm;

	vorbis_dsp_state vds;
	vorbis_info vinfo;
	vorbis_block vblk;

	vorbis_comment vcmt;
	ffstr tagname
		, tagval;

	union {
	uint nsamples;
	uint nhdr;
	uint ncomm;
	};

	size_t datalen;
	const char *data;

	size_t pcmlen;
	const char *pcm;

	unsigned ostm_valid :1
		, vblk_valid :1
		, comments :1;
} ffogg;

/** Initialize ffogg. */
FF_EXTN void ffogg_init(ffogg *o);

/** Get bps. */
static FFINL uint ffogg_bitrate(ffogg *o, uint64 total_samples, uint64 total_size)
{
	uint64 dur_ms;

	if (total_size == 0)
		return o->vinfo.bitrate_nominal;

	dur_ms = total_samples * 1000 / o->vinfo.rate;
	return (uint)(total_size * 8 * 1000 / dur_ms);
}

#define ffogg_rate(o)  ((o)->vinfo.rate)
#define ffogg_channels(o)  ((o)->vinfo.channels)

static FFINL void ffogg_reset(ffogg *o)
{
	ogg_sync_reset(&o->osync);
}

enum FFOGG_R {
	FFOGG_RERR = -1
	, FFOGG_RMORE = 0
	, FFOGG_ROK
	, FFOGG_RDONE
	, FFOGG_RTAG
};

/** Find the next page. */
FF_EXTN int ffogg_pageseek(ffogg *o);

FF_EXTN int ffogg_pageread(ffogg *o);

#define ffogg_granulepos(o)  ogg_page_granulepos(&(o)->opg)

/** Open OGG stream.
Return enum FFOGG_R. */
FF_EXTN int ffogg_open(ffogg *o);

FF_EXTN void ffogg_close(ffogg *o);

enum FFOGG_VORBTAG {
	FFOGG_ALBUM
	, FFOGG_ARTIST
	, FFOGG_COMMENT
	, FFOGG_DATE
	, FFOGG_GENRE
	, FFOGG_TITLE
	, FFOGG_TRACKNO
};

/** Return enum FFOGG_VORBTAG. */
FF_EXTN uint ffogg_tag(const char *name, size_t len);

/** Decode OGG stream.
Note: decoding errors are skipped. */
FF_EXTN int ffogg_decode(ffogg *o);
