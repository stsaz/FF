/**
Copyright (c) 2013 Simon Zolin
*/

#include <FF/string.h>
#include <FF/array.h>
#include <FF/number.h>
#include <FFOS/error.h>
#include <math.h>


#ifdef FFMEM_DBG
ffatomic ffmemcpy_total;

void* ffmemcpy(void *dst, const void *src, size_t len)
{
	ffmemcpy_total += len;
	return _ffmemcpy(dst, src, len);
}
#endif

int ffs_icmp(const char *s1, const char *s2, size_t len)
{
	size_t i;
	uint ch1
		, ch2;

	for (i = 0;  i < len;  i++) {
		ch1 = (byte)s1[i];
		ch2 = (byte)s2[i];

		if (ch1 != ch2) {
			if (ffchar_isup(ch1))
				ch1 = ffchar_lower(ch1);
			if (ffchar_isup(ch2))
				ch2 = ffchar_lower(ch2);
			if (ch1 != ch2)
				return (ch1 < ch2 ? -1 : 1);
		}
	}

	return 0;
}

ssize_t ffs_cmpz(const char *s1, size_t len, const char *sz2)
{
	size_t i;
	uint ch1, ch2;

	for (i = 0;  i != len;  i++) {
		ch1 = (byte)s1[i];
		ch2 = (byte)sz2[i];

		if (ch2 == '\0')
			return i + 1; //s1 > sz2

		if (ch1 != ch2)
			return (ch1 < ch2) ? -(ssize_t)i - 1 : (ssize_t)i + 1;
	}

	if (sz2[i] != '\0')
		return -(ssize_t)i - 1; //s1 < sz2

	return 0; //s1 == sz2
}

ssize_t ffs_icmpz(const char *s1, size_t len, const char *sz2)
{
	size_t i;
	uint ch1, ch2;

	for (i = 0;  i != len;  i++) {
		ch1 = (byte)s1[i];
		ch2 = (byte)sz2[i];

		if (ch2 == '\0')
			return i + 1; //s1 > sz2

		if (ch1 != ch2) {
			if (ffchar_isup(ch1))
				ch1 = ffchar_lower(ch1);

			if (ffchar_isup(ch2))
				ch2 = ffchar_lower(ch2);

			if (ch1 != ch2)
				return (ch1 < ch2) ? -(ssize_t)i - 1 : (ssize_t)i + 1;
		}
	}

	if (sz2[i] != '\0')
		return -(ssize_t)i - 1; //s1 < sz2

	return 0; //s1 == sz2
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

char * ffs_finds(const char *s, size_t len, const char *search, size_t search_len)
{
	char *end = (char*)s + len;
	const char *to;
	uint sch0;

	if (search_len == 0)
		return (char*)s;

	if (len < search_len)
		return end;

	sch0 = (byte)*search++;
	search_len--;

	for (to = s + len - search_len;  s != to;  s++) {
		if (sch0 == (byte)*s
			&& 0 == ffmemcmp(s + 1, search, search_len))
			return (char*)s;
	}

	return end;
}

char * ffs_ifinds(const char *s, size_t len, const char *search, size_t search_len)
{
	char *end = (char*)s + len;
	const char *to;
	uint sch0;

	if (search_len == 0)
		return (char*)s;

	if (len < search_len)
		return end;

	sch0 = (byte)*search++;
	sch0 = ffchar_lower(sch0);
	search_len--;

	to = s + len - search_len;
	for (;  s != to;  s++) {
		if (sch0 == ffchar_lower((byte)*s)
			&& 0 == ffs_icmp(s + 1, search, search_len))
			return (char*)s;
	}

	return end;
}

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
	if (len <= sizeof(int64)) {
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

ssize_t ffs_findarrz(const char *const *ar, size_t n, const char *search, size_t search_len)
{
	size_t i;
	ffstr s;
	ffstr_set(&s, search, search_len);
	for (i = 0;  i < n;  i++) {
		if (ffstr_eqz(&s, ar[i]))
			return i;
	}
	return -1;
}

ssize_t ffs_ifindarrz(const char *const *ar, size_t n, const char *search, size_t search_len)
{
	size_t i;
	for (i = 0;  i != n;  i++) {
		if (0 == ffs_icmpz(search, search_len, ar[i]))
			return i;
	}
	return -1;
}

ssize_t ffszarr_findsorted(const char *const *ar, size_t n, const char *search, size_t search_len)
{
	const char *const *ar_o = ar, *const *start = ar, *const *end = ar + n;
	int r;
	while (start != end) {
		ar = start + (end - start) / 2;
		r = ffs_cmpz(search, search_len, *ar);
		if (r == 0)
			return ar - ar_o;
		else if (r < 0)
			end = ar;
		else
			start = ar + 1;
	}
	return -1;
}

ssize_t ffszarr_ifindsorted(const char *const *ar, size_t n, const char *search, size_t search_len)
{
	const char *const *ar_o = ar, *const *start = ar, *const *end = ar + n;
	int r;
	while (start != end) {
		ar = start + (end - start) / 2;
		r = ffs_icmpz(search, search_len, *ar);
		if (r == 0)
			return ar - ar_o;
		else if (r < 0)
			end = ar;
		else
			start = ar + 1;
	}
	return -1;
}

char* ffszarr_findkey(const char *const *ar, size_t n, const char *key, size_t keylen)
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

ssize_t ffcharr_findsorted(const void *ar, size_t n, size_t m, const char *search, size_t search_len)
{
	size_t i, start = 0;
	int r;

	if (search_len > m)
		return -1; //the string's too large for this array

	while (start != n) {
		i = start + (n - start) / 2;
		r = ffs_cmp(search, (char*)ar + i * m, search_len);

		if (r == 0 && search_len != m && ((char*)ar)[i * m + search_len] != '\0')
			r = -1; //found "01" in {0,1,2}

		if (r == 0)
			return i;
		else if (r < 0)
			n = i;
		else
			start = i + 1;
	}
	return -1;
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


size_t ffs_lower(char *dst, const char *end, const char *src, size_t len)
{
	size_t i;
	unsigned inplace = (dst == src);
	len = ffmin(len, end - dst);

	for (i = 0;  i < len;  i++) {
		uint ch = (byte)src[i];

		if (ffchar_isup(ch))
			dst[i] = ffchar_lower(ch);
		else if (!inplace)
			dst[i] = ch;
	}

	return i;
}

size_t ffs_upper(char *dst, const char *end, const char *src, size_t len)
{
	size_t i;
	unsigned inplace = (dst == src);
	len = ffmin(len, end - dst);

	for (i = 0;  i < len;  i++) {
		uint ch = (byte)src[i];

		if (ffchar_islow(ch))
			dst[i] = ffchar_upper(ch);
		else if (!inplace)
			dst[i] = ch;
	}

	return i;
}

size_t ffs_titlecase(char *dst, const char *end, const char *src, size_t len)
{
	size_t i;
	unsigned inplace = (dst == src);
	enum {
		i_cap
		, i_norm
	};
	int st = i_cap;
	len = ffmin(len, end - dst);

	for (i = 0;  i < len;  i++) {
		uint ch = (byte)src[i];

		switch (st) {
		case i_cap:
			if (ch != ' ') {
				st = i_norm;

				if (ffchar_islow(ch)) {
					dst[i] = ffchar_upper(ch);
					continue;
				}
			}
			break;

		case i_norm:
			if (ch == ' ')
				st = i_cap;

			else if (ffchar_isup(ch)) {
				dst[i] = ffchar_lower(ch);
				continue;
			}
			break;
		}

		if (!inplace)
			dst[i] = ch;
	}

	return i;
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

const char ffhex[] = "0123456789abcdef";
const char ffHEX[] = "0123456789ABCDEF";


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

ssize_t ffs_escape(char *dst, size_t cap, const char *s, size_t len, int type)
{
	size_t i;
	const uint *mask;
	const char *end = dst + cap;
	const char *dsto = dst;

	if (type != FFS_ESC_NONPRINT)
		return 0; //unknown type
	mask = esc_nprint;

	if (dst == NULL) {
		size_t n = 0;
		for (i = 0;  i != len;  i++) {
			if (!ffbit_testarr(mask, (byte)s[i]))
				n += FFSLEN("\\xXX") - 1;
		}
		return len + n;
	}

	for (i = 0;  i != len;  i++) {
		byte c = s[i];

		if (ffbit_testarr(mask, c)) {
			if (dst == end)
				return -(ssize_t)i;
			*dst++ = c;

		} else {
			if (dst + FFSLEN("\\xXX") > end) {
				ffs_fill(dst, end, '\0', FFSLEN("\\xXX"));
				return -(ssize_t)i;
			}

			*dst++ = '\\';
			*dst++ = 'x';
			dst += ffs_hexbyte(dst, c, ffHEX);
		}
	}

	return dst - dsto;
}

int ffs_wildcard(const char *pattern, size_t patternlen, const char *s, size_t len, uint flags)
{
	const char *endpatt = pattern + patternlen, *end = s + len;
	const char *astk = endpatt, *astk_s = NULL;
	int ch1, ch2;

	while (pattern != endpatt) {

		if (*pattern == '*') {
			if (++pattern == endpatt)
				return 0; //the rest of input string matches
			astk = pattern;
			astk_s = s + 1;
			continue;
		}

		if (s == end)
			return 1; //too short input string

		if (*pattern == '?') {
			s++;
			pattern++;
			continue;
		}

		ch1 = *pattern;
		ch2 = *s;
		if (flags & FFS_WC_ICASE) {
			if (ffchar_isup(ch1))
				ch1 = ffchar_lower(ch1);
			if (ffchar_isup(ch2))
				ch2 = ffchar_lower(ch2);
		}

		if (ch1 == ch2) {
			s++;
			pattern++;

		} else {
			if (astk == endpatt)
				return 1; //mismatch

			pattern = astk;
			s = astk_s++;
		}
	}

	return (pattern == endpatt && s == end) ? 0 : 1;
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

uint ffs_toint(const char *src, size_t len, void *dst, int flags)
{
	uint64 r = 0;
	size_t i = 0;
	ffbool minus = 0;
	uint digits = 0;
	union {
		void *ptr;
		uint64 *pi64;
		uint *pi32;
		ushort *pi16;
		byte *pi8;
	} ptr;
	ptr.ptr = dst;

	if ((flags & FFS_INTSIGN) && len > 0
		&& ((minus = (src[0] == '-')) || src[0] == '+'))
		i++;

	if (flags & FFS_INTOCTAL) {
		for (;  i != len;  i++) {
			uint b = ffchar_tonum(src[i]);
			if (b >= 8)
				break;
			r = r * 8 + b;
			digits++;
		}

		if (digits > FFSLEN("1777777777777777777777"))
			goto fail;

	} else if (!(flags & FFS_INTHEX)) {
		//dec
		for (; i < len; ++i) {
			byte b = src[i] - '0';
			if (b >= 10)
				break;
			r = r * 10 + b;
			digits++;
		}

		if (digits > FFSLEN("18446744073709551615"))
			goto fail;
	}
	else {
		//hex
		for (; i < len; ++i) {
			int b = ffchar_tohex(src[i]);
			if (b == -1)
				break;
			r = (r << 4) | b;
			digits++;
		}

		if (digits > FFSLEN("1234567812345678"))
			goto fail;
	}

	if (digits == 0)
		goto fail;

	{
		uint n = (flags & 0x0f) * 8;
		if (flags & FFS_INTSIGN)
			n--;
		if (n != 64 && (r >> n) != 0) //with VC on Winx64 r >> 64 == r
			goto fail; //bits overflow
	}

	if (minus)
		r = -(int64)r;

	switch (flags & 0x0f) {

	case FFS_INT64:
		*ptr.pi64 = r;
		break;

	case FFS_INT32:
		*ptr.pi32 = (uint)r;
		break;

	case FFS_INT16:
		*ptr.pi16 = (ushort)r;
		break;

	case FFS_INT8:
		*ptr.pi8 = (byte)r;
		break;

	default:
		goto fail; //invalid type
	}

	return (uint)i;

fail:
	return 0;
}

static FFINL char* _ffs_fromint_dec(char *ps, uint64 i, uint flags)
{
	if (i <= 0xffffffff) {
		uint i4 = (uint)i;
		do {
			*(--ps) = (byte)(i4 % 10 + '0');
			i4 /= 10;
		} while (i4 != 0);

	} else {
		do {
			*(--ps) = (byte)(i % 10 + '0');
			i /= 10;
		} while (i != 0);
	}

	return ps;
}

static FFINL char* _ffs_fromint_oct(char *ps, uint64 i, uint flags)
{
	do {
		*(--ps) = (byte)(i % 8 + '0');
		i /= 8;
	} while (i != 0);
	return ps;
}

static FFINL char* _ffs_fromint_hex(char *ps, uint64 i, uint flags)
{
	const char *phex = (flags & FFINT_HEXUP) ? ffHEX : ffhex;
	do {
		*(--ps) = phex[i & 0x0f];
		i >>= 4;
	} while (i != 0);
	return ps;
}

uint ffs_fromint(uint64 i, char *dst, size_t cap, int flags)
{
	const char *end = dst + cap;
	const char *dsto = dst;
	uint len;
	char s[64];
	char *ps = s + FFCNT(s);
	ffbool minus = 0;

	if ((flags & FFINT_SIGNED) && (int64)i < 0) {
		i = -(int64)i;
		minus = 1;
	}
	if (flags & FFINT_NEG)
		minus = 1;

	if (flags & FFINT_OCTAL)
		ps = _ffs_fromint_oct(ps, i, flags);
	else if (flags & (FFINT_HEXUP | FFINT_HEXLOW))
		ps = _ffs_fromint_hex(ps, i, flags);
	else
		ps = _ffs_fromint_dec(ps, i, flags);
	len = (uint)(s + FFCNT(s) - ps);

	if ((flags & FFINT_SEP1000) && (flags & _FFINT_WIDTH_MASK) == 0) {
		int nsep = len / 3 - ((len % 3) == 0);
		if (nsep > 0) {
			uint k, idst = len + nsep - 1;

			if (minus && dst != end)
				*(dst++) = '-';

			if (idst >= (uint)(end - dst))
				return end - dst;

			for (k = 1;  k != len;  k++) {
				dst[idst--] = s[sizeof(s) - k];
				if (!(k % 3))
					dst[idst--] = ',';
			}
			dst[0] = s[sizeof(s) - len];
			return len + nsep;
		}
	}

	if ((flags & _FFINT_WIDTH_MASK) != 0) {
		// %4d:  "  -1"
		// %04d: "-001"
		uint width = (flags >> 24) & 0xff;
		uint n = ffmin(width - (minus + len), end - dst);
		if (width > minus + len && n != 0) {
			char fill = ' ';
			if (flags & FFINT_ZEROWIDTH) {
				fill = '0';
				if (minus) {
					minus = 0;
					if (dst != end)
						*(dst++) = '-';
				}
			}

			memset(dst, fill, n);
			dst += n;
		}
	}

	if (minus && dst != end)
		*(dst++) = '-';

	len = (uint)ffmin(len, end - dst);
	ffmemcpy(dst, ps, len);
	dst += len;
	return (uint)(dst - dsto);
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

uint ffs_tofloat(const char *s, size_t len, double *dst, int flags)
{
	double d = 0.0;
	ffbool minus = 0;
	ffbool negexp = 0;
	int exp = 0;
	int e = 0;
	int digits = 0;
	size_t i;
	enum {
		iMinus, iInt, iDot, iFrac, iExpE, iExpSign, iExp
	};
	int st = iMinus;

	for (i = 0;  i != len;  i++) {
		int ch = s[i];

		switch (st) {
		case iMinus:
			st = iInt;
			if ((minus = (ch == '-')) || ch == '+')
				break;
			//break;

		case iInt:
			if (ffchar_isdigit(ch)) {
				d = d * 10 + ffchar_tonum(ch);
				digits++;
				break;
			}
			//st = iDot;
			//break;

			//case iDot:
			if (ch == '.') {
				st = iFrac;
				break;
			}
			//st = iExpE;
			//i--; //again
			//break;

		case iFrac:
			if (ffchar_isdigit(ch)) {
				d = d * 10 + ffchar_tonum(ch);
				digits++;
				exp--;
				break;
			}
			//st = iExpE;
			//break;

			//case iExpE:
			if (ffchar_lower(ch) != 'e')
				break;
			st = iExpSign;
			break;

		case iExpSign:
			st = iExp;
			if ((negexp = (ch == '-')) || ch == '+')
				break;
			//break;

		case iExp:
			if (!ffchar_isdigit(ch))
				break;
			e = e * 10 + ffchar_tonum(ch);
			break;
		}
	}

	exp += (negexp ? -e : e);

	if (digits == 0 || st == iExpSign
		|| exp > 308 || exp < -324)
		return 0;

	if (minus)
		d = -d;

	{
		double p10 = 10.0;
		e = exp;
		if (e < 0)
			e = -e;

		while (e != 0) {

			if (e & 1) {
				if (exp < 0)
					d /= p10;
				else
					d *= p10;
			}

			e >>= 1;
			p10 *= p10;
		}
	}

	*dst = d;
	return i;
}

uint ffs_fromfloat(double d, char *dst, size_t cap, uint flags)
{
	const char *end = dst + cap;
	char *buf = dst;
	uint64 num, frac = 0;
	uint width = ((flags & _FFINT_WIDTH_MASK) >> 24), wfrac = ((flags & _FFS_FLT_FRAC_WIDTH_MASK) >> 16), n, scale;
	ffbool minus = 0;
	flags &= FFINT_ZEROWIDTH;

	if (cap == 0)
		return 0;

	if (d < 0) {
		d = -d;
		minus = 1;
		flags |= FFINT_NEG;
	}

	if (isinf(d)) {
		if (minus)
			buf = ffs_copyc(buf, end, '-');
		buf = ffs_copycz(buf, end, "inf");
		return buf - dst;
	}

	num = (int64)d;

	if (wfrac != 0) {
		scale = 1;
		for (n = wfrac;  n != 0;  n--) {
			scale *= 10;
		}
		frac = (uint64)((d - (double)num) * scale + 0.5);
		if (frac == scale) {
			num++;
			frac = 0;
		}
	}

	buf += ffs_fromint(num, buf, end - buf, flags | FFINT_SIGNED | FFINT_WIDTH(width));

	if (wfrac != 0) {
		buf = ffs_copyc(buf, end, '.');
		buf += ffs_fromint(frac, buf, end - buf, FFINT_ZEROWIDTH | FFINT_WIDTH(wfrac));
	}

	return buf - dst;
}

int ffs_numlist(const char *d, size_t *len, uint *dst)
{
	ffstr s;
	size_t n = ffstr_nextval(d, *len, &s, ',' | FFS_NV_KEEPWHITE);
	*len = n;
	if (s.len != ffs_toint(s.ptr, s.len, dst, FFS_INT32))
		return -1;
	return 0;
}

size_t ffs_hexstr(char *dst, size_t cap, const char *src, size_t len, uint flags)
{
	char *d = dst;
	size_t i;
	const char *h;

	if (dst == NULL)
		return len * 2;

	if (cap < len * 2)
		return 0;

	h = (flags & FFS_HEXUP) ? ffHEX : ffhex;

	for (i = 0;  i != len;  i++) {
		d += ffs_hexbyte(d, (byte)src[i], h);
	}
	return d - dst;
}

static uint _ffs_read_int(const char **ps)
{
	const char *s;
	uint r = 0;
	for (s = *ps;  ffchar_isdigit(*s);  s++) {
		r = r * 10 + ffchar_tonum(*s);
	}
	*ps = s;
	return r;
}

ssize_t ffs_fmtv2(char *buf_o, size_t cap, const char *fmt, va_list va)
{
	size_t swidth, len = 0;
	uint width, wfrac;
	int64 num = 0;
	double d;
	uint itoaFlags = 0, hexstr_flags = 0, float_flags;
	char *buf = buf_o, *end = buf + cap, *p;

	for (;  *fmt;  ++fmt) {
		if (*fmt != '%') {
			buf = ffs_copyc(buf, end, *fmt);
			len++;
			continue;
		}
		fmt++; //skip %

		wfrac = -1;
		itoaFlags = 0;
		hexstr_flags = 0;

		if (*fmt == '0') {
			itoaFlags |= FFINT_ZEROWIDTH;
			fmt++;
		}

		width = _ffs_read_int(&fmt);
		swidth = (width != 0) ? width : (size_t)-1;

		for (;;) {
		switch (*fmt) {
		case '*':
			swidth = va_arg(va, size_t);
			break;

		case 'x':
			itoaFlags |= FFINT_HEXLOW;
			hexstr_flags |= FFS_HEXLOW;
			break;

		case 'X':
			itoaFlags |= FFINT_HEXUP;
			hexstr_flags |= FFS_HEXUP;
			break;

		case '.':
			fmt++;
			wfrac = _ffs_read_int(&fmt);
			continue;

		case ',':
			itoaFlags |= FFINT_SEP1000;
			break;

		default:
			goto format;
		}

		fmt++;
		}

format:
		switch (*fmt) {

		case 'D':
			itoaFlags |= FFINT_SIGNED;
			//break;
		case 'U':
			num = va_arg(va, int64);
			goto from_int;

		case 'd':
			itoaFlags |= FFINT_SIGNED;
			num = va_arg(va, int);
			goto from_int;
		case 'u':
			num = va_arg(va, uint);
			goto from_int;

		case 'I':
			itoaFlags |= FFINT_SIGNED;
			num = va_arg(va, ssize_t);
			goto from_int;
		case 'L':
			num = va_arg(va, size_t);
			goto from_int;

		case 'p':
			num = va_arg(va, size_t);
			width = (num <= (uint)-1) ? sizeof(int) * 2 : sizeof(void*) * 2;
			itoaFlags |= FFINT_HEXLOW | FFINT_ZEROWIDTH;
			goto from_int;

		case 'F':
			d = va_arg(va, double);
			width = ffmin(width, 255);
			if (wfrac == (uint)-1)
				wfrac = 6;
			wfrac = ffmin(wfrac, 255);
			float_flags = (itoaFlags & FFINT_ZEROWIDTH) | FFINT_WIDTH(width) | FFS_INT_WFRAC(wfrac);
			uint n = ffs_fromfloat(d, buf, end - buf, float_flags);
			buf += n;
			len += (buf != end) ? n
				: ffmax(width, FFINT_MAXCHARS) + FFSLEN(".") + ffmin(wfrac, FFINT_MAXCHARS);
			break;

#ifdef FF_UNIX
		case 'q':
#endif
		case 's': {
			const char *s = va_arg(va, char *);
			if (swidth == (size_t)-1) {
				p = buf;
				buf = ffs_copyz(buf, end, s);
				len += (buf != end) ? (size_t)(buf - p) : ffsz_len(s);

			} else {
				buf = ffs_copy(buf, end, s, swidth);
				len += swidth;
			}
			break;
		}

#ifdef FF_UNIX
		case 'Q':
#endif
		case 'S': {
			ffstr *s = va_arg(va, ffstr *);
			buf = ffs_copy(buf, end, s->ptr, s->len);
			len += s->len;
			break;
		}

#ifdef FF_WIN
		case 'q': {
			ffsyschar *s = va_arg(va, ffsyschar *);
			if (swidth == (size_t)-1)
				swidth = ffq_len(s);
			p = buf;
			buf = ffs_copyq(buf, end, s, swidth);
			len += (buf != end) ? (size_t)(buf - p) : ffq_lenq(s, swidth);
			break;
		}

		case 'Q': {
			ffqstr *s = va_arg(va, ffqstr *);
			p = buf;
			buf = ffs_copyq(buf, end, s->ptr, s->len);
			len += (buf != end) ? (size_t)(buf - p) : ffq_lenq(s->ptr, s->len);
			break;
		}
#endif

		case 'b': {
			if (swidth == (size_t)-1)
				return 0; //width must be specified
			const char *s = va_arg(va, char*);
			ssize_t r = ffs_hexstr(buf, end - buf, s, swidth, hexstr_flags);
			len += swidth * 2;
			if (buf != end)
				buf += r;
			break;
		}

#ifndef FFS_FMT_NO_e
		case 'e': {
			int e = va_arg(va, int);
			const char *se = fferr_opstr((enum FFERR)e);
			p = buf;
			buf = ffs_copyz(buf, end, se);
			len += (buf != end) ? (size_t)(buf - p) : ffsz_len(se);
			break;
		}
#endif

		case 'E': {
			int e = va_arg(va, int);
			ssize_t r = ffs_fmt2(buf, end - buf, "(%u) ", e);
			if (r < 0) {
				buf = end;
				r = -r;
			}

			if (buf != end) {
				buf += r;
				if (0 != fferr_str(e, buf, end - buf)) {
					buf = end;
					len += 255;
					break;
				}
				uint n = ffsz_len(buf);
				buf += n;
				len += r + n;

			} else {
				char tmp[256];
				fferr_str(e, tmp, sizeof(tmp));
				len += r + ffsz_len(tmp);
			}
			break;
		}

		case 'c': {
			uint ch = va_arg(va, int);
			if (swidth == (size_t)-1) {
				buf = ffs_copyc(buf, end, ch);
				len++;
			} else {
				buf += ffs_fill(buf, end, ch, swidth);
				len += swidth;
			}
			break;
		}

		case 'Z':
			buf = ffs_copyc(buf, end, '\0');
			len++;
			break;

		case '%':
			buf = ffs_copyc(buf, end, '%');
			len++;
			break;

		default:
			return 0;
		}

		if (0) {
from_int:
			width = ffmin(width, 255);
			itoaFlags |= FFINT_WIDTH(width);
			uint n = ffs_fromint(num, buf, end - buf, itoaFlags);
			buf += n;
			len += (buf != end) ? n : ffmax(width, FFINT_MAXCHARS);
		}
	}

	if (buf == end)
		return -(ssize_t)(len + 1); //+1 byte to pass (buf!=end) check next time

	return buf - buf_o;
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
	if (pos != end
		&& ((f & FFS_NV_WORDS) || spl == ' ' || spl == '\t'))
		pos = ffs_skipof(pos, end - pos, sspl.ptr, sspl.len);

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
