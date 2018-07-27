/** WebSocket reader/writer.
Copyright (c) 2018 Simon Zolin
*/

/*
Handshake request:
 GET /path HTTP/1.1
 Host: server.example.com
 Upgrade: websocket
 Connection: Upgrade
 Sec-WebSocket-Key: (base64-data)
 Origin: http://url
 Sec-WebSocket-Protocol: (proto1), (proto2)
 Sec-WebSocket-Version: 13

Handshake response:
 HTTP/1.1 101 Switching Protocols
 Upgrade: websocket
 Connection: Upgrade
 Sec-WebSocket-Accept: (base64-data)
 Sec-WebSocket-Protocol: (proto1) | (proto2)

Handshake response in case client's version isn't supported:
 HTTP/1.1 426 Upgrade Required
 Sec-WebSocket-Version: (supported-version)

(msg(TEXT | BIN) [msg(CONT)... msg(CONT+FIN)])...
msg(CLOSE)
*/

#pragma once

#include <FF/array.h>


enum FFWEBSKT_R {
	FFWEBSKT_RMORE,
	FFWEBSKT_RERR,
	FFWEBSKT_RMSG, // message header is complete
	FFWEBSKT_RDATA, // have more output data
	FFWEBSKT_RDATA_FIN, // message body is complete
};

enum FFWEBSKT_OP {
	FFWEBSKT_OP_CONT = 0,
	FFWEBSKT_OP_TEXT = 1,
	FFWEBSKT_OP_BIN = 2,
	FFWEBSKT_OP_CLOSE = 8,
	FFWEBSKT_OP_PING,
	FFWEBSKT_OP_PONG,
};

typedef struct ffwebskt {
	uint state;
	uint nxstate;

	uint gathlen;
	uint64 datalen;
	uint mask_off;
	uint mask;
	uint op; //enum FFWEBSKT_OP
	uint cont :1;
	uint server :1;

	ffstr in;
	ffstr out;

	char buf[4096]; //holds value for "Sec-WebSocket-Accept", or a message header, or decoded data
	uint nbuf;
} ffwebskt;

#define FFWEBSKT_HDR_KEY  "Sec-WebSocket-Key"
#define FFWEBSKT_HDR_ACCEPT  "Sec-WebSocket-Accept"
#define FFWEBSKT_HDR_VER  "Sec-WebSocket-Version"
#define FFWEBSKT_HDR_PROTO  "Sec-WebSocket-Protocol"

/** Get server security key.
@clientkey: value of HTTP header Sec-WebSocket-Key
Call ffwebskt_body() to get output data. */
FF_EXTN void ffwebskt_accept(struct ffwebskt *w, const ffstr *clientkey);

/** Check whether client's version is supported.
Return 0 on success. */
FF_EXTN int ffwebskt_accept_ver(struct ffwebskt *w, const ffstr *ver);

/** Set input data. */
#define ffwebskt_input(w, d, n)  ffstr_set(&(w)->in, d, n)

/** Get message opcode after FFWEBSKT_RMSG is returned. */
#define ffwebskt_opcode(w)  ((w)->op)

/** Get message body length after FFWEBSKT_RMSG is returned. */
#define ffwebskt_datalen(w)  ((w)->datalen)

/** Get message body after FFWEBSKT_RDATA is returned. */
#define ffwebskt_body(w)  ((w)->out)

/** Parse data.
Call ffwebskt_input() to set input data.
Return enum FFWEBSKT_R. */
FF_EXTN int ffwebskt_parse(struct ffwebskt *w);


typedef struct ffwebskt_cook {
	ffstr in;
	ffstr out;
	uint mask;
	char buf[4096];
} ffwebskt_cook;

/** Get message header.
Call ffwebskt_input() to set input data.
 Data must be valid until ffwebskt_writenext() returns FFWEBSKT_RDATA_FIN.
@op: enum FFWEBSKT_OP
Return enum FFWEBSKT_R. */
FF_EXTN int ffwebskt_newmsg(ffwebskt_cook *w, uint mask_key, uint op);

/** Get message body.
Return enum FFWEBSKT_R. */
FF_EXTN int ffwebskt_writenext(ffwebskt_cook *w);
