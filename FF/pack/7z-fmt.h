/**
Copyright (c) 2017 Simon Zolin
*/

#pragma once

#include <FF/array.h>
#include <FF/pack/7z-def.h>


enum {
	Z7_GHDR_LEN = 32,
	Z7_MAX_BLOCK_DEPTH = 4 + 1,
};


struct z7_info {
	uint64 hdroff;
	uint64 hdrsize;
	uint hdrcrc;
};

FF_EXTN int z7_ghdr_read(struct z7_info *info, const char *data);


enum Z7_T {
	T_End = 0x00,
	T_Header = 0x01,
	T_AdditionalStreamsInfo = 0x03,
	T_MainStreamsInfo = 0x04,
	T_FilesInfo = 0x05,
	T_PackInfo = 0x06,
	T_UnPackInfo = 0x07,
	T_SubStreamsInfo = 0x08,
	T_Size = 0x09,
	T_CRC = 0x0A,
	T_Folder = 0x0B,
	T_UnPackSize = 0x0C,
	T_NumUnPackStream = 0x0D,
	T_EmptyStream = 0x0E,
	T_EmptyFile = 0x0F,
	T_Name = 0x11,
	T_MTime = 0x14,
	T_WinAttributes = 0x15,
	T_EncodedHeader = 0x17,
};

enum Z7_F {
	F_CHILDREN = 0x100,
	F_REQ = 0x200,
	F_LAST = 0x400,
	F_SIZE = 0x800, //read block size (varint) before block body
	F_SELF = 0x1000,
	F_ALLOC_FILES = 0x4000, //allocate files array before processing this block
};

#define PRIO(n)  (n) << 24
#define GET_PRIO(f) ((f) >> 24) & 0xff

struct z7_bblock {
	uint flags;
	const void *data;
};

struct z7_block {
	uint id;
	uint used;
	uint prio;
	const struct z7_bblock *children;
};

struct z7_coder {
	byte method;
	byte nprops;
	byte props[8];
};

typedef struct z7_stream {
	uint64 off;
	uint64 pack_size;
	uint64 unpack_size;
	uint crc;
	struct z7_coder coder[2];
	ffarr files; //ff7zfile[]
	uint64 ifile;

	ffarr empty; // bit-array of total files in archive. Valid for the last stream that contains empty files.
} z7_stream;

FF_EXTN const struct z7_bblock z7_ctx_top[];

FF_EXTN int z7_varint(const char *data, size_t len, uint64 *val);

/** Search block by ID. */
FF_EXTN int z7_find_block(uint blk_id, const struct z7_bblock **pblock, struct z7_block *parent);

/** Ensure the required blocks were processed. */
FF_EXTN int z7_check_req(const struct z7_block *ctx);
