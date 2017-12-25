/**
Copyright (c) 2015 Simon Zolin
*/

#pragma once

#include <FFOS/dir.h>


typedef struct ffdirexp {
	size_t size;
	char **names;
	size_t cur;

	char *path_fn; //storage for path+fn
	size_t pathlen;

	uint flags;
} ffdirexp;

enum FFDIR_EXP {
	FFDIR_EXP_NOSORT = 1,
	FFDIR_EXP_DOT12 = 2, //include "." and ".."
	FFDIR_EXP_REL = 4, //output relative filenames
};

/** Get file names by a wildcard pattern.
@pattern:
 "/path/ *.txt"
 "/path" (i.e. "/path/ *")
 "*.txt" (all .txt files in the current directory)
@flags: enum FFDIR_EXP
Return !=0 with ENOMOREFILES if none matches the pattern. */
FF_EXTN int ffdir_expopen(ffdirexp *dex, char *pattern, uint flags);

/** Get the next file.
Return NULL if no more files. */
FF_EXTN const char* ffdir_expread(ffdirexp *dex);

/** Get file name without path. */
#define ffdir_expname(dex, path)  ((path) + (dex)->pathlen)

FF_EXTN void ffdir_expclose(ffdirexp *dex);
