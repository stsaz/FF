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
	FFTLS_RDONE,
};

typedef struct fftls {
	uint state;
	int err; //enum FFTLS_E
	uint version;
	uint hshake_type;

	ffstr in; //unprocessed input data
	ffstr buf; //data to be processed

	ffstr session_id; //session ID from C/S Hello
	ffstr ciphers; //list of ciphers from C/S Hello (ushort[], network byte order)
	ffstr hostname; //server name from Client Hello
	ffstr alpn_protos; //ALPN protocols from C/S Hello (struct alpn_proto[])
	ffstr cert; //certficate data from Server Certificate
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
