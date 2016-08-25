/** APE tag.
Copyright (c) 2015 Simon Zolin
*/

/*
[HDR]  (VAL_SIZE  FLAGS  NAME\0  VAL)...  FOOTER
*/

#pragma once

#include <FF/array.h>


//32 bytes
typedef struct ffapehdr {
	char id[8]; //"APETAGEX"
	uint ver; //2000
	uint size; //all fields + footer
	uint nfields;
	uint unsupported :31
		, has_hdr :1;
	char reserved[8];
} ffapehdr;

enum FFAPETAG_FLAGS {
	FFAPETAG_FMASK = 6,
	FFAPETAG_FBINARY = 1 << 1,
};

typedef struct ffapefld {
	uint val_len;
	uint flags; //enum FFAPETAG_FLAGS
	char name_val[0];
} ffapefld;

static FFINL ffbool ffapetag_valid(const ffapehdr *a)
{
	return !ffs_cmp(a->id, "APETAGEX", 8)
		&& a->ver == 2000
		&& a->size >= sizeof(ffapehdr);
}

/** Get the whole tag size (header+fields+footer). */
static FFINL uint ffapetag_size(const ffapehdr *a)
{
	return a->has_hdr ? a->size + sizeof(ffapehdr) : a->size;
}


typedef struct ffapetag {
	uint state;
	uint size;
	uint nlen;
	ffarr buf;
	uint has_hdr :1;

	int tag; //enum FFMMTAG
	uint flags; //enum FFAPETAG_FLAGS
	ffstr name
		, val;
} ffapetag;

enum FFAPETAG_R {
	FFAPETAG_RERR = -1,
	FFAPETAG_RNO,
	FFAPETAG_RFOOTER,
	FFAPETAG_RSEEK,
	FFAPETAG_RMORE,
	FFAPETAG_RTAG,
	FFAPETAG_RDONE,
};

static FFINL void ffapetag_parse_fin(ffapetag *a)
{
	ffarr_free(&a->buf);
}

/**
@len: [in] length of @data.  [out] processed bytes.
Return enum FFAPETAG_R. */
FF_EXTN int ffapetag_parse(ffapetag *a, const char *data, size_t *len);
