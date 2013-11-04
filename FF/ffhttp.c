/**
Copyright (c) 2013 Simon Zolin
*/

#include <FF/http.h>


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

#define add(st) FFSTR_INIT(st)
const ffstr ffhttp_sresp[] = {
	{ 0, NULL }
	, add("200 OK")
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

static FFINL uint getHdr(uint64 m, uint crc, const ffstr *name)
{
	while (m != 0) {
		uint i = ffbit_ffs64(m) - 1;
		if (crc == hdrHashes[i]
			&& ffstr_ieq2(name, &ffhttp_shdr[i]))
			return i;

		ffbit_reset64(&m, i);
	}

	return FFHTTP_HUKN;
}

enum { CR = '\r', LF = '\n' };

int ffhttp_nexthdr(ffhttp_hdr *h, const char *d, size_t len)
{
	enum { iKey = 0, iKeyData, iSpaceBeforeVal, iVal, iSpaceAfterVal, iLastLf };
	uint ihdr = FFHTTP_HUKN;
	uint i = h->len;
	uint idx = h->idx;
	ffrange *name = &h->key;
	ffrange *val = &h->val;
	uint er = FFHTTP_OK;

	len = ffmin(len, (size_t)0xffff);
	for (; i != len; ++i) {
		char ch = d[i];

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
				if (!(ffchar_isname(ch) && ch != '_')) {
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
				if (!((ffchar_isname(ch) && ch != '_') || ch == '-')) {
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

	if (h->mask != 0) {
		ffstr sname = ffrang_get(name, d);
		ihdr = getHdr(h->mask, h->crc, &sname);
	}

	h->ihdr = ihdr;
	return FFHTTP_OK;
}

void ffhttp_init(ffhttp_headers *h)
{
	memset(h, 0, sizeof(ffhttp_headers));
	ffhttp_inithdr(&h->hdr);
	h->cont_len = -1;
	h->ce_identity = 1;
	h->hdr.mask = FF_BIT64(FFHTTP_CONTENT_LENGTH) | FF_BIT64(FFHTTP_CONNECTION)
		| FF_BIT64(FFHTTP_TRANSFER_ENCODING) | FF_BIT64(FFHTTP_CONTENT_ENCODING);
}

int ffhttp_parsehdr(ffhttp_headers *h, const char *data, size_t len)
{
	ffstr val;
	int e;

	if (h->base != data)
		h->base = data;
	e = ffhttp_nexthdr(&h->hdr, data, len);
	h->len = h->hdr.len;
	if (e != FFHTTP_OK)
		return e;

	val = ffrang_get(&h->hdr.val, data);

	switch (h->hdr.ihdr) {
	case FFHTTP_CONNECTION:
		// handle "Connection: Keep-Alive, TE"
		while (val.len != 0) {
			ffstr v;
			size_t by = ffstr_nextval(val.ptr, val.len, &v, ',');
			ffstr_shift(&val, by);
			if (h->http11) {
				if (ffstr_ieq(&v, FFSTR("close"))) {
					h->conn_close = 1;
					break;
				}
			}
			else if (ffstr_ieq(&v, FFSTR("keep-alive"))) {
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
		if (ffstr_ieq(&val, FFSTR("gzip")))
			h->ce_gzip = 1;
		else if (ffstr_ieq(&val, FFSTR("identity")))
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

enum VER_E {
	iH = 32, iHT, iHTT, iHTTP, iHttpMajVer, iHttpMinVerFirst, iHttpMinVer
	, iAnyProto, iSpaceAfterHttpVer
};

/** Return FFHTTP_OK if version has been parsed.  The caller must process this char again. */
static int parseVer(char ch, uint *_Idx, ushort *ver)
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
		if (!ffchar_isnum(ch)) {
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
		if (!ffchar_isnum(ch)) {
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
		char ch = d[i];

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
	r->h.hdr.len = (ushort)i;
	return FFHTTP_MORE;

done:
	++i;
	r->h.hdr.len = (ushort)i;
	r->h.firstline_len = (ushort)i;
	r->h.base = d;
	r->h.hdr.idx = 0; //iKey
	return FFHTTP_OK;

fail:
	r->h.hdr.len = (ushort)i;
	r->h.firstline_len = (ushort)(ffs_findof(d + i, len - i, FFSTR("\r\n")) - d);
	return er;
}

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
			if (ffstr_ieq(&v, FFSTR("chunked"))) {
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
			if (ffstr_eq(&encoding, FFSTR("gzip"))) {
				r->accept_gzip = 1;
				break;
			}
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
		char ch = d[i];

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
			if (ffchar_isnum(ch)) {
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
	r->h.hdr.len = (ushort)i;
	return FFHTTP_MORE;

done:
	++i;
	r->h.hdr.len = (ushort)i;
	r->h.firstline_len = (ushort)i;
	r->h.hdr.idx = 0; //iKey
	r->h.base = d;
	return FFHTTP_OK;

fail:
	r->h.hdr.len = (ushort)i;
	r->h.firstline_len = (ushort)(ffs_findof(d + i, len - i, FFSTR("\r\n")) - d);
	return er;
}

int ffhttp_respparsehdr(ffhttp_response *r, const char *data, size_t len)
{
	int rc = ffhttp_parsehdr(&r->h, data, len);
	if (rc == FFHTTP_DONE) {
		if (r->h.has_body && (r->code == 204 || r->code == 304 || r->code/100 == 1))
			r->h.has_body = 0;
		else if (!r->h.has_body && r->h.cont_len == -1) {
			//no chunked, no content length.  So read body until connection is closed
			r->h.has_body = 1;
			r->h.body_conn_close = 1;
		}
	}
	return rc;
}
