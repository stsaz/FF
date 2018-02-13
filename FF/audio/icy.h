/** ICY media stream.
Copyright (c) 2013 Simon Zolin
*/

/*
(DATA * "icy-metaint")  [META] ...

META:
(NMETA)  (StreamTitle='artist - track';StreamUrl='';  [PADDING])
*/

#pragma once

#include <FF/array.h>
#include <FF/data/parse.h>


enum FFICY {
	FFICY_MAXMETA = 1 + 0xff * 16 //max possible length of metadata
	, FFICY_NOMETA = ~0U
};

enum FFICY_HDR {
	FFICY_HNAME
	, FFICY_HGENRE
	, FFICY_HURL
	, FFICY_HMETAINT

	//client:
	, FFICY_HMETADATA //if "1", client supports metadata
};

/** ICY header as a string. */
FF_EXTN const ffstr fficy_shdr[];


typedef struct fficy {
	uint meta_interval; //size of media data between meta blocks (constant)
	uint datasize
		, metasize;

	uint meta_len; //valid bytes in "meta"
	char meta[FFICY_MAXMETA]; //full metadata gathered from partial data blocks
} fficy;

enum FFICY_R {
	FFICY_RMETACHUNK //metadata (incomplete)
	, FFICY_RMETA
	, FFICY_RDATA
};

/**
@meta_interval: meta interval (in bytes) or FFICY_NOMETA. */
static FFINL void fficy_parseinit(fficy *ic, uint meta_interval)
{
	ic->meta_interval = ic->datasize = meta_interval;
	ic->metasize = 0;
}

/** Get the next block from an ICY stream.
@len: [in]: length of @data.  [out]: processed bytes.
Return enum FFICY_R. */
FF_EXTN int fficy_parse(fficy *ic, const char *data, size_t *len, ffstr *dst);

/** Get size of metadata in bytes. */
#define fficy_metasize(nmeta)  ((uint)(byte)(nmeta) * 16)


typedef ffparser fficymeta;

#define fficy_metaparse_init(p)  ffpars_init(p)

/** Parse ICY meta.  Get next key and its value (KEY='VAL';...).
Return enum FFPARS_E. */
FF_EXTN int fficy_metaparse(fficymeta *p, const char *data, size_t *len);
FF_EXTN int fficy_metaparse_str(fficymeta *p, ffstr *meta, ffstr *dst);

/** Get artist and title from StreamTitle tag. */
FF_EXTN int fficy_streamtitle(const char *data, size_t len, ffstr *artist, ffstr *title);


/** Prepare for writing ICY meta.
@buf: optional */
FF_EXTN int fficy_initmeta(ffarr *meta, char *buf, size_t cap);

/** Add name-value pair into meta.
Return bytes written. */
FF_EXTN size_t fficy_addmeta(ffarr *meta, const char *key, size_t keylen, const char *val, size_t vallen);

/** Finalize meta.
Return the final meta size. */
FF_EXTN uint fficy_finmeta(ffarr *meta);
