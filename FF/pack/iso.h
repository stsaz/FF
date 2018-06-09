/** .iso reader (ISO 9660 + Joliet, Rock Ridge).
Copyright (c) 2017 Simon Zolin
*/

/*
16 * (sector)
(prim vol-desc) (vol-desc)... [Joliet-vol-desc] (term vol-desc)
(path-table-LE) | (path-table-BE)
[(path-table-Jlt-LE) | (path-table-Jlt-BE)]
((dir-ent [RR-ext...]) | [dir-ent-Jlt] | file-data)...
*/

#pragma once

#include <FF/array.h>
#include <FF/rbtree.h>
#include <FF/chain.h>


enum FFISO_E {
	FFISO_EOK,
	FFISO_ELOGBLK,
	FFISO_ENOPRIM,
	FFISO_EPRIMEMPTY,
	FFISO_EPRIMID,
	FFISO_EPRIMVER,
	FFISO_EUNSUPP,
	FFISO_ELARGE,
	FFISO_ENOTREADY,
	FFISO_EDIRORDER,
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
	ffarr fullfn;

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
	FFISO_DONE, //output .iso is finished
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


struct ffiso_pathtab {
	uint size, off_le, off_be;
};

typedef struct ffiso_cook {
	uint state;
	int err;
	uint64 off;
	ffarr buf;
	ffstr in, out;
	struct ffiso_pathtab pathtab, pathtab_jlt;
	ffarr dirs; //struct dir[]
	ffarr dirs_jlt; //struct dir[]
	uint idir;
	int ifile;
	ffrbtree dirnames; // "dir/name" -> struct dir*
	uint nsectors;
	uint64 curfile_size;
	const char *name; //Volume name
	uint options; //enum FFISO_OPT
	uint filedone :1;
} ffiso_cook;

FF_EXTN const char* ffiso_werrstr(ffiso_cook *c);

/**
@flags: enum FFISO_OPT
Return 0 on success. */
FF_EXTN int ffiso_wcreate(ffiso_cook *c, uint flags);

FF_EXTN void ffiso_wclose(ffiso_cook *c);

/**
Note: no RR PX, no RR CL.
Return enum FFISO_R. */
FF_EXTN int ffiso_write(ffiso_cook *c);

#define ffiso_woffset(c)  ((c)->off)

/** Add a new file.
'f.name' must be normalized.
Files inside directories must be added after the parent directory level is complete
 ("dir/file" after "a", "dir", "z"). */
FF_EXTN void ffiso_wfile(ffiso_cook *c, const ffiso_file *f);

/** Prepare to add a new file data. */
FF_EXTN void ffiso_wfilenext(ffiso_cook *c);

/** All input data is processed. */
FF_EXTN void ffiso_wfinish(ffiso_cook *c);
