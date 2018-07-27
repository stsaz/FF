/** TLS reader.
Copyright (c) 2018 Simon Zolin
*/

#pragma once

#include <FF/array.h>


enum FFTLS_E {
	FFTLS_EDATA,
	FFTLS_EVERSION,
	FFTLS_ENOTSUPP,
};

enum FFTLS_R {
	FFTLS_RERR = 1,
	FFTLS_RMORE,
	FFTLS_RCLIENT_HELLO,
	FFTLS_RCLIENT_HELLO_SNI,
	FFTLS_RHELLO_ALPN,
	FFTLS_RSERVER_HELLO,
	FFTLS_RCERT,
	FFTLS_RKEY_EXCH,
	FFTLS_RCERT_REQ,
	FFTLS_RSERV_HELLO_DONE,
	FFTLS_RDONE, // TLS record is processed
};

typedef struct fftls {
	uint state;
	int err; //enum FFTLS_E
	uint version;
	uint hshake_type;

	ffstr in; //unprocessed input data
	ffstr buf; //data to be processed

	// available after FFTLS_RCLIENT_HELLO, FFTLS_RSERVER_HELLO:
	ffstr session_id; //session ID from C/S Hello
	ffstr ciphers; //list of ciphers from C/S Hello (ushort[], network byte order)

	ffstr hostname; //server name from Client Hello (available after FFTLS_RCLIENT_HELLO_SNI)

	ffstr alpn_protos; //ALPN protocols from C/S Hello (struct alpn_proto[]) (available after FFTLS_RHELLO_ALPN)

	ffstr cert; //certficate data from Server Certificate (available after FFTLS_RCERT)
} fftls;

/** Set input data. */
#define fftls_input(t, d, n) \
	ffstr_set(&(t)->in, d, n)

/** Parse TLS record.
Return enum FFTLS_R. */
FF_EXTN int fftls_read(fftls *t);

#define fftls_ver(t)  (t)->version

/** Get cipher name by ID. */
FF_EXTN const char* fftls_cipher_name(ushort ciph);

/** Get TLS version as a string. */
FF_EXTN const char* fftls_verstr(uint ver);

/** Get next ALPN protocol.
@buf: input buffer
@proto: output protocol name (without length)
Return the number of bytes read;  0 if no more protocols. */
FF_EXTN int fftls_alpn_next(ffstr *buf, ffstr *proto);

/** Write a TLS alert record.
Return the number of bytes written;  -1 if not enough space. */
FF_EXTN int fftls_alert(void *buffer, size_t cap, const void *data, size_t len);
