/**
Copyright (c) 2013 Simon Zolin
*/

#include <FFOS/test.h>
#include <FFOS/socket.h>
#include <FFOS/process.h>
#include <FFOS/random.h>
#include <FF/net/url.h>
#include <FF/data/parse.h>


static int test_urldecode()
{
	char decoded[255];
	char buf[255];
	size_t n;
	ffstr s;
	s.ptr = buf;

	n = ffuri_decode(decoded, FFCNT(decoded), FFSTR("/path/my%20file"), 0);
	x(n == FFSLEN("/path/my file") && 0 == ffmemcmp(decoded, "/path/my file", n));

	x(0 == ffuri_decode(decoded, FFCNT(decoded), FFSTR("/path/my%2zfile"), 0));
	x(0 == ffuri_decode(decoded, FFCNT(decoded), FFSTR("/path/my%20space%2"), 0));

	n = ffuri_decode(decoded, FFCNT(decoded), FFSTR("/1/2/3/..%2f../%2e%2e/4"), FFURI_DEC_NORM_PATH);
	x(n == FFSLEN("/4") && 0 == ffmemcmp(decoded, "/4", n));

	n = ffuri_decode(decoded, FFCNT(decoded), FFSTR("/1/2/3/..%2f../%2e%2e/4/."), FFURI_DEC_NORM_PATH);
	x(n == FFSLEN("/4/") && 0 == ffmemcmp(decoded, "/4/", n));

	n = ffuri_decode(decoded, FFCNT(decoded), FFSTR("/1/2/3/..%2f../%2e%2e/4/./"), FFURI_DEC_NORM_PATH);
	x(n == FFSLEN("/4/") && 0 == ffmemcmp(decoded, "/4/", n));

	x(0 == ffuri_decode(decoded, FFCNT(decoded), FFSTR("/1/2/3/../../../.."), FFURI_DEC_NORM_PATH));
	x(0 == ffuri_decode(decoded, FFCNT(decoded), FFSTR("/%"), 0));
	x(0 == ffuri_decode(decoded, FFCNT(decoded), FFSTR("/%1"), 0));
	x(0 == ffuri_decode(decoded, FFCNT(decoded), FFSTR("/%001"), 0));
	x(0 == ffuri_decode(decoded, FFCNT(decoded), FFSTR("/\x01"), 0));


	x(-(ssize_t)(FFSLEN("host/path?%# \x00\xff")-1) == ffuri_escape(buf
		, FFSLEN("host/path%3F%25%23%20%00%FF") - 1, FFSTR("host/path?%# \x00\xff"), FFURI_ESC_WHOLE));

	x(FFSLEN("http://host/path%3F%25%23%20%00%FF") == ffuri_escape(NULL, 0, FFSTR("http://host/path?%# \x00\xff"), FFURI_ESC_WHOLE));
	s.len = ffuri_escape(buf, FFCNT(buf), FFSTR("http://host/path?%# \x00\xff"), FFURI_ESC_WHOLE);
	x(ffstr_eqcz(&s, "http://host/path%3F%25%23%20%00%FF"));

	s.len = ffuri_escape(buf, FFCNT(buf), FFSTR("?%# \x00\xff/\\"), FFURI_ESC_PATHSEG);
	x(ffstr_eqcz(&s, "%3F%25%23%20%00%FF%2F%5C"));

	s.len = ffuri_escape(buf, FFCNT(buf), FFSTR("qs?/#&яqs"), FFURI_ESC_QSSEG);
	x(ffstr_eqz(&s, "qs?/%23%26%D1%8Fqs"));

	return 0;
}


struct qs_data_s {
	ffstr empty_val
		, v
		, v1
		, v2;
};

static const ffpars_arg qs_kv_args[] = {
	{ "",  FFPARS_TSTR | FFPARS_FCOPY,  FFPARS_DSTOFF(struct qs_data_s, v) }
	, { "key0",  FFPARS_TSTR | FFPARS_FCOPY,  FFPARS_DSTOFF(struct qs_data_s, empty_val) }
	, { "key1",  FFPARS_TSTR | FFPARS_FCOPY,  FFPARS_DSTOFF(struct qs_data_s, v1) }
	, { "my key2",  FFPARS_TSTR | FFPARS_FCOPY,  FFPARS_DSTOFF(struct qs_data_s, v2) }
};

static int test_qs_parse(void)
{
	ffparser_schem ps;
	ffurlqs p;
	ffstr input;
	int rc;
	size_t len;
	struct qs_data_s qsd;
	ffpars_ctx psctx = {0};
	FFTEST_FUNC;

	ffstr_setcz(&qsd.empty_val, "non-empty");
	ffpars_setargs(&psctx, &qsd, qs_kv_args, FFCNT(qs_kv_args));
	ffurlqs_scheminit(&ps, &p, &psctx);
	ffstr_setcz(&input, "&=my+val&key0=&key1=my+val1&&my%20key2=my%20val2");

	while (input.len != 0) {

		len = input.len;
		rc = ffurlqs_parse(&p, input.ptr, &len);
		x(!ffpars_iserr(rc));
		ffstr_shift(&input, len);

		rc = ffurlqs_schemrun(&ps, rc);
		x(!ffpars_iserr(rc));
	}

	x(ffstr_eqcz(&qsd.v, "my val"));
	x(ffstr_eqcz(&qsd.empty_val, ""));
	x(ffstr_eqcz(&qsd.v1, "my val1"));
	x(ffstr_eqcz(&qsd.v2, "my val2"));

	x(0 == ffurlqs_schemfin(&ps));
	ffurlqs_parseclose(&p);
	ffpars_schemfree(&ps);

	ffstr_free(&qsd.v);
	ffstr_free(&qsd.empty_val);
	ffstr_free(&qsd.v1);
	ffstr_free(&qsd.v2);

	return 0;
}

int test_url()
{
	ffurl u;
	ffstr comp;

	FFTEST_FUNC;

#define URL "http://[::1]:8080/path/my%20file?query%20string#sharp"
	ffurl_init(&u);
	x(FFURL_ESTOP == ffurl_parse(&u, FFSTR(URL)));
	x(u.len == FFSLEN("http://[::1]:8080/path/my%20file?query%20string"));

	comp = ffurl_get(&u, URL, FFURL_FULLHOST);
	x(ffstr_eqcz(&comp, "[::1]:8080"));

	comp = ffurl_get(&u, URL, FFURL_SCHEME);
	x(ffstr_eqcz(&comp, "http"));

	comp = ffurl_get(&u, URL, FFURL_HOST);
	x(ffstr_eqcz(&comp, "::1"));

	comp = ffurl_get(&u, URL, FFURL_PORT);
	x(ffstr_eqcz(&comp, "8080"));
	x(u.port == 8080);

	comp = ffurl_get(&u, URL, FFURL_PATH);
	x(ffstr_eqcz(&comp, "/path/my%20file"));
	x(u.complex == 1);
	x(u.decoded_pathlen == u.pathlen - FFSLEN("20"));

	comp = ffurl_get(&u, URL, FFURL_QS);
	x(ffstr_eqcz(&comp, "query%20string"));
	x(u.querystr == 1);
#undef URL

	ffurl_init(&u);
	x(FFURL_EMORE == ffurl_parse(&u, FFSTR("http://")));

	//ffurl_init(&u);
	//x(FFURL_EMORE == ffurl_split(&u, FFSTR("http://::1")));

	ffurl_init(&u);
	x(FFURL_EMORE == ffurl_parse(&u, FFSTR("http://[::1")));

#define URL "http://[::1]"
	ffurl_init(&u);
	x(FFURL_EOK == ffurl_parse(&u, FFSTR(URL)));
	comp = ffurl_get(&u, URL, FFURL_FULLHOST);
	x(ffstr_eqcz(&comp, "[::1]"));
	comp = ffurl_get(&u, URL, FFURL_HOST);
	x(ffstr_eqcz(&comp, "::1"));
	comp = ffurl_get(&u, URL, FFURL_PORT);
	x(ffstr_eqcz(&comp, ""));
	x(u.port == 0);
	comp = ffurl_get(&u, URL, FFURL_PATH);
	x(ffstr_eqcz(&comp, ""));
	comp = ffurl_get(&u, URL, FFURL_QS);
	x(ffstr_eqcz(&comp, ""));
#undef URL

#define URL "http://host"
	ffurl_init(&u);
	x(FFURL_EOK == ffurl_parse(&u, FFSTR("http://host")));
	comp = ffurl_get(&u, URL, FFURL_FULLHOST);
	x(ffstr_eqcz(&comp, "host"));
	comp = ffurl_get(&u, URL, FFURL_HOST);
	x(ffstr_eqcz(&comp, "host"));
	comp = ffurl_get(&u, URL, FFURL_PORT);
	x(ffstr_eqcz(&comp, ""));
	x(u.port == 0);
#undef URL

	ffurl_init(&u);
	x(FFURL_EMORE == ffurl_parse(&u, FFSTR("http://")));

	ffurl_init(&u);
	x(FFURL_EMORE == ffurl_parse(&u, FFSTR("http://[::")));

	ffurl_init(&u);
	x(FFURL_EMORE == ffurl_parse(&u, FFSTR("http://[::1]:")));

	ffurl_init(&u);
	x(FFURL_EMORE == ffurl_parse(&u, FFSTR("http://host:")));

#define URL "http://[::1]:8"
	ffurl_init(&u);
	x(FFURL_EOK == ffurl_parse(&u, FFSTR(URL)));
	comp = ffurl_get(&u, URL, FFURL_FULLHOST);
	x(ffstr_eqcz(&comp, "[::1]:8"));
	comp = ffurl_get(&u, URL, FFURL_HOST);
	x(ffstr_eqcz(&comp, "::1"));
	comp = ffurl_get(&u, URL, FFURL_PORT);
	x(ffstr_eqcz(&comp, "8"));
	x(u.port == 8);
#undef URL

#define URL "http://host:8"
	ffurl_init(&u);
	x(FFURL_EOK == ffurl_parse(&u, FFSTR(URL)));
	comp = ffurl_get(&u, URL, FFURL_FULLHOST);
	x(ffstr_eqcz(&comp, "host:8"));
	comp = ffurl_get(&u, URL, FFURL_HOST);
	x(ffstr_eqcz(&comp, "host"));
	comp = ffurl_get(&u, URL, FFURL_PORT);
	x(ffstr_eqcz(&comp, "8"));
	x(u.port == 8);
#undef URL

	ffurl_init(&u);
	x(FFURL_EOK == ffurl_parse(&u, FFSTR("http://[::1]:8080/")));

	ffurl_init(&u);
	x(FFURL_EMORE == ffurl_parse(&u, FFSTR("http://[::1]:8080/file%")));

	ffurl_init(&u);
	x(FFURL_EMORE == ffurl_parse(&u, FFSTR("http://[::1]:8080/file%2")));

	ffurl_init(&u);
	x(FFURL_EOK == ffurl_parse(&u, FFSTR("http://[::1]:8080/file%20")));

	ffurl_init(&u);
	x(FFURL_ESCHEME == ffurl_parse(&u, FFSTR("http:/z")));

	ffurl_init(&u);
	x(FFURL_EHOST == ffurl_parse(&u, FFSTR("http://:8080")));

	ffurl_init(&u);
	x(FFURL_ESTOP == ffurl_parse(&u, FFSTR("http://ho\0st")));
	comp = ffurl_get(&u, "http://ho\0st", FFURL_FULLHOST);
	x(ffstr_eqcz(&comp, "ho"));

	ffurl_init(&u);
	x(FFURL_EIP6 == ffurl_parse(&u, FFSTR("http://[::1\0]")));

	ffurl_init(&u);
	x(FFURL_ESTOP == ffurl_parse(&u, FFSTR("http://[::1]a")));
	comp = ffurl_get(&u, "http://[::1]a", FFURL_FULLHOST);
	x(ffstr_eqcz(&comp, "[::1]"));

	ffurl_init(&u);
	x(FFURL_ETOOLARGE == ffurl_parse(&u, FFSTR("http://[::1]:80800")));

	ffurl_init(&u);
	x(FFURL_ESTOP == ffurl_parse(&u, FFSTR("http://[::1]:80\080")));
	x(u.port == 80);

	ffurl_init(&u);
	x(FFURL_ESTOP == ffurl_parse(&u, FFSTR("http://[::1]:8080/fi\0le")));
	comp = ffurl_get(&u, "http://[::1]:8080/fi\0le", FFURL_PATH);
	x(ffstr_eqcz(&comp, "/fi"));

	ffurl_init(&u);
	x(FFURL_EPATH == ffurl_parse(&u, FFSTR("http://[::1]:8080/fi%2zle")));

	ffurl_init(&u);
	x(FFURL_EOK == ffurl_parse(&u, FFSTR("hostname")));
	x(u.ipv6 == 0);
	comp = ffurl_get(&u, "hostname", FFURL_FULLHOST);
	x(ffstr_eqcz(&comp, "hostname"));
	comp = ffurl_get(&u, "hostname", FFURL_HOST);
	x(ffstr_eqcz(&comp, "hostname"));

#define URL "hostname:8080"
	ffurl_init(&u);
	x(FFURL_EOK == ffurl_parse(&u, FFSTR(URL)));
	comp = ffurl_get(&u, URL, FFURL_FULLHOST);
	x(ffstr_eqcz(&comp, "hostname:8080"));
	comp = ffurl_get(&u, URL, FFURL_HOST);
	x(ffstr_eqcz(&comp, "hostname"));
	comp = ffurl_get(&u, URL, FFURL_PORT);
	x(ffstr_eqcz(&comp, "8080"));
	x(u.port == 8080);
#undef URL

	ffurl_init(&u);
	x(FFURL_EOK == ffurl_parse(&u, FFSTR("[::1]:8080")));
	x(u.ipv6 == 1);

	ffurl_init(&u);
	x(FFURL_EOK == ffurl_parse(&u, FFSTR("[::1]")));
	x(u.ipv6 == 1);

	ffurl_init(&u);
	x(FFURL_EOK == ffurl_parse(&u, FFSTR("127.0.0.1:8080")));
	x(u.ipv4 == 1);

	ffurl_init(&u);
	x(FFURL_EOK == ffurl_parse(&u, FFSTR("127.0.0.1")));
	x(u.ipv4 == 1);

	ffurl_init(&u);
	x(FFURL_EOK == ffurl_parse(&u, FFSTR("127.0.0.1.com:8080")));
	x(u.ipv4 == 0);

#define URL "/path/file"
	ffurl_init(&u);
	x(FFURL_EOK == ffurl_parse(&u, FFSTR(URL)));
	comp = ffurl_get(&u, URL, FFURL_FULLHOST);
	x(ffstr_eqcz(&comp, ""));
	comp = ffurl_get(&u, URL, FFURL_HOST);
	x(ffstr_eqcz(&comp, ""));
	comp = ffurl_get(&u, URL, FFURL_PORT);
	x(ffstr_eqcz(&comp, ""));
	comp = ffurl_get(&u, URL, FFURL_PATH);
	x(ffstr_eqcz(&comp, "/path/file"));
	comp = ffurl_get(&u, URL, FFURL_PATHQS);
	x(ffstr_eqcz(&comp, "/path/file"));
	comp = ffurl_get(&u, URL, FFURL_QS);
	x(ffstr_eqcz(&comp, ""));
#undef URL

	(void)ffurl_errstr(FFURL_EPATH);

	test_urldecode();
	test_qs_parse();

	return 0;
}

int test_inchk_speed(void)
{
	FFTEST_FUNC;

	ffarr m = {0};
	ffarr_alloc(&m, 20);

	for (uint j = 0;  j != 20;  j++) {
		m.ptr[j] = ffrnd_get();
	}

	for (uint i = 0;  i != 100000000;  i++) {
		uint crc = ffip4_chksum((void*)(m.ptr), 20/4);
		if (crc == 0)
			ffps_curid();
	}

	ffarr_free(&m);
	return 0;
}

void test_domain()
{
	xieq(1, ffurl_isdomain(FFSTR("com")));
	xieq(2, ffurl_isdomain(FFSTR("1.cc")));
	xieq(3, ffurl_isdomain(FFSTR("1.2.cc")));
	xieq(3, ffurl_isdomain(FFSTR("a.b.cc")));
	xieq(3, ffurl_isdomain(FFSTR("abc.abc.abc")));
	xieq(3, ffurl_isdomain(FFSTR("a-bc.ab--c.abc")));
	xieq(2, ffurl_isdomain(FFSTR("abc.xn--p1ai")));
	xieq(2, ffurl_isdomain(FFSTR("xn--p1ai.xn--p1ai")));
	xieq(2, ffurl_isdomain(FFSTR("123456789012345678901234567890123456789012345678901234567890123.cc")));

	xieq(-1, ffurl_isdomain(FFSTR("#cc")));
	xieq(-1, ffurl_isdomain(FFSTR("a.cc#")));
	xieq(-1, ffurl_isdomain(FFSTR("abc.xn--")));
	xieq(-1, ffurl_isdomain(FFSTR("abc.xn--asd")));

	xieq(-1, ffurl_isdomain(FFSTR(".a.cc")));
	xieq(-1, ffurl_isdomain(FFSTR("a.cc.")));

	xieq(-1, ffurl_isdomain(FFSTR("-a.cc")));
	xieq(-1, ffurl_isdomain(FFSTR("a-.cc")));

	xieq(-1, ffurl_isdomain(FFSTR("a.1cc")));
	xieq(-1, ffurl_isdomain(FFSTR("a.cc1")));
	xieq(-1, ffurl_isdomain(FFSTR("a.c")));

	xieq(-1, ffurl_isdomain(FFSTR("1234567890123456789012345678901234567890123456789012345678901234.cc")));
}
