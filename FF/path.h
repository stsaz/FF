/** Path.
Copyright (c) 2013 Simon Zolin
*/

#pragma once

#include <FF/array.h>


enum FFPATH_FLAGS {
	/** Merge duplicate slashes.
	 "/1///2" -> "/1/2" */
	FFPATH_MERGESLASH = 1,

	/** Handle "." and ".." in path.
	 "./1/./2/3" -> "1/2/3"
	 "/../1/../2/3" -> "/2/3"
	Note: ".." in relative paths such as below aren't merged:
	 "../1/../2/3" -> "../1/../2/3" */
	_FFPATH_MERGEDOTS = 2,
	FFPATH_MERGEDOTS = _FFPATH_MERGEDOTS | FFPATH_MERGESLASH,

	/** Fail if path is out of bounds.
	 "/../1" or "../1" -> ERROR */
	_FFPATH_STRICT_BOUNDS = 4,
	FFPATH_STRICT_BOUNDS = _FFPATH_STRICT_BOUNDS | FFPATH_MERGESLASH,

	/** Support "\" backslash and Windows disk drives "x:".  Always enabled on Windows.
	Fail if found a character prohibited to use in filename on Windows: *?:" */
	FFPATH_WINDOWS = 0x10,

	/** Convert all "\" slashes to "/".
	 "c:\1\2" -> "c:/1/2" */
	FFPATH_FORCESLASH = 0x20,

	/** Convert all "/" slashes to "\".
	 "c:/1/2" -> "c:\1\2" */
	FFPATH_FORCEBKSLASH = 0x40,

	/** Convert path to relative.
	 "/path" -> "path"
	 "../1/2" -> "1/2"
	 "c:/path" -> "path" (with FFPATH_WINDOWS) */
	FFPATH_TOREL = 0x100,
};

/** Process an absolute or relative path.
@flags: enum FFPATH_FLAGS;  default: FFPATH_MERGESLASH | FFPATH_MERGEDOTS.
Return the number of bytes written in 'dst';  0 on error. */
FF_EXTN size_t ffpath_norm(char *dst, size_t dstcap, const char *path, size_t len, int flags);

/** Replace characters that can not be used in a filename.  Trim trailing whitespace. */
FF_EXTN size_t ffpath_makefn(char *dst, size_t dstcap, const char *src, size_t len, int repl_with);

#if defined FF_UNIX
#define ffpath_findslash(path, len)  ffs_find(path, len, '/')

/** Find the last slash in path. */
#define ffpath_rfindslash(path, len)  ffs_rfind(path, len, '/')

/** Return TRUE if the filename is valid. */
#define ffpath_isvalidfn(fn, len)  ((fn) + (len) == ffs_findof(fn, len, "/\0", 2))

#else
#define ffpath_findslash(path, len)  ffs_findof(path, len, "/\\", 2)
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

/** Get name and extension. */
FF_EXTN const char* ffpath_splitname(const char *fullname, size_t len, ffstr *name, ffstr *ext);
