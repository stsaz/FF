/**
Copyright (c) 2013 Simon Zolin
*/

#include <FF/net/http.h>


struct _ffhttp_headr {
	uint hash;
	ffrange key;
	ffrange val;
};

static int ht_headr_fill(ffhttp_headers *h);
static int ht_headr_cmpkey(void *val, const char *key, size_t keylen, void *param);

static int ht_knhdr_cmpkey(void *val, const char *key, size_t keylen, void *param);


#pragma pack(push, 8)
const char ffhttp_smeth[7][8] = {
	"GET"
	, "PUT"
	, "POST"
	, "HEAD"
	, "DELETE"
	, "CONNECT"
	, "OPTIONS"
};
#pragma pack(pop)

#define add(hdr) FFSTR_INIT(hdr)
const ffstr ffhttp_shdr[] = {
	{ 0, NULL }

	// General
	, add("Connection")
	, add("Keep-Alive")
	, add("Date")
	, add("Transfer-Encoding")
	, add("Via")
	, add("Cache-Control")

	// Content
	, add("Content-Encoding")
	, add("Content-Length")
	, add("Content-Range")
	, add("Content-Type")

	// Client
	, add("Host")
	, add("Referer")
	, add("User-Agent")

	// Accept
	, add("Accept")
	, add("Accept-Encoding")
	, add("TE")

	// Conditional request
	, add("If-Match")
	, add("If-Modified-Since")
	, add("If-None-Match")
	, add("If-Range")
	, add("If-Unmodified-Since")
	, add("Range")

	// Request security
	, add("Authorization")
	, add("Cookie")
	, add("Cookie2")
	, add("Proxy-Authorization")
	, add("Proxy-Connection")

	// Response
	, add("Age")
	, add("Server")
	, add("Location")
	, add("ETag")
	, add("Expires")
	, add("Last-Modified")

	// Negotiation
	, add("Accept-Ranges")
	, add("Vary")

	// Response security
	, add("Proxy-Authenticate")
	, add("Set-Cookie")
	, add("Set-Cookie2")
	, add("WWW-Authenticate")

	, add("Upgrade")
	, add("X-Forwarded-For")

	// FastCGI
	, add("Status")
};
#undef add

static const uint hdrHashes[] = {
	           0U /**/
	,  704082790U /*Connection*/
	, 3308361288U /*Keep-Alive*/
	, 2862495610U /*Date*/
	, 1470906230U /*Transfer-Encoding*/
	,  565660880U /*Via*/
	,    9184401U /*Cache-Control*/
	, 3836410099U /*Content-Encoding*/
	,  314322716U /*Content-Length*/
	, 1051632209U /*Content-Range*/
	, 3266185539U /*Content-Type*/
	, 3475444733U /*Host*/
	, 1440495237U /*Referer*/
	, 2191772431U /*User-Agent*/
	, 3005279540U /*Accept*/
	, 2687938133U /*Accept-Encoding*/
	,  928136154U /*TE*/
	, 1168849366U /*If-Match*/
	, 1848278858U /*If-Modified-Since*/
	, 1529156225U /*If-None-Match*/
	, 2893522586U /*If-Range*/
	,  462614015U /*If-Unmodified-Since*/
	, 2475121225U /*Range*/
	, 2053999599U /*Authorization*/
	, 2329983590U /*Cookie*/
	, 3197016794U /*Cookie2*/
	,  146921266U /*Proxy-Authorization*/
	, 2570020322U /*Proxy-Connection*/
	, 2704281778U /*Age*/
	, 1517147638U /*Server*/
	, 1587448267U /*Location*/
	, 3514087100U /*ETag*/
	, 2593941644U /*Expires*/
	, 4183802465U /*Last-Modified*/
	, 2930364553U /*Accept-Ranges*/
	,  315970471U /*Vary*/
	, 2775274866U /*Proxy-Authenticate*/
	, 1431457525U /*Set-Cookie*/
	, 3612863184U /*Set-Cookie2*/
	, 1561078874U /*WWW-Authenticate*/
	, 3076944922U /*Upgrade*/
	, 2397052407U /*X-Forwarded-For*/
	, 2063623452U /*Status*/
};

static ffhstab ht_known_hdrs;

static int ht_knhdr_cmpkey(void *val, const char *key, size_t keylen, void *param)
{
	size_t i = (size_t)val;
	return ffstr_ieq(&ffhttp_shdr[i], key, keylen) ? 0 : -1;
}

int ffhttp_initheaders()
{
	size_t i;

	if (ht_known_hdrs.nslots != 0)
		return 0;

	if (0 != ffhst_init(&ht_known_hdrs, FFHTTP_HLAST))
		return -1;

	for (i = 1;  i < FFHTTP_HLAST;  i++) {
		if (ffhst_ins(&ht_known_hdrs, hdrHashes[i], (void*)i) < 0) {
			ffhst_free(&ht_known_hdrs);
			return -1;
		}
	}

	ht_known_hdrs.cmpkey = &ht_knhdr_cmpkey;
	return 0;
}

void ffhttp_freeheaders() {
	ffhst_free(&ht_known_hdrs);
}

const ushort ffhttp_codes[] = {
	200, 206
	, 301, 302, 304
	, 400
	, 403, 404, 405
	, 413, 415, 416
	, 500, 501, 502
	, 504
};

#define add(st) FFSTR_INIT(st)
const ffstr ffhttp_sresp[] = {
	add("200 OK")
	, add("206 Partial Content")

	, add("301 Moved Permanently")
	, add("302 Found")
	, add("304 Not Modified")

	, add("400 Bad Request")
	, add("403 Forbidden")
	, add("404 Not Found")
	, add("405 Method Not Allowed")
	, add("413 Request Entity Too Large")
	, add("415 Unsupported Media Type")
	, add("416 Requested Range Not Satisfiable")

	, add("500 Internal Server Error")
	, add("501 Not Implemented")
	, add("502 Bad Gateway")
	, add("504 Gateway Time-out")
};
#undef add

static const char *const serr[] = {
	"ok"
	, "system"
	, "bad method"
	, "bad URI"
	, "bad Host"
	, "bad HTTP version"
	, "bad HTTP status"
	, "Host expected"
	, "eol expected"
	, "no header value"
	, "bad header name"
	, "bad header value"
	, "duplicate header"
	, "data is too large"
};

const char *ffhttp_errstr(int code)
{
	if (code & FFHTTP_EURLPARSE)
		return ffurl_errstr(code & ~FFHTTP_EURLPARSE);
	return serr[code];
}

enum { CR = '\r', LF = '\n' };

int ffhttp_nexthdr(ffhttp_hdr *h, const char *d, size_t len)
{
	enum { iKey = 0, iKeyData, iSpaceBeforeVal, iVal, iSpaceAfterVal, iLastLf };
	uint i = h->len;
	uint idx = h->idx;
	ffrange *name = &h->key;
	ffrange *val = &h->val;
	uint er = FFHTTP_OK;

	len = ffmin(len, (size_t)0xffff);
	for (; i != len; ++i) {
		int ch = d[i];

		switch (idx) {
		case iKey:
			h->crc = ffcrc32_start();
			name->off = (ushort)i; //save hdr start pos

			switch (ch) {
			case CR:
				name->len = 0;
				idx = iLastLf;
				break;

			case LF:
				name->len = 0;
				goto done;
				//break;

			default:
				if (!ffchar_isname(ch)) {
					// headers starting with '-' are not allowed
					er = FFHTTP_EHDRKEY;
					goto fail;
				}
				i--; //again
				idx = iKeyData;
			}
			break;

		case iKeyData:
			switch (ch) {
			case ':':
				name->len = i - name->off;
				idx = iSpaceBeforeVal;
				break;

			case CR:
			case LF:
				er = FFHTTP_ENOVAL;
				goto fail;
				//break;

			default:
				if (!(ffchar_isname(ch) || ch == '-')) {
					er = FFHTTP_EHDRKEY;
					goto fail;
				}
				ffcrc32_update(&h->crc, ch, 1);
			}
			break;

		case iSpaceBeforeVal:
			if (ch == ' ')
				break;
			val->off = (ushort)i; //save val start pos
			idx = iVal;
			//break;

		case iVal:
			switch (ch) {
			case ' ':
				val->len = i - val->off;
				idx = iSpaceAfterVal;
				break;

			case CR:
				val->len = i - val->off;
				idx = iLastLf;
				break;

			case LF:
				val->len = i - val->off;
				goto done;
				//break;
			}
			break;

		case iSpaceAfterVal:
			switch (ch) {
			case ' ':
				break;

			case CR:
				idx = iLastLf;
				break;

			case LF:
				goto done;
				//break;

			default:
				val->len = i;
				idx = iVal;
			}
			break;

		case iLastLf:
			if (ch != LF) {
				er = FFHTTP_EEOL;
				goto fail;
			}
			goto done;
			//break;
		}
	}

	if (i == 0xffff) {
		er = FFHTTP_ETOOLARGE;
		goto fail;
	}

	h->idx = idx;
	h->len = (ushort)i;
	return FFHTTP_MORE;

fail:
	h->idx = idx;
	h->len = (ushort)i;
	return er;

done:
	ffcrc32_finish(&h->crc);
	idx = iKey;
	h->idx = idx;
	h->len = (ushort)i + 1;
	if (name->len == 0)
		return FFHTTP_DONE;

	{
		ffstr sname = ffrang_get(name, d);
		h->ihdr = (int)(size_t)ffhst_find(&ht_known_hdrs, h->crc, sname.ptr, sname.len, NULL);
	}

	return FFHTTP_OK;
}

void ffhttp_init(ffhttp_headers *h)
{
	memset(h, 0, sizeof(ffhttp_headers));
	ffhttp_inithdr(&h->hdr);
	h->cont_len = -1;
	h->ce_identity = 1;
	h->index_headers = 1;
	h->htheaders.cmpkey = &ht_headr_cmpkey;
}

int ffhttp_parsehdr(ffhttp_headers *h, const char *data, size_t len)
{
	ffstr val;
	int e;

	if (h->base != data)
		h->base = data;
	e = ffhttp_nexthdr(&h->hdr, data, len);
	h->len = h->hdr.len;

	if (e == FFHTTP_DONE) {

		if (h->hidx.len != 0 && 0 != ht_headr_fill(h))
			return FFHTTP_ESYS;

		return FFHTTP_DONE;
	}

	if (e != FFHTTP_OK)
		return e;

	if (h->index_headers) {
		// add this header to index
		_ffhttp_headr *ar;
		_ffhttp_headr *hh;
		ar = ffmem_realloc(h->hidx.ptr, (h->hidx.len + 1) * sizeof(_ffhttp_headr));
		if (ar == NULL)
			return FFHTTP_ESYS;

		hh = &ar[h->hidx.len];
		hh->hash = h->hdr.crc;
		hh->key = h->hdr.key;
		hh->val = h->hdr.val;

		h->hidx.ptr = ar;
		h->hidx.len++;
	}

	val = ffrang_get(&h->hdr.val, data);

	switch (h->hdr.ihdr) {
	case FFHTTP_CONNECTION:
		// handle "Connection: Keep-Alive, TE"
		while (val.len != 0) {
			ffstr v;
			size_t by = ffstr_nextval(val.ptr, val.len, &v, ',');
			ffstr_shift(&val, by);
			if (h->http11) {
				if (ffstr_ieqcz(&v, "close")) {
					h->conn_close = 1;
					break;
				}
			}
			else if (ffstr_ieqcz(&v, "keep-alive")) {
				h->conn_close = 0;
				break;
			}
		}
		break;

	case FFHTTP_CONTENT_LENGTH:
		if (h->cont_len != -1)
			return FFHTTP_EDUPHDR;
		if (!h->has_body) {
			if (val.len != ffs_toint(val.ptr, val.len, &h->cont_len, FFS_INT64))
				return FFHTTP_EHDRVAL;
			if (h->cont_len != 0)
				h->has_body = 1;
		}
		break;

	case FFHTTP_CONTENT_ENCODING:
		h->ce_gzip = h->ce_identity = 0;
		if (ffstr_ieqcz(&val, "gzip"))
			h->ce_gzip = 1;
		else if (ffstr_ieqcz(&val, "identity"))
			h->ce_identity = 1;
		break;

	case FFHTTP_TRANSFER_ENCODING:
		if (ffstr_imatch(&val, FFSTR("chunked"))) // "chunked [; transfer-extension]"
			h->chunked = 1;
		h->has_body = 1;
		if (h->cont_len != -1)
			h->cont_len = -1;
		break;
	}

	return FFHTTP_OK;
}

// build hash table for headers
static int ht_headr_fill(ffhttp_headers *h)
{
	_ffhttp_headr *hh
		, *end = h->hidx.ptr + h->hidx.len;

	if (0 != ffhst_init(&h->htheaders, h->hidx.len))
		return -1;

	for (hh = h->hidx.ptr;  hh != end;  hh++) {
		if (ffhst_ins(&h->htheaders, hh->hash, hh) < 0)
			return -1;
	}

	return 0;
}

static int ht_headr_cmpkey(void *val, const char *key, size_t keylen, void *param)
{
	const ffhttp_headers *h = param;
	const _ffhttp_headr *hh = val;
	ffstr name = ffrang_get(&hh->key, h->base);
	return ffstr_ieq(&name, key, keylen) ? 0 : -1;
}

int ffhttp_findhdr(const ffhttp_headers *h, const char *name, size_t namelen, ffstr *dst)
{
	uint hash = ffcrc32_get(name, namelen, 1);
	const _ffhttp_headr *hh = ffhst_find(&h->htheaders, hash, name, namelen, (void*)h);
	if (hh == NULL)
		return 0;
	if (dst != NULL)
		*dst = ffrang_get(&hh->val, h->base);
	return 1;
}

int ffhttp_gethdr(const ffhttp_headers *h, uint idx, ffstr *key, ffstr *val)
{
	_ffhttp_headr *hh;
	int i;

	if (idx >= h->hidx.len)
		return FFHTTP_DONE;

	hh = &h->hidx.ptr[idx];
	if (key != NULL)
		*key = ffrang_get(&hh->key, h->base);
	if (val != NULL)
		*val = ffrang_get(&hh->val, h->base);

	i = (int)(size_t)ffhst_find(&ht_known_hdrs, hh->hash, key->ptr, key->len, NULL);
	return i;
}


enum VER_E {
	iH = 32, iHT, iHTT, iHTTP, iHttpMajVer, iHttpMinVerFirst, iHttpMinVer
	, iAnyProto, iSpaceAfterHttpVer
};

/** Return FFHTTP_OK if version has been parsed.  The caller must process this char again. */
static int parseVer(int ch, uint *_Idx, ushort *ver)
{
	uint idx = *_Idx;
	switch (idx) {
	case iH:
		if (ch != 'T')
			goto fail;
		idx = iHT;
		break;

	case iHT:
		if (ch != 'T')
			goto fail;
		idx = iHTT;
		break;

	case iHTT:
		if (ch != 'P')
			goto fail;
		idx = iHTTP;
		break;

	case iHTTP:
		if (ch != '/')
			goto fail;
		idx = iHttpMajVer;
		break;

	case iHttpMajVer:
		if (!ffchar_isdigit(ch)) {
			if (ch == '.') {
				idx = iHttpMinVerFirst;
				break;
			}
			goto fail;
		}
		{
			byte majVer = *ver >> 8;
			const uint mv = (uint)majVer * 10 + ch - '0';
			if (mv & ~0xff)
				goto fail;
			*ver = (*ver & 0x00ff) | (mv << 8);
		}
		break;

	case iHttpMinVerFirst:
	case iHttpMinVer:
		if (!ffchar_isdigit(ch)) {
			if (idx == iHttpMinVerFirst)
				goto fail; // must be at least 1 digit long
			return FFHTTP_OK;
		}
		if (idx == iHttpMinVerFirst)
			idx = iHttpMinVer;
		{
			byte minVer = (byte)*ver;
			uint mv = (uint)minVer * 10 + ch - '0';
			if (mv & ~0xff)
				goto fail;
			*ver = (*ver & 0xff00) | mv;
		}
		break;

	default:
		return FFHTTP_EVERSION;
	}

	*_Idx = idx;
	return FFHTTP_MORE;

fail:
	return FFHTTP_EVERSION;
}

int ffhttp_reqparse(ffhttp_request *r, const char *d, size_t len)
{
	enum {
		iMethod, iBeforeUri, iURL
		, iSharpSkip, iNonStdUri, iAfterUri
		, iLastLf
	};

	int er = FFHTTP_OK;
	uint i = r->h.hdr.len;
	uint idx = r->h.hdr.idx;
	ffbool again = 0;
	len = ffmin(len, (size_t)0xffff);

	if (r->h.base != d)
		r->h.base = d;

	for (; i != len; ++i) {
		int ch = d[i];

		switch (idx) {
		case iMethod:
			if (ch == ' ') {
				r->methodlen = (byte)i;
				r->method = ffhttp_findmethod(d, i);
				idx = iBeforeUri;
			}
			else if (!ffchar_isname(ch) && ch != '_')
				er = FFHTTP_EMETHOD; //valid method: a-zA-Z0-9_
			else if (i > 0xff)
				er = FFHTTP_EMETHOD; //max method length is 255
			break;

		case iBeforeUri:
			if (ch == ' ')
			{}
			else if (r->method > _FFHTTP_MLASTURI) {
				r->url.offpath = (ushort)i;
				idx = iNonStdUri;
			}
			else {
				r->offurl = (ushort)i;
				idx = iURL;
				again = 1;
			}
			break;

		case iURL:
			er = ffurl_parse(&r->url, d + r->offurl, len - r->offurl);
			if (er == FFURL_EOK || er == FFURL_EMORE) {
				i = (ushort)len - 1;
				goto more; //url is not parsed completely yet
			}

			ffurl_rebase(&r->url, d + r->offurl, d);

			if (er != FFURL_ESTOP) {
				er |= FFHTTP_EURLPARSE;
				break;
			}

			// "GET http://host/path HTT"
			// if host exists, scheme must exist too
			if (r->url.hostlen != 0 && r->url.offhost == r->offurl) {
				er = FFHTTP_EURLPARSE | FFURL_ESCHEME;
				break;
			}

			er = 0;
			i = r->url.len;

			switch (d[i]) {

			case CR: // GET / CRLF
			case LF: // GET / LF
				er = FFHTTP_EVERSION;
				break;

			case '#': // GET /url#sharp HTT
			case ' ': // GET / HTT
				idx = (d[i] == ' ' ? iAfterUri : iSharpSkip);

				if (r->url.complex) {
					ffstr fn;
					if (NULL == ffstr_alloc(&r->decoded_url, r->url.decoded_pathlen)) {
						er = FFHTTP_ESYS;
						break;
					}
					fn = ffhttp_requrl(r, FFURL_PATH);
					r->decoded_url.len = ffuri_decode(r->decoded_url.ptr, r->url.decoded_pathlen, fn.ptr, fn.len);
					if (r->decoded_url.len == 0) {
						er = FFHTTP_EURI;
						break;
					}
				}
				break;

			default:
				er = FFHTTP_EURI;
			}
			break;

		case iSharpSkip:
			if (ch == ' ')
				idx = iAfterUri;
			break;

		case iNonStdUri:
			if (ch == ' ') {
				r->url.pathlen = (ushort)(i - r->url.offpath);
				idx = iAfterUri;
			}
			break;

		case iAfterUri:
			if (ch == ' ')
			{}
			else if (ch != 'H')
				er = FFHTTP_EVERSION;
			else
				idx = iH;
			break;

		case iSpaceAfterHttpVer:
			switch (ch) {
			case ' ':
				break;
			case CR:
				idx = iLastLf;
				break;
			case LF:
				r->h.firstcrlf = 1;
				goto done;
				//break;
			default:
				er = FFHTTP_EVERSION;
			}
			break;

		case iLastLf:
			if (ch != LF) {
				er = FFHTTP_EEOL;
				break;
			}
			r->h.firstcrlf = 2;
			goto done;
			//break;

		default: {
			er = parseVer(ch, &idx, &r->h.ver);
			if (er == FFHTTP_OK) {
				if (r->h.ver >= 0x0101) {
					r->h.http11 = 1;
					r->accept_chunked = 1;
				}
				else
					r->h.conn_close = 1;
				idx = iSpaceAfterHttpVer;
				again = 1;
			}
			else if (er != FFHTTP_MORE)
			{}
			else
				er = 0;
			}
			//break;
		}

		if (er != FFHTTP_OK)
			goto fail;

		if (again) {
			again = 0;
			i--;
			continue;
		}
	}

	if (i == 0xffff) {
		er = FFHTTP_ETOOLARGE;
		goto fail;
	}

more:
	r->h.hdr.idx = idx;
	r->h.len = r->h.hdr.len = (ushort)i;
	return FFHTTP_MORE;

done:
	++i;
	r->h.len = r->h.hdr.len = (ushort)i;
	r->h.firstline_len = (ushort)i;
	r->h.base = d;
	r->h.hdr.idx = 0; //iKey
	return FFHTTP_OK;

fail:
	r->h.len = r->h.hdr.len = (ushort)i;
	r->h.firstline_len = (ushort)(ffs_findof(d + i, len - i, FFSTR("\r\n")) - d);
	return er;
}

static const char uniq_hdrs[] = {
	FFHTTP_IFNONE_MATCH, FFHTTP_IFMATCH
	, FFHTTP_IFMODIFIED_SINCE, FFHTTP_IFUNMODIFIED_SINCE
	, FFHTTP_IFRANGE, FFHTTP_AUTHORIZATION
};

int ffhttp_reqparsehdr(ffhttp_request *r, const char *data, size_t len)
{
	ffstr val;
	ffstr v;
	int ihdr;
	int e = ffhttp_parsehdr(&r->h, data, len);
	if (e != FFHTTP_OK) {
		if (e == FFHTTP_DONE && r->h.http11 && r->url.hostlen == 0)
			return FFHTTP_EHOST11;
		return e;
	}

	ihdr = r->h.hdr.ihdr;
	val = ffrang_get(&r->h.hdr.val, data);

	switch (ihdr) {
	case FFHTTP_HOST: // the header is lower priority than URI
		if (r->url.hostlen == 0) {
			ffurl u;
			ffurl_init(&u);
			if (FFURL_EOK != ffurl_parse(&u, val.ptr, val.len)
				|| u.offhost != 0 || u.pathlen != 0) //scheme, path are not allowed
				return FFHTTP_EHOST;
			ffurl_rebase(&u, val.ptr, data);

			r->url.offhost = u.offhost;
			r->url.hostlen = u.hostlen;
			r->url.port = u.port;
			r->url.portlen = u.portlen;
			r->url.ipv6 = u.ipv6;
		}
		break;

	case FFHTTP_TE: //TE: deflate, chunked
		while (val.len != 0) {
			size_t by = ffstr_nextval(val.ptr, val.len, &v, ',');
			ffstr_shift(&val, by);
			if (ffstr_ieqcz(&v, "chunked")) {
				r->accept_chunked = 1;
				break;
			}
		}
		break;

	case FFHTTP_ACCEPT_ENCODING: // Accept-Encoding: compress;q=0.5, gzip;q=1
		while (val.len != 0) {
			ffstr encoding;
			size_t by = ffstr_nextval(val.ptr, val.len, &v, ',');
			ffstr_shift(&val, by);
			ffstr_nextval(v.ptr, v.len, &encoding, ';');
			if (ffstr_eqcz(&encoding, "gzip")) {
				r->accept_gzip = 1;
				break;
			}
		}
		break;

	case FFHTTP_IFNONE_MATCH:
	case FFHTTP_IFMATCH:
	case FFHTTP_IFMODIFIED_SINCE:
	case FFHTTP_IFUNMODIFIED_SINCE:
	case FFHTTP_IFRANGE:
	case FFHTTP_AUTHORIZATION:
		{
		const char *f = ffs_findc(uniq_hdrs, FFCNT(uniq_hdrs), ihdr);
		int idx = (int)(size_t)(f - uniq_hdrs);
		uint u = r->uniq_hdrs_mask;

		if (ffbit_test32(u, idx))
			return FFHTTP_EDUPHDR;

		ffbit_set32(&u, idx);
		r->uniq_hdrs_mask = (byte)u;
		}
		break;
	}

	return FFHTTP_OK;
}

int ffhttp_respparse(ffhttp_response *r, const char *d, size_t len, int flags)
{
	enum { iRespStart, iCode, iStatusStr, iLastLf };

	int er = 0;
	uint i = r->h.hdr.len;
	uint idx = r->h.hdr.idx;

	len = ffmin(len, (size_t)0xffff);
	if (r->h.base != d)
		r->h.base = d;

	for (; i != len; ++i) {
		int ch = d[i];

		switch (idx) {
		case iRespStart:
			if (ch != 'H') {
				if (flags & FFHTTP_IGN_STATUS_PROTO) {
					r->h.conn_close = 1;
					idx = iAnyProto;
					break;
				}

				er = FFHTTP_EVERSION;
				break;
			}
			idx = iH;
			break;

		case iAnyProto:
			if (ch != ' ')
				break;
			idx = iSpaceAfterHttpVer;
			//break;

		case iSpaceAfterHttpVer:
			if (ch == ' ')
				break;
			idx = iCode;
			r->status.off = (ushort)i;
			//break;

		case iCode:
			if (ffchar_isdigit(ch)) {
				uint t = (uint)r->code * 10 + ch - '0';
				if (t > 999) {
					er = FFHTTP_ESTATUS;
					break;
				}
				r->code = (ushort)t;
				break;
			}
			switch (ch) {
			case ' ':
				if (r->code < 100) {
					er = FFHTTP_ESTATUS;
					break;
				}
				idx = iStatusStr;
				break;
			default:
				er = FFHTTP_ESTATUS;
				goto fail;
			}
			break;

		case iStatusStr:
			if (ch == CR) {
				r->status.len = (ushort)(i - r->status.off);
				r->h.firstcrlf = 2;
				idx = iLastLf;
			}
			else if (ch == LF) {
				r->status.len = (ushort)(i - r->status.off);
				r->h.firstcrlf = 1;
				goto done;
			}
			break;

		case iLastLf:
			if (ch != LF) {
				er = FFHTTP_EEOL;
				break;
			}
			goto done;
			//break;

		default: {
			int rc = parseVer(ch, &idx, &r->h.ver);
			if (rc == FFHTTP_OK) {
				if (r->h.ver >= 0x0101)
					r->h.http11 = 1;
				else
					r->h.conn_close = 1;
				idx = iSpaceAfterHttpVer;
				--i;
			}
			else if (rc != FFHTTP_MORE) {
				if (flags & FFHTTP_IGN_STATUS_PROTO) {
					r->h.conn_close = 1;
					idx = iAnyProto;
					--i;
					break;
				}

				er = rc;
				break;
			}
			}
			//break;
		}

		if (er != 0)
			goto fail;

		if (ch == LF) {
			er = FFHTTP_ESTATUS;
			goto fail;
		}
	}

	if (i == 0xffff) {
		er = FFHTTP_ETOOLARGE;
		goto fail;
	}

	r->h.hdr.idx = idx;
	r->h.len = r->h.hdr.len = (ushort)i;
	return FFHTTP_MORE;

done:
	++i;
	r->h.len = r->h.hdr.len = (ushort)i;
	r->h.firstline_len = (ushort)i;
	r->h.hdr.idx = 0; //iKey
	r->h.base = d;
	return FFHTTP_OK;

fail:
	r->h.len = r->h.hdr.len = (ushort)i;
	r->h.firstline_len = (ushort)(ffs_findof(d + i, len - i, FFSTR("\r\n")) - d);
	return er;
}

int ffhttp_respparsehdr(ffhttp_response *r, const char *data, size_t len)
{
	int rc = ffhttp_parsehdr(&r->h, data, len);
	if (rc == FFHTTP_DONE) {
		if (ffhttp_respnobody(r->code))
			r->h.has_body = 0;
		else if (!r->h.has_body && r->h.cont_len == -1) {
			//no chunked, no content length.  So read body until connection is closed
			r->h.has_body = 1;
			r->h.body_conn_close = 1;
		}
	}
	return rc;
}


static const ffstr scachctl[] = {
	FFSTR_INIT("no-cache")
	, FFSTR_INIT("no-store")
	, FFSTR_INIT("private")
	, FFSTR_INIT("must-revalidate")
	, FFSTR_INIT("public")
	, FFSTR_INIT("max-age")
	, FFSTR_INIT("max-stale")
	, FFSTR_INIT("min-fresh")
	, FFSTR_INIT("s-max-age")
};

enum { _MAXAGE_IDX = 5 };

int ffhttp_parsecachctl(ffhttp_cachectl *cctl, const char *val, size_t vallen)
{
	ffstr hdr;
	ffstr vv;
	int rc = 0;
	size_t i;

	ffstr_set(&hdr, val, vallen);

	while (hdr.len != 0) {
		size_t by = ffstr_nextval(hdr.ptr, hdr.len, &vv, ',');
		ffstr_shift(&hdr, by);

		for (i = 0;  i < _MAXAGE_IDX;  ++i) {
			if (ffstr_ieq(&vv, FFSTR2(scachctl[i]))) {
				rc |= (1 << i);
				break;
			}
		}

		if (i < _MAXAGE_IDX)
			continue;

		for (i = _MAXAGE_IDX;  i < FFCNT(scachctl);  ++i) {
			const ffstr *cc = &scachctl[i];

			if (vv.len > cc->len + FFSLEN("=") && vv.ptr[cc->len] == '='
				&& 0 == ffs_icmp(vv.ptr, cc->ptr, cc->len))
			{
				uint n;
				ffstr_shift(&vv, cc->len + FFSLEN("="));
				if (vv.len == ffs_toint(vv.ptr, vv.len, &n, FFS_INT32)) {
					uint *dst = &cctl->maxage + (i - _MAXAGE_IDX);
					*dst = n;
					rc |= (1 << i);
				}
				break;
			}
		}
	}

	return rc;
}


ffbool ffhttp_ifnonematch(const char *etag, size_t len, const ffstr *ifNonMatch)
{
	ffstr nmatch = *ifNonMatch;
	while (nmatch.len != 0) {
		ffstr v;
		ffstr_shift(&nmatch, ffstr_nextval(nmatch.ptr, nmatch.len, &v, ','));
		if (ffstr_eq(&v, etag, len))
			return 0;
	}
	return 1;
}

/* Parse:
. "begin-"
. "begin-end"
. "-size" */
int64 ffhttp_range(const char *d, size_t len, uint64 *size)
{
	size_t sz;
	uint64 off = 0;
	const char *dash;

	if (len == 1)
		return -1; //too small input

	dash = ffs_find(d, len, '-');
	if (dash == d + len)
		return -1; //no "-"

	if (dash != d) { //"begin-..."
		sz = dash - d;
		if (sz != ffs_toint(d, sz, &off, FFS_INT64))
			return -1; //invalid offset
		if (off >= *size)
			return -1; //offset too large
	}

	if (dash == d + len - 1)
		*size -= off; //"begin-"
	else {
		uint64 n;
		sz = d + len - (dash + 1);
		if (sz != ffs_toint(dash + 1, sz, &n, FFS_INT64))
			return -1; //invalid end/size

		if (dash == d) { //"-size"
			if (n == 0)
				return -1; //"-0"

			if (n > *size)
				n = *size;
			off = *size - n;
			*size = n;

		} else { //"begin-end"
			if (n + off >= *size)
				*size -= off;
			else
				*size = n - off + 1;
		}
	}

	return off;
}


static const byte idxs[] = {
	FFHTTP_LOCATION, FFHTTP_LAST_MODIFIED, FFHTTP_CONTENT_TYPE
	, FFHTTP_CONTENT_ENCODING, FFHTTP_TRANSFER_ENCODING, FFHTTP_DATE
};

#define _OFF(member) FFOFF(ffhttp_cook, member)
static const byte offs[] = {
	_OFF(location), _OFF(last_mod), _OFF(cont_type)
	, _OFF(cont_enc), _OFF(trans_enc), _OFF(date)
};
#undef _OFF

int ffhttp_cookflush(ffhttp_cook *c)
{
	uint i;
	for (i = 0;  i < FFCNT(offs);  i++) {
		const ffstr *s = (ffstr*)((char*)c + offs[i]);
		if (s->len != 0)
			ffhttp_addihdr(c, idxs[i], FFSTR2(*s));
	}

	if (c->cont_len != -1) {
		char s[FFINT_MAXCHARS];
		uint r = ffs_fromint(c->cont_len, s, FFCNT(s), 0);
		ffhttp_addihdr(c, FFHTTP_CONTENT_LENGTH, s, r);
	}

	if (c->conn_close)
		ffhttp_addihdr(c, FFHTTP_CONNECTION, FFSTR("close"));
	else if (c->http10_keepalive)
		ffhttp_addihdr(c, FFHTTP_CONNECTION, FFSTR("keep-alive"));

	return (c->buf.len == c->buf.cap);
}


static FFINL int chnk_size(ffhttp_chunked *c, int ch)
{
	int bb = ffchar_tohex(ch);
	if (bb != -1) {
		if (c->cursiz & FF_BIT64(63))
			return FFHTTP_ETOOLARGE; // the chunk size is too long

		c->cursiz = c->cursiz * 16 + bb;
		return FFHTTP_MORE;
	}

	if (c->cursiz == 0)
		c->last = 1;

	return FFHTTP_OK;
}

/*
2  CRLF
hi CRLF
0  CRLF
   CRLF */
int ffhttp_chunkparse(ffhttp_chunked *c, const char *body, size_t *len, ffstr *dst)
{
	size_t i;
	int r = FFHTTP_EEOL;
	int t;
	int st = c->state;
	enum {
		iStart = 0, iSize
		, iExt
		, iBeforeDataLF
		, iData, iAfterData, iAfterDataLF
		, iTrlStart, iTrlLF
		, iTrlHdr, iTrlHdrLF
	};

	for (i = 0;  i < *len;  i++) {
		int ch = body[i];
		switch(st) {
		case iStart:
			t = ffchar_tohex(ch);
			if (t == -1) {
				r = FFHTTP_EHDRVAL;
				*len = i;
				goto end; //invalid hex number
			}
			c->cursiz = t;
			c->last = 0;
			st = iSize;
			break;

		case iSize:
			t = chnk_size(c, ch);

			if (t == FFHTTP_MORE)
				break;
			else if (t > 0) {
				r = t;
				*len = i;
				goto end;
			}

			switch (ch) {
			case CR:
				st = iBeforeDataLF;
				break;

			case LF:
				st = iData;
				break;

			case ';':
			case ' ':
			case '\t':
				st = iExt;
				break;

			default:
				r = FFHTTP_EHDRVAL;
				*len = i;
				goto end;
			}
			break;

		case iExt:
			if (ch == CR)
				st = iBeforeDataLF;
			else if (ch == LF)
				st = iData;
			break;

		case iBeforeDataLF:
			if (ch == LF)
				st = iData;
			else
				goto end;
			break;

		case iData:
			if (c->last) {
				st = iTrlStart;
				i--;
				break;
			}

			if (c->cursiz != 0) {
				size_t sz = (size_t)ffmin64(c->cursiz, (uint64)(*len - i));
				c->cursiz -= sz;
				ffstr_set(dst, body + i, sz);
				*len = i + sz; //bytes processed
				r = FFHTTP_OK;
				goto end;
			}
			//state = iAfterData;
			//break;

		case iAfterData:
			if (ch == CR)
				st = iAfterDataLF;
			else if (ch == LF)
				st = iStart;
			else
				goto end;
			break;

		case iAfterDataLF:
			if (ch == LF)
				st = iStart;
			else
				goto end;
			break;

		case iTrlStart:
			if (ch == CR)
				st = iTrlLF;
			else if (ch == LF) {
				*len = i + 1;
				r = FFHTTP_DONE;
				goto end;
			}
			else
				st = iTrlHdr;
			break;

		case iTrlLF:
			if (ch == LF) {
				*len = i + 1;
				r = FFHTTP_DONE;
			}
			//else
			//	r = FFHTTP_EEOL;
			goto end;
			//break;

		case iTrlHdr:
			if (ch == CR)
				st = iTrlHdrLF;
			else if (ch == LF)
				st = iTrlStart;
			break;

		case iTrlHdrLF:
			if (ch == LF)
				st = iTrlStart;
			else
				goto end;
			break;
		}
	}

	c->state = st;
	return FFHTTP_MORE;

end:
	if (r == FFHTTP_EEOL)
		*len = i;
	c->state = st;
	return r;
}

static const char chnkLast[] = FFCRLF "0" FFCRLF FFCRLF;
static const char *const sChnkEnd[] = {
	FFCRLF
	, chnkLast + FFSLEN(FFCRLF) //FFHTTP_CHUNKZERO
	, chnkLast //FFHTTP_CHUNKLAST
};
static const byte nChnkEnd[] = {
	FFSLEN(FFCRLF)
	, FFSLEN(chnkLast) - FFSLEN(FFCRLF)
	, FFSLEN(chnkLast)
};

int ffhttp_chunkfin(const char **pbuf, int flags)
{
	*pbuf = sChnkEnd[flags];
	return nChnkEnd[flags];
}
