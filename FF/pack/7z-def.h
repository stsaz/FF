/**
Copyright (c) 2017 Simon Zolin
*/

#pragma once

#include <FF/time.h>


enum FF7Z_E {
	FF7Z_EOK,
	FF7Z_ESYS,
	FF7Z_EHDRSIGN,
	FF7Z_EHDRVER,
	FF7Z_EHDRCRC,
	FF7Z_EBADID,
	FF7Z_EUKNID,
	FF7Z_EDUPBLOCK,
	FF7Z_ENOREQ,
	FF7Z_EUNSUPP,
	FF7Z_EUKNCODER,
	FF7Z_EFOLDER_FLAGS,
	FF7Z_EMORE,
	FF7Z_EORDER,
	FF7Z_EDATA,
	FF7Z_EDATACRC,
	FF7Z_ELZMA,
	FF7Z_EZLIB,
};

enum FF7Z_METHOD {
	FF7Z_M_UKN,
	FF7Z_M_STORE,
	FF7Z_M_LZMA1,
	FF7Z_M_X86,
	FF7Z_M_X86_BCJ2,
	FF7Z_M_DEFLATE,
	FF7Z_M_LZMA2,
};

typedef struct ff7zfile {
	char *name;
	fftime mtime;
	uint attr; //enum FFWIN_FILEATTR
	uint crc;
	uint64 off;
	uint64 size;
} ff7zfile;
