/** HTTP.
Copyright (c) 2013 Simon Zolin
*/

#pragma once

#include <FF/array.h>
#include <FF/url.h>
#include <FF/crc.h>


/** Method. */
enum FFHTTP_METH {
	FFHTTP_GET
	, FFHTTP_PUT
	, FFHTTP_POST
	, FFHTTP_HEAD
	, FFHTTP_DELETE
	, _FFHTTP_MLASTURI = FFHTTP_DELETE
	, FFHTTP_CONNECT
	, FFHTTP_OPTIONS
	, FFHTTP_MUKN
};

/** Method as a string. */
FF_EXTN const char ffhttp_smeth[7][8];

/** Header. */
enum FFHTTP_HDR {
	FFHTTP_HUKN
	, FFHTTP_CONNECTION
	, FFHTTP_KEEPALIVE
	, FFHTTP_DATE
	, FFHTTP_TRANSFER_ENCODING
	, FFHTTP_VIA
	, FFHTTP_CACHE_CONTROL
	, FFHTTP_CONTENT_ENCODING
	, FFHTTP_CONTENT_LENGTH
	, FFHTTP_CONTENT_RANGE
	, FFHTTP_CONTENT_TYPE
	, FFHTTP_HOST
	, FFHTTP_REFERER
	, FFHTTP_USERAGENT
	, FFHTTP_ACCEPT
	, FFHTTP_ACCEPT_ENCODING
	, FFHTTP_TE
	, FFHTTP_IFMATCH
	, FFHTTP_IFMODIFIED_SINCE
	, FFHTTP_IFNONE_MATCH
	, FFHTTP_IFRANGE
	, FFHTTP_IFUNMODIFIED_SINCE
	, FFHTTP_RANGE
	, FFHTTP_AUTHORIZATION
	, FFHTTP_COOKIE
	, FFHTTP_COOKIE2
	, FFHTTP_PROXY_AUTHORIZATION
	, FFHTTP_PROXY_CONNECTION
	, FFHTTP_AGE
	, FFHTTP_SERVER
	, FFHTTP_LOCATION
	, FFHTTP_ETAG
	, FFHTTP_EXPIRES
	, FFHTTP_LAST_MODIFIED
	, FFHTTP_ACCEPT_RANGES
	, FFHTTP_VARY
	, FFHTTP_PROXY_AUTHENTICATE
	, FFHTTP_SETCOOKIE
	, FFHTTP_SETCOOKIE2
	, FFHTTP_WWW_AUTHENTICATE
	, FFHTTP_UPGRADE
	, FFHTTP_XFORWARDEDFOR
	, FFHTTP_HSTATUS
	, FFHTTP_HLAST
	// Pragma
};

/** Header as a string. */
FF_EXTN const ffstr ffhttp_shdr[];

/** Status. */
enum FFHTTP_STATUS {
	FFHTTP_0

	, FFHTTP_200_OK
	, FFHTTP_206_PARTIAL

	, FFHTTP_301_MOVED_PERMANENTLY
	, FFHTTP_302_FOUND
	, FFHTTP_304_NOT_MODIFIED

	, FFHTTP_400_BAD_REQUEST
	, FFHTTP_403_FORBIDDEN
	, FFHTTP_404_NOT_FOUND
	, FFHTTP_405_METHOD_NOT_ALLOWED
	, FFHTTP_413_REQUEST_ENTITY_TOO_LARGE
	, FFHTTP_415_UNSUPPORTED_MEDIA_TYPE
	, FFHTTP_416_REQUESTED_RANGE_NOT_SATISFIABLE

	, FFHTTP_500_INTERNAL_SERVER_ERROR
	, FFHTTP_501_NOT_IMPLEMENTED
	, FFHTTP_502_BAD_GATEWAY
	, FFHTTP_504_GATEWAY_TIMEOUT

	, FFHTTP_SLAST
	, FFHTTP_SCUSTOM = 0xff
};

/** Status as a string. */
FF_EXTN const ffstr ffhttp_sresp[];

/** Error code. */
enum FFHTTP_E {
	FFHTTP_OK = 0
	, FFHTTP_MORE = -3
	, FFHTTP_DONE = -2

	, FFHTTP_ESYS = 1
	, FFHTTP_EMETHOD
	, FFHTTP_EURI
	, FFHTTP_EHOST
	, FFHTTP_EVERSION
	, FFHTTP_ESTATUS
	, FFHTTP_EHOST11
	, FFHTTP_EEOL
	, FFHTTP_ENOVAL
	, FFHTTP_EHDRKEY
	, FFHTTP_EHDRVAL
	, FFHTTP_EDUPHDR
	, FFHTTP_ETOOLARGE

	, FFHTTP_EURLPARSE = 0x80 ///< URL parsing error.  See FFURL_E.
};

/** Get error message. */
FF_EXTN const char *ffhttp_errstr(int code);

/** Header information. */
typedef struct {
	ushort len;
	byte idx;
	byte ihdr; //enum FFHTTP_HDR
	uint crc;
	uint64 mask; ///< bitmask of the headers the caller is interested in
	ffrange key
		, val;
} ffhttp_hdr;

static FFINL void ffhttp_inithdr(ffhttp_hdr *h) {
	memset(h, 0, sizeof(ffhttp_hdr));
	h->crc = ffcrc32_start();
}

/** Get name and value of the next HTTP header.
Headers must be within 64k boundary.
Return enum FFHTTP_E. */
FF_EXTN int ffhttp_nexthdr(ffhttp_hdr *hdr, const char *d, size_t len);

/** Return FFHTTP_METH. */
static FFINL int ffhttp_findmethod(const char *data, size_t len) {
	return (int)ffs_findarr(data, len, ffhttp_smeth, sizeof(*ffhttp_smeth), FFCNT(ffhttp_smeth));
}

/** Parsed headers information. */
typedef struct {
	const char *base;
	ushort len;
	ushort ver; ///< HTTP version, e.g. 0x0100 = http/1.0
	ushort firstline_len;
	unsigned http11 : 1
		, firstcrlf : 2
		, conn_close : 1
		, has_body : 1
		, chunked : 1 ///< Transfer-Encoding: chunked
		, body_conn_close : 1 // for response

		, ce_gzip : 1 ///< Content-Encoding: gzip
		, ce_identity : 1 ///< no Content-Encoding or Content-Encoding: identity
		;
	int64 cont_len; ///< Content-Length value or -1

	ffhttp_hdr hdr; ///< The header being parsed currently
} ffhttp_headers;

FF_EXTN void ffhttp_init(ffhttp_headers *h);

/** Parse and process the next header.
Return enum FFHTTP_E. */
FF_EXTN int ffhttp_parsehdr(ffhttp_headers *h, const char *data, size_t len);

/** Get HTTP headers including the last CRLF. */
static FFINL ffstr ffhttp_hdrs(const ffhttp_headers *h) {
	ffstr s;
	ffstr_set(&s, h->base + h->firstline_len, h->hdr.len - h->firstline_len);
	return s;
}

/** Get the first line (without CRLF). */
static FFINL ffstr ffhttp_firstline(const ffhttp_headers *h) {
	ffstr s;
	ffstr_set(&s, h->base + 0, h->firstline_len - h->firstcrlf);
	return s;
}


/** Parsed request. */
typedef struct {
	ffhttp_headers h;

	ffurl url;
	ffstr decoded_url;
	ushort offurl;
	byte methodlen;
	byte method; //FFHTTP_METH
	unsigned accept_gzip : 1 // Accept-Encoding: gzip
		, accept_chunked : 1 // the client understands Transfer-Encoding: chunked
		;
} ffhttp_request;

static FFINL void ffhttp_reqinit(ffhttp_request *r) {
	memset(r, 0, sizeof(ffhttp_request));
	ffhttp_init(&r->h);
	r->h.hdr.mask |= FF_BIT64(FFHTTP_HOST) | FF_BIT64(FFHTTP_TE) | FF_BIT64(FFHTTP_ACCEPT_ENCODING);
}

/** Deallocate memory associated with ffhttp_request. */
static FFINL void ffhttp_reqfree(ffhttp_request *r) {
	ffstr_free(&r->decoded_url);
}

/** Parse request line.
Return enum FFHTTP_E. */
FF_EXTN int ffhttp_reqparse(ffhttp_request *r, const char *data, size_t len);

/** Parse request header.
Return enum FFHTTP_E. */
FF_EXTN int ffhttp_reqparsehdr(ffhttp_request *r, const char *data, size_t len);

/** Parse all request headers. */
static FFINL int ffhttp_reqparsehdrs(ffhttp_request *r, const char *data, size_t len) {
	int e = FFHTTP_OK;
	while (e == FFHTTP_OK)
		e = ffhttp_reqparsehdr(r, data, len);
	return e;
}

/** Get method string. */
static FFINL ffstr ffhttp_reqmethod(const ffhttp_request *r) {
	ffstr s;
	ffstr_set(&s, r->h.base, r->methodlen);
	return s;
}

/** Get URI component.
Note: getting scheme is not supported. */
static FFINL ffstr ffhttp_requrl(const ffhttp_request *r, int component) {
	return ffurl_get(&r->url, r->h.base, component);
}

/** Get decoded path. */
static FFINL ffstr ffhttp_reqpath(const ffhttp_request *r) {
	if (!r->url.complex)
		return ffurl_get(&r->url, r->h.base, FFURL_PATH);
	return r->decoded_url;
}


/** Parsed response. */
typedef struct {
	ffhttp_headers h;

	ushort code;
	ffrange status;
} ffhttp_response;

static FFINL void ffhttp_respinit(ffhttp_response *r) {
	memset(r, 0, sizeof(ffhttp_response));
	ffhttp_init(&r->h);
}

enum FFHTTP_RESPF {
	FFHTTP_IGN_STATUS_PROTO = 1 //don't fail if the scheme is not "HTTP/"
};

/** Parse response.
'flags': enum FFHTTP_RESPF. */
FF_EXTN int ffhttp_respparse(ffhttp_response *r, const char *data, size_t len, int flags);

/** Parse response header. */
FF_EXTN int ffhttp_respparsehdr(ffhttp_response *r, const char *data, size_t len);

/** Parse all response headers. */
static FFINL int ffhttp_respparsehdrs(ffhttp_response *r, const char *data, size_t len) {
	int e = FFHTTP_OK;
	while (e == FFHTTP_OK)
		e = ffhttp_respparsehdr(r, data, len);
	return e;
}

#define ffhttp_respstatus(r)  ffrang_get(&(r)->status, (r)->h.base)
