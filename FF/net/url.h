/** URL.  IPv4, IPv6.
Copyright (c) 2013 Simon Zolin
*/

#pragma once

#include <FF/array.h>
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

/** Decode %xx in URI.
Normalize path: /./ and /../
Return the number of bytes written into dst.
Return 0 on error. */
FF_EXTN size_t ffuri_decode(char *dst, size_t dstcap, const char *d, size_t len);


/** Parse IPv4 address.
Return 0 on success. */
FF_EXTN int ffip4_parse(struct in_addr *a, const char *s, size_t len);

/** Convert IPv4 address to string, e.g. "127.0.0.1:80" or "127.0.0.1"
'port': optional parameter.
Return the number of characters written. */
FF_EXTN size_t ffip4_tostr(char *dst, size_t cap, const void *addr, size_t addrlen, int port);

/** Parse IPv6 address.
Return 0 on success.
Note: v4-mapped address is not supported. */
FF_EXTN int ffip6_parse(struct in6_addr *a, const char *s, size_t len);

/** Convert IPv6 address to string, e.g. "[::1]:80" or "::1"
'port': optional parameter.
Return the number of characters written.
Note: v4-mapped address is not supported. */
FF_EXTN size_t ffip6_tostr(char *dst, size_t cap, const void *addr, size_t addrlen, int port);

enum FFADDR_FLAGS {
	FFADDR_USEPORT = 1
};

/** Convert IPv4/IPv6 address to string. */
static FFINL size_t ffaddr_tostr(const ffaddr *a, char *dst, size_t cap, int flags) {
	int port = 0;
	if (flags & FFADDR_USEPORT)
		port = ffip_port(a);

	if (ffaddr_family(a) == AF_INET)
		return ffip4_tostr(dst, cap, &a->ip4.sin_addr, 4, port);
	else //AF_INET6
		return ffip6_tostr(dst, cap, &a->ip6.sin6_addr, 16, port);
}
