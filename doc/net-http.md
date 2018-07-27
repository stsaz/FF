# HTTP

* HTTP request reader
* HTTP response reader
* HTTP 'Transfer-Encoding: chunked' reader


Include:

	#include <FF/net/http.h>


## HTTP reader

HTTP reader uses a global hash table to check for several standard headers during the parsing process.
It must be initialized and uninitialized once per module.

	ffhttp_initheaders();
	/*
	application code
	*/
	ffhttp_freeheaders();


## HTTP request reader

An example of how to parse HTTP request data:

	int r;
	ffhttp_request r;
	ffhttp_req_init(&r);

	char data[] = ...;
	size_t length = ...;

	for (;;) {

		r = ffhttp_req(&r, data, length);

		switch (r) {
		case FFHTTP_DONE:
			break;

		case FFHTTP_MORE:
			length = append_more(data, ...);
			continue;

		default:
			printf("%s", ffhttp_errstr(r));
			return;
		}
		break;
	}

Use elements from request line:

	ffstr method = ffhttp_req_method(&r);
	ffstr path_decoded = ffhttp_req_path(&r);
	ffstr path_original = ffhttp_req_url(&r, FFURL_PATHQS);
	ffstr host = ffhttp_req_host(&r);
	ffstr version_string = ffhttp_req_verstr(&r);

Get request line and headers as a string:

	ffstr request_line = ffhttp_firstline(&r.h);
	ffstr headers = ffhttp_hdrs(&r.h);

Get value for a specific header field:

	ffstr connection_value;
	if (!ffhttp_findhdr(&r.h, "Connection", 12, &connection_value))
		// there's no "Connection" header field
	else
		// the value of "Connection" is stored in 'connection_value'

Traverse header fields:

	for (uint i = 0;  ;  i++) {
		ffstr key, value;
		if (FFHTTP_DONE == ffhttp_gethdr(&r.h, i, &key, &value))
			break;
		// 'key' contains a header field name
		// and 'value' contains its value
	}

Close HTTP reader object:

	ffhttp_req_free(&r);


## HTTP response reader

An example of how to parse HTTP response data:

	int r;
	ffhttp_response r;
	ffhttp_resp_init(&r);

	char data[] = ...;
	size_t length = ...;

	for (;;) {

		r = ffhttp_resp(&r, data, length);

		switch (r) {
		case FFHTTP_DONE:
			break;

		case FFHTTP_MORE:
			length = append_more(data, ...);
			continue;

		default:
			printf("%s", ffhttp_errstr(r));
			return;
		}
		break;
	}

Use elements from response line:

	uint status_code = ffhttp_resp_code(&r);
	ffstr status_code_and_message = ffhttp_resp_status(&r);
	ffstr status_message = ffhttp_resp_msg(&r);
	ffstr version_string = ffhttp_resp_verstr(&r);

Close HTTP reader object:

	ffhttp_resp_free(&r);


## HTTP 'Transfer-Encoding: chunked' reader

	int r;
	ffhttp_chunked c;
	ffhttp_chunkinit(&c);
	ffstr data = ...;
	ffstr decoded_body;

	for (;;) {

		r = ffhttp_chunkparse_str(&c, &data, &decoded_body);

		switch (r) {
		case FFHTTP_OK:
			process(decoded_body);
			data = ...;
			continue;

		case FFHTTP_DONE:
			break;

		case FFHTTP_MORE:
			data = ...;
			continue;
		}
		break;
	}
