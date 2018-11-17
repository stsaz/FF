# Functions to work with URLs

* URL parser
* URL query string parser

Include:

	#include <FF/net/url.h>


## URL parser

Initialize:

	ffurl u;
	ffurl_init(&u);

Parse the whole input data:

	const char *data = "http://[::1]:8080/path/my%20file?query%20string";
	int r = ffurl_parse(&u, data, ffsz_len(data));
	if (r != FFURL_EOK) {
		printf("error: %s\n", ffurl_errstr(r));
		return;
	}

Parse input data by chunks:

	const char *data = ...;
	size_t len = ...;
	for (;;) {
		int r = ffurl_parse(&u, data, len);

		switch (r) {
		case FFURL_ESTOP:
			/* an unknown character has been met -
			 this means that URL string ends at byte 'u.len'
			*/
			...
			return;

		case FFURL_EOK:
		case FFURL_EMORE:
			// add more data to the buffer pointed by 'data'
			memcpy(...);
			len += ...;
			continue;

		default:
			printf("error: %s\n", ffurl_errstr(r));
			return;
		}
	}

Use parsed URL components:

	ffstr comp;

	comp = ffurl_get(&u, URL, FFURL_SCHEME); //"http"

	comp = ffurl_get(&u, URL, FFURL_FULLHOST); //"[::1]:8080"
	comp = ffurl_get(&u, URL, FFURL_HOST); //"::1"

	ffip6 ip;
	AF_INET6 == ffurl_parse_ip(&u, URL, &ip);
	// 'ip' contains binary IPv6 address

	comp = ffurl_get(&u, URL, FFURL_PORT); //"8080"
	// u.port == 8080;

	comp = ffurl_get(&u, URL, FFURL_PATHQS); // "/path/my%20file?query%20string"
	comp = ffurl_get(&u, URL, FFURL_PATH); // "/path/my%20file"
	comp = ffurl_get(&u, URL, FFURL_QS); // "query%20string"
	// u.complex == 1;
	// u.decoded_pathlen == u.pathlen - FFSLEN("20");
	// u.querystr == 1;


## URL query string parser

Initialize:

	ffurlqs uqs;
	ffurlqs_parseinit(&uqs);
	ffstr qs;
	ffstr_setz(&qs, "key=value&key2=value%20with%20space");

Get query string keys and values:

	while (qs.len != 0) {

		int r = ffurlqs_parsestr(&uqs, &qs);
		if (ffpars_iserr(r)) {
			printf("%s\n", ffpars_errstr(r));
			return;
		}

		switch (r) {
		case FFPARS_KEY:
			// use key from uqs.val
			break;

		case FFPARS_VAL:
			// use value from uqs.val
			break;

		case FFPARS_MORE:
			// incomplete input data
			return;
		}
	}

Close:

	ffurlqs_parseclose(&uqs);
