/** TLS reader tests.
Copyright (c) 2018 Simon Zolin
*/

#include <FFOS/test.h>
#include <FF/net/tls.h>

#define x FFTEST_BOOL


static const char tls13_clienthello_ciphers[] =
"\x13\x01\x13\x03\x13\x02\xc0\x2b\xc0\x2f\xcc\xa9\xcc\xa8\xc0\x2c"
"\xc0\x30\xc0\x13\xc0\x14\x00\x2f\x00\x35";

static const char tls13_clienthello_sessiondata[] =
"\x4e\x7d\x05\xe7\x0d\xc8\xaf\x83\xae\x85\xf9\x26\xe2\x12\x08\xff"
"\x8e\x90\xf5\x26\x8c\x9f\xca\x08\xe3\x0e\x36\x77\x75\xc3\x9b\x8a";

static const char tls13_clienthello_alpn[] =
"\x02\x68\x32\x08\x68\x74\x74\x70\x2f\x31\x2e\x31";

/*
TLS1.3 (0x7f17) Client Hello to www.google.com
Session ID: tls13_clienthello_sessiondata
Ciphers: tls13_clienthello_ciphers
ALPN: tls13_clienthello_alpn
*/
static const char tls13_clienthello[] =
"\x16\x03\x01\x02\x00\x01\x00\x01\xfc\x03\x03\xf4\x2f\x88\xe2\x26"
"\xbb\xe7\xd4\x2b\xd7\x74\x2d\xbc\x30\xf6\xf7\x8f\x02\x65\x31\x66"
"\x13\x2b\x3e\xdb\x2b\x28\x41\xfe\x4e\xd8\xef\x20\x4e\x7d\x05\xe7"
"\x0d\xc8\xaf\x83\xae\x85\xf9\x26\xe2\x12\x08\xff\x8e\x90\xf5\x26"
"\x8c\x9f\xca\x08\xe3\x0e\x36\x77\x75\xc3\x9b\x8a\x00\x1a\x13\x01"
"\x13\x03\x13\x02\xc0\x2b\xc0\x2f\xcc\xa9\xcc\xa8\xc0\x2c\xc0\x30"
"\xc0\x13\xc0\x14\x00\x2f\x00\x35\x01\x00\x01\x99\x00\x00\x00\x13"
"\x00\x11\x00\x00\x0e\x77\x77\x77\x2e\x67\x6f\x6f\x67\x6c\x65\x2e"
"\x63\x6f\x6d\x00\x17\x00\x00\xff\x01\x00\x01\x00\x00\x0a\x00\x0e"
"\x00\x0c\x00\x1d\x00\x17\x00\x18\x00\x19\x01\x00\x01\x01\x00\x0b"
"\x00\x02\x01\x00\x00\x23\x00\x00\x00\x10\x00\x0e\x00\x0c\x02\x68"
"\x32\x08\x68\x74\x74\x70\x2f\x31\x2e\x31\x00\x05\x00\x05\x01\x00"
"\x00\x00\x00\x00\x33\x00\x6b\x00\x69\x00\x1d\x00\x20\xe6\xe0\x92"
"\xee\x4d\x7e\x67\x18\x84\x59\x2d\x98\x66\xb8\x73\x21\xd1\xde\xde"
"\x04\x45\x9a\x99\x9c\xff\x29\x52\xae\x99\x21\x84\x1b\x00\x17\x00"
"\x41\x04\x4e\xb3\x0c\xea\x87\x05\xd8\x2e\x0c\x84\x0d\x7b\x5a\xcf"
"\xa6\x32\xad\x2e\x99\xcb\x61\xfb\x12\xcb\x94\x4b\x9f\x87\xdb\x84"
"\xd8\x05\xc2\x9c\x2c\x90\x30\xc9\x0f\x23\xd2\xea\x53\x31\xd5\x40"
"\xd2\x17\x58\x64\x1b\x60\x31\x28\x2d\x5f\xee\x3b\x7b\xe8\x39\x44"
"\xcc\x0e\x00\x2b\x00\x09\x08\x7f\x17\x03\x03\x03\x02\x03\x01\x00"
"\x0d\x00\x18\x00\x16\x04\x03\x05\x03\x06\x03\x08\x04\x08\x05\x08"
"\x06\x04\x01\x05\x01\x06\x01\x02\x03\x02\x01\x00\x2d\x00\x02\x01"
"\x01\x00\x15\x00\xa0\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
"\x00\x00\x00\x00\x00";

static const char tls13_serverhello_cipher[] = "\x13\x01";

static const char tls13_serverhello[] =
"\x16\x03\x03\x00\x7a\x02\x00\x00\x76\x03\x03\x37\xe2\x6e\xba\x83"
"\xad\x81\x2f\x49\x65\x2d\xd1\x11\x22\x92\x69\x6c\x73\x73\xf2\xc1"
"\x21\xdd\xac\x07\x69\x73\x61\x12\x63\xf9\xd6\x20\x4e\x7d\x05\xe7"
"\x0d\xc8\xaf\x83\xae\x85\xf9\x26\xe2\x12\x08\xff\x8e\x90\xf5\x26"
"\x8c\x9f\xca\x08\xe3\x0e\x36\x77\x75\xc3\x9b\x8a\x13\x01\x00\x00"
"\x2e\x00\x33\x00\x24\x00\x1d\x00\x20\xcd\x0e\x0b\x23\x85\x7f\xee"
"\x1c\x44\x1e\x22\x98\x7a\x46\x08\x31\x8e\x98\xc1\x0f\x11\x01\x41"
"\xc8\xe9\xed\x72\xf8\x64\x76\x3c\x65\x00\x2b\x00\x02\x7f\x17";

int test_tls(void)
{
	FFTEST_FUNC;

	fftls tls;

	ffmem_tzero(&tls);
	fftls_input(&tls, tls13_clienthello, 10);
	x(FFTLS_RMORE == fftls_read(&tls));

	ffmem_tzero(&tls);
	fftls_input(&tls, tls13_clienthello + 1, 10);
	x(FFTLS_RERR == fftls_read(&tls));

	ffmem_tzero(&tls);
	fftls_input(&tls, tls13_clienthello, FFSLEN(tls13_clienthello));
	x(FFTLS_RCLIENT_HELLO == fftls_read(&tls));
	x(fftls_ver(&tls) == 0x0303);
	x(ffstr_eq(&tls.session_id, tls13_clienthello_sessiondata, FFSLEN(tls13_clienthello_sessiondata)));
	x(ffstr_eq(&tls.ciphers, tls13_clienthello_ciphers, FFSLEN(tls13_clienthello_ciphers)));
	x(FFTLS_RCLIENT_HELLO_SNI == fftls_read(&tls));
	x(ffstr_eqz(&tls.hostname, "www.google.com"));
	x(FFTLS_RHELLO_ALPN == fftls_read(&tls));
	x(ffstr_eq(&tls.alpn_protos, tls13_clienthello_alpn, FFSLEN(tls13_clienthello_alpn)));

	ffstr alpn;
	x(3 == fftls_alpn_next(&tls.alpn_protos, &alpn));
	x(ffstr_eqz(&alpn, "h2"));
	x(9 == fftls_alpn_next(&tls.alpn_protos, &alpn));
	x(ffstr_eqz(&alpn, "http/1.1"));
	x(0 == fftls_alpn_next(&tls.alpn_protos, &alpn));

	x(FFTLS_RDONE == fftls_read(&tls));
	x(fftls_ver(&tls) == 0x7f17);

	ffmem_tzero(&tls);
	fftls_input(&tls, tls13_serverhello, FFSLEN(tls13_serverhello));
	x(FFTLS_RSERVER_HELLO == fftls_read(&tls));
	x(fftls_ver(&tls) == 0x0303);
	x(ffstr_eq(&tls.session_id, tls13_clienthello_sessiondata, FFSLEN(tls13_clienthello_sessiondata)));
	x(ffstr_eq(&tls.ciphers, tls13_serverhello_cipher, FFSLEN(tls13_serverhello_cipher)));
	x(FFTLS_RDONE == fftls_read(&tls));
	x(fftls_ver(&tls) == 0x7f17);

	return 0;
}