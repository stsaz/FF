/** Test WebSocket I/O.
Copyright (c) 2018 Simon Zolin
*/

#include <FF/net/websocket.h>
#include <FFOS/test.h>


static const char ws_key[] = "dGhlIHNhbXBsZSBub25jZQ==";
static const char ws_accept_key[] = "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=";
static const char ws_data[] = "\x81\x86\xe5\x17\xb1\x29\x88\x6e\xd5\x48\x91\x76";
static const char ws_serv_data12[] = "\x81\x02";
static const char ws_serv_data3456[] = "\x81\x04";

static void test_webskt_reader()
{
	ffwebskt w = {};

	ffstr k;
	ffstr_setz(&k, ws_key);
	ffwebskt_accept(&w, &k);
	x(w.out.len == strlen(ws_accept_key)
		&& !memcmp(w.out.ptr, ws_accept_key, strlen(ws_accept_key)));

	x(FFWEBSKT_RMORE == ffwebskt_parse(&w));
	ffwebskt_input(&w, ws_data, 3);
	x(FFWEBSKT_RMORE == ffwebskt_parse(&w));
	ffwebskt_input(&w, ws_data + 3, sizeof(ws_data) - 3);
	x(FFWEBSKT_RMSG == ffwebskt_parse(&w));
	x(ffwebskt_opcode(&w) == FFWEBSKT_OP_TEXT);
	x(ffwebskt_datalen(&w) == 6);
	x(FFWEBSKT_RDATA == ffwebskt_parse(&w));
	x(w.out.len == 6
		&& !memcmp(w.out.ptr, "mydata", 6));
	x(FFWEBSKT_RDATA_FIN == ffwebskt_parse(&w));
	x(FFWEBSKT_RMORE == ffwebskt_parse(&w));
}

static void test_webskt_writer()
{
	ffwebskt_cook w = {};

	ffwebskt_input(&w, "12", 2);
	x(FFWEBSKT_RDATA == ffwebskt_newmsg(&w, 0, FFWEBSKT_OP_TEXT));
	x(w.out.len == 2
		&& !memcmp(w.out.ptr, ws_serv_data12, 2));
	x(FFWEBSKT_RDATA == ffwebskt_writenext(&w));
	x(w.out.len == 2
		&& !memcmp(w.out.ptr, "12", 2));
	x(FFWEBSKT_RDATA_FIN == ffwebskt_writenext(&w));

	ffwebskt_input(&w, "3456", 4);
	x(FFWEBSKT_RDATA == ffwebskt_newmsg(&w, 0, FFWEBSKT_OP_TEXT));
	x(w.out.len == 2
		&& !memcmp(w.out.ptr, ws_serv_data3456, 2));
	x(FFWEBSKT_RDATA == ffwebskt_writenext(&w));
	x(w.out.len == 4
		&& !memcmp(w.out.ptr, "3456", 4));
	x(FFWEBSKT_RDATA_FIN == ffwebskt_writenext(&w));
}

int test_webskt()
{
	test_webskt_reader();
	test_webskt_writer();
	return 0;
}
