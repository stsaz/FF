/**
Copyright (c) 2013 Simon Zolin
*/

#include <FF/string.h>
#include <FF/number.h>
#include <FFOS/error.h>
#include <math.h>


#ifdef FFMEM_DBG
ffatomic ffmemcpy_total;

void* ffmemcpy(void *dst, const void *src, size_t len)
{
	ffatom_add(&ffmemcpy_total, len);
	return _ffmemcpy(dst, src, len);
}
#endif

void ffmem_xor(byte *dst, const byte *src, size_t len, const byte *key, size_t nkey)
{
	for (size_t i = 0;  i != len;  i++) {
		dst[i] = src[i] ^ key[i % nkey];
	}
}

void ffmem_xor4(void *dst, const void *src, size_t len, uint key)
{
	byte *key1 = (byte*)&key, *dst1 = dst;
	const byte *src1 = src;
	uint *dst4 = dst;
	const uint *src4 = src;
	size_t i;

	for (i = 0;  i != len / 4;  i++) {
		dst4[i] = src4[i] ^ key;
	}

	for (i = i * 4;  i != len;  i++) {
		dst1[i] = src1[i] ^ key1[i % 4];
	}
}

ssize_t ffs_cmpn(const char *s1, const char *s2, size_t len)
{
	for (size_t i = 0;  i != len;  i++) {
		int c1 = s1[i], c2 = s2[i];
		if (c1 != c2)
			return (c1 < c2) ? -(ssize_t)i - 1 : (ssize_t)i + 1;
	}

	return 0; //s1 == s2
}

void * ffmemchr(const void *_d, int b, size_t len)
{
	const byte *d = _d;
	const byte *end;
	for (end = d + len; d != end; d++) {
		if ((uint)b == *d)
			return (byte*)d;
	}
	return NULL;
}

size_t ffs_nfindc(const char *buf, size_t len, int ch)
{
	const char *end = buf + len;
	size_t n = 0;

	for (;  buf != end;  n++, buf++) {
		buf = ffs_findc(buf, end - buf, ch);
		if (buf == NULL)
			break;
	}

	return n;
}

#if defined FF_WIN || defined FF_APPLE
char * ffs_rfind(const char *buf, size_t len, int ch)
{
	char *end = (char*)buf + len;
	while (end != buf) {
		--end;
		if (ch == *end)
			return end;
	}
	return (char*)buf + len;
}
#endif

char * ffs_findof(const char *buf, size_t len, const char *anyof, size_t cnt)
{
	size_t i;
	for (i = 0; i < len; ++i) {
		if (NULL != ffs_findc(anyof, cnt, buf[i]))
			return (char*)buf + i;
	}
	return (char*)buf + len;
}

char * ffs_rfindof(const char *buf, size_t len, const char *anyof, size_t cnt)
{
	const char *end = buf + len;
	while (end != buf) {
		char c = *(--end);
		if (anyof + cnt != ffs_rfind(anyof, cnt, c))
			return (char*)end;
	}
	return (char*)buf + len;
}

#if defined FF_WIN
ffsyschar * ffq_rfind(const ffsyschar *buf, size_t len, int ch)
{
	ffsyschar *end = (ffsyschar*)buf + len;
	while (end != buf) {
		end--;
		if (ch == *end)
			return end;
	}
	return (ffsyschar*)buf + len;
}

ffsyschar * ffq_rfindof(const ffsyschar *begin, size_t sz, const ffsyschar *matchAr, size_t matchSz)
{
	const ffsyschar *end = begin + sz;
	const ffsyschar *end_o = end;
	while (end != begin) {
		ffsyschar c = *--end;
		size_t im;
		for (im = 0; im < matchSz; ++im) {
			if (c == matchAr[im])
				return (ffsyschar*)end;
		}
	}
	return (ffsyschar*)end_o;
}
#endif

char * ffs_skip(const char *buf, size_t len, int ch)
{
	const char *end = buf + len;
	while (buf != end && *buf == ch)
		++buf;
	return (char*)buf;
}

char * ffs_skipof(const char *buf, size_t len, const char *anyof, size_t cnt)
{
	const char *end = buf + len;
	while (buf != end && NULL != ffs_findc(anyof, cnt, *buf))
		buf++;
	return (char*)buf;
}

char * ffs_rskip(const char *buf, size_t len, int ch)
{
	const char *end = buf + len;
	while (end != buf && *(end - 1) == ch)
		end--;
	return (char*)end;
}

char * ffs_rskipof(const char *buf, size_t len, const char *anyof, size_t cnt)
{
	const char *end = buf + len;
	while (end != buf && NULL != ffs_findc(anyof, cnt, *(end - 1)))
		end--;
	return (char*)end;
}

char* ffs_skip_mask(const char *buf, size_t len, const uint *mask)
{
	for (size_t i = 0;  i != len;  i++) {
		if (!ffbit_testarr(mask, (byte)buf[i]))
			return (char*)buf + i;
	}
	return (char*)buf + len;
}


static const int64 ff_intmasks[9] = {
	0
	, 0xff, 0xffff, 0xffffff, 0xffffffff
	, 0xffffffffffULL, 0xffffffffffffULL, 0xffffffffffffffULL, 0xffffffffffffffffULL
};

ssize_t ffs_findarr(const void *ar, size_t n, uint elsz, const void *s, size_t len)
{
	if (len <= sizeof(int)) {
		int imask = ff_intmasks[len];
		int left = *(int*)s & imask;
		for (size_t i = 0;  i != n;  i++) {
			if (left == (*(int*)ar & imask) && ((byte*)ar)[len] == 0x00)
				return i;
			ar = (byte*)ar + elsz;
		}
	} else if (len <= sizeof(int64)) {
		int64 left;
		size_t i;
		int64 imask;
		imask = ff_intmasks[len];
		left = *(int64*)s & imask;
		for (i = 0;  i != n;  i++) {
			if (left == (*(int64*)ar & imask) && ((byte*)ar)[len] == 0x00)
				return i;
			ar = (byte*)ar + elsz;
		}
	}
	return -1;
}

char* ffszarr_findkeyz(const char *const *ar, const char *key, size_t keylen)
{
	for (uint i = 0;  ar[i] != NULL;  i++) {
		if (ffsz_match(ar[i], key, keylen)
			&& ar[i][keylen] == '=')
			return (char*)ar[i] + keylen + FFSLEN("=");
	}
	return NULL;
}

size_t ffszarr_countz(const char *const *arz)
{
	size_t i;
	for (i = 0;  arz[i] != NULL;  i++){}
	return i;
}

const char* ffs_split2(const char *s, size_t len, const char *at, ffstr *first, ffstr *second)
{
	if (at == NULL || at == s + len) {
		if (first != NULL)
			ffstr_set(first, s, len);
		if (second != NULL)
			ffstr_null(second);
		return NULL;
	}

	if (first != NULL)
		ffstr_set(first, s, at - s);

	if (second != NULL)
		ffstr_set(second, at + 1, s + len - (at + 1));
	return at;
}


size_t ffs_replacechar(const char *src, size_t len, char *dst, size_t cap, int search, int replace, size_t *n)
{
	size_t nrepl = 0;
	const char *srcend = src + len
		, *dsto = dst
		, *dstend = dst + cap;

	while (src != srcend && dst != dstend) {
		if (*src == search) {
			*dst = replace;
			nrepl++;
		}
		else
			*dst = *src;
		dst++;
		src++;
	}

	if (n != NULL)
		*n = nrepl;

	return dst - dsto;
}

ssize_t ffstr_replace(ffstr *dst, const ffstr *src, const ffstr *search, const ffstr *replace, uint flags)
{
	ssize_t r;

	if (flags & FFSTR_REPL_ICASE)
		r = ffstr_ifindstr(src, search);
	else
		r = ffstr_find2(src, search);
	if (r < 0) {
		dst->len = 0;
		return -1;
	}

	// dst = src... [-search] [+replace] ...src
	char *p = ffmem_copy(dst->ptr, src->ptr, r);
	p = ffmem_copy(p, replace->ptr, replace->len);
	if (!(flags & FFSTR_REPL_NOTAIL)) {
		size_t n = r + search->len;
		p = ffmem_copy(p, src->ptr + n, src->len - n);
	}
	r += replace->len;

	dst->len = p - dst->ptr;
	return r;
}


const uint ffcharmask_name[] = {
	0,
	            // ?>=< ;:98 7654 3210  /.-, +*)( '&%$ #"!
	0x03ff0000, // 0000 0011 1111 1111  0000 0000 0000 0000
	            // _^]\ [ZYX WVUT SRQP  ONML KJIH GFED CBA@
	0x87fffffe, // 1000 0111 1111 1111  1111 1111 1111 1110
	            //  ~}| {zyx wvut srqp  onml kjih gfed cba`
	0x07fffffe, // 0000 0111 1111 1111  1111 1111 1111 1110
	0,
	0,
	0,
	0
};

const uint ffcharmask_nowhite[] = {
	0,
	            // ?>=< ;:98 7654 3210  /.-, +*)( '&%$ #"!
	0xfffffffe, // 1111 1111 1111 1111  1111 1111 1111 1110
	            // _^]\ [ZYX WVUT SRQP  ONML KJIH GFED CBA@
	0xffffffff, // 1111 1111 1111 1111  1111 1111 1111 1111
	            //  ~}| {zyx wvut srqp  onml kjih gfed cba`
	0x7fffffff, // 0111 1111 1111 1111  1111 1111 1111 1111
	0,
	0,
	0,
	0
};

const uint ffcharmask_printable[] = {
	0,
	            // ?>=< ;:98 7654 3210  /.-, +*)( '&%$ #"!
	0xffffffff, // 1111 1111 1111 1111  1111 1111 1111 1111
	            // _^]\ [ZYX WVUT SRQP  ONML KJIH GFED CBA@
	0xffffffff, // 1111 1111 1111 1111  1111 1111 1111 1111
	            //  ~}| {zyx wvut srqp  onml kjih gfed cba`
	0x7fffffff, // 0111 1111 1111 1111  1111 1111 1111 1111
	0xffffffff,
	0xffffffff,
	0xffffffff,
	0xffffffff
};

const uint ffcharmask_printable_tabcrlf[] = {
	            // .... .... .... ....  ..r. .nt. .... ....
	0x00002600, // 0000 0000 0000 0000  0010 0110 0000 0000
	            // ?>=< ;:98 7654 3210  /.-, +*)( '&%$ #"!
	0xffffffff, // 1111 1111 1111 1111  1111 1111 1111 1111
	            // _^]\ [ZYX WVUT SRQP  ONML KJIH GFED CBA@
	0xffffffff, // 1111 1111 1111 1111  1111 1111 1111 1111
	            //  ~}| {zyx wvut srqp  onml kjih gfed cba`
	0x7fffffff, // 0111 1111 1111 1111  1111 1111 1111 1111
	0xffffffff,
	0xffffffff,
	0xffffffff,
	0xffffffff
};

const uint ffcharmask_nobslash_esc[] = {
	            // .... .... .... ....  ..rf vntb a... ....
	0xffffc07f, // 1111 1111 1111 1111  1100 0000 0111 1111
	            // ?>=< ;:98 7654 3210  /.-, +*)( '&%$ #"!
	0xffffffff, // 1111 1111 1111 1111  1111 1111 1111 1111
	            // _^]\ [ZYX WVUT SRQP  ONML KJIH GFED CBA@
	0xefffffff, // 1110 1111 1111 1111  1111 1111 1111 1111
	            //  ~}| {zyx wvut srqp  onml kjih gfed cba`
	0xffffffff, // 1111 1111 1111 1111  1111 1111 1111 1111
	0xffffffff,
	0xffffffff,
	0xffffffff,
	0xffffffff
};

// allow all except '\\' and non-printable
static const uint esc_nprint[] = {
	            // .... .... .... ....  ..r. .nt. .... ....
	0x00002600, // 0000 0000 0000 0000  0010 0110 0000 0000
	            // ?>=< ;:98 7654 3210  /.-, +*)( '&%$ #"!
	0xffffffff, // 1111 1111 1111 1111  1111 1111 1111 1111
	            // _^]\ [ZYX WVUT SRQP  ONML KJIH GFED CBA@
	0xefffffff, // 1110 1111 1111 1111  1111 1111 1111 1111
	            //  ~}| {zyx wvut srqp  onml kjih gfed cba`
	0x7fffffff, // 0111 1111 1111 1111  1111 1111 1111 1111
	0xffffffff,
	0xffffffff,
	0xffffffff,
	0xffffffff
};

ssize_t _ffs_escape(char *dst, size_t cap, const char *s, size_t len, int type, const uint *mask)
{
	size_t i;
	uint nesc;
	const char *end = dst + cap;
	const char *dsto = dst;

	switch (type) {
	case FFS_ESC_BKSLX:
		nesc = FFSLEN("\\xXX"); break;
	default:
		FF_ASSERT(0);
		return 0; //unknown type
	}

	if (dst == NULL) {
		size_t n = 0;
		for (i = 0;  i != len;  i++) {
			if (!ffbit_testarr(mask, (byte)s[i]))
				n += nesc - 1;
		}
		return len + n;
	}

	switch (type) {
	case FFS_ESC_BKSLX:
		for (i = 0;  i != len;  i++) {
			byte c = s[i];

			if (ffbit_testarr(mask, c)) {
				if (dst == end)
					return -(ssize_t)i;
				*dst++ = c;

			} else {
				if (end - dst < (int)nesc) {
					ffs_fill(dst, end, '\0', nesc);
					return -(ssize_t)i;
				}

				*dst++ = '\\';
				*dst++ = 'x';
				dst += ffs_hexbyte(dst, c, ffHEX);
			}
		}
		break;
	}

	return dst - dsto;
}

ssize_t ffs_escape(char *dst, size_t cap, const char *s, size_t len, int type)
{
	return _ffs_escape(dst, cap, s, len, FFS_ESC_BKSLX, esc_nprint);
}


uint ffchar_sizesfx(int suffix)
{
	switch (ffchar_lower(suffix)) {
	case 'k':
		return 10;
	case 'm':
		return 20;
	case 'g':
		return 30;
	case 't':
		return 40;
	}
	return 0;
}

uint ffint_tosfx(uint64 size, char *sfx)
{
	uint r = 0;
	if (size >= FF_BIT64(40))
		*sfx = 't',  r = 40;
	else if (size >= FF_BIT64(30))
		*sfx = 'g',  r = 30;
	else if (size >= FF_BIT64(20))
		*sfx = 'm',  r = 20;
	else if (size >= FF_BIT64(10))
		*sfx = 'k',  r = 10;
	return r;
}

int ffs_fromsize(char *buf, size_t cap, uint64 size, uint flags)
{
	char sfx = '\0', *p;
	uint bits, frac;

	bits = ffint_tosfx(size, &sfx);
	p = buf + ffs_fromint((size >> bits), buf, cap, 0);

	if ((flags & FFS_FROMSIZE_FRAC)
		&& 0 != (frac = (size % FF_BIT64(bits)))) {
		char s[32];
		ffs_fromint(frac, s, sizeof(s), 0);
		p = ffs_copyc(p, buf + cap, '.');
		p = ffs_copyc(p, buf + cap, s[0]);
	}

	if (bits != 0)
		p = ffs_copyc(p, buf + cap, sfx);

	if (flags & FFS_FROMSIZE_Z)
		ffs_copyc(p, buf + cap, '\0');

	return p - buf;
}

uint ffs_tobool(const char *s, size_t len, ffbool *dst, uint flags)
{
	if (len >= 4 && !ffs_icmp(s, "true", 4)) {
		*dst = 1;
		return 4;
	} else if (len >= 5 && !ffs_icmp(s, "false", 5)) {
		*dst = 0;
		return 5;
	}
	return 0;
}

int ffs_numlist(const char *d, size_t *len, uint *dst)
{
	ffstr s = {};
	size_t n = ffstr_nextval(d, *len, &s, ',' | FFS_NV_KEEPWHITE);
	*len = n;
	if (s.len != ffs_toint(s.ptr, s.len, dst, FFS_INT32))
		return -1;
	return 0;
}

size_t ffs_fmatchv(const char *s, size_t len, const char *fmt, va_list va)
{
	const char *s_o = s, *s_end = s + len;
	uint width, iflags = 0;
	union {
		char *s;
		ffstr *str;
		uint *i4;
		uint64 *i8;
	} dst;
	dst.s = NULL;

	for (;  s != s_end && *fmt != '\0';  s++) {

		if (*fmt != '%') {
			if (*fmt != *s)
				goto fail; //mismatch
			fmt++;
			continue;
		}

		fmt++; //skip %

		if (*fmt == '%') {
			if (*fmt != *s)
				goto fail; //mismatch
			fmt++;
			continue;
		}

		width = 0;
		while (ffchar_isdigit(*fmt)) {
			width = width * 10 + (*fmt++ - '0');
		}

		switch (*fmt) {
		case 'x':
			iflags |= FFS_INTHEX;
			fmt++;
			break;
		}

		switch (*fmt) {
		case 'U':
			dst.i8 = va_arg(va, uint64*);
			iflags |= FFS_INT64;
			break;

		case 'u':
			dst.i4 = va_arg(va, uint*);
			iflags |= FFS_INT32;
			break;

		case 's':
			if (iflags != 0)
				goto fail; //unsupported modifier
			dst.s = va_arg(va, char*);
			if (width == 0)
				goto fail; //width must be specified for %s
			ffmemcpy(dst.s, s, width);
			s += width;
			break;

		case 'S':
			if (iflags != 0)
				goto fail; //unsupported modifier
			dst.str = va_arg(va, ffstr*);
			dst.str->ptr = (void*)s;

			if (width != 0) {
				dst.str->len = width;
				s += width;

			} else {

				for (;  s != s_end;  s++) {
					if (!ffchar_isletter(*s))
						break;
				}
				dst.str->len = s - dst.str->ptr;
			}
			break;

		default:
			goto fail; //invalid format specifier
		}

		if (iflags != 0) {
			size_t n = (width == 0) ? (size_t)(s_end - s) : ffmin(s_end - s, width);
			n = ffs_toint(s, n, dst.i8, iflags);
			if (n == 0)
				goto fail; //bad integer
			s += n;
			iflags = 0;
		}

		fmt++;
		s--;
	}

	if (*fmt != '\0')
		goto fail; //input string is too short

	return s - s_o;

fail:
	return -(s - s_o + 1);
}


ssize_t ffstr_findarr(const ffstr *ar, size_t n, const char *search, size_t search_len)
{
	size_t i;
	for (i = 0; i < n; ++i) {
		if (search_len == ar[i].len
			&& 0 == ffmemcmp(search, ar[i].ptr, search_len))
			return i;
	}

	return -1;
}

ssize_t ffstr_ifindarr(const ffstr *ar, size_t n, const char *search, size_t search_len)
{
	size_t i;
	for (i = 0; i < n; ++i) {
		if (search_len == ar[i].len
			&& 0 == ffs_icmp(search, ar[i].ptr, search_len))
			return i;
	}

	return -1;
}

size_t ffstr_nextval(const char *buf, size_t len, ffstr *dst, int spl)
{
	const char *end = buf + len;
	const char *pos;
	ffstr spc, sspl = {0};
	uint f = spl & ~0xff;
	spl &= 0xff;

	ffstr_setcz(&spc, " ");
	if (f & FFS_NV_TABS)
		ffstr_setcz(&spc, "\t ");
	if (f & FFS_NV_CR)
		ffstr_setcz(&spc, " \t\r");

	if (f & FFS_NV_WORDS)
		sspl = spc;

	if (f & FFS_NV_REVERSE) {
		if (sspl.ptr != NULL)
			pos = ffs_rfindof(buf, end - buf, sspl.ptr, sspl.len);
		else
			pos = ffs_rfind(buf, end - buf, spl);
		if (pos == end)
			pos = buf;
		else {
			len = end - pos;
			if (pos == buf && pos + 1 != end)
				len--; // don't remove the last split char, e.g. ",val"
			pos++;
		}

		if (!(f & FFS_NV_KEEPWHITE)) {
			pos = ffs_skipof(pos, end - pos, spc.ptr, spc.len);
			end = ffs_rskipof(pos, end - pos, spc.ptr, spc.len);
		}

		ffstr_set(dst, pos, end - pos);
		return len;
	}

	if (!(f & FFS_NV_KEEPWHITE))
		buf = ffs_skipof(buf, end - buf, spc.ptr, spc.len);

	if (buf == end) {
		dst->len = 0;
		return len;
	}

	if ((f & FFSTR_NV_DBLQUOT) && buf[0] == '"') {
		buf++;
		pos = ffs_find(buf, end - buf, '"');
		ffstr_set(dst, buf, pos - buf);
		if (pos != end)
			pos++;

		if (!(f & FFS_NV_KEEPWHITE))
			pos = ffs_skipof(pos, end - pos, spc.ptr, spc.len);

		return pos - (end - len);
	}

	if (sspl.ptr != NULL)
		pos = ffs_findof(buf, end - buf, sspl.ptr, sspl.len);
	else
		pos = ffs_find(buf, end - buf, spl);

	// merge whitespace if splitting by whitespace: "val1   val2" -> "val1", "val2"
	if ((f & FFS_NV_WORDS) || spl == ' ' || spl == '\t') {
		ffstr_set(dst, buf, pos - buf);
		if (sspl.ptr != NULL)
			pos = ffs_skipof(pos, end - pos, sspl.ptr, sspl.len);
		else
			pos = ffs_skip(pos, end - pos, spl);
		return pos - (end - len);
	}

	if (pos != end) {
		len = pos - (end - len) + 1;
		if (pos + 1 == end && pos != buf)
			len--; // don't remove the last split char, e.g. "val,"
	}

	if (!(f & FFS_NV_KEEPWHITE))
		pos = ffs_rskipof(buf, pos - buf, spc.ptr, spc.len);

	ffstr_set(dst, buf, pos - buf);
	return len;
}


ffbstr * ffbstr_push(ffstr *buf, const char *data, size_t len)
{
	ffbstr *bs;
	char *p = ffmem_realloc(buf->ptr, buf->len + sizeof(ffbstr) + len);
	if (p == NULL)
		return NULL;

	bs = (ffbstr*)(p + buf->len);
	if (data != NULL)
		ffbstr_copy(bs, data, len);

	buf->ptr = p;
	buf->len += sizeof(ffbstr) + len;
	return bs;
}


int ffstr_vercmp(const ffstr *v1, const ffstr *v2)
{
	int r = 0;
	uint64 n1, n2;
	ffstr s = *v1, d = *v2, part;

	for (;;) {

		if (s.len == 0 && d.len == 0)
			return r;

		n1 = n2 = 0;

		if (s.len != 0) {
			ffstr_nextval3(&s, &part, '.' | FFS_NV_KEEPWHITE);
			if (!ffstr_toint(&part, &n1, FFS_INT64))
				return FFSTR_VERCMP_ERRV1; // the component is not a number
		}

		if (d.len != 0) {
			ffstr_nextval3(&d, &part, '.' | FFS_NV_KEEPWHITE);
			if (!ffstr_toint(&part, &n2, FFS_INT64))
				return FFSTR_VERCMP_ERRV2; // the component is not a number
		}

		if (r == 0)
			r = ffint_cmp(n1, n2);
	}
}


size_t ffbit_count(const void *d, size_t len)
{
	const size_t *pn = d;
	const byte *b = d;
	uint i, k;
	size_t r = 0;

	for (k = 0;  k + sizeof(void*) < len;  k += sizeof(void*)) {
		size_t n = pn[k / sizeof(void*)];
		while (0 <= (int)(i = ffbit_ffs(n) - 1)) {
			r++;
			ffbit_reset(&n, i);
		}
	}

	for (;  k != len;  k++) {
		uint n = b[k];
		while (0 <= (int)(i = ffbit_ffs32(n) - 1)) {
			r++;
			ffbit_reset32(&n, i);
		}
	}

	return r;
}
