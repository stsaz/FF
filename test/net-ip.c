/**
Copyright (c) 2019 Simon Zolin
*/

#include <FF/net/url.h>
#include <FFOS/test.h>
#define x FFTEST_BOOL


static int test_ip4()
{
	ffip4 a4;
	char buf[64];
	ffstr sip;

	FFTEST_FUNC;

	sip.ptr = buf;

	x(0 == ffip4_parse(&a4, FFSTR("1.65.192.255")));
	x(!memcmp(&a4, FFSTR("\x01\x41\xc0\xff")));

	x(FFSLEN("1.65.192.255") == ffip4_parse(&a4, FFSTR("1.65.192.255.")));
	x(!memcmp(&a4, "\x01\x41\xc0\xff", 4));
	x(FFSLEN("1.65.192.2") == ffip4_parse(&a4, FFSTR("1.65.192.2/")));
	x(!memcmp(&a4, "\x01\x41\xc0\x02", 4));

	x(0 > ffip4_parse(&a4, FFSTR(".1.65.192.255")));
	x(0 > ffip4_parse(&a4, FFSTR("1.65..192.255")));
	x(0 > ffip4_parse(&a4, FFSTR("1.65.192.256")));
	x(0 > ffip4_parse(&a4, FFSTR("1.65,192.255")));

	x(24 == ffip4_parse_subnet(&a4, FFSTR("1.65.192.0/24")));
	x(!memcmp(&a4, "\x01\x41\xc0\x00", 4));
	x(0 > ffip4_parse_subnet(&a4, FFSTR("1.65.192.0/33")));
	x(0 > ffip4_parse_subnet(&a4, FFSTR("1.65.192.0/0")));
	x(0 > ffip4_parse_subnet(&a4, FFSTR("1.65.192.0/")));

	sip.len = ffip4_tostr(buf, FFCNT(buf), (void*)"\x7f\0\0\x01");
	x(ffstr_eqz(&sip, "127.0.0.1"));

	sip.len = ffip_tostr(buf, FFCNT(buf), AF_INET, (void*)"\x7f\0\0\x01", 8080);
	x(ffstr_eqz(&sip, "127.0.0.1:8080"));

	x(ffip4_mask(33, buf, FFCNT(buf)) < 0);
	sip.len = ffip4_mask(32, buf, FFCNT(buf));
	x(ffstr_eqz(&sip, "255.255.255.255"));
	sip.len = ffip4_mask(31, buf, FFCNT(buf));
	x(ffstr_eqz(&sip, "255.255.255.254"));
	sip.len = ffip4_mask(16, buf, FFCNT(buf));
	x(ffstr_eqz(&sip, "255.255.0.0"));
	sip.len = ffip4_mask(0, buf, FFCNT(buf));
	x(ffstr_eqz(&sip, "0.0.0.0"));

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

static int test_ip6()
{
	char a[16];
	char buf[64];
	ffstr sip;
	size_t i;
	ffip6 a6;

	FFTEST_FUNC;

	sip.ptr = buf;

	for (i = 0;  i < FFCNT(ip6_data);  i += 2) {
		const char *ip6 = ip6_data[i];
		const char *sip6 = ip6_data[i + 1];

		sip.len = ffip6_tostr(buf, FFCNT(buf), ip6);
		x(ffstr_eqz(&sip, sip6));
	}

	ffmem_zero(a, 16);
	a[15] = '\x01';
	sip.len = ffip_tostr(buf, FFCNT(buf), AF_INET6, a, 8080);
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
	x(0 != ffip6_parse(&a6, FFSTR("1234::1234:")));
	x(0 != ffip6_parse(&a6, FFSTR(":1234")));
	x(0 != ffip6_parse(&a6, FFSTR(":::")));
	x(0 != ffip6_parse(&a6, FFSTR("::1::")));
	x(0 != ffip6_parse(&a6, FFSTR("0:12345::")));
	x(0 != ffip6_parse(&a6, FFSTR("0:123z::")));
	x(0 != ffip6_parse(&a6, FFSTR("0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:1")));

	x(128 == ffip6_parse_subnet(&a6, FFSTR("123::abcd:0:0:6789:abcd/128")));
	x(!memcmp(&a6, "\x01\x23\0\0\0\0\xab\xcd\0\0\0\0\x67\x89\xab\xcd", 16));
	x(128 == ffip6_parse_subnet(&a6, FFSTR("123::abcd:0:0:6789:abc/128")));
	x(!memcmp(&a6, "\x01\x23\0\0\0\0\xab\xcd\0\0\0\0\x67\x89\x0a\xbc", 16));
	x(128 == ffip6_parse_subnet(&a6, FFSTR("123::/128")));
	x(128 == ffip6_parse_subnet(&a6, FFSTR("::123/128")));
	x(128 == ffip6_parse_subnet(&a6, FFSTR("::/128")));
	x(0 > ffip6_parse_subnet(&a6, FFSTR("123::abcd:0:0:6789:abcd0/128")));
	x(0 > ffip6_parse_subnet(&a6, FFSTR("123::abcd:0:0:6789:/128")));

	ffip4 a4;
	ffip4_parse(&a4, FFSTR("1.2.3.4"));
	ffip6_v4mapped_set(&a6, &a4);
	x(ffip6_v4mapped(&a6));
	x(0 == ffip4_cmp(ffip6_tov4(&a6), &a4));
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

static void test_eth(void)
{
	FFTEST_FUNC;
	ffeth mac;
	char smac[FFETH_STRLEN];
	x(0 > ffeth_parse(&mac, "12:34:56:78:ab:XX", FFETH_STRLEN));
	x(FFETH_STRLEN == ffeth_parse(&mac, "12:34:56:78:ab:CD12345", FFETH_STRLEN + 5));
	x(FFETH_STRLEN == ffeth_tostr(smac, sizeof(smac), &mac));
	x(!ffmemcmp(smac, "12:34:56:78:AB:CD", FFETH_STRLEN));
}

int test_ip()
{
	ffstr ip, port;
	x(0 == ffip_split(FFSTR("127.0.0.1"), &ip, &port)
		&& ffstr_eqz(&ip, "127.0.0.1")
		&& ffstr_eqz(&port, ""));
	x(0 == ffip_split(FFSTR("127.0.0.1:8080"), &ip, &port)
		&& ffstr_eqz(&ip, "127.0.0.1")
		&& ffstr_eqz(&port, "8080"));
	x(0 == ffip_split(FFSTR("[::1]"), &ip, &port)
		&& ffstr_eqz(&ip, "::1")
		&& ffstr_eqz(&port, ""));
	x(0 == ffip_split(FFSTR("[::1]:8080"), &ip, &port)
		&& ffstr_eqz(&ip, "::1")
		&& ffstr_eqz(&port, "8080"));
	x(0 == ffip_split(FFSTR(":8080"), &ip, &port)
		&& ffstr_eqz(&ip, "")
		&& ffstr_eqz(&port, "8080"));
	x(0 != ffip_split(FFSTR("127.0.0.1:"), &ip, &port));

	test_ip4();
	test_ip6();
	test_addr();
	test_eth();
	return 0;
}
