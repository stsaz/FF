/**
Copyright (c) 2013 Simon Zolin
*/

#include <FF/string.h>
#include <FF/array.h>


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

ffbool ffchar_tohex(char ch, byte *dst)
{
	if (ffchar_isnum(ch))
		*dst = ch - '0';
	else {
		byte b = ffchar_lower(ch) - 'a' + 10;
		if (b <= 0x0f)
			*dst = b;
		else
			return 0;
	}
	return 1;
}

uint ffs_toint(const char *src, size_t len, void *dst, int flags)
{
	uint64 r = 0;
	size_t i = 0;
	ffbool minus = 0;
	union {
		void *ptr;
		uint64 *pi64;
		uint *pi32;
		ushort *pi16;
		byte *pi8;
	} ptr;
	ptr.ptr = dst;

	if ((flags & FFS_INTSIGN) && len > 0) {
		if (src[0] == '-') {
			minus = 1;
			i++;
		}
		else if (src[0] == '+')
			i++;
	}

	if (!(flags & FFS_INTHEX)) {
		//dec
		for (; i < len; ++i) {
			byte b = src[i] - '0';
			if (b >= 10)
				goto fail; //0-9
			r = r * 10 + b;
		}
	}
	else {
		//hex
		for (; i < len; ++i) {
			byte b;
			if (!ffchar_tohex(src[i], &b))
				goto fail; //0-9a-f
			r = (r << 4) | b;
		}
	}

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
	ffstr s;

	ffstr_set(&s, search, search_len);
	for (i = 0; i < n; ++i) {
		if (ffstr_eq(&s, FFSTR2(ar[i])))
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
