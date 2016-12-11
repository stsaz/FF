/**
Copyright (c) 2013 Simon Zolin
*/

#include <FF/path.h>
#include <FFOS/dir.h>


size_t ffpath_norm(char *dst, size_t dstcap, const char *path, size_t len, int flags)
{
	ffstr name;
	const char *p = path, *path_end = path + len, *dstend = dst + dstcap;
	char *dsto = dst, *slash, *prev_slash;
	int lev = 0, abs = 0, abs_win, root;

	if (flags == 0)
		flags = FFPATH_MERGESLASH | FFPATH_MERGEDOTS;

#ifdef FF_WIN
	flags |= FFPATH_WINDOWS;
#endif

	if (p[0] == '/')
		abs = 1;
	else if (flags & FFPATH_WINDOWS) {
		abs_win = (len >= FFSLEN("c:") && p[1] == ':' && ffchar_isletter(p[0]));
		if (abs_win || p[0] == '\\')
			abs = 1;
	}
	root = abs;

	for (;  p < path_end;  p = slash + 1) {

		if (flags & FFPATH_WINDOWS)
			slash = ffs_findof(p, path_end - p, "/\\", 2);
		else
			slash = ffs_find(p, path_end - p, '/');
		ffstr_set(&name, p, slash - p);

		if ((flags & FFPATH_MERGESLASH) && name.len == 0 && slash != path)
			continue;

		else if (name.len == 1 && name.ptr[0] == '.') {
			if (flags & _FFPATH_MERGEDOTS)
				continue;

		} else if (name.len == 2 && name.ptr[0] == '.' && name.ptr[1] == '.') {
			lev--;

			if ((flags & _FFPATH_STRICT_BOUNDS) && lev < 0) {
				return 0;

			} else if (flags & FFPATH_TOREL) {
				dst = dsto;
				continue;

			} else if (flags & _FFPATH_MERGEDOTS) {
				if (lev < 0) {
					if (abs) {
						lev = 0;
						continue; // "/.." -> "/"
					}

					// relative path: add ".." as is

				} else {
					if (dst != dsto)
						dst -= FFSLEN("/");

					if (flags & FFPATH_WINDOWS)
						prev_slash = ffs_rfindof(dsto, dst - dsto, "/\\", 2);
					else
						prev_slash = ffs_rfind(dsto, dst - dsto, '/');

					if (prev_slash == dst)
						dst = dsto; // "abc/.." -> ""
					else
						dst = prev_slash + 1;
					continue;
				}
			}

		} else if (root) {
			root = 0;

		} else {
			if (flags & FFPATH_WINDOWS) {
				if (ffarr_end(&name) != ffs_findof(name.ptr, name.len, "*?:\"\0", 5))
					return 0;
			} else {
				if (ffarr_end(&name) != ffs_find(name.ptr, name.len, '\0'))
					return 0;
			}

			lev++;
		}

		if (dst + name.len + (slash != path_end) > dstend)
			return 0;
		memmove(dst, name.ptr, name.len);
		dst += name.len;
		if (slash != path_end) {
			if (flags & (FFPATH_FORCESLASH | FFPATH_FORCEBKSLASH))
				*dst++ = "/\\"[(flags & FFPATH_FORCEBKSLASH) != 0];
			else
				*dst++ = *slash;
		}
	}

	if (dst == dsto && len != 0)
		*dst++ = '.';

	return dst - dsto;
}

/** All printable except *, ?, /, \\, :, \", <, >, |. */
static const uint _ffpath_charmask_filename[] = {
	0,
	            // ?>=< ;:98 7654 3210  /.-, +*)( '&%$ #"!
	0x2bff7bfb, // 0010 1011 1111 1111  0111 1011 1111 1011
	            // _^]\ [ZYX WVUT SRQP  ONML KJIH GFED CBA@
	0xefffffff, // 1110 1111 1111 1111  1111 1111 1111 1111
	            //  ~}| {zyx wvut srqp  onml kjih gfed cba`
	0x6fffffff, // 0110 1111 1111 1111  1111 1111 1111 1111
	0xffffffff,
	0xffffffff,
	0xffffffff,
	0xffffffff
};

size_t ffpath_makefn(char *dst, size_t dstcap, const char *src, size_t len, int repl_with)
{
	size_t i;
	const char *dsto = dst;
	const char *end = dst + dstcap;

	const char *pos = ffs_rskip(src, len, ' ');
	len = pos - src;

	for (i = 0;  i < len && dst != end;  i++) {
		if (!ffbit_testarr(_ffpath_charmask_filename, (byte)src[i]))
			*dst = (byte)repl_with;
		else
			*dst = src[i];
		dst++;
	}
	return dst - dsto;
}

const char* ffpath_splitname(const char *fullname, size_t len, ffstr *name, ffstr *ext)
{
	char *dot = ffs_rfind(fullname, len, '.');
	if (dot == fullname) {
		//handle filename ".foo"
		if (name != NULL)
			ffstr_set(name, fullname, len);
		if (ext != NULL)
			ext->len = 0;
		return dot;
	}
	return ffs_split2(fullname, len, dot, name, ext);
}
