/**
Copyright (c) 2016 Simon Zolin
*/

#pragma once

#include <FF/string.h>


enum OGG_F {
	OGG_FCONTINUED = 1,
	OGG_FFIRST = 2,
	OGG_FLAST = 4,
};

typedef struct ogg_hdr {
	char sync[4]; //"OggS"
	byte version; //0
	byte flags; //enum OGG_F
	byte granulepos[8];
	byte serial[4];
	byte number[4];
	byte crc[4];
	byte nsegments;
	byte segments[0];
} ogg_hdr;

enum {
	OGG_MAXHDR = sizeof(ogg_hdr) + 255,
};

enum OGG_E {
	OGG_EOK,
	OGG_ESEEK,
	OGG_EHDR,
	OGG_EVERSION,
	OGG_ESERIAL,
	OGG_EPAGENUM,
	OGG_EJUNKDATA,
	OGG_ECRC,
	OGG_ENOSYNC,
	OGG_ECONTPKT,

	OGG_ESYS,
};

typedef struct ffogg_page {
	uint size;
	uint nsegments;
	byte segs[255];

	uint serial;
	uint number;
} ffogg_page;


/** Get page checksum. */
FF_EXTN uint ogg_checksum(const char *d, size_t len);

/** Parse header.
Return header length;  0 if more data is needed;  -OGG_E* on error. */
FF_EXTN int ogg_parse(ogg_hdr *h, const char *data, size_t len);

FF_EXTN uint ogg_pagesize(const ogg_hdr *h);

/** Get number of packets in page. */
FF_EXTN uint ogg_packets(const ogg_hdr *h);

/**
Return offset of the page;  -1 on error. */
FF_EXTN int ogg_findpage(const char *data, size_t len, ogg_hdr *h);

/** Get next packet from page.

Packets may be split across multiple pages:
PAGE0{PKT0 PKT1...}  PAGE1{...PKT1 PKT2}
 In this case the last segment length in PAGE0 is 0xff and PAGE1.flags_continued = 1.

@segoff: current offset within ogg_hdr.segments
@bodyoff: current offset within page body
Return packet body size;  -1 if no more packets;  -2 for an incomplete packet. */
FF_EXTN int ogg_pkt_next(ffstr *pkt, const char *buf, uint *segoff, uint *bodyoff);

/** Add packet into page.
Return the number of bytes written. */
FF_EXTN uint ogg_pkt_write(ffogg_page *p, char *buf, const char *pkt, size_t len);

/** Write page header into the position in buffer before page body.
Buffer: [... OGG_HDR PKT1 PKT2 ...]
@page: is set to page data within buffer.
@flags: enum OGG_F.
Return OGG data length (overhead). */
FF_EXTN uint ogg_page_write(ffogg_page *p, char *buf, uint64 granulepos, uint flags, ffstr *page);
