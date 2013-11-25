/**
Copyright (c) 2013 Simon Zolin
*/

#include <FF/path.h>
#include <FFOS/dir.h>


size_t ffpath_norm(char *dst, size_t dstcap, const char *path, size_t len, int flags)
{
	enum { iNorm, iSlash, iDot, iDot2 };
	ffbool strictBounds = (flags & FFPATH_STRICT_BOUNDS);
	int idx = iNorm;
	size_t i;
	const char *dsto = dst;
	char *r;

	if (!ffpath_abs(path, len))
		return 0;

	for (i = 0; i < len; ++i) {
		int ch = path[i];

		switch (idx) {
		case iNorm:
			if (ffpath_slash(ch)) {
				*dst++ = '/';
				idx = iSlash;
			}
			else {
				*dst++ = ch;
				if (ch == '.')
					idx = iDot;
			}
			break;

		case iSlash:
			if (!ffpath_slash(ch)) { // "//"
				i--;
				idx = iNorm;
			}
			break;

		case iDot:
			if (ffpath_slash(ch)) { // "/./"
				dst -= FFSLEN(".");
				idx = iSlash;
			}
			else {
				*dst++ = ch;
				idx = ch == '.' ? iDot2 : iNorm;
			}
			break;

		case iDot2:
			if (ffpath_slash(ch)) { // "/../"
				dst -= FFSLEN("/..");
				r = ffs_rfind(dsto, dst - dsto, '/');
				if (r == dst && strictBounds)
					return 0;
				dst = r + 1;
				idx = iSlash;
			}
			else {
				*dst++ = ch;
				idx = iNorm;
			}
			break;
		}
	}

	if (idx == iDot)
		dst -= FFSLEN(".");

	if (idx == iDot2) {
		dst -= FFSLEN("/..");
		r = ffs_rfind(dsto, dst - dsto, '/');
		if (r == dst && strictBounds)
			return 0;
		dst = r + 1;
	}

	return dst - dsto;
}

static const char badFilenameChars[] = "*?/\\:\"";

size_t ffpath_makefn(char *dst, size_t dstcap, const char *src, size_t len, int repl_with)
{
	size_t i;
	const char *dsto = dst;
	const char *end = dst + dstcap;

	for (i = 0;  i < len && dst != end;  i++) {
		const char *pos = src + i;
		if ((byte)src[i] >= ' ') //replace chars 0-32
			pos = ffs_findof(src + i, 1, badFilenameChars, FFCNT(badFilenameChars));
		if (pos == src + i)
			*dst = (byte)repl_with;
		else
			*dst = src[i];
		dst++;
	}
	return dst - dsto;
}

ffstr ffpath_fileext(const char *fn, size_t len)
{
	ffstr s = { 0 };
	char *pos = ffs_rfind(fn, len, '.');
	if (pos != fn + len) {
		pos++;
		ffstr_set(&s, pos, fn + len - pos);
	}
	return s;
}

