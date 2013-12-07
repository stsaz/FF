/** URL.
Copyright (c) 2013 Simon Zolin
*/

#pragma once

#include <FF/array.h>


/** URL structure. */
typedef struct {
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


struct in_addr;

/** Parse IPv4 address.
Return 4 on success.
Return 0 on error. */
FF_EXTN int ffip_parse4(struct in_addr *a, const char *s, size_t len);
