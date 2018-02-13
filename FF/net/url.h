/** URL.
Copyright (c) 2013 Simon Zolin
*/

#pragma once

#include <FF/array.h>
#include <FF/data/parse.h>
#include <FF/net/proto.h>
#include <FFOS/socket.h>


/** URL structure. */
typedef struct ffurl {
	ushort offhost;
	ushort port; //number
	byte hostlen;
	byte portlen;

	ushort len;
	ushort offpath;
	ushort pathlen;
	ushort decoded_pathlen; //length of decoded filename

	unsigned idx : 4
		, ipv4 : 1
		, ipv6 : 1
		, querystr : 1 //has query string
		, complex : 1; //path contains %xx or "/." or "//"
} ffurl;

static FFINL void ffurl_init(ffurl *url) {
	memset(url, 0, sizeof(ffurl));
}

/** URL parsing error code. */
enum FFURL_E {
	FFURL_EOK ///< the whole input data has been processed
	, FFURL_EMORE ///< need more data
	, FFURL_ESTOP ///< unknown character has been met
	, FFURL_ETOOLARGE
	, FFURL_ESCHEME
	, FFURL_EIP6
	, FFURL_EHOST
	, FFURL_EPATH
};

/** Parse URL.
Return enum FFURL_E. */
FF_EXTN int ffurl_parse(ffurl *url, const char *s, size_t len);

/** Set a new base for the parsed URL structure. */
static FFINL void ffurl_rebase(ffurl *url, const char *oldbase, const char *newbase) {
	ssize_t off = oldbase - newbase;
	if (url->hostlen != 0)
		url->offhost += (short)off;
	url->offpath += (short)off;
	url->len += (short)off;
}

/** Get error message. */
FF_EXTN const char *ffurl_errstr(int er);

/** URL component. */
enum FFURL_COMP {
	FFURL_FULLHOST // "host:8080"
	, FFURL_SCHEME // "http"
	, FFURL_HOST // "host"
	, FFURL_PORT
	, FFURL_PATH // "/file%20name"
	, FFURL_QS // "query%20string"
	, FFURL_PATHQS // "/file%20name?query%20string"
};

/** Get a component of the URL. */
FF_EXTN ffstr ffurl_get(const ffurl *url, const char *base, int comp);

/**
Return address family;  0 if not an IP address;  -1 on error */
FF_EXTN int ffurl_parse_ip(ffurl *u, const char *base, ffip6 *dst);


enum FFURI_DECODE {
	FFURI_DEC_NORM_PATH = 0x10, // normalize path: merge dots & slashes, force strict bounds
};

/** Decode %xx in URI.
@flags: enum FFURI_DECODE
Return the number of bytes written into dst.
Return 0 on error. */
FF_EXTN size_t ffuri_decode(char *dst, size_t dstcap, const char *d, size_t len, uint flags);

enum FFURI_ESCAPE {
	FFURI_ESC_WHOLE, // http://host/path
	FFURI_ESC_PATHSEG, // http://host/path/SEGMENT/path
	FFURI_ESC_QSSEG, // http://host/path?qs&SEGMENT&qs
};

/** Replace special characters in URI with %XX.
@type: enum FFURI_ESCAPE.
Return the number of bytes written.
Return <0 if there is no enough space. */
FF_EXTN ssize_t ffuri_escape(char *dst, size_t cap, const char *s, size_t len, uint type);

/** Parse URI scheme.
Return scheme length on success. */
static FFINL uint ffuri_scheme(const char *s, size_t len)
{
	ffstr scheme;
	if (0 >= (ssize_t)ffs_fmatch(s, len, "%S://", &scheme))
		return 0;
	return scheme.len;
}

/** Get port number by scheme name.
Return 0 if unknown. */
FF_EXTN uint ffuri_scheme2port(const char *scheme, size_t schemelen);


/** Parse and decode query string.
@d: full and valid query string. */
FF_EXTN int ffurlqs_parse(ffparser *p, const char *d, size_t *len);

FF_EXTN int ffurlqs_parseinit(ffparser *p);

FF_EXTN int ffurlqs_scheminit(ffparser_schem *ps, ffparser *p, const ffpars_ctx *ctx);

FF_EXTN int ffurlqs_schemfin(ffparser_schem *ps);


typedef struct ffiplist {
	ffarr2 ip4; //ffip4[]
	ffarr2 ip6; //ffip6[]
} ffiplist;

typedef struct ffip_iter {
	uint idx;
	ffiplist *list;
	ffaddrinfo *ai;
} ffip_iter;

static FFINL void ffip_list_set(ffiplist *l, uint family, const void *ip)
{
	ffarr2 *a = (family == AF_INET) ? &l->ip4 : &l->ip6;
	a->len = 1;
	a->ptr = (void*)ip;
}

#define ffip_iter_set(a, iplist, ainfo) \
do { \
	(a)->idx = 0; \
	(a)->list = iplist; \
	(a)->ai = ainfo; \
} while (0)

/** Get next address.
Return address family;  0 if no next address. */
FF_EXTN int ffip_next(ffip_iter *it, void **ip);


/** Convert IP address to string
'port': optional parameter (e.g. "127.0.0.1:80", "[::1]:80"). */
FF_EXTN uint ffip_tostr(char *buf, size_t cap, uint family, const void *ip, uint port);

/** Split "IP:PORT" address string.
e.g.: "127.0.0.1:80", "[::1]:80", ":80".
Return 0 on success. */
FF_EXTN int ffip_split(const char *s, size_t len, ffstr *ip, ffstr *port);

enum FFADDR_FLAGS {
	FFADDR_USEPORT = 1
};

/** Convert IPv4/IPv6 address to string.
@flags: enum FFADDR_FLAGS. */
static FFINL size_t ffaddr_tostr(const ffaddr *a, char *dst, size_t cap, int flags) {
	const void *ip = (ffaddr_family(a) == AF_INET) ? (void*)&a->ip4.sin_addr : (void*)&a->ip6.sin6_addr;
	return ffip_tostr(dst, cap, ffaddr_family(a), ip, (flags & FFADDR_USEPORT) ? ffip_port(a) : 0);
}

/** Set address and port.
Return 0 on success. */
FF_EXTN int ffaddr_set(ffaddr *a, const char *ip, size_t iplen, const char *port, size_t portlen);
