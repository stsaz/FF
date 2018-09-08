/**
Copyright (c) 2018 Simon Zolin
*/

#include <FF/net/ssl.h>
#include <FF/time.h>
#include <FFOS/file.h>
#include <FFOS/test.h>

#define x FFTEST_BOOL


void test_ssl(void)
{
	void *key;
	X509 *cert;
	x(!ffssl_cert_create_key(&key, 2048, FFSSL_PKEY_RSA));

	struct ffssl_cert_newinfo ci = {};
	ffstr_setz(&ci.subject, "/O=Org/CN=Name");
	ci.serial = 0x12345678;

	fftime t;
	fftime_now(&t);
	ci.from_time = t.sec;
	ci.until_time = t.sec + 365 * FFTIME_DAY_SECS;
	ci.pkey = key;
	ci.pkey_type = FFSSL_PKEY_RSA;
	x(!ffssl_cert_create(&cert, &ci));

	struct ffssl_cert_info info;
	ffssl_cert_info(cert, &info);
	x(ffstr_eqz(&ci.subject, info.subject));
	x(ffstr_eqz(&ci.subject, info.issuer));
	x(info.valid_from == ci.from_time);
	x(info.valid_until == ci.until_time);

	SSL_CTX *ctx;
	ffssl_ctx_create(&ctx);
	struct ffssl_ctx_conf conf = {};
	conf.cert = cert;
	conf.pkey = key;
	x(!ffssl_ctx_conf(ctx, &conf));
	ffssl_ctx_free(ctx);

	ffstr data;
	ffssl_cert_print(cert, &data);
	fffile_writeall("./cert.pem", data.ptr, data.len, 0);
	ffstr_free(&data);
	ffssl_cert_key_print(key, &data);
	fffile_writeall("./cert.pem", data.ptr, data.len, FFO_APPEND);
	ffstr_free(&data);

	ffssl_cert_key_free(key);
	ffssl_cert_free(cert);
}

int main()
{
	ffmem_init();
	ffssl_init();
	FFTEST_TIMECALL( test_ssl() );
	ffssl_uninit();
	return 0;
}
