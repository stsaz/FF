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
	BIO_METHOD *bio_meth;
};
static struct ssl_s *_ffssl;

struct ssl_rbuf {
	uint cap
		, off
		, len;
	char data[MINRECV];
};

struct ssl_iobuf {
	ssize_t len;
	void *ptr;

	struct ssl_rbuf rbuf;
};

static int ssl_verify_cb(int preverify_ok, X509_STORE_CTX *x509ctx);
static int ssl_tls_srvname(SSL *ssl, int *ad, void *arg);
static int _ffssl_ctx_tls_srvname_set(SSL_CTX *ctx, ffssl_tls_srvname_cb func);

/* BIO method for asynchronous I/O.  Reduces data copying. */
static int async_bio_write(BIO *bio, const char *buf, int len);
static int async_bio_read(BIO *bio, char *buf, int len);
static long async_bio_ctrl(BIO *bio, int cmd, long num, void *ptr);
static int async_bio_create(BIO *bio);
static int async_bio_destroy(BIO *bio);


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

	BIO_METHOD *bm;
	if (NULL == (bm = BIO_meth_new(BIO_TYPE_MEM, "aio")))
		return FFSSL_ESYS;
	BIO_meth_set_write(bm, &async_bio_write);
	BIO_meth_set_read(bm, &async_bio_read);
	BIO_meth_set_ctrl(bm, &async_bio_ctrl);
	BIO_meth_set_create(bm, &async_bio_create);
	BIO_meth_set_destroy(bm, &async_bio_destroy);
	_ffssl->bio_meth = bm;

	return 0;
}

void ffssl_uninit(void)
{
	ERR_free_strings();
	BIO_meth_free(_ffssl->bio_meth);
	ffmem_safefree0(_ffssl);
}


static const char *const ffssl_funcstr[] = {
	"",
	"system",

	"SSL_CTX_new",
	"SSL_CTX_set_cipher_list",
	"SSL_CTX_use_certificate",
	"SSL_CTX_use_PrivateKey",
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

void _ffssl_ctx_protoallow(SSL_CTX *ctx, uint protos)
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

static int _ffssl_ctx_cert(SSL_CTX *ctx, const struct ffssl_ctx_conf *o)
{
	if (o->certfile != NULL
		&& 1 != SSL_CTX_use_certificate_chain_file(ctx, o->certfile))
		return FFSSL_EUSECERT;

	if (o->certdata.len != 0) {
		X509 *cert;
		if (NULL == (cert = ffssl_cert_read(o->certdata.ptr, o->certdata.len, 0)))
			return FFSSL_EUSECERT;
		int r = SSL_CTX_use_certificate(ctx, cert);
		X509_free(cert);
		if (r != 1)
			return FFSSL_EUSECERT;
	}

	if (o->cert != NULL
		&& 1 != SSL_CTX_use_certificate(ctx, o->cert))
		return FFSSL_EUSECERT;

	return 0;
}

static int _ffssl_ctx_pkey(SSL_CTX *ctx, const struct ffssl_ctx_conf *o)
{
	if (o->pkeyfile != NULL
		&& 1 != SSL_CTX_use_PrivateKey_file(ctx, o->pkeyfile, SSL_FILETYPE_PEM))
		return FFSSL_EUSEPKEY;

	if (o->pkeydata.len != 0) {
		EVP_PKEY *pk;
		if (NULL == (pk = ffssl_cert_key_read(o->pkeydata.ptr, o->pkeydata.len, 0)))
			return FFSSL_EUSEPKEY;
		int r = SSL_CTX_use_PrivateKey(ctx, pk);
		EVP_PKEY_free(pk);
		if (r != 1)
			return FFSSL_EUSEPKEY;
	}

	if (o->pkey != NULL
		&& 1 != SSL_CTX_use_PrivateKey(ctx, o->pkey))
		return FFSSL_EUSEPKEY;

	return 0;
}

int ffssl_ctx_conf(SSL_CTX *ctx, const struct ffssl_ctx_conf *o)
{
	int r;

	if (0 != (r = _ffssl_ctx_cert(ctx, o)))
		return r;

	if (0 != (r = _ffssl_ctx_pkey(ctx, o)))
		return r;

	if (o->ciphers != NULL
		&& 1 != SSL_CTX_set_cipher_list(ctx, (o->ciphers[0] != '\0') ? o->ciphers : DEF_CIPHERS))
		return FFSSL_ESETCIPHER;

	if (o->use_server_cipher)
		SSL_CTX_set_options(ctx, SSL_OP_CIPHER_SERVER_PREFERENCE);

	if (o->tls_srvname_func != NULL
		&& 0 != (r = _ffssl_ctx_tls_srvname_set(ctx, o->tls_srvname_func)))
		return r;

	_ffssl_ctx_protoallow(ctx, o->allowed_protocols);

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

static int _ffssl_ctx_tls_srvname_set(SSL_CTX *ctx, ffssl_tls_srvname_cb func)
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
		struct ssl_iobuf *iobuf;
		if (NULL == (iobuf = ffmem_alloc(sizeof(struct ssl_iobuf)))) {
			e = FFSSL_ESYS;
			goto fail;
		}
		iobuf->rbuf.cap = MINRECV;
		iobuf->rbuf.len = iobuf->rbuf.off = 0;
		iobuf->ptr = NULL;
		iobuf->len = -1;

		if (NULL == (bio = BIO_new(_ffssl->bio_meth))) {
			ffmem_free(iobuf);
			e = FFSSL_EBIONEW;
			goto fail;
		}
		BIO_set_data(bio, iobuf);
		BIO_set_ex_data(bio, _ffssl->bio_con_idx, iobuf);
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


/*
Server-side handshake:
1.
ffssl_handshake() -> libssl... -> BIO
ffssl_handshake() <-(want-read)-- libssl... <-(retry)-- BIO

2.
write to iobuf
ffssl_handshake() -> libssl... -> BIO
libssl <-(data)-- BIO

3.
libssl... -> BIO
ffssl_handshake() <-(want-write)-- libssl... <-(retry)-- BIO

4.
read from iobuf
ffssl_handshake() -> libssl... -> BIO
libssl <-(ok)-- BIO
*/
void ffssl_iobuf(SSL *c, ffstr *data)
{
	BIO *bio = SSL_get_rbio(c);
	struct ssl_iobuf *iob = BIO_get_ex_data(bio, _ffssl->bio_con_idx);
	ffstr_set(data, iob->ptr, iob->len);
}

void ffssl_input(SSL *c, size_t len)
{
	BIO *bio = SSL_get_rbio(c);
	struct ssl_iobuf *iob = BIO_get_ex_data(bio, _ffssl->bio_con_idx);
	iob->len = len;
}

static int async_bio_read(BIO *bio, char *buf, int len)
{
	ssize_t r;
	struct ssl_iobuf *iobuf = BIO_get_data(bio);
	struct ssl_rbuf *rbuf = &iobuf->rbuf;

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
	struct ssl_iobuf *iobuf = BIO_get_data(bio);

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
	case BIO_CTRL_DUP:
	case BIO_CTRL_FLUSH:
		return 1;
	}

	return 0;
}

static int async_bio_create(BIO *bio)
{
	BIO_set_init(bio, 1);
	return 1;
}

static int async_bio_destroy(BIO *bio)
{
	struct ssl_iobuf *iobuf = BIO_get_data(bio);
	ffmem_free(iobuf);
	return 1;
}


static uint64 time_from_ASN1time(const ASN1_TIME *src)
{
	time_t t = 0;
	ASN1_TIME *at = ASN1_TIME_new();
	X509_time_adj_ex(at, 0, 0, &t);
	int days = 0, secs = 0;
	ASN1_TIME_diff(&days, &secs, at, src);
	ASN1_TIME_free(at);
	return (int64)days * 24 * 60 * 60 + secs;
}

void ffssl_cert_info(X509 *cert, struct ffssl_cert_info *info)
{
	X509_NAME_oneline(X509_get_subject_name(cert), info->subject, sizeof(info->subject));
	X509_NAME_oneline(X509_get_issuer_name(cert), info->issuer, sizeof(info->issuer));
	info->valid_from = time_from_ASN1time(X509_get_notBefore(cert));
	info->valid_until = time_from_ASN1time(X509_get_notAfter(cert));
}

X509* ffssl_cert_read(const char *data, size_t len, uint flags)
{
	BIO *b;
	X509 *x;

	if (NULL == (b = BIO_new_mem_buf(data, len)))
		return NULL;

	x = PEM_read_bio_X509(b, NULL, NULL, NULL);

	BIO_free_all(b);
	return x;
}

void* ffssl_cert_key_read(const char *data, size_t len, uint flags)
{
	BIO *b;
	void *key;

	if (NULL == (b = BIO_new_mem_buf(data, len)))
		return NULL;

	key = PEM_read_bio_PrivateKey(b, NULL, NULL, NULL);

	BIO_free_all(b);
	return key;
}

int ffssl_cert_create_key(void **key, uint bits, uint flags)
{
	int r = -1;
	RSA *rsa = NULL;

	switch (flags & 0xff) {
	case FFSSL_PKEY_RSA:
		if (NULL == (rsa = RSA_generate_key(bits, RSA_F4, NULL, NULL)))
			goto end;
		break;
	default:
		goto end;
	}

	*key = rsa;
	r = 0;

end:
	return r;
}

/** Create EVP_PKEY object. */
static EVP_PKEY* _evp_pkey(void *key, uint type)
{
	EVP_PKEY *pk;
	if (NULL == (pk = EVP_PKEY_new()))
		goto end;
	switch (type) {
	case FFSSL_PKEY_RSA:
		if (0 == EVP_PKEY_set1_RSA(pk, key))
			goto end;
		break;
	default:
		goto end;
	}

	return pk;

end:
	FF_SAFECLOSE(pk, NULL, EVP_PKEY_free);
	return NULL;
}

/** Fill X509_NAME object. */
static int _x509_name(X509_NAME *name, const ffstr *subject)
{
	int r;
	ffstr subj = *subject, pair, k, v;
	char buf[1024];

	if (subj.len != 0 && subj.ptr[0] == '/')
		ffstr_shift(&subj, 1);

	while (subj.len != 0) {
		ffstr_nextval3(&subj, &pair, '/' | FFS_NV_KEEPWHITE);

		if (NULL == ffs_split2by(pair.ptr, pair.len, '=', &k, &v)
			|| k.len == 0)
			goto end; // must be K=[V] pair

		const char *z = ffsz_copy(buf, sizeof(buf), k.ptr, k.len);
		if (z + 1 == buf + sizeof(buf))
			goto end; // too large key
		r = X509_NAME_add_entry_by_txt(name, buf, MBSTRING_ASC, (byte*)v.ptr, v.len, -1, 0);
		if (r == 0)
			goto end;
	}

	return 0;

end:
	return -1;
}

int ffssl_cert_create(X509 **px509, struct ffssl_cert_newinfo *info)
{
	int r = -1;
	EVP_PKEY *pk = NULL, *isspk = NULL;
	X509 *x509 = NULL;

	if (NULL == (pk = _evp_pkey(info->pkey, info->pkey_type)))
		goto end;

	if (NULL == (x509 = X509_new()))
		goto end;

	if (0 == X509_set_version(x509, 2))
		goto end;
	ASN1_INTEGER_set(X509_get_serialNumber(x509), info->serial);
	if (0 == X509_set_pubkey(x509, pk))
		goto end;

	if (NULL == X509_time_adj_ex(X509_get_notBefore(x509), 0, 0, &info->from_time))
		goto end;
	if (NULL == X509_time_adj_ex(X509_get_notAfter(x509), 0, 0, &info->until_time))
		goto end;

	X509_NAME *name = X509_get_subject_name(x509);
	if (0 != _x509_name(name, &info->subject))
		goto end;

	X509_NAME *issname = name;
	EVP_PKEY *ipk = pk;
	if (info->issuer_name != NULL) {
		issname = info->issuer_name;
		if (NULL == (isspk = _evp_pkey(info->issuer_pkey, info->issuer_pkey_type)))
			goto end;
		ipk = isspk;
	}
	if (0 == X509_set_issuer_name(x509, issname))
		goto end;
	if (0 == X509_sign(x509, ipk, EVP_sha1()))
		goto end;
	*px509 = x509;
	r = 0;

end:
	if (r != 0)
		FF_SAFECLOSE(x509, NULL, X509_free);
	FF_SAFECLOSE(pk, NULL, EVP_PKEY_free);
	FF_SAFECLOSE(isspk, NULL, EVP_PKEY_free);
	return r;
}

int ffssl_cert_key_print(void *key, ffstr *data)
{
	int r = -1;
	BIO *bio;
	BUF_MEM *bm;
	if (NULL == (bio = BIO_new(BIO_s_mem())))
		goto end;
	if (0 == PEM_write_bio_RSAPrivateKey(bio, key, NULL, NULL, 0, NULL, NULL))
		goto end;
	if (0 == BIO_get_mem_ptr(bio, &bm))
		goto end;
	if (NULL == ffstr_dup(data, bm->data, bm->length))
		goto end;
	r = 0;

end:
	FF_SAFECLOSE(bio, NULL, BIO_free_all);
	return r;
}

int ffssl_cert_print(X509 *x509, ffstr *data)
{
	int r = -1;
	BIO *bio;
	BUF_MEM *bm;
	if (NULL == (bio = BIO_new(BIO_s_mem())))
		goto end;
	if (0 == PEM_write_bio_X509(bio, x509))
		goto end;
	if (0 == BIO_get_mem_ptr(bio, &bm))
		goto end;
	if (NULL == ffstr_dup(data, bm->data, bm->length))
		goto end;
	r = 0;

end:
	FF_SAFECLOSE(bio, NULL, BIO_free_all);
	return r;
}
