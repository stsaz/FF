/** OpenSSL wrapper.
Copyright (c) 2015 Simon Zolin
*/

#pragma once

#include <FF/string.h>

#ifdef FF_WIN
#define OPENSSL_SYS_WIN32
#endif
#include <openssl/ssl.h>
#include <openssl/err.h>


enum FFSSL_E {
	FFSSL_EOK = 0,
	FFSSL_ESYS,

	FFSSL_ECTXNEW,
	FFSSL_ESETCIPHER,
	FFSSL_EUSECERT,
	FFSSL_EUSEPKEY,
	FFSSL_EVRFYLOC,
	FFSSL_ELOADCA,
	FFSSL_ESETTLSSRVNAME,

	FFSSL_ENEW,
	FFSSL_ENEWIDX,
	FFSSL_ESETDATA,
	FFSSL_ESETHOSTNAME,
	FFSSL_EBIONEW,
};

// enum FFSSL_E
FF_EXTN const char *const ffssl_funcstr[];

FF_EXTN size_t ffssl_errstr(char *buf, size_t cap);


FF_EXTN int ffssl_init(void);

FF_EXTN void ffssl_uninit(void);



FF_EXTN int ffssl_ctx_create(SSL_CTX **ctx);

#define ffssl_ctx_free  SSL_CTX_free

enum FFSSL_PROTO {
	FFSSL_PROTO_DEFAULT = 0,
	FFSSL_PROTO_V3 = 1,
	FFSSL_PROTO_TLS1 = 2,
	FFSSL_PROTO_TLS11 = 4,
	FFSSL_PROTO_TLS12 = 8,
};

/** Set which protocols are allowed.
@protos: enum FFSSL_PROTO */
FF_EXTN void ffssl_ctx_protoallow(SSL_CTX *ctx, uint protos);

FF_EXTN int ffssl_ctx_cert(SSL_CTX *ctx, const char *certfile, const char *pkeyfile, const char *ciphers);

typedef int (*ffssl_verify_cb)(int preverify_ok, X509_STORE_CTX *x509ctx, void *udata);

FF_EXTN int ffssl_ctx_ca(SSL_CTX *ctx, ffssl_verify_cb func, uint verify_depth, const char *fn);

typedef int (*ffssl_tls_srvname_cb)(SSL *ssl, int *ad, void *arg, void *udata);

FF_EXTN int ffssl_ctx_tls_srvname_set(SSL_CTX *ctx, ffssl_tls_srvname_cb func);


enum FFSSL_CREATE {
	/** Connection type: client (default) or server. */
	FFSSL_CONNECT = 0,
	FFSSL_ACCEPT = 1,

	/** Don't perform any I/O operations within the library itself.
	The caller must handle FFSSL_WANTREAD and FFSSL_WANTWRITE error codes.
	The object of type 'ffssl_iobuf' contains SSL data that needs to be read/sent.
	After I/O is performed, ffssl_iobuf.len must be set to the number of bytes transferred. */
	FFSSL_IOBUF = 2,
};

typedef struct ffssl_iobuf {
	ssize_t len;
	void *ptr;

	void *rbuf;
} ffssl_iobuf;

typedef struct ffssl_opt {
	void *udata; //opaque data for callback functions
	const char *tls_hostname; //set hostname for SNI

	ffssl_iobuf *iobuf; //used with FFSSL_IOBUF
} ffssl_opt;

/** Create a connection.
@flags: enum FFSSL_CREATE
@opt: additional options.
Return enum FFSSL_E. */
FF_EXTN int ffssl_create(SSL **c, SSL_CTX *ctx, uint flags, ffssl_opt *opt);

FF_EXTN void ffssl_free(SSL *c);

enum FFSSL_EIO {
	FFSSL_WANTREAD = SSL_ERROR_WANT_READ,
	FFSSL_WANTWRITE = SSL_ERROR_WANT_WRITE,
};

/**
Return enum FFSSL_EIO or error. */
FF_EXTN int ffssl_handshake(SSL *c);

/**
Return the number of bytes read or enum FFSSL_EIO (negative value). */
FF_EXTN int ffssl_read(SSL *c, void *buf, size_t size);

/**
Return the number of bytes sent or enum FFSSL_EIO (negative value). */
FF_EXTN int ffssl_write(SSL *c, const void *buf, size_t size);

/**
Return enum FFSSL_EIO or error. */
FF_EXTN int ffssl_shut(SSL *c);

static FFINL void ffssl_setctx(SSL *c, SSL_CTX *ctx)
{
	SSL_set_SSL_CTX(c, ctx);
	SSL_set_options(c, SSL_CTX_get_options(ctx));
}
