/**
Copyright (c) 2013 Simon Zolin
*/

#include <FF/net/url.h>
#include <FF/net/http.h>
#include <FF/path.h>
#include <FF/number.h>


/*
host:
	host.com
	127.0.0.1
	[::1]

valid input:
	[http://] host [:80] [/path [?query]]
	/path [?query]
*/
int ffurl_parse(ffurl *url, const char *s, size_t len)
{
	enum {
		iHostStart = 0, iHost, iIp6, iAfterHost, iPort
		, iPathStart, iPath, iQuoted1, iQuoted2, iQs
		, iPortOrScheme, iSchemeSlash2
	};
	int er = 0;
	int idx = url->idx;
	size_t i;
	ffbool again = 0;
	ffbool consistent;

	for (i = url->len;  i < len;  i++) {
		int ch = s[i];

		switch (idx) {
		case iSchemeSlash2:
			if (ch != '/') {
				er = FFURL_ESCHEME;
				break;
			}
			// http://
			url->hostlen = 0;
			idx = iHostStart;
			url->offhost = (ushort)i + 1;
			break;

		case iIp6:
			if (i - url->offhost > 45 + FFSLEN("[]")) {
				er = FFURL_ETOOLARGE;
				break;
			}
			if (ch == ']') {
				idx = iAfterHost;
			}
			else if (!(ffchar_ishex(ch) || ch == ':' || ch == '%')) {// valid ip6 addr: 0-9a-f:%
				er = FFURL_EIP6;
				break;
			}
			url->hostlen++;
			break;

		case iHostStart:
			if (ch == '[') {
				// [::1
				idx = iIp6;
				url->hostlen++;
				url->ipv6 = 1;
				break;
			}
			else if (ffchar_isdigit(ch))
				url->ipv4 = 1;
			else if (ch == '/') {
				idx = iPathStart;
				again = 1;
				break;
			}
			// host or 127.
			idx = iHost;
			//break;

		case iHost:
			if (ffchar_isname(ch) || ch == '-' || ch == '.') { // a-zA-Z0-9_\-\.
				if (i - url->offhost > 0xff) {
					er = FFURL_ETOOLARGE;
					break;
				}
				if (url->ipv4 && !ffchar_isdigit(ch) && ch != '.')
					url->ipv4 = 0;
				url->hostlen++;
				break;
			}
			if (url->hostlen == 0) {
				er = FFURL_EHOST;
				break; //host can not be empty
			}
			idx = iAfterHost;
			//break;

		case iAfterHost:
			if (ch == ':') {
				// host: or http:
				idx = (url->offhost == 0 ? iPortOrScheme : iPort);
			}
			else {
				idx = iPathStart;
				again = 1;
			}
			break;

		case iPortOrScheme:
			if (ch == '/') {
				idx = iSchemeSlash2;
				break;
			}
			idx = iPort;
			//break;

		case iPort:
			{
				uint p;
				if (!ffchar_isdigit(ch)) {
					idx = iPathStart;
					again = 1;
					break;
				}
				p = (uint)url->port * 10 + (ch - '0');
				if (p > 0xffff) {
					er = FFURL_ETOOLARGE;
					break;
				}
				url->port = (ushort)p;
			}
			url->portlen++;
			break;

		case iPathStart:
			if (ch != '/') {
				er = FFURL_ESTOP;
				break;
			}
			url->offpath = (ushort)i;
			idx = iPath;
			//break;

		case iPath:
			if (ch == '%') {
				if (!url->complex)
					url->complex = 1;
				idx = iQuoted1;
			}
			else if (ch == '?') {
				idx = iQs;
				break;
			}
			else if (ch == '/' || ch == '.') {
				if (!url->complex && i != 0 && s[i - 1] == '/')
					url->complex = 1; //handle "//" and "/./", "/." and "/../", "/.."
			}
			else if (ffchar_isansiwhite(ch) || ch == '#') {
				er = FFURL_ESTOP;
				break;
			}
			url->decoded_pathlen++;
			url->pathlen++;
			break;

		case iQuoted1:
		case iQuoted2:
			if (!ffchar_ishex(ch)) {
				er = FFURL_EPATH;
				break;
			}
			idx = (idx == iQuoted1 ? iQuoted2 : iPath);
			url->pathlen++;
			break;

		case iQs:
			if (ffchar_isansiwhite(ch) || ch == '#') {
				er = FFURL_ESTOP;
				break;
			}
			if (!url->querystr)
				url->querystr = 1;
			break;
		}

		if (er != 0)
			goto fin;

		if (again) {
			again = 0;
			i--;
			continue;
		}
	}

	consistent = ((idx == iHost && url->hostlen != 0) || idx == iAfterHost
		|| (idx == iPort && url->portlen != 0)
		|| idx == iPath || idx == iQs);
	if (consistent)
		er = FFURL_EOK;
	else
		er = FFURL_EMORE;

fin:
	url->idx = (byte)idx;
	url->len = (ushort)i;

	if (er != FFURL_EMORE && !(idx == iPath || idx == iQs))
		url->offpath = url->len;

	return er;
}

static const char *const serr[] = {
	"ok"
	, "more data"
	, "done"
	, "value too large"
	, "bad scheme"
	, "bad IPv6 address"
	, "bad host"
	, "bad path"
};

const char *ffurl_errstr(int er)
{
	return serr[er];
}

ffstr ffurl_get(const ffurl *url, const char *base, int comp)
{
	ffstr s = { 0, NULL };

	switch (comp) {
	case FFURL_FULLHOST:
		{
			size_t n = url->hostlen;
			if (url->portlen != 0)
				n += FFSLEN(":") + url->portlen;
			ffstr_set(&s, base + url->offhost, n);
		}
		break;

	case FFURL_SCHEME:
		{
			size_t n = (url->offhost != 0 ? url->offhost - FFSLEN("://") : 0);
			ffstr_set(&s, base + 0, n);
		}
		break;

	case FFURL_HOST:
		ffstr_set(&s, base + url->offhost, url->hostlen);
		// [::1] -> ::1
		if (url->hostlen > 2 && base[url->offhost] == '[') {
			s.ptr++;
			s.len -= 2;
		}
		break;

	case FFURL_PORT:
		ffstr_set(&s, base + url->offhost + url->hostlen + FFSLEN(":"), url->portlen);
		break;

	case FFURL_PATH:
		ffstr_set(&s, base + url->offpath, url->pathlen);
		break;

	case FFURL_QS:
		if (url->querystr) {
			size_t off = url->offpath + url->pathlen + FFSLEN("?");
			ffstr_set(&s, base + off, url->len - off);
		}
		break;

	case FFURL_PATHQS:
		ffstr_set(&s, base + url->offpath, url->len - url->offpath);
		break;
	}

	return s;
}

int ffurl_parse_ip(ffurl *u, const char *base, ffip6 *dst)
{
	ffstr s = ffurl_get(u, base, FFURL_HOST);
	if (u->ipv4) {
		if (0 != ffip4_parse((void*)dst, s.ptr, s.len))
			return -1;
		return AF_INET;

	} else if (u->ipv6) {
		if (0 != ffip6_parse((void*)dst, s.ptr, s.len))
			return -1;
		return AF_INET6;
	}

	return 0;
}

int ffip_parse(const char *ip, size_t len, ffip6 *dst)
{
	if (0 == ffip4_parse((void*)dst, ip, len))
		return AF_INET;
	if (0 == ffip6_parse((void*)dst, ip, len))
		return AF_INET6;
	return 0;
}

int ffurl_joinstr(ffstr *dst, const ffstr *scheme, const ffstr *host, uint port, const ffstr *path, const ffstr *querystr)
{
	if (scheme->len == 0 || host->len == 0
		|| (path->len != 0 && path->ptr[0] != '/'))
		return 0;

	ffarr a = {};
	if (NULL == ffarr_alloc(&a, scheme->len + FFSLEN("://:12345/?") + host->len + path->len + querystr->len))
		return 0;
	char *p = a.ptr;
	const char *end = ffarr_edge(&a);

	p += ffs_lower(p, end, scheme->ptr, scheme->len);
	p = ffs_copy(p, end, "://", 3);

	p += ffs_lower(p, end, host->ptr, host->len);

	if (port != 0) {
		FF_ASSERT(0 == (port & ~0xffff));
		p = ffs_copy(p, end, ":", 1);
		p += ffs_fromint(port & 0xffff, p, end - p, 0);
	}

	if (path->len == 0)
		p = ffs_copy(p, end, "/", 1);
	else
		p += ffpath_norm(p, end - p, path->ptr, path->len, FFPATH_MERGEDOTS | FFPATH_NOWINDOWS | FFPATH_FORCESLASH);

	if (querystr->len != 0) {
		p = ffs_copy(p, end, "?", 1);
		p = ffs_copy(p, end, querystr->ptr, querystr->len);
	}

	FF_ASSERT(a.cap >= (size_t)(p - a.ptr));
	ffstr_set(dst, a.ptr, p - a.ptr);
	return dst->len;
}

size_t ffuri_decode(char *dst, size_t dstcap, const char *d, size_t len, uint flags)
{
	enum { iUri, iQuoted1, iQuoted2 };
	int idx = iUri;
	size_t idst = 0;
	uint b = 0;
	int bt;
	size_t i;

	for (i = 0;  i != len && idst < dstcap;  ++i) {
		int ch = d[i];

		switch (idx) {
		case iUri:
			if (ch == '%')
				idx = iQuoted1;
			else if (ffchar_isansiwhite(ch) || ch == '#')
				goto fail;
			else
				dst[idst++] = ch;
			break;

		case iQuoted1:
			bt = ffchar_tohex(ch);
			if (bt == -1)
				goto fail;
			b = bt;
			idx = iQuoted2;
			break;

		case iQuoted2:
			bt = ffchar_tohex(ch);
			if (bt == -1)
				goto fail;
			b = (b << 4) | bt;
			if (b == '\0')
				goto fail;
			dst[idst++] = (char)b;
			idx = iUri;
			break;
		}
	}

	if (idx != iUri)
		goto fail; // incomplete quoted sequence, e.g. "%2"

	if (flags & FFURI_DEC_NORM_PATH)
		idst = ffpath_norm(dst, dstcap, dst, idst, FFPATH_MERGEDOTS | FFPATH_STRICT_BOUNDS | FFPATH_WINDOWS | FFPATH_FORCESLASH);
	if (idst == 0)
		goto fail;
	return idst;

fail:
	return 0;
}

// escape non-ANSI and ' ', '#', '%', '?'
static const uint uriesc[] = {
	0,
	            // ?>=< ;:98 7654 3210  /.-, +*)( '&%$ #"!
	0x7fffffd6, // 0111 1111 1111 1111  1111 1111 1101 0110
	            // _^]\ [ZYX WVUT SRQP  ONML KJIH GFED CBA@
	0xffffffff, // 1111 1111 1111 1111  1111 1111 1111 1111
	            //  ~}| {zyx wvut srqp  onml kjih gfed cba`
	0x7fffffff, // 0111 1111 1111 1111  1111 1111 1111 1111
	0,
	0,
	0,
	0
};

// escape non-ANSI and ' ', '#', '%', '?', '\\', '/'
static const uint uriesc_pathseg[] = {
	0,
	            // ?>=< ;:98 7654 3210  /.-, +*)( '&%$ #"!
	0x7fff7fd6, // 0111 1111 1111 1111  0111 1111 1101 0110
	            // _^]\ [ZYX WVUT SRQP  ONML KJIH GFED CBA@
	0xefffffff, // 1110 1111 1111 1111  1111 1111 1111 1111
	            //  ~}| {zyx wvut srqp  onml kjih gfed cba`
	0x7fffffff, // 0111 1111 1111 1111  1111 1111 1111 1111
	0,
	0,
	0,
	0
};

// escape non-ANSI and ' ', '#', '%', '&'
static const uint uriesc_qsseg[] = {
	0,
	            // ?>=< ;:98 7654 3210  /.-, +*)( '&%$ #"!
	0xffffff96, // 1111 1111 1111 1111  1111 1111 1001 0110
	            // _^]\ [ZYX WVUT SRQP  ONML KJIH GFED CBA@
	0xffffffff, // 1111 1111 1111 1111  1111 1111 1111 1111
	            //  ~}| {zyx wvut srqp  onml kjih gfed cba`
	0x7fffffff, // 0111 1111 1111 1111  1111 1111 1111 1111
	0,
	0,
	0,
	0
};

static const uint *const uri_encode_types[] = { uriesc, uriesc_pathseg, uriesc_qsseg };

ssize_t ffuri_escape(char *dst, size_t cap, const char *s, size_t len, uint type)
{
	size_t i;
	const uint *mask;
	const char *end = dst + cap;
	const char *dsto = dst;

	if (type >= FFCNT(uri_encode_types))
		return 0; //unknown type
	mask = uri_encode_types[type];

	if (dst == NULL) {
		size_t n = 0;

		for (i = 0;  i != len;  i++) {
			if (!ffbit_testarr(mask, (byte)s[i]))
				n += FFSLEN("%XX") - 1;
		}

		return len + n;
	}

	for (i = 0;  i != len;  i++) {
		byte c = s[i];

		if (ffbit_testarr(mask, c)) {
			if (dst == end)
				return -(ssize_t)i;
			*dst++ = c;

		} else {
			if (dst + FFSLEN("%XX") > end) {
				ffs_fill(dst, end, '\0', FFSLEN("%XX"));
				return -(ssize_t)i;
			}

			*dst++ = '%';
			dst += ffs_hexbyte(dst, c, ffHEX);
		}
	}

	return dst - dsto;
}

uint ffuri_scheme2port(const char *scheme, size_t schemelen)
{
	if (ffs_eqcz(scheme, schemelen, "http"))
		return FFHTTP_PORT;
	else if (ffs_eqcz(scheme, schemelen, "https"))
		return FFHTTPS_PORT;
	return 0;
}


static int val_store(ffarr *buf, const char *s, size_t len)
{
	if (NULL == ffarr_grow(buf, len, 256 | FFARR_GROWQUARTER))
		return FFPARS_ESYS;
	ffarr_append(buf, s, len);
	return 0;
}

static int val_add(ffarr *buf, const char *s, size_t len)
{
	if (buf->cap != 0)
		return val_store(buf, s, len);

	if (buf->len == 0)
		buf->ptr = (char*)s;
	buf->len += len;
	return 0;
}

static int _ffurlqs_process_str(ffurlqs *p, const char **pd)
{
	const char *d = *pd;
	char c;

	if (*d == '+') {
		c = ' ';
		return val_store(&p->buf, &c, 1);

	} else if (*d == '%') {
		//no check for invalid hex char
		int b = (ffchar_tohex(d[1]) << 4);
		c = b | ffchar_tohex(d[2]);
		*pd += FFSLEN("XX");
		return val_store(&p->buf, &c, 1);
	}

	return val_add(&p->buf, d, 1);
}

int ffurlqs_parse(ffurlqs *p, const char *d, size_t *len)
{
	enum {
		qs_key_start, qs_key, qs_val_start, qs_val
	};
	const char *datao = d;
	const char *end = d + *len;
	int r = FFPARS_MORE, st = p->state;

	for (;  d != end;  d++) {
		int ch = *d;
		p->ch++;

		switch (st) {

		case qs_key_start:
			if (ch == '&')
				break;

			ffarr_free(&p->buf);
			st = qs_key;
			//break;

		case qs_key:
			if (ch == '=') {
				st = qs_val_start;
				r = FFPARS_KEY;
				break;
			}

			r = _ffurlqs_process_str(p, &d);
			break;

		case qs_val_start:
			ffarr_free(&p->buf);
			st = qs_val;
			//break;

		case qs_val:
			if (ch == '&') {
				st = qs_key_start;
				r = FFPARS_VAL;
				break;
			}

			r = _ffurlqs_process_str(p, &d);
			break;
		}

		if (r != 0) {
			d++;
			break;
		}
	}

	if (r == FFPARS_MORE && st == qs_val)
		r = FFPARS_VAL; //the last value

	ffstr_set2(&p->val, &p->buf);
	p->state = st;
	*len = d - datao;
	return r;
}

void ffurlqs_parseinit(ffurlqs *p)
{
	ffmem_tzero(p);
}

int ffurlqs_scheminit(ffparser_schem *ps, ffurlqs *p, const ffpars_ctx *ctx)
{
	const ffpars_arg top = { NULL, FFPARS_TOBJ | FFPARS_FPTR, FFPARS_DST(ctx) };
	ffpars_scheminit(ps, p, &top);

	ffurlqs_parseinit(p);
	if (FFPARS_OPEN != _ffpars_schemrun(ps, FFPARS_OPEN))
		return 1;

	return 0;
}

int ffurlqs_schemrun(ffparser_schem *ps, int r)
{
	ffurlqs *p = ps->p;

	if (ps->ctxs.len == 0)
		return FFPARS_ECONF;

	switch (r) {
	case FFPARS_KEY:
		return _ffpars_schemrun_key(ps, &ffarr_back(&ps->ctxs), &p->val);
	case FFPARS_VAL:
		return ffpars_arg_process(ps->curarg, &p->val, ffarr_back(&ps->ctxs).obj, ps);
	default:
		return FFPARS_EINTL;
	}
}

int ffurlqs_schemfin(ffparser_schem *ps)
{
	int r = _ffpars_schemrun(ps, FFPARS_CLOSE);
	if (r != FFPARS_CLOSE)
		return r;
	return 0;
}


int ffip_next(ffip_iter *it, void **ip)
{
	if (it->list != NULL) {

		if (it->idx < it->list->ip4.len) {
			*ip = ffarr_itemT(&it->list->ip4, it->idx, ffip4);
			it->idx++;
			return AF_INET;
		}
		if (it->idx - it->list->ip4.len < it->list->ip6.len) {
			*ip = ffarr_itemT(&it->list->ip6, it->idx - it->list->ip4.len, ffip6);
			it->idx++;
			return AF_INET6;
		}
		it->list = NULL;
	}

	if (it->ai != NULL) {
		uint family = it->ai->ai_family;
		union {
			struct sockaddr_in *a;
			struct sockaddr_in6 *a6;
		} u;
		u.a = (void*)it->ai->ai_addr;
		*ip = (family == AF_INET) ? (void*)&u.a->sin_addr : (void*)&u.a6->sin6_addr;
		it->ai = it->ai->ai_next;
		return family;
	}

	return 0;
}


uint ffip_tostr(char *buf, size_t cap, uint family, const void *ip, uint port)
{
	char *end = buf + cap, *p = buf;
	uint n;

	if (family == AF_INET) {
		if (0 == (n = ffip4_tostr(buf, cap, ip)))
			return 0;
		p += n;

	} else {
		if (port != 0)
			p = ffs_copyc(p, end, '[');
		if (0 == (n = ffip6_tostr(p, end - p, ip)))
			return 0;
		p += n;
		if (port != 0)
			p = ffs_copyc(p, end, ']');
	}

	if (port != 0)
		p += ffs_fmt(p, end, ":%u", port);

	return p - buf;
}

int ffip_split(const char *data, size_t len, ffstr *ip, ffstr *port)
{
	const char *pos = ffs_rsplit2by(data, len, ':', ip, port);

	if (ip->len != 0 && ip->ptr[0] == '[') {
		if (data[len - 1] == ']') {
			ip->len = len;
			ffstr_null(port);
			pos = NULL;
		}
		if (!(ip->len >= FFSLEN("[::]") && ip->ptr[ip->len - 1] == ']'))
			return -1;
		ip->ptr += FFSLEN("[");
		ip->len -= FFSLEN("[]");
	}

	if (pos != NULL && port->len == 0)
		return -1; // "ip:"

	return 0;
}


int ffaddr_set(ffaddr *a, const char *ip, size_t iplen, const char *port, size_t portlen)
{
	ushort nport;
	ffip6 a6;
	ffip4 a4;

	if (iplen == 0) {
		ffaddr_setany(a, AF_INET6);
	} else if (0 == ffip4_parse(&a4, ip, iplen))
		ffip4_set(a, (void*)&a4);
	else if (0 == ffip6_parse(&a6, ip, iplen))
		ffip6_set(a, (void*)&a6);
	else
		return 1; //invalid IP

	if (portlen != 0) {
		if (portlen != ffs_toint(port, portlen, &nport, FFS_INT16))
			return 1; //invalid port
		ffip_setport(a, nport);
	}

	return 0;
}
