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
	ffid31 tag;
	uint ntag;
	char trkno[sizeof("255")];
	int field; //enum FFID3_FRAME
	ffstr val;
} ffid31ex;

#define ffid31_parse_fin(id31ex)

/** Get the next value from ID3v1 tag.
Return enum FFID3_R. */
FF_EXTN int ffid31_parse(ffid31ex *id31ex, const char *data, size_t *len);


/** Prepare ID3v1 tag. */
FF_EXTN void ffid31_init(ffid31 *id31);

/**
@id: enum FFID3_FRAME
Return the number of bytes copied. */
FF_EXTN int ffid31_add(ffid31 *id31, uint id, const char *data, size_t len);


//10 bytes
typedef struct ffid3_hdr {
	char id3[3]; //"ID3"
	byte ver[2]; //e.g. \3 \0
	union {
	byte flags;
	struct {
		byte unsupported :7
			, unsync :1;
	};
	};
	byte size[4]; //7-bit numbers
} ffid3_hdr;

/** Return TRUE if valid ID3v2 header. */
FF_EXTN ffbool ffid3_valid(const ffid3_hdr *h);

/** Get length of ID3v2 data. */
FF_EXTN uint ffid3_size(const ffid3_hdr *h);


enum FFID3_FRAME {
	FFID3_PICTURE //APIC
	, FFID3_COMMENT //COMM: "LNG" "SHORT" \0 "TEXT"
	, FFID3_ALBUM //TALB
	, FFID3_GENRE //TCON: "Genre" | "(NN)Genre" | "(NN)" where NN is ID3v1 genre index
	, FFID3_RECTIME //TDRC: "yyyy[-MM[-dd[THH[:mm[:ss]]]]]"
	, FFID3_ENCODEDBY //TENC
	, FFID3_TITLE //TIT2
	, FFID3_LENGTH //TLEN
	, FFID3_ARTIST //TPE1
	, FFID3_ALBUMARTIST //TPE2
	, FFID3_TRACKNO //TRCK: "N[/TOTAL]"
	, FFID3_YEAR //TYER

	, FFID3_TRACKTOTAL
};

FF_EXTN const char ffid3_frames[][4];

//10 bytes
typedef struct ffid3_frhdr {
	char id[4];
	byte size[4]; //v2.4: 7-bit numbers.
	union {
	byte flags[2];
	struct {
		byte unsupported1;
		byte unsupported2 :1
			, unsync :1
			, unsupported3 :6;
	};
	};
} ffid3_frhdr;

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
	uint size //bytes left in the whole ID3v2
		, frsize; //bytes left in the frame
	int txtenc; //enum FFID3_TXTENC
	uint flags; //enum FFID3_F
	int frame; //enum FFID3_FRAME or -1 if unknown frame
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

/** Write ID3v2 header. */
FF_EXTN void ffid3_fin(ffid3_cook *id3);
