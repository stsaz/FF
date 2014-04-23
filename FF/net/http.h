/** HTTP.
Copyright (c) 2013 Simon Zolin
*/

#pragma once

#include <FF/array.h>
#include <FF/net/url.h>
#include <FF/crc.h>
#include <FF/hashtab.h>


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

/** Initialize static hash table of known headers.
Return 0 on succes. */
FF_EXTN int ffhttp_initheaders();

/** Destroy hash table of headers. */
FF_EXTN void ffhttp_freeheaders();

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
typedef struct ffhttp_hdr {
	ushort len;
	byte idx;
	byte ihdr; //enum FFHTTP_HDR
	uint crc;
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

typedef struct _ffhttp_headr _ffhttp_headr;

/** Parsed headers information. */
typedef struct ffhttp_headers {
	const char *base;
	ushort len;
	ushort ver; ///< HTTP version, e.g. 0x0100 = http/1.0
	ushort firstline_len;
	byte http11 : 1
		, firstcrlf : 2
		, conn_close : 1
		, has_body : 1
		, chunked : 1 ///< Transfer-Encoding: chunked
		, body_conn_close : 1 // for response
		, index_headers :1 //if set, collect headers in hidx and then build htheaders
		;
	byte ce_gzip : 1 ///< Content-Encoding: gzip
		, ce_identity : 1 ///< no Content-Encoding or Content-Encoding: identity
		;
	int64 cont_len; ///< Content-Length value or -1

	ffhstab htheaders;
	struct {
		uint len;
		_ffhttp_headr *ptr;
	} hidx;

	ffhttp_hdr hdr; ///< The header being parsed currently
} ffhttp_headers;

FF_EXTN void ffhttp_init(ffhttp_headers *h);

static FFINL void ffhttp_fin(ffhttp_headers *h) {
	ffhst_free(&h->htheaders);
	FF_SAFECLOSE(h->hidx.ptr, NULL, ffmem_free);
}

/** Parse and process the next header.
Return enum FFHTTP_E. */
FF_EXTN int ffhttp_parsehdr(ffhttp_headers *h, const char *data, size_t len);

/** Get header value.
Return 0 if header is not found. */
FF_EXTN int ffhttp_findhdr(const ffhttp_headers *h, const char *name, size_t namelen, ffstr *dst);

#define ffhttp_findihdr(h, ihdr, dst) \
	ffhttp_findhdr(h, ffhttp_shdr[ihdr].ptr, ffhttp_shdr[ihdr].len, dst)

/** Get header by index.
Return enum FFHTTP_HDR.
Return FFHTTP_DONE if the header with the specified index does not exist. */
FF_EXTN int ffhttp_gethdr(const ffhttp_headers *h, uint idx, ffstr *key, ffstr *val);

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
typedef struct ffhttp_request {
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
}

/** Deallocate memory associated with ffhttp_request. */
static FFINL void ffhttp_reqfree(ffhttp_request *r) {
	ffhttp_fin(&r->h);
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

/** Get hostname from request. */
static FFINL ffstr ffhttp_reqhost(const ffhttp_request *r) {
	return ffurl_get(&r->url, r->h.base, FFURL_HOST);
}

/** Get decoded path. */
static FFINL ffstr ffhttp_reqpath(const ffhttp_request *r) {
	if (!r->url.complex)
		return ffurl_get(&r->url, r->h.base, FFURL_PATH);
	return r->decoded_url;
}


/** Parsed response. */
typedef struct ffhttp_response {
	ffhttp_headers h;

	ushort code;
	ffrange status;
} ffhttp_response;

/** Initialize response parser. */
static FFINL void ffhttp_respinit(ffhttp_response *r) {
	memset(r, 0, sizeof(ffhttp_response));
	ffhttp_init(&r->h);
}

static FFINL void ffhttp_respfree(ffhttp_response *r) {
	ffhttp_fin(&r->h);
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

typedef struct ffhttp_cachectl {
	uint maxage
		, maxstale
		, minfresh
		, smaxage;
} ffhttp_cachectl;

/** Parse Cache-Control header value.
'cctl' is filled with numeric values.
Return mask of enum FFHTTP_CACHE. */
FF_EXTN int ffhttp_parsecachctl(ffhttp_cachectl *cctl, const char *val, size_t vallen);


/** Check ETag value against If-None-Match.
Note: ',' within the value is not supported. */
FF_EXTN ffbool ffhttp_ifnonematch(const char *etag, size_t len, const ffstr *ifNonMatch);

/** Parse 1 element of Range header value.  Perform range validation checks.
'size': the maximum size of the content.
Return offset.  'size' is set to the size of content requested.
Return -1 on error. */
FF_EXTN int64 ffhttp_range(const char *d, size_t len, uint64 *size);


typedef struct ffhttp_cook {
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
		, trans_enc
		, date;
} ffhttp_cook;

/** Initialize ffhttp_cook object. */
static FFINL void ffhttp_cookinit(ffhttp_cook *c, char *buf, size_t cap) {
	ffmem_tzero(c);
	c->cont_len = -1;
	c->buf.ptr = buf;
	c->buf.cap = cap;
	ffstr_setcz(&c->proto, "HTTP/1.1");
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


typedef struct ffhttp_chunked {
	uint64 cursiz;
	uint state;
	unsigned last :1;
} ffhttp_chunked;

static FFINL void ffhttp_chunkinit(ffhttp_chunked *c) {
	c->state = 0;
}

/** Parse chunked-encoded data and get content.
Return enum FFHTTP_E.
If there is data, return FFHTTP_OK and set 'dst'.  'len' is set to the number of processed bytes. */
FF_EXTN int ffhttp_chunkparse(ffhttp_chunked *c, const char *body, size_t *len, ffstr *dst);

/** Begin chunk. */
static FFINL int ffhttp_chunkbegin(char *buf, size_t cap, uint64 chunk_len) {
	uint r = ffs_fromint(chunk_len, buf, cap, FFINT_HEXLOW);
	if (cap - r < 2)
		return 0;
	buf[r++] = '\r';
	buf[r++] = '\n';
	return r;
}

enum FFHTTP_CHUNKED {
	FFHTTP_CHUNKFIN
	, FFHTTP_CHUNKZERO
	, FFHTTP_CHUNKLAST
};

/** Finish chunk.  Add zero-length chunk if needed.
'flags': enum FFHTTP_CHUNKED.
'pbuf' is set to point to the static string, and the number of valid bytes is returned. */
FF_EXTN int ffhttp_chunkfin(const char **pbuf, int flags);
