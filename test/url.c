/**
Copyright (c) 2013 Simon Zolin
*/

#include <FFOS/test.h>
#include <FFOS/socket.h>
#include <FF/net/url.h>

#define x FFTEST_BOOL

static int test_urldecode()
{
	char decoded[255];
	char buf[255];
	size_t n;
	ffstr s;
	s.ptr = buf;

	n = ffuri_decode(decoded, FFCNT(decoded), FFSTR("/path/my%20file"));
	x(n == FFSLEN("/path/my file") && 0 == ffmemcmp(decoded, "/path/my file", n));

	x(0 == ffuri_decode(decoded, FFCNT(decoded), FFSTR("/path/my%2zfile")));
	x(0 == ffuri_decode(decoded, FFCNT(decoded), FFSTR("/path/my%20space%2")));

	n = ffuri_decode(decoded, FFCNT(decoded), FFSTR("/1/2/3/..%2f../%2e%2e/4"));
	x(n == FFSLEN("/4") && 0 == ffmemcmp(decoded, "/4", n));

	n = ffuri_decode(decoded, FFCNT(decoded), FFSTR("/1/2/3/..%2f../%2e%2e/4/."));
	x(n == FFSLEN("/4/") && 0 == ffmemcmp(decoded, "/4/", n));

	n = ffuri_decode(decoded, FFCNT(decoded), FFSTR("/1/2/3/..%2f../%2e%2e/4/./"));
	x(n == FFSLEN("/4/") && 0 == ffmemcmp(decoded, "/4/", n));

	x(0 == ffuri_decode(decoded, FFCNT(decoded), FFSTR("/1/2/3/../../../..")));
	x(0 == ffuri_decode(decoded, FFCNT(decoded), FFSTR("/%001")));
	x(0 == ffuri_decode(decoded, FFCNT(decoded), FFSTR("/\x01")));


	x(-(ssize_t)(FFSLEN("host/path?%# \x00\xff")-1) == ffuri_escape(buf
		, FFSLEN("host/path%3F%25%23%20%00%FF") - 1, FFSTR("host/path?%# \x00\xff"), FFURI_ESC_WHOLE));

	x(FFSLEN("http://host/path%3F%25%23%20%00%FF") == ffuri_escape(NULL, 0, FFSTR("http://host/path?%# \x00\xff"), FFURI_ESC_WHOLE));
	s.len = ffuri_escape(buf, FFCNT(buf), FFSTR("http://host/path?%# \x00\xff"), FFURI_ESC_WHOLE);
	x(ffstr_eqcz(&s, "http://host/path%3F%25%23%20%00%FF"));

	x(FFSLEN("%3F%25%23%20%00%FF%2F%5C") == ffuri_escape(NULL, 0, FFSTR("?%# \x00\xff/\\"), FFURI_ESC_PATHSEG));
	s.len = ffuri_escape(buf, FFCNT(buf), FFSTR("?%# \x00\xff/\\"), FFURI_ESC_PATHSEG);
	x(ffstr_eqcz(&s, "%3F%25%23%20%00%FF%2F%5C"));

	return 0;
}

int test_ip4()
{
	struct in_addr a4;
	char buf[64];
	ffstr sip;

	FFTEST_FUNC;

	sip.ptr = buf;

	x(0 == ffip4_parse(&a4, FFSTR("1.65.192.255")));
	x(!memcmp(&a4, FFSTR("\x01\x41\xc0\xff")));

	x(0 != ffip4_parse(&a4, FFSTR(".1.65.192.255")));
	x(0 != ffip4_parse(&a4, FFSTR("1.65.192.255.")));
	x(0 != ffip4_parse(&a4, FFSTR("1.65..192.255")));
	x(0 != ffip4_parse(&a4, FFSTR("1.65.192.256")));
	x(0 != ffip4_parse(&a4, FFSTR("1.65.192.255.1")));
	x(0 != ffip4_parse(&a4, FFSTR("1.65,192.255")));

	sip.len = ffip4_tostr(buf, FFCNT(buf), "\x7f\0\0\x01", 4, 0);
	x(ffstr_eqz(&sip, "127.0.0.1"));

	sip.len = ffip4_tostr(buf, FFCNT(buf), "\x7f\0\0\x01", 4, 8080);
	x(ffstr_eqz(&sip, "127.0.0.1:8080"));

	return 0;
}

static const char *const ip6_data[] = {
	"\x01\x23\x45\x67\x89\0\xab\xcd\xef\x01\x23\x45\x67\x89\x0a\xbc"
	, "123:4567:8900:abcd:ef01:2345:6789:abc"

	, "\x01\x23\0\0\0\0\xab\xcd\0\0\0\0\x67\x89\x0a\xbc"
	, "123::abcd:0:0:6789:abc"

	, "\x01\x23\0\0\x12\x12\xab\xcd\0\0\0\0\x67\x89\x0a\xbc"
	, "123:0:1212:abcd::6789:abc"

	, "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
	, "::"

	, "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\x01"
	, "::1"

	, "\x01\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
	, "100::"

	, "\x01\0\0\0\0\0\0\0\0\0\0\0\0\0\0\x01"
	, "100::1"

	, "\0\0\0\x01\0\0\0\0\0\0\0\0\0\0\0\x01"
	, "0:1::1"

	, "\x01\x02\0\0\0\0\0\0\0\0\0\0\0\0\x03\x04"
	, "102::304"
};

int test_ip6()
{
	char a[16];
	char buf[64];
	ffstr sip;
	size_t i;
	struct in6_addr a6;

	FFTEST_FUNC;

	sip.ptr = buf;

	for (i = 0;  i < FFCNT(ip6_data);  i += 2) {
		const char *ip6 = ip6_data[i];
		const char *sip6 = ip6_data[i + 1];

		sip.len = ffip6_tostr(buf, FFCNT(buf), ip6, 16, 0);
		x(ffstr_eqz(&sip, sip6));
	}

	ffmem_zero(a, 16);
	a[15] = '\x01';
	sip.len = ffip6_tostr(buf, FFCNT(buf), a, 16, 8080);
	x(ffstr_eqz(&sip, "[::1]:8080"));


	{
		ffaddr a;
		ffaddr_init(&a);
		ffip6_set(&a, &in6addr_loopback);
		ffip_setport(&a, 8080);
		sip.len = ffaddr_tostr(&a, buf, FFCNT(buf), FFADDR_USEPORT);
		x(ffstr_eqz(&sip, "[::1]:8080"));
	}

	for (i = 0;  i < FFCNT(ip6_data);  i += 2) {
		const char *ip6 = ip6_data[i];
		const char *sip6 = ip6_data[i + 1];

		x(0 == ffip6_parse(&a6, sip6, strlen(sip6)));
		x(0 == memcmp(&a6, ip6, 16));
	}

	x(0 != ffip6_parse(&a6, FFSTR("1234:")));
	x(0 != ffip6_parse(&a6, FFSTR(":1234")));
	x(0 != ffip6_parse(&a6, FFSTR(":::")));
	x(0 != ffip6_parse(&a6, FFSTR("::1::")));
	x(0 != ffip6_parse(&a6, FFSTR("0:12345::")));
	x(0 != ffip6_parse(&a6, FFSTR("0:123z::")));
	x(0 != ffip6_parse(&a6, FFSTR("0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:1")));

	return 0;
}

static int test_addr()
{
	ffaddr a;
	x(0 == ffaddr_set(&a, FFSTR("127.0.0.1"), FFSTR("8080")));
	x(AF_INET == ffaddr_family(&a) && 8080 == ffip_port(&a));

	x(0 == ffaddr_set(&a, FFSTR("::1"), FFSTR("8080")));
	x(AF_INET6 == ffaddr_family(&a));

	x(0 != ffaddr_set(&a, FFSTR("::11111"), FFSTR("8080")));
	x(0 != ffaddr_set(&a, FFSTR("::1"), FFSTR("88080")));
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
	test_ip4();
	test_ip6();
	test_addr();

	return 0;
}
