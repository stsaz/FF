/** PE reader.
Copyright (c) 2019 Simon Zolin
*/

/*
PE file structure:
DOS_HDR
DOS_STUB
PE_SIG
COFF_HDR
PE_OPT_HDR(
	STD_FIELDS
	WIN_FIELDS
	DATA_DIR(export=0,import,resource,...,[IAT=12],...)
		->IMP_DIR
	)
SECTION_HDR...
...
IMP_DIR... NULL_DIR
	[->LOOKUP_ENT] [->DLLNAME] ->PROCNAME
...
[LOOKUP_ENT... NULL_ENT] [->PROCNAME]
[DLLNAME...]
PROCNAME...
*/

#include <FF/array.h>


enum FFCOFF_MACHINE {
	FFCOFF_MACHINE_I386 = 0x14c,
	FFCOFF_MACHINE_AMD64 = 0x8664,
};

struct ffpe_info {
	uint machine; //enum FFCOFF_MACHINE
	uint sections;
	uint tm_created;
	uint linker_ver[2];
	uint code_size;
	uint init_data_size;
	uint uninit_data_size;
	uint entry_addr;
	uint64 stack_size_res;
	uint64 stack_size_commit;
	uint pe32plus :1;
};

struct ffpe_data_dir {
	uint vaddr;
	uint vsize;
};

struct ffpe_sect {
	const char *name;
	uint vaddr;
	uint vsize;
	uint raw_off;
	uint raw_size;
	uint flags;
};

struct ffpe_imp_ent {
	const char *dll_name;
	const char *sym_name;
	uint64 sym_ordinal;
};

typedef struct ffpe {
	uint state, next;
	int err;
	uint gather_size;

	const struct pe_data_dir *dd;
	const void *dd_end;

	ffarr sections; //struct ffpe_sect[]
	ffarr4 imp_dir;
	ffarr4 imp_lkp;
	ffarr2 iat;

	struct ffpe_data_dir idata;

	struct ffpe_data_dir edata;

	ffarr buf;
	ffstr dat;
	ffstr in;
	uint off;

	struct ffpe_info info;
	struct ffpe_data_dir data_dir;
	struct ffpe_sect section;
	struct ffpe_imp_ent import;
} ffpe;

enum FFPE_R {
	FFPE_ERR, /* error: ffpe_errstr() */
	FFPE_MORE, /* need more input data: ffpe_input() */
	FFPE_SEEK, /* need seek on input: ffpe_offset() */
	FFPE_HDR, /* PE header is complete: ffpe.info */
	FFPE_DD, /* one data-directory is available: ffpe.data_dir */
	FFPE_SECT, /* one section header is available: ffpe.section */
	FFPE_IMPDIR, /* Import directories are available: ffpe.imp_dir */
	FFPE_IMPORT, /* one import entry is available: ffpe.import */
	FFPE_DONE,
};

FF_EXTN int ffpe_open(ffpe *p);

FF_EXTN void ffpe_close(ffpe *p);

FF_EXTN const char* ffpe_errstr(ffpe *p);

/** Set input data. */
#define ffpe_input(p, data, len)  ffstr_set(&(p)->in, data, len)

/** Get input offset. */
#define ffpe_offset(p)  ((p)->off)

/** Read PE data.
Return enum FFPE_R. */
FF_EXTN int ffpe_read(ffpe *p);
