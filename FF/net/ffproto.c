/*
Copyright (c) 2017 Simon Zolin
*/

#include <FF/net/proto.h>
#include <FF/string.h>
#include <FFOS/cpu.h>


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

	return (len == FFETH_STRLEN) ? 0 : FFETH_STRLEN;
}


int ffip4_parse_wildcard(ffip4 *ip4, const char *s, size_t len, uint *subnet)
{
	uint nadr = 0, ndig = 0, b = 0, i;
	*subnet = 32;

	for (i = 0;  i != len;  i++) {
		uint ch = (byte)s[i];

		if (ffchar_isdigit(ch) && ndig != 3) {
			b = b * 10 + ffchar_tonum(ch);
			if (b > 255)
				return -1; //"256."
			ndig++;

		} else if (ndig == 0) {
			if (ch == '*') {
				// "1.*"
				ffmem_zero(ip4->a + nadr, 4 - nadr);
				*subnet = nadr * 8;
				return (i + 1 == len) ? 0 : i + 1;
			}
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

int ffip4hdr_tostr(ffip4hdr *ip4, char *buf, size_t cap)
{
	size_t n, n2;
	char src[FFIP4_STRLEN], dst[FFIP4_STRLEN];

	n = ffip4_tostr(src, sizeof(src), &ip4->saddr);
	n2 = ffip4_tostr(dst, sizeof(dst), &ip4->daddr);

	return ffs_fmt(buf, buf + cap, "%*s -> %*s  len:%xu  ttl:%u  proto:%xu"
		, n, src, n2, dst
		, ffint_ntoh16(ip4->total_len), ip4->ttl, ip4->proto);
}


int ffip6_parse_wildcard(void *a, const char *s, size_t len, uint *subnet)
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

		if (ndigs == 0 && b == '*' && i + 1 == len) {
			// "1:2:*"
			*subnet = (dst - (char*)a) * 8;
			ffmem_zero(dst, end - dst);
			return 0;
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

	*subnet = 16 * 8;
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

int ffip6hdr_tostr(ffip6hdr *ip6, char *buf, size_t cap)
{
	size_t n, n2;
	char src[FFIP6_STRLEN], dst[FFIP6_STRLEN];

	n = ffip6_tostr(src, sizeof(src), &ip6->saddr);
	n2 = ffip6_tostr(dst, sizeof(dst), &ip6->daddr);

	return ffs_fmt(buf, buf + cap, "%*s -> %*s  len:%xu  proto:%xu"
		, n, src, n2, dst
		, ffint_ntoh16(ip6->payload_len), ip6->nexthdr);
}


/** Compute Internet checksum with data length which is a multiple of 4. */
static FFINL uint in_chksum_4(const void *buf, uint len4)
{
	uint sum = 0;
	const uint *n = buf;

	for (;  len4 != 0;  len4--) {
		sum = ffint_addcarry32(sum, *n++);
	}
	return sum;
}

/** Add hi-word to low-word with Carry flag: (HI(sum) + LO(sum)) + CF. */
static FFINL uint in_chksum_reduce(uint sum)
{
	sum = (sum >> 16) + (sum & 0xffff);
	sum = (sum >> 16) + (sum & 0xffff);
	return sum;
}

uint ffip4_chksum(const void *hdr, uint ihl)
{
	uint sum = in_chksum_4(hdr, ihl);
	return ~in_chksum_reduce(sum) & 0xffff;
}


int ffarp_tostr(ffarphdr *h, char *buf, size_t cap)
{
	size_t n0, n1;
	char eth[2][FFETH_STRLEN], ip[2][FFIP4_STRLEN];

	ffeth_tostr(eth[0], sizeof(eth[0]), &h->sha);
	ffeth_tostr(eth[1], sizeof(eth[1]), &h->tha);

	n0 = ffip4_tostr(ip[0], sizeof(ip[0]), &h->spa);
	n1 = ffip4_tostr(ip[1], sizeof(ip[1]), &h->tpa);

	return ffs_fmt(buf, buf + cap, "op:%u  srchw:%*s  src-ip:%*s  dsthw:%*s  dst-ip:%*s"
		, ffint_ntoh16(h->op)
		, (size_t)FFETH_STRLEN, eth[0], n0, ip[0]
		, (size_t)FFETH_STRLEN, eth[1], n1, ip[1]);
}
