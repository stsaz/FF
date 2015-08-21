/**
Copyright (c) 2013 Simon Zolin
*/

#include <FFOS/test.h>
#include <FF/net/http.h>

#define x FFTEST_BOOL


static int test_meth()
{
	x(FFHTTP_GET == ffhttp_findmethod(FFSTR("GET")));
	x(FFHTTP_HEAD == ffhttp_findmethod(FFSTR("HEAD")));
	x(FFHTTP_HEAD == ffhttp_findmethod(FFSTR("HEAD\0")));
	x(FFHTTP_OPTIONS == ffhttp_findmethod(FFSTR("OPTIONS")));
	x(FFHTTP_MUKN == ffhttp_findmethod(FFSTR("OPTIONS ")));
	x(FFHTTP_MUKN == ffhttp_findmethod(FFSTR("")));
	return 0;
}

static int test_req()
{
	ffhttp_request r;
	ffstr s;

	test_meth();

	ffhttp_reqinit(&r);
	x(FFHTTP_OK == ffhttp_reqparse(&r, FFSTR("CONNECT www.host:443 HTTP/1.0" FFCRLF)));
	x(r.method == FFHTTP_CONNECT);
	s = ffhttp_reqpath(&r);
	x(ffstr_eqcz(&s, "www.host:443"));

	ffhttp_reqinit(&r);
	x(FFHTTP_OK == ffhttp_reqparse(&r, FFSTR("GET /%0f1/2/3 HTTP/1.0" FFCRLF)));
	ffhttp_reqfree(&r);

	ffhttp_reqinit(&r);
	x(FFHTTP_OK == ffhttp_reqparse(&r, FFSTR("GET /%0a%0d1/2/3 HTTP/1.0" FFCRLF)));
	ffhttp_reqfree(&r);

	ffhttp_reqinit(&r);
	x(FFHTTP_OK == ffhttp_reqparse(&r, FFSTR("GET /%0a%0d1/2/3?%%00?%g HTTP/1.0" FFCRLF)));
	ffhttp_reqfree(&r);

	ffhttp_reqinit(&r);
	x((FFHTTP_EURLPARSE | FFURL_EPATH) == ffhttp_reqparse(&r, FFSTR("GET /% ")));

	//http 09
	//"GET /" FFCRLF FFCRLF


#define HDRS "host:  myhost:8080  " FFCRLF \
	"accept-encoding: gzip" FFCRLF \
	"content-encoding: gzip" FFCRLF \
	"content-length:  123456  " FFCRLF \
	FFCRLF

#define DATA "GET  /file%20name?qu%20e?&ry%01#sharp  HTTP/1.1 " FFCRLF \
	HDRS

	ffhttp_reqinit(&r);
	x(FFHTTP_OK == ffhttp_reqparse(&r, FFSTR(DATA)));
	s = ffhttp_firstline(&r.h);
	x(ffstr_eqcz(&s, "GET  /file%20name?qu%20e?&ry%01#sharp  HTTP/1.1 "));
	x(r.method == FFHTTP_GET);
	s = ffhttp_reqmethod(&r);
	x(ffstr_eqcz(&s, "GET"));
	s = ffhttp_requrl(&r, FFURL_PATHQS);
	x(ffstr_eqcz(&s, "/file%20name?qu%20e?&ry%01"));
	s = ffhttp_requrl(&r, FFURL_PATH);
	x(ffstr_eqcz(&s, "/file%20name"));
	s = ffhttp_requrl(&r, FFURL_QS);
	x(ffstr_eqcz(&s, "qu%20e?&ry%01"));

	s = ffhttp_reqpath(&r);
	x(ffstr_eqcz(&s, "/file name"));
	x(r.h.http11 && r.h.ver == 0x0101);

	x(FFHTTP_DONE == ffhttp_reqparsehdrs(&r, FFSTR(DATA)));
	s = ffhttp_hdrs(&r.h);
	x(ffstr_eqcz(&s, HDRS));
	s = ffhttp_requrl(&r, FFURL_FULLHOST);
	x(ffstr_eqcz(&s, "myhost:8080"));
	s = ffhttp_reqhost(&r);
	x(ffstr_eqcz(&s, "myhost"));
	x(!r.h.conn_close && r.h.has_body && !r.h.chunked && r.h.cont_len == 123456);
	x(r.h.ce_gzip && !r.h.ce_identity);
	x(r.accept_gzip && r.accept_chunked);

	ffhttp_reqfree(&r);
#undef DATA
#undef HDRS

#define DATA "GET http://urlhost:80/file?qs HTTP/1.1" FFCRLF \
	"Host: hdrhost" FFCRLF \
	"content-length:  123456" FFCRLF \
	"Transfer-Encoding: chunked" FFCRLF FFCRLF

	ffhttp_reqinit(&r);
	x(FFHTTP_OK == ffhttp_reqparse(&r, FFSTR(DATA)));
	x(FFHTTP_DONE == ffhttp_reqparsehdrs(&r, FFSTR(DATA)));
	s = ffhttp_requrl(&r, FFURL_FULLHOST);
	x(ffstr_eqcz(&s, "urlhost:80"));
	s = ffhttp_reqhost(&r);
	x(ffstr_eqcz(&s, "urlhost"));
	s = ffhttp_requrl(&r, FFURL_PATHQS);
	x(ffstr_eqcz(&s, "/file?qs"));
	x(r.h.chunked && r.h.cont_len == -1 && r.h.has_body && r.h.ce_identity);
	ffhttp_reqfree(&r);
#undef DATA

#define DATA "GET / HTTP/1.0" FFCRLF FFCRLF
	ffhttp_reqinit(&r);
	x(FFHTTP_OK == ffhttp_reqparse(&r, FFSTR(DATA)));
	x(FFHTTP_DONE == ffhttp_reqparsehdrs(&r, FFSTR(DATA)));
	x(!r.h.http11 && r.h.ver == 0x0100);
	x(r.h.conn_close && !r.accept_chunked);
	ffhttp_reqfree(&r);
#undef DATA

#define DATA "GET / HTTP/1.0" FFCRLF \
	"Connection: keep-alive" FFCRLF \
	"Accept-Encoding: compress;q=0.5, gzip;q=1" FFCRLF \
	"TE: deflate, chunked" FFCRLF \
	FFCRLF

	ffhttp_reqinit(&r);
	x(FFHTTP_OK == ffhttp_reqparse(&r, FFSTR(DATA)));
	x(FFHTTP_DONE == ffhttp_reqparsehdrs(&r, FFSTR(DATA)));
	x(!r.h.conn_close && r.accept_chunked);
	x(r.accept_gzip);
	ffhttp_reqfree(&r);
#undef DATA

#define DATA "GET / HTTP/1.0" FFCRLF \
	"Content-length: 123" FFCRLF \
	"Content-length: 1234" FFCRLF \
	FFCRLF

	ffhttp_reqinit(&r);
	x(FFHTTP_OK == ffhttp_reqparse(&r, FFSTR(DATA)));
	x(FFHTTP_EDUPHDR == ffhttp_reqparsehdrs(&r, FFSTR(DATA)));
	ffhttp_reqfree(&r);
#undef DATA

#define DATA "DELETE / HTTP/1.1" FFCRLF FFCRLF
	ffhttp_reqinit(&r);
	x(FFHTTP_OK == ffhttp_reqparse(&r, FFSTR(DATA)));
	x(r.method == FFHTTP_DELETE);
	x(FFHTTP_EHOST11 == ffhttp_reqparsehdrs(&r, FFSTR(DATA)));
	ffhttp_reqfree(&r);
#undef DATA

	ffhttp_reqinit(&r);
	x(FFHTTP_OK == ffhttp_reqparse(&r, FFSTR("METHOD ?& HTTP/1.1" FFCRLF)));
	x(r.method == FFHTTP_MUKN);
	ffhttp_reqfree(&r);

#define DATA "GET * HTTP/1.1"
	ffhttp_reqinit(&r);
	x((FFHTTP_EURLPARSE | FFURL_EHOST) == ffhttp_reqparse(&r, FFSTR(DATA FFCRLF FFCRLF)));
	s = ffhttp_firstline(&r.h);
	x(ffstr_eqcz(&s, DATA));
	ffhttp_reqfree(&r);
#undef DATA

	ffhttp_reqinit(&r);
	x(FFHTTP_EDUPHDR == ffhttp_reqparsehdrs(&r, FFSTR("if-none-match: \"123\"" FFCRLF
		"if-none-match: \"456\"" FFCRLF)));
	ffhttp_reqfree(&r);

	x(NULL != ffhttp_errstr(FFHTTP_ETOOLARGE));

	return 0;
}

static int test_ver()
{
	ffhttp_response r;
	ffstr s;

	ffhttp_respinit(&r);
	x(FFHTTP_OK == ffhttp_respparse(&r, FFSTR("HTTP/255.255 200 OK" FFCRLF), 0));
	x(r.h.http11 && r.h.ver == 0xffff);

	ffhttp_respinit(&r);
	x(FFHTTP_OK == ffhttp_respparse(&r, FFSTR("HTTP/1.255 200 OK" FFCRLF), 0));
	x(r.h.http11 && r.h.ver == 0x01ff);

#define DATA "HTTP/256.1 200 OK"
	ffhttp_respinit(&r);
	x(FFHTTP_EVERSION == ffhttp_respparse(&r, FFSTR(DATA FFCRLF), 0));
	s = ffhttp_firstline(&r.h);
	x(ffstr_eqcz(&s, DATA));
#undef DATA

	ffhttp_respinit(&r);
	x(FFHTTP_EVERSION == ffhttp_respparse(&r, FFSTR("HTTP/1.256 200 OK" FFCRLF), 0));
	ffhttp_respinit(&r);
	x(FFHTTP_EVERSION == ffhttp_respparse(&r, FFSTR("HTTP/-1.1 200 OK" FFCRLF), 0));
	ffhttp_respinit(&r);
	x(FFHTTP_EVERSION == ffhttp_respparse(&r, FFSTR("HTTP/1.-1 200 OK" FFCRLF), 0));
	ffhttp_respinit(&r);
	x(FFHTTP_EVERSION == ffhttp_respparse(&r, FFSTR("HTTP/ 1.1 200 OK" FFCRLF), 0));

	return 0;
}

static int test_resp()
{
	ffstr s;
	ffhttp_response r;

	test_ver();

#define DATA "HTTP/1.1 200 OK" FFCRLF \
	"Connection: close" FFCRLF FFCRLF

	ffhttp_respinit(&r);
	x(FFHTTP_OK == ffhttp_respparse(&r, FFSTR(DATA), 0));
	x(FFHTTP_DONE == ffhttp_respparsehdrs(&r, FFSTR(DATA)));
	s = ffhttp_firstline(&r.h);
	x(ffstr_eqcz(&s, "HTTP/1.1 200 OK"));
	s = ffhttp_hdrs(&r.h);
	x(ffstr_eqcz(&s, "Connection: close" FFCRLF FFCRLF));
	s = ffhttp_respstatus(&r);
	x(ffstr_eqcz(&s, "200 OK"));
	x(r.h.ver == 0x0101);
	x(r.h.has_body && r.h.body_conn_close);
	ffhttp_respfree(&r);
#undef DATA

#define DATA "HTTP/1.1 304 Not Modified" FFCRLF \
	"Content-length: 123" FFCRLF FFCRLF

	ffhttp_respinit(&r);
	x(FFHTTP_OK == ffhttp_respparse(&r, FFSTR(DATA), 0));
	x(FFHTTP_DONE == ffhttp_respparsehdrs(&r, FFSTR(DATA)));
	x(!r.h.has_body && !r.h.body_conn_close && r.h.cont_len != -1);
	ffhttp_respfree(&r);
#undef DATA

	ffhttp_respinit(&r);
	x(FFHTTP_ESTATUS == ffhttp_respparse(&r, FFSTR("HTTP/1.1 0" FFCRLF), 0));

	ffhttp_respinit(&r);
	x(FFHTTP_ESTATUS == ffhttp_respparse(&r, FFSTR("HTTP/1.1 20 OK" FFCRLF), 0));

#define DATA "HTTP/1.1 2000 OK"
	ffhttp_respinit(&r);
	x(FFHTTP_ESTATUS == ffhttp_respparse(&r, FFSTR(DATA FFCRLF), 0));
	s = ffhttp_firstline(&r.h);
	x(ffstr_eqcz(&s, DATA));
#undef DATA

	ffhttp_respinit(&r);
	x(FFHTTP_ESTATUS == ffhttp_respparse(&r, FFSTR("HTTP/1.1 " FFCRLF), 0));

	ffhttp_respinit(&r);
	x(FFHTTP_EVERSION == ffhttp_respparse(&r, FFSTR("ICY 200 OK" FFCRLF), 0));

	ffhttp_respinit(&r);
	x(FFHTTP_OK == ffhttp_respparse(&r, FFSTR("ICY 200 OK" FFCRLF), FFHTTP_IGN_STATUS_PROTO));

	return 0;
}

static int test_hdrs()
{
	ffhttp_hdr h;
	int i;

#define HDRS "age: ageval" FFCRLF \
	"server:serverval" FFCRLF \
	FFCRLF
	ffhttp_inithdr(&h);
	i = 0;

	while (FFHTTP_OK == ffhttp_nexthdr(&h, FFSTR(HDRS))) {
		ffstr key = ffrang_get(&h.key, HDRS);
		ffstr val = ffrang_get(&h.val, HDRS);

		switch (h.ihdr) {
		case FFHTTP_AGE:
			i |= 1;
			x(ffstr_eqcz(&key, "age"));
			x(ffstr_eqcz(&val, "ageval"));
			break;
		case FFHTTP_SERVER:
			i |= 2;
			x(ffstr_eqcz(&key, "server"));
			x(ffstr_eqcz(&val, "serverval"));
			break;
		}
	}
	x(i == (1 | 2));
#undef HDRS

#define HDRS "-hdr:val" FFCRLF \
	FFCRLF
	ffhttp_inithdr(&h);
	x(FFHTTP_EHDRKEY == ffhttp_nexthdr(&h, FFSTR(HDRS)));
#undef HDRS

#define HDRS "hdr" FFCRLF \
	FFCRLF
	ffhttp_inithdr(&h);
	x(FFHTTP_ENOVAL == ffhttp_nexthdr(&h, FFSTR(HDRS)));
#undef HDRS

	{
		ffhttp_cachectl cctl = { 0 };
		x((FFHTTP_CACH_NOCACHE | FFHTTP_CACH_PUBLIC | FFHTTP_CACH_PRIVATE
			| FFHTTP_CACH_MAXAGE | FFHTTP_CACH_SMAXAGE)
			== ffhttp_parsecachctl(&cctl, FFSTR(
			"asdf, NO-CACHE, no-store\0, public, private"
			", max-age=123456, s-max-age=876543, s-max-age=123sdf")));
		x(cctl.maxage == 123456);
		x(cctl.smaxage == 876543);
	}

	return 0;
}

static int test_findhdr()
{
	char buf[4096];
	char *pbuf = buf;
	char *end = buf + FFCNT(buf);
	ffstr key, val;
	size_t i;
	ffhttp_headers h;
	int r;
	int ihdr;

	for (i = 1;  i < FFHTTP_HLAST;  i++) {
		pbuf += ffs_fmt(pbuf, end, "%S: %u\r\n"
			, &ffhttp_shdr[i], i);
	}
	pbuf = ffs_copyz(pbuf, end, "\r\n");

	ffhttp_init(&h);

	do {
		r = ffhttp_parsehdr(&h, buf, pbuf - buf);
	} while (r == FFHTTP_OK);
	x(r == FFHTTP_DONE);

	for (i = 1;  i < FFHTTP_HLAST;  i++) {
		int n;
		x(0 != ffhttp_findhdr(&h, ffhttp_shdr[i].ptr, ffhttp_shdr[i].len, &val));
		ffs_toint(val.ptr, val.len, &n, FFS_INT32);
		x(i == n);
	}

	for (i = 1;  ihdr = ffhttp_gethdr(&h, (int)i - 1, &key, &val), ihdr != FFHTTP_DONE;  i++) {
		int n;

		x(ihdr == i);
		x(ffstr_eq2(&key, &ffhttp_shdr[i]));

		ffs_toint(val.ptr, val.len, &n, FFS_INT32);
		x(i == n);
	}

	ffhttp_fin(&h);
	return 0;
}

static int test_cook()
{
	ffhttp_cook c;

	ffhttp_cookinit(&c, NULL, 0);
	ffhttp_addrequest(&c, FFSTR("GET"), FFSTR("/someurl"));
	ffhttp_addhdr(&c, FFSTR("my-hdr"), FFSTR("my value"));
	ffhttp_addihdr(&c, FFHTTP_LOCATION, FFSTR("my location"));
	c.cont_len = 123456;
	c.conn_close = 1;
	ffstr_setcz(&c.trans_enc, "chunked");
	ffstr_setcz(&c.cont_type, "text/plain");
	ffhttp_cookflush(&c);
	x(0 == ffhttp_cookfin(&c));
	x(ffstr_eqcz(&c.buf,
		"GET /someurl HTTP/1.1" FFCRLF
		"my-hdr: my value" FFCRLF
		"Location: my location" FFCRLF
		"Content-Type: text/plain" FFCRLF
		"Transfer-Encoding: chunked" FFCRLF
		"Content-Length: 123456" FFCRLF
		"Connection: close" FFCRLF
		FFCRLF));

	ffhttp_cookreset(&c);
	ffhttp_setstatus(&c, FFHTTP_502_BAD_GATEWAY);
	ffhttp_addstatus(&c);
	x(ffstr_eqcz(&c.buf, "HTTP/1.1 502 Bad Gateway" FFCRLF));

	ffhttp_cookreset(&c);
	ffhttp_setstatus4(&c, 999, FFSTR("999 some status"));
	ffhttp_addstatus(&c);
	ffhttp_cookflush(&c);
	x(0 == ffhttp_cookfin(&c));
	x(ffstr_eqcz(&c.buf,
		"HTTP/1.1 999 some status" FFCRLF
		FFCRLF));
	ffhttp_cookdestroy(&c);

	return 0;
}

static int test_chunked()
{
	ffhttp_chunked c;
	ffstr dst;
	size_t n;
	ffhttp_chunkinit(&c);

	n = 1;
	x(ffhttp_chunkparse(&c, "1", &n, &dst) == FFHTTP_MORE);
	n = 2;
	x(ffhttp_chunkparse(&c, "2\r", &n, &dst) == FFHTTP_MORE);
	n = 11;
	x(ffhttp_chunkparse(&c, "\n1234567890", &n, &dst) == FFHTTP_OK && n == 11 && ffstr_eqcz(&dst, "1234567890"));
	n = 9;
	x(ffhttp_chunkparse(&c, "98765432\r", &n, &dst) == FFHTTP_OK && n == 8 && ffstr_eqcz(&dst, "98765432"));
	n = 1;
	x(ffhttp_chunkparse(&c, "\r", &n, &dst) == FFHTTP_MORE);
	n = 3 + 3 + 3;
	x(ffhttp_chunkparse(&c, "\n3\nhey\n0\n", &n, &dst) == FFHTTP_OK && n == 3+3 && ffstr_eqcz(&dst, "hey"));
	n = 3;
	x(ffhttp_chunkparse(&c, "\n0\n", &n, &dst) == FFHTTP_MORE);
	n = 1 + 5;
	x(ffhttp_chunkparse(&c, "\n12345", &n, &dst) == FFHTTP_DONE && n == 1);

	ffhttp_chunkinit(&c);
	n = 1;
	x(ffhttp_chunkparse(&c, "x", &n, &dst) == FFHTTP_EHDRVAL && n == 0);

	ffhttp_chunkinit(&c);
	n = 4;
	x(ffhttp_chunkparse(&c, "123x", &n, &dst) == FFHTTP_EHDRVAL && n == 3);

	ffhttp_chunkinit(&c);
	n = 5;
	x(ffhttp_chunkparse(&c, "123\rx", &n, &dst) == FFHTTP_EEOL && n == 4);
	return 0;
}

static int test_condnl()
{
	ffstr ifnonmatch;

	ffstr_setcz(&ifnonmatch, "\"1234\", \"12345\"");
	x(0 == ffhttp_ifnonematch(FFSTR("\"12345\""), &ifnonmatch));
	x(1 == ffhttp_ifnonematch(FFSTR("\"123456\""), &ifnonmatch));
	return 0;
}

static int test_range()
{
	uint64 sz;

	sz = 789;
	x(123 == ffhttp_range(FFSTR("123-456"), &sz) && sz == 456 - 123 + 1);

	sz = 789;
	x(0 == ffhttp_range(FFSTR("0-0"), &sz) && sz == 1);

	sz = 789;
	x(789 - 456 == ffhttp_range(FFSTR("-456"), &sz) && sz == 456);

	sz = 789;
	x(0 == ffhttp_range(FFSTR("-900"), &sz) && sz == 789);

	sz = 789;
	x(123 == ffhttp_range(FFSTR("123-"), &sz) && sz == 789 - 123);

	sz = 789;
	x(-1 == ffhttp_range(FFSTR("123-x"), &sz) && sz == 789);
	return 0;
}

int test_http()
{
	FFTEST_FUNC;
	ffhttp_initheaders();

	test_req();
	test_resp();
	test_hdrs();
	test_findhdr();
	test_cook();
	test_chunked();
	test_range();
	test_condnl();

	ffhttp_freeheaders();
	return 0;
}
