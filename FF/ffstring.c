/**
Copyright (c) 2013 Simon Zolin
*/

#include <FF/string.h>
#include <FF/array.h>
#include <FFOS/file.h>
#include <FFOS/error.h>


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
	ssize_t i;
	uint ch1, ch2;

	for (i = 0;  i != len;  i++) {
		ch1 = (byte)s1[i];
		ch2 = (byte)sz2[i];

		if (ch2 == '\0')
			return i + 1; //s1 > sz2

		if (ch1 != ch2)
			return (ch1 < ch2) ? -i - 1 : i + 1;
	}

	if (sz2[i] != '\0')
		return -i - 1; //s1 < sz2

	return 0; //s1 == sz2
}

ssize_t ffs_icmpz(const char *s1, size_t len, const char *sz2)
{
	ssize_t i;
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
				return (ch1 < ch2) ? -i - 1 : i + 1;
		}
	}

	if (sz2[i] != '\0')
		return -i - 1; //s1 < sz2

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

#if defined FF_MSVC || defined FF_MINGW
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


static const int64 ff_intmasks[9] = {
	0
	, 0xff, 0xffff, 0xffffff, 0xffffffff
	, 0xffffffffffULL, 0xffffffffffffULL, 0xffffffffffffffULL, 0xffffffffffffffffULL
};

size_t ffs_findarr(const void *s, size_t len, const void *ar, ssize_t elsz, size_t count)
{
	if (len <= sizeof(int64)) {
		int64 left;
		size_t i;
		int64 imask;
		imask = ff_intmasks[len];
		left = *(int64*)s & imask;
		for (i = 0; i < count; i++) {
			if (left == (*(int64*)ar & imask) && ((byte*)ar)[len] == 0x00)
				return i;
			ar = (byte*)ar + elsz;
		}
	}
	return count;
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
	ssize_t i;
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
				return -i;
			*dst++ = c;

		} else {
			if (dst + FFSLEN("\\xXX") > end) {
				ffs_fill(dst, end, '\0', FFSLEN("\\xXX"));
				return -i;
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

uint ffs_toint(const char *src, size_t len, void *dst, int flags)
{
	uint64 r = 0;
	size_t i = 0;
	ffbool minus = 0;
	int digits = 0;
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

	if (!(flags & FFS_INTHEX)) {
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

uint ffs_fromint(uint64 i, char *dst, size_t cap, int flags)
{
	const char *end = dst + cap;
	const char *dsto = dst;
	uint len;
	char s[FFINT_MAXCHARS];
	char *ps = s + FFCNT(s);

	if ((flags & FFINT_SIGNED) && (int64)i < 0) {
		if (dst != end)
			*dst++ = '-';
		i = -(int64)i;
	}

	if ((flags & (FFINT_HEXUP | FFINT_HEXLOW)) == 0) {
		//decimal
		if (i <= 0xffffffff) {
			uint i4 = (uint)i;
			do {
				*(--ps) = (byte)(i4 % 10 + '0');
				i4 /= 10;
			} while (i4 != 0);
		}
		else {
			do {
				*(--ps) = (byte)(i % 10 + '0');
				i /= 10;
			} while (i != 0);
		}
	}
	else {
		//hex
		const char *phex = ffhex;
		if (flags & FFINT_HEXUP)
			phex = ffHEX;
		do {
			*(--ps) = phex[i & 0x0f];
			i >>= 4;
		}
		while (i != 0);
	}

	{
		uint width = flags >> 24;
		char fill = ' ';
		if (flags & FFINT_ZEROWIDTH)
			fill = '0';
		len = (uint)(s + FFCNT(s) - ps);
		while (len < width && dst != end) {
			*(dst++) = fill;
			len++;
		}
	}

	len = (uint)ffmin(end - dst, s + FFCNT(s) - ps);
	memcpy(dst, ps, len);
	dst += len;
	return (uint)(dst - dsto);
}

uint ffs_tofloat(const char *s, size_t len, double *dst, int flags)
{
	double d = 0.0;
	ffbool minus = 0;
	ffbool negexp = 0;
	int exp = 0;
	int e = 0;
	int digits = 0;
	int i;
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
	uint width = (byte)(flags >> 24), wfrac = (byte)(flags >> 16), n, scale;

	if (d < 0) {
		buf = ffs_copyc(buf, end, '-');
		d = -d;
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

	buf += ffs_fromint(num, buf, end - buf, (flags & FFINT_ZEROWIDTH) | FFINT_WIDTH(width));

	if (wfrac != 0) {
		buf = ffs_copyc(buf, end, '.');
		buf += ffs_fromint(frac, buf, end - buf, FFINT_ZEROWIDTH | FFINT_WIDTH(wfrac));
	}

	return buf - dst;
}

size_t ffs_fmtv(char *buf_o, const char *end, const char *fmt, va_list va)
{
	size_t swidth, len = 0;
	uint width, wfrac;
	int64 num = 0;
	double d;
	uint itoaFlags = 0;
	ffbool itoa = 0;
	char *buf = buf_o;
	const void *rend = (buf != NULL) ? end : (void*)-1;

	for (;  *fmt && buf != rend;  ++fmt) {
		if (*fmt != '%') {
			if (buf != NULL)
				*buf++ = *fmt;
			else
				len++;
			continue;
		}

		fmt++; //skip %

		swidth = (size_t)-1;
		width = wfrac = 0;

		if (*fmt == '0') {
			itoaFlags |= FFINT_ZEROWIDTH;
			fmt++;
		}

		while (ffchar_isdigit(*fmt)) {
			width = width * 10 + (*fmt++ - '0');
		}

		switch (*fmt) {
		case '*':
			swidth = va_arg(va, size_t);
			fmt++;
			break;

		case 'x':
			itoaFlags |= FFINT_HEXLOW;
			fmt++;
			break;

		case 'X':
			itoaFlags |= FFINT_HEXUP;
			fmt++;
			break;

		case '.':
			fmt++;
			while (ffchar_isdigit(*fmt)) {
				wfrac = wfrac * 10 + (*fmt++ - '0');
			}
			break;
		}

		switch (*fmt) {

		case 'D':
			itoaFlags |= FFINT_SIGNED;
			//break;
		case 'U':
			num = va_arg(va, int64);
			itoa = 1;
			break;

		case 'd':
			itoaFlags |= FFINT_SIGNED;
			num = va_arg(va, int);
			break;
		case 'u':
			num = va_arg(va, uint);
			itoa = 1;
			break;

		case 'I':
			itoaFlags |= FFINT_SIGNED;
			num = va_arg(va, ssize_t);
			break;
		case 'L':
			num = va_arg(va, size_t);
			itoa = 1;
			break;

		case 'p':
			num = va_arg(va, size_t);
			width = sizeof(void*) * 2;
			itoaFlags |= FFINT_HEXLOW | FFINT_ZEROWIDTH;
			break;

		case 'F':
			d = va_arg(va, double);
			itoaFlags |= FFINT_WIDTH(width) | (wfrac << 16);
			buf += ffs_fromfloat(d, buf, end - buf, itoaFlags);
			len += width + FFINT_MAXCHARS + FFSLEN(".") + wfrac + FFINT_MAXCHARS;
			itoaFlags = 0;
			break;

#ifdef FF_UNIX
		case 'q':
#endif
		case 's': {
			const char *s = va_arg(va, char *);
			if (swidth == (size_t)-1) {
				buf = ffs_copyz(buf, end, s);
				if (buf == NULL)
					len += strlen(s);

			} else {
				buf = ffs_copy(buf, end, s, swidth);
				len += swidth;
			}
			}
			break;

#ifdef FF_UNIX
		case 'Q':
#endif
		case 'S': {
			ffstr *s = va_arg(va, ffstr *);
			buf = ffs_copy(buf, end, s->ptr, s->len);
			len += s->len;
			}
			break;

#ifdef FF_WIN
		case 'q': {
			ffsyschar *s = va_arg(va, ffsyschar *);
			if (swidth == (size_t)-1)
				swidth = ffq_len(s);
			buf = ffs_copyq(buf, end, s, swidth);
			if (buf == NULL)
				len += ffq_lenq(s, swidth);
			}
			break;

		case 'Q': {
			ffqstr *s = va_arg(va, ffqstr *);
			buf = ffs_copyq(buf, end, s->ptr, s->len);
			if (buf == NULL)
				len += ffq_lenq(s->ptr, s->len);
			}
			break;
#endif

		case 'e': {
			int e = va_arg(va, int);
			const char *se = fferr_opstr((enum FFERR)e);
			buf = ffs_copyz(buf, end, se);
			if (buf == NULL)
				len += strlen(se);
			}
			break;

		case 'E': {
			int e = va_arg(va, int);
			size_t r = ffs_fmt(buf, end, "(%u) ", e);
			if (buf != NULL) {
				buf += r;
				fferr_str(e, buf, end - buf);
				buf += ffsz_len(buf);

			} else {
				char tmp[256];
				fferr_str(e, tmp, sizeof(tmp));
				len += r + ffsz_len(tmp);
			}
			}
			break;

		case 'c': {
			uint ch = va_arg(va, int);
			if (swidth == (size_t)-1)
				swidth = 1;

			if (buf != NULL) {
				while (swidth-- != 0) {
					*buf++ = (char)ch;
				}
			} else
				len += swidth;
			}
			break;

		case 'Z':
			if (buf != NULL)
				*buf++ = '\0';
			else
				len++;
			break;

		case '%':
			if (buf != NULL)
				*buf++ = '%';
			else
				len++;
			break;

		default:
			return 0;
		}

		if (itoaFlags != 0 || itoa) {
			if (width != 0) {
				if (width > 255)
					width = 255;
				itoaFlags |= FFINT_WIDTH(width);
			}
			buf += ffs_fromint(num, buf, end - buf, itoaFlags);
			len += width + FFINT_MAXCHARS;
			itoaFlags = 0;
			itoa = 0;
		}
	}

	if (buf == NULL)
		return len;

	return buf - buf_o;
}

size_t ffstr_catfmtv(ffstr3 *s, const char *fmt, va_list args)
{
	size_t r;
	va_list va;

	va_copy(va, args);
	r = ffs_fmtv(NULL, NULL, fmt, va);
	va_end(va);

	if (r == 0 || NULL == ffarr_grow(s, r, 0))
		return 0;

	va_copy(va, args);
	r = ffs_fmtv(ffarr_end(s), s->ptr + s->cap, fmt, va);
	va_end(va);

	s->len += r;
	return r;
}

size_t fffile_fmt(fffd fd, ffstr3 *buf, const char *fmt, ...)
{
	size_t r;
	va_list args;
	ffstr3 dst = { 0 };

	if (buf == NULL) {
		if (NULL == ffarr_realloc(&dst, 1024))
			return 0;
		buf = &dst;
	}
	else
		buf->len = 0;

	va_start(args, fmt);
	r = ffstr_catfmtv(buf, fmt, args);
	va_end(args);

	if (r != 0)
		r = fffile_write(fd, buf->ptr, r);

	ffarr_free(&dst);
	return r;
}

size_t ffs_fmatchv(const char *s, size_t len, const char *fmt, va_list va)
{
	const char *s_o = s, *s_end = s + len;
	uint width = 0;
	union {
		char *s;
		uint *i4;
		uint64 *i8;
	} dst;
	dst.s = NULL;

	for (;  s != s_end && *fmt != '\0';  s++) {

		if (width != 0) {
			switch (*fmt) {
			case 'U':
				if (!ffchar_isdigit(*s))
					goto fail;
				*dst.i8 = *dst.i8 * 10 + ffchar_tonum(*s);
				break;

			case 'u':
				if (!ffchar_isdigit(*s))
					goto fail;
				*dst.i4 = *dst.i4 * 10 + ffchar_tonum(*s);
				break;

			case 's':
				*(dst.s++) = *s;
				break;
			}

			if (--width == 0)
				fmt++;
			continue;
		}

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

		while (ffchar_isdigit(*fmt)) {
			width = width * 10 + (*fmt++ - '0');
		}

		switch (*fmt) {
		case 'U':
			dst.i8 = va_arg(va, uint64*);
			*dst.i8 = 0;
			break;

		case 'u':
			dst.i4 = va_arg(va, uint*);
			*dst.i4 = 0;
			break;

		case 's':
			dst.s = va_arg(va, char*);
			break;

		default:
			goto fail; //invalid format specifier
		}

		if (width == 0)
			fmt++;
		s--;
	}

	if (*fmt != '\0')
		goto fail; //input string is too short

	return s - s_o;

fail:
	return -(s - s_o + 1);
}


void * _ffarr_realloc(ffarr *ar, size_t newlen, size_t elsz)
{
	void *d = NULL;
	if (ar->cap != 0) {
		if (ar->cap == newlen)
			return ar->ptr; //nothing to do
		d = ar->ptr;
	}
	d = ffmem_realloc(d, newlen * elsz);
	if (d == NULL)
		return NULL;

	ar->ptr = d;
	ar->cap = newlen;
	ar->len = ffmin(ar->len, newlen);
	return d;
}

char *_ffarr_grow(ffarr *ar, size_t by, ssize_t lowat, size_t elsz)
{
	size_t newcap = ar->len + by;
	if (ar->cap >= newcap)
		return ar->ptr;

	if (lowat == FFARR_GROWQUARTER)
		lowat = newcap / 4; //allocate 25% more
	if (by < (size_t)lowat)
		newcap = ar->len + (size_t)lowat;
	return (char*)_ffarr_realloc(ar, newcap, elsz);
}

void _ffarr_free(ffarr *ar)
{
	if (ar->cap != 0) {
		FF_ASSERT(ar->ptr != NULL);
		FF_ASSERT(ar->cap >= ar->len);
		ffmem_free(ar->ptr);
		ar->cap = 0;
	}
	ar->ptr = NULL;
	ar->len = 0;
}

void * _ffarr_push(ffarr *ar, size_t elsz)
{
	if (ar->cap < ar->len + 1
		&& NULL == _ffarr_realloc(ar, ar->len + 1, elsz))
		return NULL;

	ar->len += 1;
	return ar->ptr + (ar->len - 1) * elsz;
}

void * _ffarr_append(ffarr *ar, const void *src, size_t num, size_t elsz)
{
	if (ar->cap < ar->len + num
		&& NULL == _ffarr_realloc(ar, ar->len + num, elsz))
		return NULL;

	ffmemcpy(ar->ptr + ar->len * elsz, src, num * elsz);
	ar->len += num;
	return ar->ptr + ar->len * elsz;
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
	uint f = spl & ~0xff;
	spl &= 0xff;

	if (f & FFS_NV_REVERSE) {
		pos = ffs_rfind(buf, end - buf, spl);
		if (pos == end)
			pos = buf;
		else {
			len = end - pos;
			pos++;
		}

		if (!(f & FFS_NV_KEEPWHITE)) {
			pos = ffs_skip(pos, end - pos, ' ');
			end = ffs_rskip(pos, end - pos, ' ');
		}

		ffstr_set(dst, pos, end - pos);
		return len;
	}

	if (!(f & FFS_NV_KEEPWHITE))
		buf = ffs_skip(buf, end - buf, ' ');

	if (buf == end) {
		dst->len = 0;
		return len;
	}

	if ((f & FFSTR_NV_DBLQUOT) && buf[0] == '"') {
		buf++;
		spl = '"';
	}

	pos = ffs_find(buf, end - buf, spl);
	if (pos != end)
		len = pos - (end - len) + 1;

	if (!(f & FFS_NV_KEEPWHITE))
		pos = ffs_rskip(buf, pos - buf, ' ');

	ffstr_set(dst, buf, pos - buf);
	return len;
}

size_t ffbuf_add(ffstr3 *buf, const char *src, size_t len, ffstr *dst)
{
	size_t sz;

	if (buf->len == 0 && len > buf->cap) {
		// input is larger than buffer
		sz = len - (len % buf->cap);
		ffstr_set(dst, src, sz);
		return sz;
	}

	sz = ffmin(len, ffarr_unused(buf));
	ffmemcpy(buf->ptr + buf->len, src, sz);
	buf->len += sz;

	if (buf->cap != buf->len) {
		dst->len = 0;
		return sz;
	}

	//buffer is full
	ffstr_set(dst, buf->ptr, buf->len);
	buf->len = 0;
	return sz;
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


#ifdef FFDBG_PRINT_DEF
#include <FFOS/atomic.h>
#include <FFOS/file.h>

static ffatomic counter;
int ffdbg_print(int t, const char *fmt, ...)
{
	char buf[4096];
	size_t n;
	va_list va;
	va_start(va, fmt);

	n = ffs_fmt(buf, buf + FFCNT(buf), "%p#%L "
		, &counter, (size_t)ffatom_incret(&counter));

	n += ffs_fmtv(buf + n, buf + FFCNT(buf), fmt, va);
	fffile_write(ffstdout, buf, n);

	va_end(va);
	return 0;
}
#endif
