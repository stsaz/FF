/** AAC ADTS reader (.aac).
Copyright (c) 2017 Simon Zolin
*/

/*
(HDR [CRC] DATA)...
*/

#pragma once

#include <FF/array.h>


enum FFAAC_ADTS_R {
	FFAAC_ADTS_RWARN = -2,
	FFAAC_ADTS_RERR = -1,
	FFAAC_ADTS_RDATA,
	FFAAC_ADTS_RFRAME,
	FFAAC_ADTS_RMORE,
	FFAAC_ADTS_RHDR, //output data contains MPEG-4 ASC
	FFAAC_ADTS_RDONE,
};

FF_EXTN const char* ffaac_adts_errstr(void *a);

enum FFAAC_ADTS_OPT {
	FFAAC_ADTS_OPT_WHOLEFRAME = 1, //return the whole frames with header, not just their body
};

typedef struct ffaac_adts {
	uint state;
	uint nxstate;
	uint err;
	uint frno;
	uint64 off;
	uint gathlen;
	uint frlen;
	int shift;
	uint64 nsamples;
	ffarr buf;
	ffstr in, out;
	char asc[2];
	byte firsthdr[7];
	uint fin :1;
	uint options; //enum FFAAC_ADTS_OPT

	struct {
		byte codec;
		byte channels;
		uint sample_rate;
	} info;
} ffaac_adts;

FF_EXTN void ffaac_adts_open(ffaac_adts *a);
FF_EXTN void ffaac_adts_close(ffaac_adts *a);

/** Return enum FFAAC_ADTS_R. */
FF_EXTN int ffaac_adts_read(ffaac_adts *a);

#define ffaac_adts_pos(a)  ((a)->nsamples - 1024)
#define ffaac_adts_off(a)  ((a)->off)
#define ffaac_adts_frsamples(a)  1024
#define ffaac_adts_froffset(a)  ((a)->off - (a)->out.len)

#define ffaac_adts_input(a, data, len)  ffstr_set(&(a)->in, data, len)
#define ffaac_adts_output(a, dst)  (*(dst) = (a)->out)
#define ffaac_adts_fin(a)  ((a)->fin = 1)
