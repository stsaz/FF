/** .iso reader (ISO 9660 + Joliet, Rock Ridge).
Copyright (c) 2017 Simon Zolin
*/

/*
16 * (sector)
(prim vol-desc) (vol-desc)... [Joliet-vol-desc] (term vol-desc)
((dir-ent [RR-ext...]) | [dir-ent-Jlt] | file-data)...
*/

#include <FF/array.h>
#include <FF/chain.h>
#include <FFOS/time.h>


enum FFISO_E {
	FFISO_EOK,
	FFISO_ELOGBLK,
	FFISO_ENOPRIM,
	FFISO_EPRIMEMPTY,
	FFISO_EPRIMID,
	FFISO_EPRIMVER,
	FFISO_EUNSUPP,
	FFISO_ELARGE,
	FFISO_ESYS,
};


typedef struct ffiso_file {
	ffstr name;
	uint attr;
	uint64 off;
	uint64 size;
	fftime mtime;
	ffchain_item sib;
} ffiso_file;

#define ffiso_file_isdir(f)  ((f)->attr & FFUNIX_FILE_DIR)

typedef struct ffiso {
	uint state;
	uint nxstate;
	uint err; //enum FFISO_E
	uint64 fsize;
	uint64 root_start;
	ffiso_file curfile;
	ffchain files;
	ffchain_item *fcursor;
	ffiso_file *curdir;
	ffarr buf;
	ffstr d;
	ffarr fn;

	uint64 inoff;
	ffstr in;
	ffstr out;
	uint options; //enum FFISO_OPT
	uint joliet :1;
} ffiso;

enum FFISO_OPT {
	FFISO_NOJOLIET = 1, //don't parse Joliet extensions
	FFISO_NORR = 2, //don't parse RR extensions
};

FF_EXTN const char* ffiso_errstr(ffiso *c);

FF_EXTN void ffiso_init(ffiso *c);

FF_EXTN void ffiso_close(ffiso *c);

enum FFISO_R {
	FFISO_ERR = -1,
	FFISO_MORE,
	FFISO_SEEK,
	FFISO_HDR, //header is read
	FFISO_FILEMETA, //call ffiso_getfile() to get file meta
	FFISO_LISTEND, //reached the end of meta data
	FFISO_DATA, //call ffiso_output() to get file data
	FFISO_FILEDONE, //file data is finished
};

/**
Return enum FFISO_R. */
FF_EXTN int ffiso_read(ffiso *c);

/** Get current file. */
#define ffiso_getfile(c)  (&(c)->curfile)

/** Get next file header stored in contents table.
Return NULL if no more files. */
FF_EXTN ffiso_file* ffiso_nextfile(ffiso *c);

/** Save file header in contents table. */
FF_EXTN int ffiso_storefile(ffiso *c);

/** Start reading a file. */
FF_EXTN void ffiso_readfile(ffiso *c, ffiso_file *f);

#define ffiso_input(c, data, len) \
	ffstr_set(&(c)->in, data, len)

#define ffiso_output(c)  ((c)->out)

#define ffiso_offset(c)  ((c)->inoff)