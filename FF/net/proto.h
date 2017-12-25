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

static FFINL uint ffeth_tostrz(char *buf, size_t cap, const ffeth *eth)
{
	uint r = ffeth_tostr(buf, cap, eth);
	if (r == 0 || r == cap)
		return 0;
	buf[r] = '\0';
	return r;
}

/** Parse Ethernet address from string.
Return 0 if the whole input is parsed;  >0 number of processed bytes;  <0 on error. */
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

static FFINL uint ffip4_tostrz(char *dst, size_t cap, const ffip4 *ip4)
{
	uint r = ffip4_tostr(dst, cap, ip4);
	if (r == 0 || r == cap)
		return 0;
	dst[r] = '\0';
	return r;
}

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

FF_EXTN int ffip4hdr_tostr(ffip4hdr *h, char *buf, size_t cap);

/** Compute IPv4 header checksum.
To compute checksum for the first time, set checksum field to 0.
To validate checksum in a complete header, just compare the result with 0. */
FF_EXTN uint ffip4_chksum(const void *hdr, uint ihl);


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

static FFINL uint ffip6_tostrz(char *dst, size_t cap, const ffip6 *ip6)
{
	uint r = ffip6_tostr(dst, cap, ip6);
	if (r == 0 || r == cap)
		return 0;
	dst[r] = '\0';
	return r;
}

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

FF_EXTN int ffip6hdr_tostr(ffip6hdr *h, char *buf, size_t cap);


enum FFARP_HW {
	FFARP_ETH = 1,
};

enum FFARP_OP {
	FFARP_REQ = 1,
	FFARP_RESP,
};

typedef struct ffarphdr {
	byte hwtype[2]; //enum FFARP_HW
	byte proto[2];
	byte hlen; //6 for ethernet
	byte plen; //4 for ipv4
	byte op[2]; //enum FFARP_OP

	ffeth sha;
	ffip4 spa;
	ffeth tha;
	ffip4 tpa;
} ffarphdr;

FF_EXTN int ffarp_tostr(ffarphdr *h, char *buf, size_t cap);


enum FFICMP_TYPE {
	FFICMP_ECHO_REPLY = 0,
	FFICMP_ECHO_REQ = 8,
};

typedef struct fficmphdr {
	byte type; //enum FFICMP_TYPE
	byte code;
	byte crc[2];

	union {
	byte hdrdata[4];
	struct {
		byte id[2];
		byte seq[2];
	} echo;
	};
} fficmphdr;


typedef struct fftcphdr {
	byte sport[2];
	byte dport[2];
	byte seq[4];
	byte ack[4];
	byte off; //off[4] res[3] f[1]
	byte flags; //enum FFTCP_F
	byte window[2];
	byte crc[2];
	byte urgent_ptr[2];
} fftcphdr;

enum FFTCP_F {
	FFTCP_FIN = 0x01,
	FFTCP_SYN = 0x02,
	FFTCP_RST = 0x04,
	FFTCP_PUSH = 0x08,
	FFTCP_ACK = 0x10,
};

#define fftcp_hdrlen(t)  ((((t)->off & 0xf0) >> 4) * 4)


typedef struct ffudphdr {
	byte sport[2];
	byte dport[2];
	byte length[2];
	byte crc[2];
} ffudphdr;
