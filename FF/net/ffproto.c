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

int ffethhdr_tostr(ffeth_hdr *h, char *buf, size_t cap)
{
	uint n;
	char src[FFETH_STRLEN], dst[FFETH_STRLEN];

	ffeth_tostr(src, sizeof(src), &h->saddr);
	ffeth_tostr(dst, sizeof(dst), &h->daddr);

	const ffvlanhdr *vlan = NULL;
	uint l3type = ffint_ntoh16(h->type);
	if (l3type == FFETH_VLAN) {
		vlan = (void*)(h + 1);
		l3type = ffint_ntoh16(vlan->eth_type);
	}

	n = ffs_fmt(buf, buf + cap, "%*s -> %*s  type:%xu"
		, (size_t)FFETH_STRLEN, src, (size_t)FFETH_STRLEN, dst
		, l3type);

	if (vlan != NULL)
		n += ffs_fmt(buf + n, buf + cap, "  VLAN ID:%xu"
			, ffvlan_id(vlan));
	return n;
}


int ffip4hdr_tostr(ffip4hdr *ip4, char *buf, size_t cap)
{
	size_t n, n2;
	char src[FFIP4_STRLEN], dst[FFIP4_STRLEN];

	n = ffip4_tostr(&ip4->saddr, src, sizeof(src));
	n2 = ffip4_tostr(&ip4->daddr, dst, sizeof(dst));

	return ffs_fmt(buf, buf + cap, "%*s -> %*s  len:%xu  ttl:%u  proto:%xu"
		, n, src, n2, dst
		, ffint_ntoh16(ip4->total_len), ip4->ttl, ip4->proto);
}


int ffip6hdr_tostr(ffip6hdr *ip6, char *buf, size_t cap)
{
	size_t n, n2;
	char src[FFIP6_STRLEN], dst[FFIP6_STRLEN];

	n = ffip6_tostr(&ip6->saddr, src, sizeof(src));
	n2 = ffip6_tostr(&ip6->daddr, dst, sizeof(dst));

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

	n0 = ffip4_tostr(&h->spa, ip[0], sizeof(ip[0]));
	n1 = ffip4_tostr(&h->tpa, ip[1], sizeof(ip[1]));

	return ffs_fmt(buf, buf + cap, "op:%u  srchw:%*s  src-ip:%*s  dsthw:%*s  dst-ip:%*s"
		, ffint_ntoh16(h->op)
		, (size_t)FFETH_STRLEN, eth[0], n0, ip[0]
		, (size_t)FFETH_STRLEN, eth[1], n1, ip[1]);
}
