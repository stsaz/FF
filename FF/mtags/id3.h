/** ID3v1, ID3v2 tags.
Copyright (c) 2013 Simon Zolin
*/

/*
[ID3v2]  DATA...  [ID3v1]

ID3v2:
ID3-HEADER  [EXT-HDR]  (FRAME-HEADER  [TEXT-ENCODING]  DATA...)...  PADDING
*/

#pragma once

#include <FF/mtags/mmtag.h>
#include <FF/array.h>


//128 bytes
typedef struct ffid31 {
	char tag[3]; //"TAG"
	char title[30];
	char artist[30];
	char album[30];
	char year[4];

	union {
	char comment30[30];

	// if comment30[28] == '\0':
	struct {
	char comment[29];
	byte track_no; //undefined: 0
	};};

	byte genre; //undefined: 0xff
} ffid31;

/** Return TRUE if valid ID3v1 header. */
static FFINL ffbool ffid31_valid(const ffid31 *h)
{
	return h->tag[0] == 'T' && h->tag[1] == 'A' && h->tag[2] == 'G';
}

typedef struct ffid31ex {
	uint state;
	char trkno[sizeof("255")];
	int field; //enum FFID3_FRAME
	ffstr val;
} ffid31ex;

/** Get the next value from ID3v1 tag.
Return enum FFID3_R. */
FF_EXTN int ffid31_parse(ffid31ex *id31ex, const char *data, size_t len);


/** Prepare ID3v1 tag. */
FF_EXTN void ffid31_init(ffid31 *id31);

/**
@id: enum FFID3_FRAME
Return the number of bytes copied. */
FF_EXTN int ffid31_add(ffid31 *id31, uint id, const char *data, size_t len);

/** Return genre as text. */
FF_EXTN const char* ffid31_genre(uint id);


//10 bytes
typedef struct ffid3_hdr {
	char id3[3]; //"ID3"
	byte ver[2]; //e.g. \3 \0
	byte flags; //enum FFID3_FHDR
	byte size[4]; //7-bit numbers
} ffid3_hdr;

enum FFID3_FHDR {
	FFID3_FHDR_EXTHDR = 0x40, // extended header follows
	FFID3_FHDR_UNSYNC = 0x80,
};

/** Return TRUE if valid ID3v2 header. */
FF_EXTN ffbool ffid3_valid(const ffid3_hdr *h);

/** Get length of ID3v2 data. */
FF_EXTN uint ffid3_size(const ffid3_hdr *h);


typedef struct ffid3_exthdr {
	byte size[4];
	//...
} ffid3_exthdr;


enum FFID3_FRAME {
	FFID3_LENGTH = _FFMMTAG_N,
};

//10 bytes
typedef struct ffid3_frhdr {
	char id[4];
	byte size[4]; //v2.4: 7-bit numbers.
	byte flags[2]; //[1]: enum FFID324_FFR1
} ffid3_frhdr;

enum FFID324_FFR1 {
	FFID324_FFR1_DATALEN = 1, //4 bytes follow the frame header
	FFID324_FFR1_UNSYNC = 2,
};

//6 bytes
typedef struct ffid3_frhdr22 {
	char id[3];
	byte size[3];
} ffid3_frhdr22;

enum FFID3_TXTENC {
	FFID3_ANSI
	, FFID3_UTF16BOM
	, FFID3_UTF16BE //no BOM.  v2.4
	, FFID3_UTF8 //v2.4
};


enum FFID3_F {
	FFID3_FWHOLE = 1 //the whole frame data will be reported once via FFID3_RDATA
};

typedef struct ffid3 {
	ffid3_hdr h;
	union {
	ffid3_frhdr fr; //the currently processed frame
	ffid3_frhdr22 fr22;
	};
	uint state;
	uint nxstate;
	uint gstate;
	uint gsize;
	uint size //bytes left in the whole ID3v2
		, frsize; //bytes left in the frame
	uint unsync_n; // number of bytes skipped (with 'unsync' flag)
	int txtenc; //enum FFID3_TXTENC
	uint flags; //enum FFID3_F
	int frame; //enum FFMMTAG or enum FFID3_FRAME (<0)
	int err;
	ffstr3 data; //frame data
} ffid3;

enum FFID3_R {
	FFID3_RDONE
	, FFID3_RMORE
	, FFID3_RERR
	, FFID3_RHDR
	, FFID3_RFRAME
	, FFID3_RDATA //ffid3.data contains a chunk of frame data
	, FFID3_RNO
};

static FFINL void ffid3_parseinit(ffid3 *p)
{
	ffmem_tzero(p);
	p->txtenc = -1;
}

/** Parse ID3v2.
@len: [in] length of @data.  [out] processed bytes.
Return enum FFID3_R. */
FF_EXTN int ffid3_parse(ffid3 *p, const char *data, size_t *len);

static FFINL void ffid3_parsefin(ffid3 *p)
{
	ffarr_free(&p->data);
}

FF_EXTN const char* ffid3_errstr(int e);


/** Get data from ID3v2 frame.  Convert to UTF-8 if necessary.
@frame: enum FFID3_FRAME or -1.
@txtenc: enum FFID3_TXTENC.
@codepage: code page, 0 (default) or FFU_*.
Return number of bytes copied. */
FF_EXTN int ffid3_getdata(int frame, const char *data, size_t len, int txtenc, uint codepage, ffstr3 *dst);


typedef struct ffid3_cook {
	ffarr buf;
	char trackno[32];
	char tracktotal[32];
} ffid3_cook;

/** Add ID3v2 frame.
Return the number of bytes written. */
FF_EXTN uint ffid3_addframe(ffid3_cook *id3, const char id[4], const char *data, size_t len, uint flags);

FF_EXTN uint ffid3_add(ffid3_cook *id3, uint id, const char *data, size_t len);

FF_EXTN uint ffid3_flush(ffid3_cook *id3);

FF_EXTN int ffid3_padding(ffid3_cook *id3, size_t len);

/** Write ID3v2 header. */
FF_EXTN void ffid3_fin(ffid3_cook *id3);
