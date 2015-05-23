/** ID3v1, ID3v2 tags.
Copyright (c) 2013 Simon Zolin
*/

/*
[ID3v2]  DATA...  [ID3v1]

ID3v2:
ID3-HEADER  (FRAME-HEADER  [TEXT-ENCODING]  DATA...)...  PADDING
*/

#pragma once

#include <FF/array.h>


//128 bytes
typedef struct ffid31 {
	char tag[3]; //"TAG"
	char title[30];
	char artist[30];
	char album[30];
	byte year[4];
	char comment[29];
	byte track_no; //undefined: 0
	byte genre; //undefined: 0xff
} ffid31;

/** Return TRUE if valid ID3v1 header. */
static FFINL ffbool ffid31_valid(const ffid31 *h)
{
	return h->tag[0] == 'T' && h->tag[1] == 'A' && h->tag[2] == 'G';
}


//10 bytes
typedef struct ffid3_hdr {
	char id3[3]; //"ID3"
	byte ver[2]; //e.g. \3 \0
	byte flags;
	byte size[4]; //7-bit numbers
} ffid3_hdr;

/** Return TRUE if valid ID3v2 header. */
FF_EXTN ffbool ffid3_valid(const ffid3_hdr *h);

/** Get length of ID3v2 data. */
FF_EXTN uint ffid3_size(const ffid3_hdr *h);


enum FFID3_FRAME {
	FFID3_PICTURE //APIC
	, FFID3_COMMENT //COMM
	, FFID3_ALBUM //TALB
	, FFID3_GENRE //TCON
	, FFID3_TITLE //TIT2
	, FFID3_ARTIST //TPE1
	, FFID3_ALBUMARTIST //TPE2
	, FFID3_TRACKNO //TRCK
	, FFID3_YEAR //TYER
};

FF_EXTN const char ffid3_frames[][4];

//10 bytes
typedef struct ffid3_frhdr {
	char id[4];
	byte size[4]; //v2.4: 7-bit numbers.
	byte flags[2];
} ffid3_frhdr;

enum FFID3_TXTENC {
	FFID3_ANSI
	, FFID3_UTF16BOM
	, FFID3_UTF16BE //no BOM.  v2.4
	, FFID3_UTF8 //v2.4
};

/** Return enum FFID3_FRAME. */
FF_EXTN uint ffid3_frame(const ffid3_frhdr *fr);

/** Get frame size.
@majver: ffid3_hdr.ver[0] */
FF_EXTN uint ffid3_frsize(const ffid3_frhdr *fr, uint majver);


enum FFID3_F {
	FFID3_FWHOLE = 1 //the whole frame data will be reported once via FFID3_RDATA
};

typedef struct ffid3 {
	ffid3_hdr h;
	ffid3_frhdr fr; //the currently processed frame
	uint state;
	uint size //bytes left in the whole ID3v2
		, frsize; //bytes left in the frame
	uint txtenc; //enum FFID3_TXTENC
	uint flags; //enum FFID3_F
	ffstr3 data; //frame data
} ffid3;

enum FFID3_R {
	FFID3_RDONE
	, FFID3_RMORE
	, FFID3_RERR
	, FFID3_RHDR
	, FFID3_RFRAME
	, FFID3_RDATA //ffid3.data contains a chunk of frame data
};

static FFINL void ffid3_parseinit(ffid3 *p)
{
	ffmem_tzero(p);
}

/** Parse ID3v2.
Note: the first data chunk must be at least of sizeof(ffid3_hdr) bytes long.
@len: [in] length of @data.  [out] processed bytes.
Return enum FFID3_R. */
FF_EXTN int ffid3_parse(ffid3 *p, const char *data, size_t *len);

static FFINL void ffid3_parsefin(ffid3 *p)
{
	ffarr_free(&p->data);
}


/** Get data from ID3v2 frame.  Convert to UTF-8 if necessary.
@txtenc: enum FFID3_TXTENC.
@codepage: code page, 0 (default) or FFU_*.
Return number of bytes copied. */
FF_EXTN int ffid3_getdata(const char *data, size_t len, uint txtenc, uint codepage, ffstr3 *dst);
