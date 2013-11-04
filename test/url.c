/**
Copyright (c) 2013 Simon Zolin
*/

#include <FFOS/test.h>
#include <FF/url.h>

#define x FFTEST_BOOL

static int test_urldecode()
{
	char decoded[255];
	size_t n;

	n = ffuri_decode(decoded, FFCNT(decoded), FFSTR("/path/my%20file"));
	x(n == FFSLEN("/path/my file") && 0 == ffs_cmp(decoded, "/path/my file", n));

	x(0 == ffuri_decode(decoded, FFCNT(decoded), FFSTR("/path/my%2zfile")));
	x(0 == ffuri_decode(decoded, FFCNT(decoded), FFSTR("/path/my%20space%2")));

	n = ffuri_decode(decoded, FFCNT(decoded), FFSTR("/1/2/3/..%2f../%2e%2e/4"));
	x(n == FFSLEN("/4") && 0 == ffs_cmp(decoded, "/4", n));

	n = ffuri_decode(decoded, FFCNT(decoded), FFSTR("/1/2/3/..%2f../%2e%2e/4/."));
	x(n == FFSLEN("/4/") && 0 == ffs_cmp(decoded, "/4/", n));

	n = ffuri_decode(decoded, FFCNT(decoded), FFSTR("/1/2/3/..%2f../%2e%2e/4/./"));
	x(n == FFSLEN("/4/") && 0 == ffs_cmp(decoded, "/4/", n));

	x(0 == ffuri_decode(decoded, FFCNT(decoded), FFSTR("/1/2/3/../../../..")));
	x(0 == ffuri_decode(decoded, FFCNT(decoded), FFSTR("/%001")));
	x(0 == ffuri_decode(decoded, FFCNT(decoded), FFSTR("/\x01")));

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
	x(ffstr_eq(&comp, FFSTR("[::1]:8080")));

	comp = ffurl_get(&u, URL, FFURL_SCHEME);
	x(ffstr_eq(&comp, FFSTR("http")));

	comp = ffurl_get(&u, URL, FFURL_HOST);
	x(ffstr_eq(&comp, FFSTR("::1")));

	comp = ffurl_get(&u, URL, FFURL_PORT);
	x(ffstr_eq(&comp, FFSTR("8080")));
	x(u.port == 8080);

	comp = ffurl_get(&u, URL, FFURL_PATH);
	x(ffstr_eq(&comp, FFSTR("/path/my%20file")));
	x(u.complex == 1);
	x(u.decoded_pathlen == u.pathlen - FFSLEN("20"));

	comp = ffurl_get(&u, URL, FFURL_QS);
	x(ffstr_eq(&comp, FFSTR("query%20string")));
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
	x(ffstr_eq(&comp, FFSTR("[::1]")));
	comp = ffurl_get(&u, URL, FFURL_HOST);
	x(ffstr_eq(&comp, FFSTR("::1")));
	comp = ffurl_get(&u, URL, FFURL_PORT);
	x(ffstr_eq(&comp, FFSTR("")));
	x(u.port == 0);
	comp = ffurl_get(&u, URL, FFURL_PATH);
	x(ffstr_eq(&comp, FFSTR("")));
	comp = ffurl_get(&u, URL, FFURL_QS);
	x(ffstr_eq(&comp, FFSTR("")));
#undef URL

#define URL "http://host"
	ffurl_init(&u);
	x(FFURL_EOK == ffurl_parse(&u, FFSTR("http://host")));
	comp = ffurl_get(&u, URL, FFURL_FULLHOST);
	x(ffstr_eq(&comp, FFSTR("host")));
	comp = ffurl_get(&u, URL, FFURL_HOST);
	x(ffstr_eq(&comp, FFSTR("host")));
	comp = ffurl_get(&u, URL, FFURL_PORT);
	x(ffstr_eq(&comp, FFSTR("")));
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
	x(ffstr_eq(&comp, FFSTR("[::1]:8")));
	comp = ffurl_get(&u, URL, FFURL_HOST);
	x(ffstr_eq(&comp, FFSTR("::1")));
	comp = ffurl_get(&u, URL, FFURL_PORT);
	x(ffstr_eq(&comp, FFSTR("8")));
	x(u.port == 8);
#undef URL

#define URL "http://host:8"
	ffurl_init(&u);
	x(FFURL_EOK == ffurl_parse(&u, FFSTR(URL)));
	comp = ffurl_get(&u, URL, FFURL_FULLHOST);
	x(ffstr_eq(&comp, FFSTR("host:8")));
	comp = ffurl_get(&u, URL, FFURL_HOST);
	x(ffstr_eq(&comp, FFSTR("host")));
	comp = ffurl_get(&u, URL, FFURL_PORT);
	x(ffstr_eq(&comp, FFSTR("8")));
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
	x(ffstr_eq(&comp, FFSTR("ho")));

	ffurl_init(&u);
	x(FFURL_EIP6 == ffurl_parse(&u, FFSTR("http://[::1\0]")));

	ffurl_init(&u);
	x(FFURL_ESTOP == ffurl_parse(&u, FFSTR("http://[::1]a")));
	comp = ffurl_get(&u, "http://[::1]a", FFURL_FULLHOST);
	x(ffstr_eq(&comp, FFSTR("[::1]")));

	ffurl_init(&u);
	x(FFURL_ETOOLARGE == ffurl_parse(&u, FFSTR("http://[::1]:80800")));

	ffurl_init(&u);
	x(FFURL_ESTOP == ffurl_parse(&u, FFSTR("http://[::1]:80\080")));
	x(u.port == 80);

	ffurl_init(&u);
	x(FFURL_ESTOP == ffurl_parse(&u, FFSTR("http://[::1]:8080/fi\0le")));
	comp = ffurl_get(&u, "http://[::1]:8080/fi\0le", FFURL_PATH);
	x(ffstr_eq(&comp, FFSTR("/fi")));

	ffurl_init(&u);
	x(FFURL_EPATH == ffurl_parse(&u, FFSTR("http://[::1]:8080/fi%2zle")));

	ffurl_init(&u);
	x(FFURL_EOK == ffurl_parse(&u, FFSTR("hostname")));
	x(u.ipv6 == 0);
	comp = ffurl_get(&u, "hostname", FFURL_FULLHOST);
	x(ffstr_eq(&comp, FFSTR("hostname")));
	comp = ffurl_get(&u, "hostname", FFURL_HOST);
	x(ffstr_eq(&comp, FFSTR("hostname")));

#define URL "hostname:8080"
	ffurl_init(&u);
	x(FFURL_EOK == ffurl_parse(&u, FFSTR(URL)));
	comp = ffurl_get(&u, URL, FFURL_FULLHOST);
	x(ffstr_eq(&comp, FFSTR("hostname:8080")));
	comp = ffurl_get(&u, URL, FFURL_HOST);
	x(ffstr_eq(&comp, FFSTR("hostname")));
	comp = ffurl_get(&u, URL, FFURL_PORT);
	x(ffstr_eq(&comp, FFSTR("8080")));
	x(u.port == 8080);
#undef URL

	ffurl_init(&u);
	x(FFURL_EOK == ffurl_parse(&u, FFSTR("[::1]:8080")));
	x(u.ipv6 == 1);

#define URL "/path/file"
	ffurl_init(&u);
	x(FFURL_EOK == ffurl_parse(&u, FFSTR(URL)));
	comp = ffurl_get(&u, URL, FFURL_FULLHOST);
	x(ffstr_eq(&comp, FFSTR("")));
	comp = ffurl_get(&u, URL, FFURL_HOST);
	x(ffstr_eq(&comp, FFSTR("")));
	comp = ffurl_get(&u, URL, FFURL_PORT);
	x(ffstr_eq(&comp, FFSTR("")));
	comp = ffurl_get(&u, URL, FFURL_PATH);
	x(ffstr_eq(&comp, FFSTR("/path/file")));
	comp = ffurl_get(&u, URL, FFURL_PATHQS);
	x(ffstr_eq(&comp, FFSTR("/path/file")));
	comp = ffurl_get(&u, URL, FFURL_QS);
	x(ffstr_eq(&comp, FFSTR("")));
#undef URL

	(void)ffurl_errstr(FFURL_EPATH);

	test_urldecode();

	return 0;
}
