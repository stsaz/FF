/**
Copyright (c) 2018 Simon Zolin
*/


#include <FF/net/websocket.h>
#include <FF/number.h>
#include <sha1/sha1.h>
#include <base64/base64.h>


enum B1 {
	WS_F_FIN = 0x80,
	WS_F_RES = 0x70, //must be 0
	WS_F_OPCODE = 0x0f, //enum FFWEBSKT_OP
};
enum B2 {
	WS_F_MASK = 0x80, //must be set for client->server data transfer
	WS_F_DATALEN = 0x7f,
};
struct ws_msg {
	byte flags_opcode; //enum B1
	byte mask_datalen; //enum B2
	// byte ext_datalen[2] if WS_F_DATALEN == 126
	// byte ext_datalen[8] if WS_F_DATALEN == 127.  ext_datalen[0] must be 0
	// byte mask_key[4] if WS_F_MASK == 1
	// byte data[]
};

enum {
	WS_MAXHDR = 2+8+4,
	WS_MAXMSG1 = 126,
	WS_MAXMSG2 = 0xffff,
	WS_MAXMSG = 0x00ffffffffffffffULL,
};

/** The version supported by this code. */
#define WEBSKT_VER  "13"

#define KEY_GUID  "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

/* base64(SHA-1(request."Sec-WebSocket-Key" KEY_GUID)) */
void ffwebskt_accept(ffwebskt *w, const ffstr *clientkey)
{
	byte buf[SHA1_LENGTH];
	sha1_ctx sha;
	sha1_init(&sha);
	sha1_update(&sha, clientkey->ptr, clientkey->len);
	sha1_update(&sha, KEY_GUID, ffsz_len(KEY_GUID));
	sha1_fin(&sha, buf);

	uint r = base64_encode(w->buf, sizeof(w->buf), buf, sizeof(buf));
	w->out.ptr = w->buf,  w->out.len = r;
	w->server = 1;
}

int ffwebskt_accept_ver(ffwebskt *w, const ffstr *ver)
{
	if (ffstr_eqz(ver, WEBSKT_VER))
		return 0;
	return -1;
}

#define GATHER(w, st, n) \
	(w)->state = R_GATHER,  (w)->nxstate = st,  (w)->gathlen = n

/* WebSocket reading algorithm:
. Gather 2 bytes
  . Determine opcode, header length, whether body is encrypted
. Gather complete header
  . Determine data length
  . Return FFWEBSKT_RMSG
. Decrypt body if necessary
. Return message body (FFWEBSKT_RDATA, FFWEBSKT_RDATA_FIN)
*/
int ffwebskt_parse(ffwebskt *w)
{
	uint n, hdrsize;
	enum {
		R_INIT, R_GATHER,
		R_HDR, R_HLEN2, R_HLEN8, R_HOK,
		R_BODY,
	};

	for (;;) {
	switch (w->state) {

	case R_INIT:
		GATHER(w, R_HDR, 2);
		continue;

	case R_GATHER:
		n = ffmin64(w->gathlen - w->nbuf, w->in.len);
		ffmemcpy(w->buf + w->nbuf, w->in.ptr, n);
		w->nbuf += n;
		ffarr_shift(&w->in, n);
		if (w->nbuf != w->gathlen)
			return FFWEBSKT_RMORE;
		w->state = w->nxstate;
		continue;

	case R_HDR:
		w->op = w->buf[0] & WS_F_OPCODE;

		if ((w->buf[0] & WS_F_RES) != 0)
			return FFWEBSKT_RERR;

		if (!w->cont && w->op == FFWEBSKT_OP_CONT)
			return FFWEBSKT_RERR; //unexpected "continuation" frame
		else if (w->cont && w->op != FFWEBSKT_OP_CONT)
			return FFWEBSKT_RERR; //expected "continuation" frame
		w->cont = !(w->buf[0] & WS_F_FIN);

		w->mask_off = 0;

		n = (w->buf[1] & WS_F_DATALEN);
		switch (n) {
		case 126:
			GATHER(w, R_HLEN2, 0);
			hdrsize = 2;
			break;
		case 127:
			GATHER(w, R_HLEN8, 0);
			hdrsize = 8;
			break;
		default:
			w->datalen = n;
			hdrsize = 0;
		}

		if (w->buf[1] & WS_F_MASK) {
			if (hdrsize == 0)
				GATHER(w, R_HOK, 0);
			w->mask_off = 2 + hdrsize;
			hdrsize += 4;
		} else if (w->server)
			return FFWEBSKT_RERR; //"mask" flag is required

		if (hdrsize != 0) {
			w->gathlen = 2 + hdrsize;
			continue;
		}

		w->state = R_HOK;
		continue;

	case R_HLEN2:
		w->datalen = ffint_ntoh16(w->buf + 2);
		w->state = R_HOK;
		continue;

	case R_HLEN8:
		if (w->buf[2] != 0)
			return FFWEBSKT_RERR;
		w->datalen = ffint_ntoh64(w->buf + 2);
		w->state = R_HOK;
		continue;

	case R_HOK:
		if (w->mask_off != 0)
			w->mask = *(int*)(w->buf + w->mask_off);
		w->state = R_BODY;
		if (w->op == FFWEBSKT_OP_CONT)
			continue;
		return FFWEBSKT_RMSG;

	case R_BODY: {
		if (w->datalen == 0) {
			w->nbuf = 0;
			GATHER(w, R_HDR, 2);
			return FFWEBSKT_RDATA_FIN;
		}

		if (w->in.len == 0)
			return FFWEBSKT_RMORE;

		size_t nn = ffmin64(w->datalen, w->in.len);
		if (w->mask_off != 0) {
			nn = ffmin64(nn, sizeof(w->buf));
			ffmem_xor4(w->buf, w->in.ptr, nn, w->mask);
			ffstr_set(&w->out, w->buf, nn);
		} else {
			ffstr_set(&w->out, w->in.ptr, nn);
		}
		ffarr_shift(&w->in, nn);
		w->datalen -= nn;
		return FFWEBSKT_RDATA;
	}

	}
	}
	return FFWEBSKT_RERR;
}


int ffwebskt_newmsg(ffwebskt_cook *w, uint mask_key, uint op)
{
	FF_ASSERT(0 == (op & ~WS_F_OPCODE));

	char *p;
	w->buf[0] = 0;
	w->buf[0] |= WS_F_FIN;
	w->buf[0] |= op;
	w->buf[1] = 0;

	if (w->in.len < WS_MAXMSG1) {
		w->buf[1] |= w->in.len;
		p = w->buf + 2;
	} else if (w->in.len <= WS_MAXMSG2) {
		ffint_hton16(w->buf + 2, w->in.len);
		p = w->buf + 2 + 2;
	} else if ((uint64)w->in.len <= WS_MAXMSG) {
		ffint_hton64(w->buf + 2, w->in.len);
		p = w->buf + 2 + 8;
	} else
		return FFWEBSKT_RERR;

	if (mask_key != 0) {
		w->buf[1] |= WS_F_MASK;
		ffmemcpy(p, &mask_key, 4);
		p += 4;
	}

	w->mask = mask_key;
	w->out.ptr = w->buf,  w->out.len = p - w->buf;
	return FFWEBSKT_RDATA;
}

int ffwebskt_writenext(ffwebskt_cook *w)
{
	if (w->in.len == 0)
		return FFWEBSKT_RDATA_FIN;

	if (w->mask != 0) {
		size_t n = ffmin64(w->in.len, sizeof(w->buf));
		ffmem_xor4(w->buf, w->in.ptr, n, w->mask);
		w->out.ptr = w->buf,  w->out.len = n;
		ffarr_shift(&w->in, n);
		return FFWEBSKT_RDATA;
	}

	w->out = w->in;
	w->in.len = 0;
	return FFWEBSKT_RDATA;
}
