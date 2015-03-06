/**
Copyright (c) 2015 Simon Zolin
*/

#pragma once

#include <FFOS/dir.h>


typedef struct ffdirexp {
	size_t size;
	char **names;
	size_t cur;
} ffdirexp;

enum FFDIR_EXP {
	FFDIR_EXP_NOSORT = 1
};

/** Get file names by a wildcard pattern.
@flags: enum FFDIR_EXP
Return !=0 with ENOMOREFILES if none matches the pattern. */
FF_EXTN int ffdir_expopen(ffdirexp *dex, char *pattern, uint flags);

/** Get the next file.
Return NULL if no more files. */
static FFINL const char * ffdir_expread(ffdirexp *dex)
{
	if (dex->cur == dex->size)
		return NULL;
	return dex->names[dex->cur++];
}

FF_EXTN void ffdir_expclose(ffdirexp *dex);
