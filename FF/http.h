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
	FFHTTP_200_OK
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
};

/** Status code numbers. */
FF_EXTN const ushort ffhttp_codes[];

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

/** Initialize request parser. */
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

/** Initialize response parser. */
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

/** Get response status code and message. */
#define ffhttp_respstatus(r)  ffrang_get(&(r)->status, (r)->h.base)

/** Return TRUE if response has no body. */
static FFINL ffbool ffhttp_respnobody(int code) {
	return (code == 204 || code == 304 || code < 200);
}


enum FFHTTP_CACHE {
	FFHTTP_CACH_NOCACHE = 1
	, FFHTTP_CACH_NOSTORE = 1 << 1
	, FFHTTP_CACH_PRIVATE = 1 << 2
	, FFHTTP_CACH_REVALIDATE = 1 << 3 //resp
	, FFHTTP_CACH_PUBLIC = 1 << 4 //resp
	, FFHTTP_CACH_MAXAGE = 1 << 5
	, FFHTTP_CACH_MAXSTALE = 1 << 6 //req
	, FFHTTP_CACH_MINFRESH = 1 << 7 //req
	, FFHTTP_CACH_SMAXAGE = 1 << 8 //resp
};

typedef struct {
	uint maxage
		, maxstale
		, minfresh
		, smaxage;
} ffhttp_cachectl;

/** Parse Cache-Control header value.
'cctl' is filled with numeric values.
Return mask of enum FFHTTP_CACHE. */
FF_EXTN int ffhttp_parsecachctl(ffhttp_cachectl *cctl, const char *val, size_t vallen);


typedef struct {
	ffstr3 buf;
	ushort code;
	unsigned conn_close : 1
		, http10_keepalive : 1;
	ffstr proto
		, status;

	int64 cont_len; ///< -1=unset
	ffstr cont_type
		, last_mod
		, location
		, cont_enc
		, trans_enc;
} ffhttp_cook;

/** Initialize ffhttp_cook object. */
static FFINL void ffhttp_cookinit(ffhttp_cook *c, char *buf, size_t cap) {
	ffmemzero(c, sizeof(ffhttp_cook));
	c->cont_len = -1;
	c->buf.ptr = buf;
	c->buf.cap = cap;
	ffstr_set(&c->proto, FFSTR("HTTP/1.1"));
}

static FFINL void ffhttp_cookreset(ffhttp_cook *c) {
	ffhttp_cookinit(c, c->buf.ptr, c->buf.cap);
}

/** Add request line. */
static FFINL void ffhttp_addrequest(ffhttp_cook *c, const char *method, size_t methlen, const char *uri, size_t urilen) {
	ffstr3 *s = &c->buf;
	s->len += ffs_fmt(ffarr_end(s), s->ptr + s->cap, "%*s %*s %S" FFCRLF
		, (size_t)methlen, method, (size_t)urilen, uri, &c->proto);
}

/** Set response code and default status message. */
static FFINL void ffhttp_setstatus(ffhttp_cook *c, enum FFHTTP_STATUS code) {
	FF_ASSERT(code < FFHTTP_SLAST);
	c->code = ffhttp_codes[code];
	c->status = ffhttp_sresp[code];
}

/** Example:
ffhttp_setstatus4(&htpck, 200, FFSTR("200 OK")); */
static FFINL void ffhttp_setstatus4(ffhttp_cook *c, uint code, const char *status, size_t statuslen) {
	c->code = code;
	ffstr_set(&c->status, status, statuslen);
}

/** Write status line. */
static FFINL void ffhttp_addstatus(ffhttp_cook *c) {
	ffstr3 *s = &c->buf;
	s->len += ffs_fmt(ffarr_end(s), s->ptr + s->cap, "%S %S" FFCRLF
		, &c->proto, &c->status);
}

/** Write header line. */
static FFINL void ffhttp_addhdr(ffhttp_cook *c, const char *name, size_t namelen, const char *val, size_t vallen) {
	ffstr3 *s = &c->buf;
	s->len += ffs_fmt(ffarr_end(s), s->ptr + s->cap, "%*s: %*s" FFCRLF
		, (size_t)namelen, name, (size_t)vallen, val);
}

/** Write header line.
'ihdr': enum FFHTTP_HDR. */
static FFINL void ffhttp_addihdr(ffhttp_cook *c, uint ihdr, const char *val, size_t vallen) {
	ffhttp_addhdr(c, FFSTR2(ffhttp_shdr[ihdr]), val, vallen);
}

/** Write special headers in ffhttp_cook.
Return 0 on success.
Return 1 if buffer is full. */
FF_EXTN int ffhttp_cookflush(ffhttp_cook *c);

/** Write the last CRLF.
Return 0 on success.
Return 1 if buffer is full. */
static FFINL int ffhttp_cookfin(ffhttp_cook *c) {
	ffstr3_cat(&c->buf, FFSTR(FFCRLF));
	return (c->buf.len == c->buf.cap);
}
