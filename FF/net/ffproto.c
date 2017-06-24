/*
Copyright (c) 2017 Simon Zolin
*/

#include <FF/net/proto.h>
#include <FF/string.h>


uint ffeth_tostr(char *buf, size_t cap, const ffeth *eth)
{
	if (cap < FFETH_STRLEN)
		return 0;

	buf += ffs_hexbyte(buf, eth->a[0], ffHEX);
	for (uint i = 1;  i != 6;  i++, buf += 3) {
		buf[0] = ':';
		ffs_hexbyte(buf + 1, eth->a[i], ffHEX);
	}

	return FFETH_STRLEN;
}

int ffeth_parse(ffeth *eth, const char *s, size_t len)
{
	int lo, hi;
	if (len < FFETH_STRLEN)
		return -1;

	for (uint i = 0;  i != 6;  i++, s += 3) {

		hi = ffchar_tohex(s[0]);
		lo = ffchar_tohex(s[1]);
		eth->a[i] = (hi << 4) | lo;

		if (hi < 0 || lo < 0
			|| (i != 5 && s[2] != ':'))
			return -1;
	}

	return FFETH_STRLEN;
}


int ffip4_parse(ffip4 *ip4, const char *s, size_t len)
{
	uint nadr = 0, ndig = 0, b = 0, i;

	for (i = 0;  i != len;  i++) {
		uint ch = (byte)s[i];

		if (ffchar_isdigit(ch) && ndig != 3) {
			b = b * 10 + ffchar_tonum(ch);
			if (b > 255)
				return -1; //"256."
			ndig++;

		} else if (ndig == 0) {
			return -1; //"1.?"

		} else if (nadr == 3) {
			ip4->a[nadr] = b;
			return i;

		} else if (ch == '.') {
			ip4->a[nadr++] = b;
			b = 0;
			ndig = 0;

		} else
			return -1;
	}

	if (nadr == 3 && ndig != 0) {
		ip4->a[nadr] = b;
		return 0;
	}

	return -1;
}

int ffip4_parse_subnet(ffip4 *ip4, const char *s, size_t len)
{
	uint subnet, r;
	r = ffip4_parse(ip4, s, len);
	if ((int)r <= 0)
		return -1;

	if (!(len >= r + 1 && s[r] == '/'))
		return -1;
	r++;

	if (len - r != ffs_toint(s + r, len - r, &subnet, FFS_INT32)
		|| subnet == 0 || subnet > 32)
		return -1;

	return subnet;
}

size_t ffip4_tostr(char *dst, size_t cap, const ffip4 *ip4)
{
	char *p = dst, *end = dst + cap;
	uint n;
	if (cap < FFIP4_STRLEN)
		return 0;

	for (uint i = 0;  i != 4;  i++) {
		n = ffs_fromint((byte)ip4->a[i], p, end - p, 0);
		if (n == 0)
			return 0;
		p += n;
		if (i != 3)
			*p++ = '.';
	}

	return p - dst;
}


int ffip6_parse(void *a, const char *s, size_t len)
{
	uint i, chunk = 0, ndigs = 0;
	char *dst = (char*)a;
	const char *end = (char*)a + 16, *zr = NULL;
	int hx;

	for (i = 0;  i != len;  i++) {
		int b = s[i];

		if (dst == end)
			return -1; // too large input

		if (b == ':') {

			if (ndigs == 0) { // "::"
				uint k;

				if (i == 0) {
					i++;
					if (i == len || s[i] != ':')
						return -1; // ":" or ":?"
				}

				if (zr != NULL)
					return -1; // second "::"

				// count the number of chunks after zeros
				zr = end;
				if (i != len - 1)
					zr -= 2;
				for (k = i + 1;  k < len;  k++) {
					if (s[k] == ':')
						zr -= 2;
				}

				// fill with zeros
				while (dst != zr)
					*dst++ = '\0';

				continue;
			}

			*dst++ = chunk >> 8;
			*dst++ = chunk & 0xff;
			ndigs = 0;
			chunk = 0;
			continue;
		}

		if (ndigs == 4)
			return -1; // ":12345"

		hx = ffchar_tohex(b);
		if (hx == -1)
			return -1; // invalid hex char

		chunk = (chunk << 4) | hx;
		ndigs++;
	}

	if (ndigs != 0) {
		*dst++ = chunk >> 8;
		*dst++ = chunk & 0xff;
	}

	if (dst != end)
		return -1; // too small input

	return 0;
}

size_t ffip6_tostr(char *dst, size_t cap, const void *addr)
{
	const byte *a = addr;
	char *p = dst;
	const char *end = dst + cap;
	int i;
	int cut_from = -1
		, cut_len = 0;
	int zrbegin = 0
		, nzr = 0;

	if (cap < FFIP6_STRLEN)
		return 0;

	// get the maximum length of zeros to cut off
	for (i = 0;  i < 16;  i += 2) {
		if (a[i] == '\0' && a[i + 1] == '\0') {
			if (nzr == 0)
				zrbegin = i;
			nzr += 2;

		} else if (nzr != 0) {
			if (nzr > cut_len) {
				cut_from = zrbegin;
				cut_len = nzr;
			}
			nzr = 0;
		}
	}

	if (nzr > cut_len) {
		// zeros at the end of address
		cut_from = zrbegin;
		cut_len = nzr;
	}

	for (i = 0;  i < 16; ) {
		if (i == cut_from) {
			// cut off the sequence of zeros
			*p++ = ':';
			i = cut_from + cut_len;
			if (i == 16)
				*p++ = ':';
			continue;
		}

		if (i != 0)
			*p++ = ':';
		p += ffs_fromint(ffint_ntoh16(a + i), p, end - p, FFINT_HEXLOW); //convert 16-bit number to string
		i += 2;
	}

	return p - dst;
}
