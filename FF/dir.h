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
} ffdirexp;

enum FFDIR_EXP {
	FFDIR_EXP_NOSORT = 1,
	FFDIR_EXP_DOT12 = 2, //include "." and ".."
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
static FFINL const char * ffdir_expread(ffdirexp *dex)
{
	if (dex->cur == dex->size)
		return NULL;
	strcpy(dex->path_fn + dex->pathlen, dex->names[dex->cur++]);
	return dex->path_fn;
}

FF_EXTN void ffdir_expclose(ffdirexp *dex);
