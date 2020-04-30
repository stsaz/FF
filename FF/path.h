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

	/** Disable automatic FFPATH_WINDOWS. */
	FFPATH_NOWINDOWS = 8,

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
@dstcap: always safe if it's at least @len bytes
@flags: enum FFPATH_FLAGS;  default: FFPATH_MERGESLASH | FFPATH_MERGEDOTS.
Return the number of bytes written in 'dst';  0 on error. */
FF_EXTN size_t ffpath_norm(char *dst, size_t dstcap, const char *path, size_t len, int flags);

static inline size_t ffpath_normstr(ffarr *dst, ffstr path, uint flags)
{
	dst->len = 0;
	if (NULL == ffarr_realloc(dst, path.len))
		return 0;
	dst->len = ffpath_norm(dst->ptr, dst->cap, path.ptr, path.len, flags);
	return dst->len;
}

/** Replace characters that can not be used in a filename.  Trim trailing whitespace. */
FF_EXTN size_t ffpath_makefn(char *dst, size_t dstcap, const char *src, size_t len, int repl_with);

FF_EXTN size_t ffpath_makefn_full(char *dst, size_t dstcap, const char *src, size_t len, uint flags);

/** Make filename: [out_dir/] [in_dir/] in_name .out_ext
idir: the path is converted to relative and normalized */
FF_EXTN int ffpath_makefn_out(ffarr *fn, const ffstr *idir, const ffstr *iname, const ffstr *odir, const ffstr *oext);

#if defined FF_UNIX
#define ffpath_findslash(path, len)  ffs_find(path, len, '/')

/** Find the last slash in path. */
#define ffpath_rfindslash(path, len)  ffs_rfind(path, len, '/')

#else
#define ffpath_findslash(path, len)  ffs_findof(path, len, "/\\", 2)
#define ffpath_rfindslash(path, len)  ffs_rfindof(path, len, "/\\", 2)
#endif

/** Count the number of slash characters. */
FF_EXTN size_t ffpath_nslash(const char *path, size_t len);


enum FFPATH_FN {
	FFPATH_FN_ANY,
	FFPATH_FN_UNIX,
	FFPATH_FN_WIN,
};

/**
@flags: enum FFPATH_FN
Return TRUE if the filename is valid. */
FF_EXTN ffbool ffpath_isvalidfn(const char *fn, size_t len, uint flags);

/** Get filename and directory (without the last slash). */
FF_EXTN const char* ffpath_split2(const char *fn, size_t len, ffstr *dir, ffstr *name);

/** Get name and extension. */
FF_EXTN const char* ffpath_splitname(const char *fullname, size_t len, ffstr *name, ffstr *ext);

FF_EXTN const char* ffpath_split3(const char *fullname, size_t len, ffstr *path, ffstr *name, ffstr *ext);

/** Get the next file path part. */
FF_EXTN ffstr ffpath_next(ffstr *path);

enum FFPATH_CASE {
	FFPATH_CASE_DEF,
	FFPATH_CASE_SENS,
	FFPATH_CASE_ISENS,
};

/** Compare normalized file paths.
Note: backslash ('\\') isn't supported.
@flags: enum FFPATH_CASE
Return 0 if equal;  <0 if p1 < p2;  >0 otherwise. */
FF_EXTN int ffpath_cmp(const ffstr *p1, const ffstr *p2, uint flags);

/** Get max. shared parent directory (without the last slash). */
FF_EXTN int ffpath_parent(const ffstr *p1, const ffstr *p2, ffstr *dir);

/** Check if a path is a parent of another path.
@flags: enum FFPATH_CASE
Return TRUE if match. */
FF_EXTN ffbool ffpath_match(const ffstr *path, const ffstr *match, uint flags);
