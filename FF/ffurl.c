/**
Copyright (c) 2013 Simon Zolin
*/

#include <FF/url.h>
#include <FF/path.h>


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
		|| idx == iPathStart || idx == iPath || idx == iQs);
	if (consistent)
		er = FFURL_EOK;
	else
		er = FFURL_EMORE;

fin:
	url->idx = (byte)idx;
	url->len = (ushort)i;
	if (!(idx == iPath || idx == iQs))
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
		if (base[url->offhost] == '[') {
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


int ffip_parse4(struct in_addr *a, const char *s, size_t len)
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
				return 0;
			ndig++;
		}
		else if (ch == '.' && nadr != 4 && ndig != 0) {
			adr[nadr++] = b;
			b = 0;
			ndig = 0;
		}
		else
			return 0;
	}

	if (nadr == 4-1 && ndig != 0) {
		adr[nadr] = b;
		return 4;
	}

	return 0;
}
