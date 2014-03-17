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

#ifdef FF_MSVC
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
		if (anyof + cnt != ffs_find(anyof, cnt, buf[i]))
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

#pragma pack(push, 8)
const byte ff_intmasks[9][8] = {
	{ 0, 0, 0, 0, 0, 0, 0, 0 }
	, { 0xff, 0, 0, 0, 0, 0, 0, 0 }
	, { 0xff, 0xff, 0, 0, 0, 0, 0, 0 }
	, { 0xff, 0xff, 0xff, 0, 0, 0, 0, 0 }
	, { 0xff, 0xff, 0xff, 0xff, 0, 0, 0, 0 }
	, { 0xff, 0xff, 0xff, 0xff, 0xff, 0, 0, 0 }
	, { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0, 0 }
	, { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0 }
	, { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff }
};
#pragma pack(pop)

size_t ffs_findarr(const void *s, size_t len, const void *ar, ssize_t elsz, size_t count)
{
	if (len <= sizeof(int64)) {
		int64 left;
		size_t i;
		int64 imask;
		imask = *(int64*)ff_intmasks[len];
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

size_t ffutf8_len(const char *p, size_t len)
{
	size_t nchars = 0;
	const char *end = p + len;
	while (p < end) {
		uint d = (byte)*p;
		if (d < 0x80) //1 byte-seq: U+0000..U+007F
			p++;
		else if ((d & 0xe0) == 0xc0) //2 byte-seq: U+0080..U+07FF
			p += 2;
		else if ((d & 0xf0) == 0xe0) //3 byte-seq: U+0800..U+FFFF
			p += 3;
		else if ((d & 0xf8) == 0xf0) //4 byte-seq: U+10000..U+10FFFF
			p += 4;
		else //invalid char
			p++;
		nchars++;
	}
	return nchars;
}

char * ffs_lower(char *dst, const char *bufend, const char *src, size_t len)
{
	size_t i;
	len = ffmin(len, bufend - dst);

	for (i = 0;  i < len;  i++) {
		uint ch = (byte)src[i];
		if (ffchar_isup(ch))
			ch = ffchar_lower(ch);
		dst[i] = ch;
	}

	return dst + i;
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

	if ((flags & FFINT_SIGNED) && (int64)i < 0 && dst != end) {
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

size_t ffs_fmtv(char *buf_o, const char *end, const char *fmt, va_list va)
{
	size_t swidth;
	uint width;
	int64 num = 0;
	uint itoaFlags = 0;
	ffbool itoa = 0;
	char *buf = buf_o;

	for (; *fmt && buf < end; ++fmt) {
		if (*fmt != '%') {
			*buf++ = *fmt;
			continue;
		}

		fmt++; //skip %

		swidth = (size_t)-1;
		width = 0;

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
			*buf++ = '0';
			if (buf + 1 >= end)
				goto done;
			*buf++ = 'x';
			break;

#ifdef FF_UNIX
		case 'q':
#endif
		case 's': {
			const char *s = va_arg(va, char *);
			if (swidth == (size_t)-1)
				buf = ffs_copyz(buf, end, s);
			else
				buf = ffs_copy(buf, end, s, swidth);
			}
			break;

#ifdef FF_UNIX
		case 'Q':
#endif
		case 'S': {
			ffstr *s = va_arg(va, ffstr *);
			buf = ffs_copy(buf, end, s->ptr, s->len);
			}
			break;

#ifdef FF_WIN
		case 'q': {
			ffsyschar *s = va_arg(va, ffsyschar *);
			if (swidth == (size_t)-1)
				swidth = ffq_len(s);
			buf = ffs_copyq(buf, end, s, swidth);
			}
			break;

		case 'Q': {
			ffqstr *s = va_arg(va, ffqstr *);
			buf = ffs_copyq(buf, end, s->ptr, s->len);
			}
			break;
#endif

		case 'e': {
			int e = va_arg(va, int);
			buf = ffs_copyz(buf, end, fferr_opstr((enum FFERR)e));
			}
			break;

		case 'E': {
			int e = va_arg(va, int);
			ffsyschar tmp[256];
			int r = fferr_str(e, tmp, FFCNT(tmp));
			buf += ffs_fmt(buf, end, "(%u) %*q", (int)e, (size_t)r, tmp);
			}
			break;

		case 'c': {
			uint ch = va_arg(va, int);
			if (swidth == (size_t)-1)
				swidth = 1;
			while (swidth-- != 0) {
				*buf++ = (char)ch;
			}
			}
			break;

		case '%':
			*buf++ = '%';
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
			itoaFlags = 0;
			itoa = 0;
		}
	}

done:
	return buf - buf_o;
}

size_t ffstr_catfmtv(ffstr3 *s, const char *fmt, va_list args)
{
	size_t r;
	size_t n = 512;
	va_list va;

	do {
		if (NULL == ffarr_grow(s, n, FFARR_GROWQUARTER))
			return 0;

		va_copy(va, args);
		r = ffs_fmtv(ffarr_end(s), s->ptr + s->cap, fmt, va);
		va_end(va);

		n += ffarr_unused(s) + 512;
	} while (r == ffarr_unused(s));

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


void * _ffarr_realloc(ffarr *ar, size_t newlen, size_t elsz)
{
	void *d = NULL;
	if (ar->cap != 0)
		d = ar->ptr;
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

	memcpy(ar->ptr + ar->len * elsz, src, num * elsz);
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

	pos = ffs_find(buf, end - buf, spl);
	if (pos != end)
		len = pos - buf + 1;

	buf = ffs_skip(buf, end - buf, ' ');
	pos = ffs_rskip(buf, pos - buf, ' ');
	ffstr_set(dst, buf, pos - buf);
	return len;
}
