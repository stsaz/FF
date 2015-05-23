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
Return 0 on error:
	- not an absolute path.
	- path contains invalid characters: \0.
	- not enough space.
	- ".." is out of bounds and FFPATH_STRICT_BOUNDS flag is set. */
FF_EXTN size_t ffpath_norm(char *dst, size_t dstcap, const char *path, size_t len, int flags);

/** Replace characters that can not be used in a filename. */
FF_EXTN size_t ffpath_makefn(char *dst, size_t dstcap, const char *src, size_t len, int repl_with);

#if defined FF_UNIX
/** Find the last slash in path. */
#define ffpath_rfindslash(path, len)  ffs_rfind(path, len, '/')

/** Return TRUE if the filename is valid. */
#define ffpath_isvalidfn(fn, len)  ((fn) + (len) == ffs_findof(fn, len, "/\0", 2))

#else
#define ffpath_rfindslash(path, len)  ffs_rfindof(path, len, "/\\", 2)
#define ffpath_isvalidfn(fn, len)  ((fn) + (len) == ffs_findof(fn, len, "/\\\0", 3))
#endif

/** Get filename and directory (without the last slash). */
static FFINL const char* ffpath_split2(const char *fn, size_t len, ffstr *dir, ffstr *name)
{
	const char *slash = ffpath_rfindslash(fn, len);
	if (slash == fn + len) {
		if (dir != NULL)
 			dir->len = 0;
		if (name != NULL)
			ffstr_set(name, fn, len);
		return NULL;
 	}
	return ffs_split2(fn, len, slash, dir, name);
}
