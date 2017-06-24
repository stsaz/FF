/** Ethernet, IPv4, IPv6.
Copyright (c) 2017 Simon Zolin
*/

#pragma once

#include <FFOS/string.h>
#include <FF/number.h>


typedef struct { char a[6]; } ffeth;

enum { FFETH_STRLEN = FFSLEN("00:00:00:00:00:00") };

/** Convert Ethernet address into string.
Return number of bytes written;  0 if not enough space. */
FF_EXTN uint ffeth_tostr(char *buf, size_t cap, const ffeth *eth);

/** Parse Ethernet address from string.
Return the number of bytes processed;  <0 on error. */
FF_EXTN int ffeth_parse(ffeth *addr, const char *s, size_t len);

typedef struct ffeth_hdr {
	ffeth daddr;
	ffeth saddr;
	byte type[2]; //enum FFETH_T
} ffeth_hdr;

enum FFETH_T {
	FFETH_IP4 = 0x0800,
	FFETH_ARP = 0x0806,
	FFETH_VLAN = 0x8100,
	FFETH_IP6 = 0x86dd,
	FFETH_MPLS = 0x8847,
};

#define ffeth_cpy(dst, src)  ffmemcpy(dst, src, 6)


typedef struct ffvlanhdr {
	byte info[2]; //PCP[3]  DEI[1]  VID[12]
	byte eth_type[2]; //enum FFETH_T
} ffvlanhdr;

#define ffvlan_id(vh)  (ffint_ntoh16((vh)->info) & 0x0fff)


typedef struct { char a[4]; } ffip4;

enum { FFIP4_STRLEN = FFSLEN("000.000.000.000") };

/** Parse IPv4 address.
Return 0 if the whole input is parsed;  >0 number of processed bytes;  <0 on error. */
FF_EXTN int ffip4_parse(ffip4 *ip4, const char *s, size_t len);

/** Parse "1.1.1.1/24"
Return subnet mask bits;  <0 on error. */
FF_EXTN int ffip4_parse_subnet(ffip4 *ip4, const char *s, size_t len);

/** Convert IPv4 address to string.
Return the number of characters written. */
FF_EXTN size_t ffip4_tostr(char *dst, size_t cap, const ffip4 *ip4);

typedef struct ffip4hdr {
#ifdef FF_BIG_ENDIAN
	byte version :4 //=4
		, ihl :4; //>=5
	byte dscp :6
		, ecn :2;
#else
	byte ihl :4
		, version :4;
	byte ecn :2
		, dscp :6;
#endif
	byte total_len[2];
	byte id[2];
	byte frag[2]; // flags[3]:[res, dont_frag, more_frags]  frag_off[13]
	byte ttl;
	byte proto; //enum FFIP4_PROTO
	byte crc[2];
	ffip4 saddr;
	ffip4 daddr;

	byte opts[0];
} ffip4hdr;

enum FFIP4_PROTO {
	FFIP_TCP = 6,
	FFIP_UDP = 17,
};

#define ffip4_frag_more(ip4) (!!((ip4)->frag[0] & 0x20))
#define ffip4_frag_dont(ip4) (!!((ip4)->frag[0] & 0x40))

/** Fragment offset.  The last one has more_frags=0. */
#define ffip4_frag_off(ip4)  ((ffint_ntoh16((ip4)->frag) & 0x1fff) * 8)

#define ffip4_hdrlen(ip4)  ((ip4)->ihl * 4)

#define ffip4_datalen(ip4)  ffint_ntoh16((ip4)->total_len) - ffip4_hdrlen(ip4)


typedef struct { char a[16]; } ffip6;

enum { FFIP6_STRLEN = FFSLEN("abcd:") * 8 - 1 };

/** Parse IPv6 address.
Return 0 on success.
Note: v4-mapped address is not supported. */
FF_EXTN int ffip6_parse(void *addr, const char *s, size_t len);

/** Convert IPv6 address to string.
Return the number of characters written.
Note: v4-mapped address is not supported. */
FF_EXTN size_t ffip6_tostr(char *dst, size_t cap, const void *addr);

typedef struct ffip6hdr {
	byte ver_tc_fl[4]; // ver[4] traf_class[8] flow_label[20]
	byte payload_len[2];
	byte nexthdr;
	byte hop_limit;
	ffip6 saddr;
	ffip6 daddr;
} ffip6hdr;

#define ffip6_ver(ip6)  ((ip6)->ver_tc_fl[0] >> 4)

#define ffip6_datalen(ip6)  ffint_ntoh16((ip6)->payload_len)
