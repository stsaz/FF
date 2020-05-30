/**
Copyright (c) 2013 Simon Zolin
*/

#include <FF/path.h>
#include <FFOS/dir.h>


static const char ffpath_fn_restricted_win[] = "/\\:*\"?<>|\0";

size_t ffpath_norm(char *dst, size_t dstcap, const char *path, size_t len, int flags)
{
	ffstr name;
	const char *p = path, *path_end = path + len, *dstend = dst + dstcap;
	char *dsto = dst, *slash, *prev_slash;
	int lev = 0, abs = 0, abs_win, root;

	if (flags == 0)
		flags = FFPATH_MERGESLASH | FFPATH_MERGEDOTS;

#ifdef FF_WIN
	flags |= (flags & FFPATH_NOWINDOWS) ? 0 : FFPATH_WINDOWS;
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
			if (flags & FFPATH_TOREL)
				continue;

		} else {
			if (flags & FFPATH_WINDOWS) {
				if (-1 != ffstr_findanyz(&name, ffpath_fn_restricted_win))
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
			if (flags & FFPATH_FORCESLASH)
				*dst++ = '/';
			else if (flags & FFPATH_FORCEBKSLASH)
				*dst++ = '\\';
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

size_t ffpath_makefn_full(char *dst, size_t dstcap, const char *src, size_t len, uint flags)
{
	size_t i, k = 0;
	int repl_with = (byte)flags;
	for (i = 0;  i != len;  i++) {
		if (k == dstcap)
			break;
		if (!ffbit_testarr(_ffpath_charmask_filename, (byte)src[i])) {
			if (ffpath_slash(src[i]))
				dst[k] = src[i];
			else
				dst[k] = (byte)repl_with;
		} else
			dst[k] = src[i];
		k++;
	}

	return k;
}

int ffpath_makefn_out(ffarr *fn, const ffstr *idir, const ffstr *iname, const ffstr *odir, const ffstr *oext)
{
	fn->len = 0;
	if (NULL == ffarr_realloc(fn, odir->len + FFSLEN("/") + idir->len + FFSLEN("/") + iname->len + FFSLEN(".") + oext->len + 1))
		return -1;

	if (odir->len != 0) {
		ffarr_append(fn, odir->ptr, odir->len);
		ffarr_append(fn, "/", 1);
	}

	if (idir->len != 0) {
		fn->len += ffpath_norm(ffarr_end(fn), ffarr_unused(fn), idir->ptr, idir->len, FFPATH_TOREL | FFPATH_MERGEDOTS);
		ffarr_append(fn, "/", 1);
	}

	ffstr_catfmt(fn, "%S.%S%Z", iname, oext);
	fn->len--;
	return 0;
}

size_t ffpath_nslash(const char *path, size_t len)
{
	const char *end = path + len;
	size_t n = 0;

	for (;;) {
		path = ffpath_findslash(path, end - path);
		if (path == end)
			break;
		path++;
		n++;
	}

	return n;
}

ffbool ffpath_isvalidfn(const char *fn, size_t len, uint flags)
{
	if (flags == FFPATH_FN_ANY) {
#if defined FF_UNIX
		flags = FFPATH_FN_UNIX;
#elif defined FF_WIN
		flags = FFPATH_FN_WIN;
#endif
	}

	if (flags & FFPATH_FN_WIN) {
		for (size_t i = 0;  i != len;  i++) {
			if (!ffbit_testarr(_ffpath_charmask_filename, (byte)fn[i]))
				return 0;
		}
	}

	if (flags & FFPATH_FN_UNIX)
		return (fn + len == ffs_findof(fn, len, "/\0", 2));

	return 1;
}

const char* ffpath_split2(const char *fn, size_t len, ffstr *dir, ffstr *name)
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

const char* ffpath_split3(const char *fullname, size_t len, ffstr *path, ffstr *name, ffstr *ext)
{
	ffstr nm;
	const char *slash = ffpath_split2(fullname, len, path, &nm);
	ffpath_splitname(nm.ptr, nm.len, name, ext);
	return slash;
}

ffstr ffpath_next(ffstr *path)
{
	ffstr r;
	const char *sl = ffs_findof(path->ptr, path->len, "/\\", 2);
	ffs_split2(path->ptr, path->len, sl, &r, path);
	return r;
}

/*
/a < /b
/a < /a/b
/a/b < /b
/a/b < /a/B (FFPATH_CASE_SENS)
*/
int ffpath_cmp(const ffstr *p1, const ffstr *p2, uint flags)
{
	if (flags == FFPATH_CASE_DEF) {
#if defined FF_UNIX
		flags = FFPATH_CASE_SENS;
#elif defined FF_WIN
		flags = FFPATH_CASE_ISENS;
#endif
	}

	const byte *left = (void*)p1->ptr, *right = (void*)p2->ptr;
	uint n = ffmin(p1->len, p2->len);

	for (uint i = 0;  i != n;  i++) {
		uint l = left[i], r = right[i], ll, lr;
		if (l == r)
			continue;

		if (l == '/')
			return -1; // "a/" < "aa/"
		else if (r == '/')
			return 1; // "aa/" > "a/"

		ll = ffchar_isup(l) ? ffchar_lower(l) : l;
		lr = ffchar_isup(r) ? ffchar_lower(r) : r;
		if ((flags & FFPATH_CASE_ISENS) && ll == lr)
			continue;
		if ((flags & FFPATH_CASE_SENS) && ll == lr)
			return (l > r) ? -1 : 1; // "a" (0x60) < "A" (0x40)

		return (ll < lr) ? -1 : 1;
	}

	return p1->len - p2->len;
}

int ffpath_parent(const ffstr *p1, const ffstr *p2, ffstr *dir)
{
	size_t n = ffmin(p1->len, p2->len);
	ssize_t i = ffs_cmpn(p1->ptr, p2->ptr, n);
	if (i == 0) {
		ffstr_set(dir, p1->ptr, n);
		return 0;
	}
	if (i < 0)
		i = -i;
	i--;

	const char *sl = ffpath_rfindslash(p1->ptr, i);
	if (sl == p1->ptr + i)
		return -1;

	ffstr_set(dir, p1->ptr, sl - p1->ptr);
	return 0;
}

int ffpath_match(const ffstr *path, const ffstr *match, uint flags)
{
	char p1[4096], p2[4096];
	if (flags == FFPATH_CASE_DEF) {
#if defined FF_UNIX
		flags = FFPATH_CASE_SENS;
#elif defined FF_WIN
		flags = FFPATH_CASE_ISENS;
#endif
	}

	uint normflags = FFPATH_MERGESLASH | FFPATH_MERGEDOTS | FFPATH_FORCESLASH;

	size_t r1 = ffpath_norm(p1, sizeof(p1) - 1, path->ptr, path->len, normflags);
	if (r1 == 0)
		return 0;
	p1[r1++] = '/';

	size_t r2 = ffpath_norm(p2, sizeof(p2) - 1, match->ptr, match->len, normflags);
	if (r2 == 0)
		return 0;
	p2[r2++] = '/';

	if (flags & FFPATH_CASE_ISENS)
		return ffs_imatch(p1, r1, p2, r2);
	return ffs_match(p1, r1, p2, r2);
}
