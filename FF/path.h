/** Path.
Copyright (c) 2013 Simon Zolin
*/

#pragma once

#include <FF/array.h>


enum FFPATH_FLAGS {
	FFPATH_STRICT_BOUNDS = 1
};

/** Parse and process an absolute path.
- Merge slashes //
- Handle . and ..
Windows: all "\" slashes are translated into "/".
Return the number of bytes written in dst.
Return 0 when ".." is out of bounds and FFPATH_STRICT_BOUNDS flag is set. */
FF_EXTN size_t ffpath_norm(char *dst, size_t dstcap, const char *path, size_t len, int flags);

/** Replace characters that can not be used in a filename. */
FF_EXTN size_t ffpath_makefn(char *dst, size_t dstcap, const char *src, size_t len, int repl_with);

#if defined FF_UNIX
/** Find the last slash in path. */
#define ffpath_rfindslash(path, len)  ffs_rfind(path, len, '/')
#define ffpathq_rfindslash(path, len)  ffq_rfind(path, len, '/')

#else
#define ffpath_rfindslash(path, len)  ffs_rfindof(path, len, "/\\", 2)
#define ffpathq_rfindslash(path, len)  ffq_rfindof(path, len, TEXT("/\\"), 2)
#endif

/** Get filename extension without a dot. */
FF_EXTN ffstr ffpath_fileext(const char *fn, size_t len);
