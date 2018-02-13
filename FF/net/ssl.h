/** OpenSSL wrapper.
Copyright (c) 2015 Simon Zolin
*/

#pragma once

#include <FF/array.h>

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

	/* For these codes SSL_get_error() value is stored in LSB.1:
	    uint e = "00 00 SSL_get_error FFSSL_E" */
	FFSSL_EHANDSHAKE,
	FFSSL_EREAD,
	FFSSL_EWRITE,
	FFSSL_ESHUT,
};

/**
@e: enum FFSSL_E */
FF_EXTN size_t ffssl_errstr(int e, char *buf, size_t cap);


FF_EXTN int ffssl_init(void);

FF_EXTN void ffssl_uninit(void);



FF_EXTN int ffssl_ctx_create(SSL_CTX **ctx);

#define ffssl_ctx_free  SSL_CTX_free

enum FFSSL_SRVNAME {
	FFSSL_SRVNAME_OK = SSL_TLSEXT_ERR_OK,
	FFSSL_SRVNAME_NOACK = SSL_TLSEXT_ERR_NOACK,
};

/** Return enum FFSSL_SRVNAME */
typedef int (*ffssl_tls_srvname_cb)(SSL *ssl, int *ad, void *arg, void *udata);

enum FFSSL_PROTO {
	FFSSL_PROTO_DEFAULT = 0, //all TLS
	FFSSL_PROTO_V3 = 1,
	FFSSL_PROTO_TLS1 = 2,
	FFSSL_PROTO_TLS11 = 4,
	FFSSL_PROTO_TLS12 = 8,
};

struct ffssl_ctx_conf {
	char *certfile;
	ffstr certdata;
	void *cert;

	char *pkeyfile;
	ffstr pkeydata;
	void *pkey;

	char *ciphers;
	uint use_server_cipher :1;

	ffssl_tls_srvname_cb tls_srvname_func;

	uint allowed_protocols; //enum FFSSL_PROTO
};

/** Configurate SSL context. */
FF_EXTN int ffssl_ctx_conf(SSL_CTX *ctx, const struct ffssl_ctx_conf *conf);

typedef int (*ffssl_verify_cb)(int preverify_ok, X509_STORE_CTX *x509ctx, void *udata);

FF_EXTN int ffssl_ctx_ca(SSL_CTX *ctx, ffssl_verify_cb func, uint verify_depth, const char *fn);

/**
@size: 0:default;  -1:disabled;  >0:cache size. */
FF_EXTN int ffssl_ctx_cache(SSL_CTX *ctx, int size);

static FFINL void ffssl_ctx_sess_del(SSL_CTX *ctx, SSL *c)
{
	SSL_CTX_remove_session(ctx, SSL_get_session(c));
}


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

enum FFSSL_FOPT {
	FFSSL_HOSTNAME,
	FFSSL_CIPHER_NAME,
	FFSSL_VERSION,
	FFSSL_SESS_REUSED,
	FFSSL_NUM_RENEGOTIATIONS,
	FFSSL_CERT_VERIFY_RESULT, //X509_V_OK or other X509_V_*
};

/**
@flags: enum FFSSL_FOPT */
FF_EXTN size_t ffssl_get(SSL *c, uint flags);
FF_EXTN const void* ffssl_getptr(SSL *c, uint flags);

/**
These codes must be handled by user.
Call ffssl_errstr() for any other code. */
enum FFSSL_EIO {
	FFSSL_WANTREAD = SSL_ERROR_WANT_READ,
	FFSSL_WANTWRITE = SSL_ERROR_WANT_WRITE,
};

/**
Return 0 on success;  enum FFSSL_EIO for more I/O;  enum FFSSL_E on error. */
FF_EXTN int ffssl_handshake(SSL *c);

/**
Return the number of bytes read or enum FFSSL_EIO (negative value). */
FF_EXTN int ffssl_read(SSL *c, void *buf, size_t size);

/**
Return the number of bytes sent or enum FFSSL_EIO (negative value). */
FF_EXTN int ffssl_write(SSL *c, const void *buf, size_t size);

/**
Return 0 on success;  enum FFSSL_EIO for more I/O;  enum FFSSL_E on error. */
FF_EXTN int ffssl_shut(SSL *c);

static FFINL void ffssl_setctx(SSL *c, SSL_CTX *ctx)
{
	SSL_set_SSL_CTX(c, ctx);
	SSL_set_options(c, SSL_CTX_get_options(ctx));
}


struct ffssl_cert_info {
	char subject[1024];
	char issuer[1024];
};

FF_EXTN void ffssl_cert_info(X509 *cert, struct ffssl_cert_info *info);

#define ffssl_cert_verify_errstr(e)  X509_verify_cert_error_string(e)

FF_EXTN X509* ffssl_cert_read(const char *data, size_t len, uint flags);

FF_EXTN void* ffssl_cert_key_read(const char *data, size_t len, uint flags);
