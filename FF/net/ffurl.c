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
				if (!url->complex && s[i - 1] == '/')
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

size_t ffuri_decode(char *dst, size_t dstcap, const char *d, size_t len)
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
			else if (ch == '?')
				goto done;
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

done:
	idst = ffpath_norm(dst, dstcap, dst, idst, FFPATH_STRICT_BOUNDS);
	if (idst == 0)
		goto fail;
	return idst;

fail:
	return 0;
}

// escape ' ', '#', '%', '?' and non-ANSI
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

// escape '\\', '/', ' ', '#', '%', '?' and non-ANSI
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

static const uint *const uri_encode_types[] = { uriesc, uriesc_pathseg };

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


static int _ffurlqs_process_str(ffparser *p, const char **pd)
{
	const char *d = *pd;
	char c;

	if (*d == '+') {
		c = ' ';
		return _ffpars_copyBuf(p, &c, sizeof(char));

	} else if (*d == '%') {
		//no check for invalid hex char
		int b = (ffchar_tohex(d[1]) << 4);
		c = b | ffchar_tohex(d[2]);
		*pd += FFSLEN("XX");
		return _ffpars_copyBuf(p, &c, sizeof(char));
	}

	c = *d;
	return _ffpars_addchar(p, c);
}

int ffurlqs_parse(ffparser *p, const char *d, size_t *len)
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

			ffpars_cleardata(p);
			p->val.ptr = (char*)d;
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
			ffpars_cleardata(p);
			p->val.ptr = (char*)d;
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

	p->state = st;
	*len = d - datao;
	p->ret = (char)r;
	return r;
}

int ffurlqs_parseinit(ffparser *p)
{
	char *ctx;
	ffpars_init(p);

	ctx = ffarr_push(&p->ctxs, char);
	if (ctx == NULL)
		return 1;

	*ctx = FFPARS_OPEN;
	p->type = FFPARS_TOBJ;
	return 0;
}

int ffurlqs_scheminit(ffparser_schem *ps, ffparser *p, const ffpars_ctx *ctx)
{
	const ffpars_arg top = { NULL, FFPARS_TOBJ | FFPARS_FPTR, FFPARS_DST(ctx) };
	ffpars_scheminit(ps, p, &top);

	if (0 != ffurlqs_parseinit(p))
		return 1;
	if (FFPARS_OPEN != ffpars_schemrun(ps, FFPARS_OPEN))
		return 1;

	return 0;
}

int ffurlqs_schemfin(ffparser_schem *ps)
{
	int r = ffpars_schemrun(ps, FFPARS_CLOSE);
	if (r != FFPARS_CLOSE)
		return r;
	return 0;
}


int ffip4_parse(struct in_addr *a, const char *s, size_t len)
{
	byte *adr = (byte*)a;
	uint nadr = 0;
	uint ndig = 0;
	uint b = 0;
	uint i;

	for (i = 0;  i < len;  i++) {
		uint ch = (byte)s[i];

		if (ffchar_isdigit(ch) && ndig != 3) {
			b = b * 10 + ffchar_tonum(ch);
			if (b > 0xff)
				return -1;
			ndig++;
		}
		else if (ch == '.' && nadr != 4 && ndig != 0) {
			adr[nadr++] = b;
			b = 0;
			ndig = 0;
		}
		else
			return -1;
	}

	if (nadr == 4-1 && ndig != 0) {
		adr[nadr] = b;
		return 0;
	}

	return -1;
}

size_t ffip4_tostr(char *dst, size_t cap, const void *addr, size_t addrlen, int port)
{
	const byte *a = addr;
	if (addrlen != 4)
		return 0;
	return ffs_fmt(dst, dst + cap, (port != 0 ? "%u.%u.%u.%u:%u" : "%u.%u.%u.%u")
		, (byte)a[0], (byte)a[1], (byte)a[2], (byte)a[3], port);
}

int ffip6_parse(struct in6_addr *a, const char *s, size_t len)
{
	uint i;
	uint chunk = 0;
	uint ndigs = 0;
	char *dst = (char*)a;
	const char *end = (char*)a + 16;
	int hx;
	const char *zr = NULL;

	for (i = 0;  i < len;  i++) {
		int b = s[i];

		if (dst == end)
			return -1; // too large input

		if (b == ':') {

			if (ndigs == 0) { // "::"
				uint k;

				if (i == 0) {
					i++;
					if (i == len || s[i] != ':')
						return -1; // ":" or ":?"
				}

				if (zr != NULL)
					return -1; // second "::"

				// count the number of chunks after zeros
				zr = end;
				if (i != len - 1)
					zr -= 2;
				for (k = i + 1;  k < len;  k++) {
					if (s[k] == ':')
						zr -= 2;
				}

				// fill with zeros
				while (dst != zr)
					*dst++ = '\0';

				continue;
			}

			*dst++ = chunk >> 8;
			*dst++ = chunk & 0xff;
			ndigs = 0;
			chunk = 0;
			continue;
		}

		if (ndigs == 4)
			return -1; // ":12345"

		hx = ffchar_tohex(b);
		if (hx == -1)
			return -1; // invalid hex char

		chunk = (chunk << 4) | hx;
		ndigs++;
	}

	if (ndigs != 0) {
		*dst++ = chunk >> 8;
		*dst++ = chunk & 0xff;
	}

	if (dst != end)
		return -1; // too small input

	return 0;
}

size_t ffip6_tostr(char *dst, size_t cap, const void *addr, size_t addrlen, int port)
{
	const byte *a = addr;
	char *p = dst;
	const char *end = dst + cap;
	int i;
	int cut_from = -1
		, cut_len = 0;
	int zrbegin = 0
		, nzr = 0;

	if (addrlen != 16)
		return 0;

	// get the maximum length of zeros to cut off
	for (i = 0;  i < 16;  i += 2) {
		if (a[i] == '\0' && a[i + 1] == '\0') {
			if (nzr == 0)
				zrbegin = i;
			nzr += 2;

		} else if (nzr != 0) {
			if (nzr > cut_len) {
				cut_from = zrbegin;
				cut_len = nzr;
			}
			nzr = 0;
		}
	}

	if (nzr > cut_len) {
		// zeros at the end of address
		cut_from = zrbegin;
		cut_len = nzr;
	}

	if (port != 0)
		p = ffs_copyc(p, end, '[');

	for (i = 0;  i < 16; ) {
		if (i == cut_from) {
			// cut off the sequence of zeros
			p = ffs_copyc(p, end, ':');
			i = cut_from + cut_len;
			if (i == 16)
				p = ffs_copyc(p, end, ':');
			continue;
		}

		if (i != 0)
			p = ffs_copyc(p, end, ':');
		p += ffs_fromint(ffint_ntoh16(a + i), p, end - p, FFINT_HEXLOW); //convert 16-bit number to string
		i += 2;
	}

	if (port != 0)
		p += ffs_fmt(p, end, "]:%u", port);

	return p - dst;
}


int ffaddr_set(ffaddr *a, const char *ip, size_t iplen, const char *port, size_t portlen)
{
	ushort nport;
	struct in6_addr a6;
	struct in_addr a4;

	if (iplen == 0) {
		ffaddr_setany(a, AF_INET6);
	} else if (0 == ffip4_parse(&a4, ip, iplen))
		ffip4_set(a, &a4);
	else if (0 == ffip6_parse(&a6, ip, iplen))
		ffip6_set(a, &a6);
	else
		return 1; //invalid IP

	if (portlen != 0) {
		if (portlen != ffs_toint(port, portlen, &nport, FFS_INT16))
			return 1; //invalid port
		ffip_setport(a, nport);
	}

	return 0;
}
