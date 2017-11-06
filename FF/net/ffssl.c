/**
Copyright (c) 2015 Simon Zolin
*/

#include <FF/net/ssl.h>
#include <FFOS/error.h>


enum {
	MINRECV = 512, //minimum buffer size for recv()
};

#define DEF_CIPHERS  "!aNULL:!eNULL:!EXP:!MD5:HIGH"

struct ssl_s {
	int conn_idx
		, verify_cb_idx
		, bio_con_idx;
};
static struct ssl_s *_ffssl;

typedef struct ssl_rbuf {
	uint cap
		, off
		, len;
	char data[0];
} ssl_rbuf;

static int ssl_verify_cb(int preverify_ok, X509_STORE_CTX *x509ctx);
static int ssl_tls_srvname(SSL *ssl, int *ad, void *arg);

static int async_bio_write(BIO *bio, const char *buf, int len);
static int async_bio_read(BIO *bio, char *buf, int len);
static long async_bio_ctrl(BIO *bio, int cmd, long num, void *ptr);
static int async_bio_create(BIO *bio);
static int async_bio_destroy(BIO *bio);
static BIO_METHOD ssl_bio_meth = {
	BIO_TYPE_SOCKET, "socket",
	&async_bio_write, &async_bio_read,
	NULL, NULL, //puts, gets
	&async_bio_ctrl, &async_bio_create, &async_bio_destroy,
	NULL,
};


int ffssl_init(void)
{
	SSL_library_init();
	SSL_load_error_strings();
	if (NULL == (_ffssl = ffmem_tcalloc1(struct ssl_s)))
		return FFSSL_ESYS;
	if (-1 == (_ffssl->conn_idx = SSL_get_ex_new_index(0, NULL, NULL, NULL, NULL)))
		return FFSSL_ENEWIDX;
	if (-1 == (_ffssl->verify_cb_idx = SSL_CTX_get_ex_new_index(0, NULL, NULL, NULL, NULL)))
		return FFSSL_ENEWIDX;
	if (-1 == (_ffssl->bio_con_idx = BIO_get_ex_new_index(0, NULL, NULL, NULL, NULL)))
		return FFSSL_ENEWIDX;
	return 0;
}

void ffssl_uninit(void)
{
	ERR_free_strings();
	ffmem_safefree0(_ffssl);
}


static const char *const ffssl_funcstr[] = {
	"",
	"system",

	"SSL_CTX_new",
	"SSL_CTX_set_cipher_list",
	"SSL_CTX_use_certificate_chain_file",
	"SSL_CTX_use_PrivateKey_file",
	"SSL_CTX_load_verify_locations",
	"SSL_load_client_CA_file",
	"SSL_CTX_set_tlsext_servername*",

	"SSL_new",
	"SSL*_get_ex_new_index",
	"SSL*_set_ex_data",
	"SSL_set_tlsext_host_name",
	"BIO_new",

	"SSL_do_handshake",
	"SSL_read",
	"SSL_write",
	"SSL_shutdown",
};

size_t ffssl_errstr(int e, char *buf, size_t cap)
{
	char *pbuf = buf, *end = buf + cap;
	uint eio = ((uint)e >> 8);

	pbuf = ffs_copyz(pbuf, end, ffssl_funcstr[e & 0xff]);
	pbuf = ffs_copy(pbuf, end, ": ", 2);

	if (e == FFSSL_ESYS
		|| eio == SSL_ERROR_SYSCALL) {

		if (pbuf == end)
			goto done;
		fferr_str(fferr_last(), pbuf, end - pbuf);
		pbuf += ffsz_len(buf);
		goto done;

	} else if (eio != 0 && eio != SSL_ERROR_SSL) {
		pbuf += ffs_fmt(pbuf, end, "(%xu)", eio);
		goto done;
	}

	while (0 != (e = ERR_get_error())) {
		pbuf += ffs_fmt(pbuf, end, "(%xd) %s in %s:%s(). "
			, e, ERR_reason_error_string(e), ERR_lib_error_string(e), ERR_func_error_string(e));
	}

done:
	return pbuf - buf;
}


int ffssl_ctx_create(SSL_CTX **pctx)
{
	SSL_CTX *ctx;
	if (NULL == (ctx = SSL_CTX_new(SSLv23_method())))
		return FFSSL_ECTXNEW;
	SSL_CTX_set_options(ctx, SSL_OP_ALL | SSL_OP_NO_SSLv2);
	*pctx = ctx;
	return 0;
}

static const uint no_protos[] = {
	SSL_OP_NO_SSLv3, SSL_OP_NO_TLSv1, SSL_OP_NO_TLSv1_1, SSL_OP_NO_TLSv1_2
};

void ffssl_ctx_protoallow(SSL_CTX *ctx, uint protos)
{
	int i, op = 0;
	if (protos == FFSSL_PROTO_DEFAULT)
		protos = FFSSL_PROTO_TLS1 | FFSSL_PROTO_TLS11 | FFSSL_PROTO_TLS12;

	for (i = 0;  i != FFCNT(no_protos);  i++) {
		if (!(protos & (1 << i)))
			op |= no_protos[i];
	}

	SSL_CTX_set_options(ctx, op);
}

int ffssl_ctx_cert(SSL_CTX *ctx, const char *certfile, const char *pkeyfile, const char *ciphers)
{
	if (1 != SSL_CTX_set_cipher_list(ctx, (ciphers != NULL) ? ciphers : DEF_CIPHERS))
		return FFSSL_ESETCIPHER;

	if (1 != SSL_CTX_use_certificate_chain_file(ctx, certfile))
		return FFSSL_EUSECERT;
	if (1 != SSL_CTX_use_PrivateKey_file(ctx, pkeyfile, SSL_FILETYPE_PEM))
		return FFSSL_EUSEPKEY;
	return 0;
}

static int ssl_verify_cb(int preverify_ok, X509_STORE_CTX *x509ctx)
{
	SSL *ssl;
	void *udata;
	ffssl_verify_cb verify;

	ssl = X509_STORE_CTX_get_ex_data(x509ctx, SSL_get_ex_data_X509_STORE_CTX_idx());
	udata = SSL_get_ex_data(ssl, _ffssl->conn_idx);
	verify = SSL_CTX_get_ex_data(SSL_get_SSL_CTX(ssl), _ffssl->verify_cb_idx);

	return verify(preverify_ok, x509ctx, udata);
}

int ffssl_ctx_ca(SSL_CTX *ctx, ffssl_verify_cb func, uint verify_depth, const char *fn)
{
	STACK_OF(X509_NAME) *names;

	SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, &ssl_verify_cb);
	SSL_CTX_set_verify_depth(ctx, verify_depth);

	if (0 == SSL_CTX_set_ex_data(ctx, _ffssl->verify_cb_idx, func))
		return FFSSL_ESETDATA;

	if (1 != SSL_CTX_load_verify_locations(ctx, fn, NULL))
		return FFSSL_EVRFYLOC;

	if (NULL == (names = SSL_load_client_CA_file(fn)))
		return FFSSL_ELOADCA;
	SSL_CTX_set_client_CA_list(ctx, names);
	return 0;
}

static int ssl_tls_srvname(SSL *ssl, int *ad, void *arg)
{
	void *udata = SSL_get_ex_data(ssl, _ffssl->conn_idx);
	ffssl_tls_srvname_cb srvname = arg;
	return srvname(ssl, ad, arg, udata);
}

int ffssl_ctx_tls_srvname_set(SSL_CTX *ctx, ffssl_tls_srvname_cb func)
{
	if (0 == SSL_CTX_set_tlsext_servername_callback(ctx, &ssl_tls_srvname))
		return FFSSL_ESETTLSSRVNAME;
	if (0 == SSL_CTX_set_tlsext_servername_arg(ctx, func))
		return FFSSL_ESETTLSSRVNAME;
	return 0;
}

int ffssl_ctx_cache(SSL_CTX *ctx, int size)
{
	if (size == -1) {
		SSL_CTX_set_session_cache_mode(ctx, SSL_SESS_CACHE_OFF);
		return 0;
	}

	int sessid_ctx = 0;
	SSL_CTX_set_session_id_context(ctx, (void*)&sessid_ctx, sizeof(int));
	if (size != 0)
		SSL_CTX_sess_set_cache_size(ctx, size);
	return 0;
}


int ffssl_create(SSL **con, SSL_CTX *ctx, uint flags, ffssl_opt *opt)
{
	SSL *c;
	int e;
	BIO *bio;
	ssl_rbuf *rbuf;

	if (NULL == (c = SSL_new(ctx)))
		return FFSSL_ENEW;

	if (opt->udata != NULL
		&& 0 == SSL_set_ex_data(c, _ffssl->conn_idx, opt->udata)) {
		e = FFSSL_ESETDATA;
		goto fail;
	}

	if (flags & FFSSL_ACCEPT) {
		SSL_set_accept_state(c);

	} else {
		SSL_set_connect_state(c);

		if (opt->tls_hostname != NULL && 1 != SSL_set_tlsext_host_name(c, opt->tls_hostname)) {
			e = FFSSL_ESETHOSTNAME;
			goto fail;
		}
	}

	if (flags & FFSSL_IOBUF) {
		if (NULL == (rbuf = ffmem_alloc(sizeof(ssl_rbuf) + MINRECV))) {
			e = FFSSL_ESYS;
			goto fail;
		}
		rbuf->cap = MINRECV;
		rbuf->len = rbuf->off = 0;
		opt->iobuf->rbuf = rbuf;
		opt->iobuf->ptr = NULL;
		opt->iobuf->len = -1;

		if (NULL == (bio = BIO_new(&ssl_bio_meth))) {
			ffmem_free(rbuf);
			e = FFSSL_EBIONEW;
			goto fail;
		}
		BIO_set_ex_data(bio, _ffssl->bio_con_idx, opt->iobuf);
		BIO_set_fd(bio, -1, BIO_NOCLOSE);
		SSL_set_bio(c, bio, bio);
	}

	*con = c;
	return 0;

fail:
	ffssl_free(c);
	return e;
}

void ffssl_free(SSL *c)
{
	BIO *bio;

	if (NULL != (bio = SSL_get_rbio(c)) && bio->method == &ssl_bio_meth) {
		ffssl_iobuf *iobuf = BIO_get_ex_data(bio, _ffssl->bio_con_idx);
		ffmem_free(iobuf->rbuf);
	}

	SSL_free(c);
}

size_t ffssl_get(SSL *c, uint flags)
{
	size_t r = 0;
	switch (flags) {
	case FFSSL_SESS_REUSED:
		r = SSL_session_reused(c);
		break;
	case FFSSL_NUM_RENEGOTIATIONS:
		r = SSL_num_renegotiations(c);
		break;
	case FFSSL_CERT_VERIFY_RESULT:
		r = SSL_get_verify_result(c);
		break;
	}
	return r;
}

const void* ffssl_getptr(SSL *c, uint flags)
{
	const void *r = NULL;
	switch (flags) {
	case FFSSL_HOSTNAME:
		r = SSL_get_servername(c, TLSEXT_NAMETYPE_host_name);
		break;
	case FFSSL_CIPHER_NAME:
		r = SSL_get_cipher_name(c);
		break;
	case FFSSL_VERSION:
		r = SSL_get_version(c);
		break;
	}
	return r;
}


int ffssl_handshake(SSL *c)
{
	int r = SSL_do_handshake(c);
	if (r == 1)
		return 0;
	r = SSL_get_error(c, r);
	if (r == SSL_ERROR_WANT_READ || r == SSL_ERROR_WANT_WRITE)
		return r;
	return FFSSL_EHANDSHAKE | (r << 8);
}

int ffssl_read(SSL *c, void *buf, size_t size)
{
	int r = SSL_read(c, buf, FF_TOINT(size));
	if (r >= 0)
		return r;
	r = SSL_get_error(c, r);
	if (!(r == SSL_ERROR_WANT_READ || r == SSL_ERROR_WANT_WRITE))
		r = FFSSL_EREAD | (r << 8);
	return -r;
}

int ffssl_write(SSL *c, const void *buf, size_t size)
{
	int r = SSL_write(c, buf, FF_TOINT(size));
	if (r >= 0)
		return r;
	r = SSL_get_error(c, r);
	if (!(r == SSL_ERROR_WANT_READ || r == SSL_ERROR_WANT_WRITE))
		r = FFSSL_EWRITE | (r << 8);
	return -r;
}

int ffssl_shut(SSL *c)
{
	int r = SSL_shutdown(c); //send 'close-notify' alert
	if (r == 1 || r == 0)
		return 0;

	r = SSL_get_error(c, r);
	if (r == SSL_ERROR_WANT_READ || r == SSL_ERROR_WANT_WRITE)
		return r;
	if (0 == ERR_peek_error())
		return 0;

	return FFSSL_ESHUT | (r << 8);
}


static int async_bio_read(BIO *bio, char *buf, int len)
{
	ssize_t r;
	ffssl_iobuf *iobuf = BIO_get_ex_data(bio, _ffssl->bio_con_idx);
	ssl_rbuf *rbuf = iobuf->rbuf;

	if (iobuf->len != -1) {
		BIO_clear_retry_flags(bio);
		if (iobuf->ptr != rbuf->data) {
			FF_ASSERT(buf == iobuf->ptr && len >= iobuf->len);
			r = iobuf->len;
			iobuf->len = -1;
			iobuf->ptr = NULL;
			return (int)r;
		}

		rbuf->len = iobuf->len;
		iobuf->len = -1;
		iobuf->ptr = NULL;
		if (rbuf->len == 0)
			return 0;
	}

	if (rbuf->len != 0) {
		r = ffs_append(buf, 0, len, rbuf->data + rbuf->off, rbuf->len);
		rbuf->len -= r;
		rbuf->off = (rbuf->len == 0) ? 0 : rbuf->off + r;
		return (int)r;
	}

	ffs_max(buf, len, rbuf->data, rbuf->cap, &iobuf->ptr, (void*)&iobuf->len);
	BIO_set_retry_read(bio);
	return -1;
}

static int async_bio_write(BIO *bio, const char *buf, int len)
{
	ssize_t r;
	ffssl_iobuf *iobuf = BIO_get_ex_data(bio, _ffssl->bio_con_idx);

	if (iobuf->len != -1) {
		FF_ASSERT(buf == iobuf->ptr && len >= iobuf->len);
		BIO_clear_retry_flags(bio);
		r = iobuf->len;
		iobuf->len = -1;
		iobuf->ptr = NULL;
		return (int)r;
	}

	iobuf->ptr = (void*)buf;
	iobuf->len = len;
	BIO_set_retry_write(bio);
	return -1;
}

static long async_bio_ctrl(BIO *bio, int cmd, long num, void *ptr)
{
	switch (cmd) {
	case BIO_C_SET_FD:
		bio->num = *(int*)ptr;
		bio->init = 1;
		return 1;

	case BIO_CTRL_DUP:
	case BIO_CTRL_FLUSH:
		return 1;
	}

	return 0;
}

static int async_bio_create(BIO *bio)
{
	return 1;
}

static int async_bio_destroy(BIO *bio)
{
	bio->init = 0;
	return 1;
}


void ffssl_cert_info(X509 *cert, struct ffssl_cert_info *info)
{
	X509_NAME_oneline(X509_get_subject_name(cert), info->subject, sizeof(info->subject));
	X509_NAME_oneline(X509_get_issuer_name(cert), info->issuer, sizeof(info->issuer));
}
