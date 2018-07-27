/**
Copyright (c) 2018 Simon Zolin
*/

/*
rec(alert | handshake(cli-hello [ext]... | serv-hello [ext]... | ...))
*/

/* Supported elements hierarchy:
rec
 hshake
  client_hello
   list16
    ext[]
     list16
      servname[]
       hostname
      list16
       alpn_proto[]
      suppvers
  server_hello
   list16
    ext[]
     srvhel_suppvers
  certificates
   list24
    cert[]
  server_key_exchange
  certificate_request
  server_hello_done
*/


#include <FF/net/tls.h>
#include <FF/number.h>


static int tls_rec_read(fftls *t, ffstr *data, uint *version, ffstr *body);
static int tls_hshake_read(fftls *t, ffstr *data);
static int tls_clihello_read(fftls *t, ffstr *data);
static int tls_srvhello_read(fftls *t, ffstr *data);
static int tlsexts_data(fftls *t, ffstr *data, ffstr *body);
static int tlsext_read(fftls *t, ffstr *data);
static int tlsext_servname_read(fftls *t, ffstr *data, ffstr *hostname);
static int tlsext_clihel_alpn_read(fftls *t, ffstr *data, ffstr *alpn_protos);
static int tlsext_clihel_suppvers_read(fftls *t, ffstr *data, uint *version);
static int tlsext_srvhel_suppvers_read(fftls *t, ffstr *data, uint *version);
static int tls_certs_read(fftls *t, ffstr *data, ffstr *cert);


struct random {
	byte gmt_unix_time[4];
	byte random[28];
};

struct sess_id {
	byte len; //0..32
	byte data[0];
};


/** Read number, shifting input data. */

static int datalen8(ffstr *data)
{
	if (1 > data->len)
		return -1;
	uint n = data->ptr[0];
	if (1 + n > data->len)
		return -1;
	ffstr_shift(data, 1);
	return n;
}

static int datalen16(ffstr *data)
{
	if (2 > data->len)
		return -1;
	uint n = ffint_ntoh16(data->ptr);
	if (2 + n > data->len)
		return -1;
	ffstr_shift(data, 2);
	return n;
}

static int datalen24(ffstr *data)
{
	if (3 > data->len)
		return -1;
	uint n = ffint_ntoh24(data->ptr);
	if (3 + n > data->len)
		return -1;
	ffstr_shift(data, 3);
	return n;
}


enum tls_rec_type {
	RT_HANDSHAKE = 22,
};

struct rec {
	byte type; //enum tls_rec_type
	byte ver[2]; //3,1 - TLSv1.0
	byte len[2];
	byte data[0];
};

/**
Return enum tls_rec_type;  <=0 on error. */
static int tls_rec_read(fftls *t, ffstr *data, uint *version, ffstr *body)
{
	if (sizeof(struct rec) > data->len)
		return 0;
	const struct rec *rec = (void*)data->ptr;

	*version = ffint_ntoh16(rec->ver);
	if (*version < 0x0301 || (*version & 0xff00) != 0x0300)
		return -FFTLS_EVERSION;

	uint n = ffint_ntoh16(rec->len);
	if (n > data->len)
		return 0;

	ffstr_shift(data, sizeof(struct rec) + n);
	ffstr_set(body, rec->data, n);
	return rec->type;
}


enum hshake_type {
	HS_CLIENT_HELLO = 1,
	HS_SERVER_HELLO = 2,
	HS_CERTIFICATE = 11,
	HS_SERVER_KEY_EXCHANGE = 12,
	HS_CERTIFICATE_REQUEST = 13,
	HS_SERVER_HELLO_DONE = 14,
};

/* type=RT_HANDSHAKE */
struct hshake {
	byte type; //enum hshake_type
	byte len[3];
	byte data[0];
};

/**
Return enum hshake_type;  <=0 on error. */
static int tls_hshake_read(fftls *t, ffstr *data)
{
	if (sizeof(struct hshake) > data->len)
		return 0;
	const struct hshake *h = (void*)data->ptr;
	ffstr_shift(data, sizeof(struct hshake));

	uint n = ffint_ntoh24(h->len);
	if (n > data->len)
		return 0;

	return h->type;
}


/* type=HS_CLIENT_HELLO */
struct client_hello {
	byte ver[2];
	struct random random;
	struct sess_id session_id;
	// list16 cipher_suites;
	// list8 compression_methods;
	// list16 exts;
};

struct cipher_suite {
	byte cipher[2];
};

/**
Return 1 on success;  <=0 on error. */
static int tls_clihello_read(fftls *t, ffstr *data)
{
	const struct client_hello *h = (void*)data->ptr;
	int n;

	if (sizeof(struct client_hello) > data->len)
		return 0;
	ffstr_shift(data, sizeof(struct client_hello));

	uint ver = ffint_ntoh16(h->ver);
	if (ver < t->version || (ver & 0xff00) != 0x0300)
		return -FFTLS_EVERSION;
	t->version = ver;

	if (h->session_id.len > data->len)
		return 0;
	ffstr_set(&t->session_id, h->session_id.data, h->session_id.len);
	ffstr_shift(data, h->session_id.len);

	//cipher_suite[]
	if ((n = datalen16(data)) < 0)
		return 0;
	ffstr_set(&t->ciphers, data->ptr, n);
	ffstr_shift(data, n);

	//comp_meth[]
	if ((n = datalen8(data)) < 0)
		return 0;
	ffstr_shift(data, n);

	return 1;
}


/* type=HS_SERVER_HELLO */
struct server_hello {
	byte ver[2];
	struct random random;
	struct sess_id session_id;
	// byte cipher_suite[2];
	// byte compression_method;
	// list16 exts;
};

/**
Return 1 on success;  <=0 on error. */
static int tls_srvhello_read(fftls *t, ffstr *data)
{
	ffstr d = *data;
	const struct server_hello *h = (void*)d.ptr;

	if (sizeof(struct server_hello) > d.len)
		return 0;
	ffstr_shift(&d, sizeof(struct server_hello));

	uint ver = ffint_ntoh16(h->ver);
	if (ver < t->version || (ver & 0xff00) != 0x0300)
		return -FFTLS_EVERSION;
	t->version = ver;

	if ((uint)h->session_id.len + 2 + 1 > d.len)
		return 0;
	ffstr_set(&t->session_id, h->session_id.data, h->session_id.len);
	ffstr_shift(&d, h->session_id.len);

	ffstr_set(&t->ciphers, d.ptr, 2);
	ffstr_shift(&d, 2);

	// compression_method
	ffstr_shift(&d, 1);

	*data = d;
	return 1;
}


enum ext_type {
	EXT_SERVER_NAME = 0,
	EXT_ALPN = 16,
	EXT_SUPP_VERS = 43,
};

struct ext {
	byte type[2]; //enum ext_type
	byte len[2];
	byte data[0];
};

enum ext_servname_type {
	EXT_SN_HOSTNAME = 0, //byte hostname[]
};

struct ext_servname {
	byte type; //enum ext_servname_type
	byte len[2];
	byte data[0];
};

/**
Return FFTLS_RCLIENT_HELLO_SNI or FFTLS_RDONE;  <=0 on error. */
static int tlsext_servname_read(fftls *t, ffstr *data, ffstr *hostname)
{
	int n;
	if ((n = datalen16(data)) < 0)
		return 0;

	for (;;) {
		const struct ext_servname *sn = (void*)data->ptr;
		if (sizeof(struct ext_servname) > data->len)
			break;
		n = ffint_ntoh16(sn->len);
		if ((uint)n > data->len)
			return 0;

		switch (sn->type) {
		case EXT_SN_HOSTNAME:
			ffstr_set(hostname, sn->data, n);
			return FFTLS_RCLIENT_HELLO_SNI;
		}

		ffstr_shift(data, sizeof(struct ext_servname) + n);
	}

	return FFTLS_RDONE;
}


struct alpn_proto {
	byte len;
	char data[0];
};

/** Get the list of ALPN protocols in a single buffer.
Return FFTLS_RHELLO_ALPN on success;  <=0 on error. */
static int tlsext_clihel_alpn_read(fftls *t, ffstr *data, ffstr *alpn_protos)
{
	int n;
	if ((n = datalen16(data)) < 0)
		return 0;

	ffstr_set(alpn_protos, data->ptr, n);
	return FFTLS_RHELLO_ALPN;
}


struct clihel_suppvers {
	byte len;
	byte data[0]; //ushort[]
};

/** Update TLS version from Client Hello 'supported-versions' extension data. */
static int tlsext_clihel_suppvers_read(fftls *t, ffstr *data, uint *version)
{
	int n;
	if ((n = datalen8(data)) < 0)
		return 0;
	for (uint i = 0;  i != data->len;  i += 2) {
		uint ver = ffint_ntoh16(data->ptr + i);
		if (ver > *version)
			*version = ver;
	}
	return FFTLS_RDONE;
}

struct srvhel_suppvers {
	byte data[2];
};

/** Update TLS version from Server Hello 'supported-versions' extension data. */
static int tlsext_srvhel_suppvers_read(fftls *t, ffstr *data, uint *version)
{
	if (sizeof(struct srvhel_suppvers) > data->len)
		return 0;
	uint ver = ffint_ntoh16(data->ptr);
	if (ver > *version)
		*version = ver;
	return FFTLS_RDONE;
}



/** Parse TLS extension.
Return enum FFTLS_R on success;  <=0 on error. */
static int tlsext_read(fftls *t, ffstr *data)
{
	int r = FFTLS_RDONE;
	uint n, type;
	const struct ext *ext = (void*)data->ptr;
	if (sizeof(struct ext) > data->len)
		return 0;
	ffstr_shift(data, sizeof(struct ext));
	n = ffint_ntoh16(ext->len);
	if (n > data->len)
		return 0;
	ffstr_shift(data, n);

	ffstr extdata;
	ffstr_set(&extdata, ext->data, n);

	type = ffint_ntoh16(ext->type);
	switch (type) {
	case EXT_SERVER_NAME:
		r = tlsext_servname_read(t, &extdata, &t->hostname);
		break;
	case EXT_ALPN:
		r = tlsext_clihel_alpn_read(t, &extdata, &t->alpn_protos);
		break;
	case EXT_SUPP_VERS:
		if (t->hshake_type == HS_CLIENT_HELLO)
			r = tlsext_clihel_suppvers_read(t, &extdata, &t->version);
		else
			r = tlsext_srvhel_suppvers_read(t, &extdata, &t->version);
		break;
	}

	return r;
}

/** Get data for TLS extensions.
Return FFTLS_RDONE on success;  <=0 on error. */
static int tlsexts_data(fftls *t, ffstr *data, ffstr *body)
{
	int n;
	if ((n = datalen16(data)) < 0)
		return 0;
	ffstr_set(body, data->ptr, n);
	ffstr_shift(data, n);
	return FFTLS_RDONE;
}


struct cert {
	byte len[3];
	byte data[0];
};

/** Parse certificates.
Note: returns early after the first certificate.
Return FFTLS_RCERT or FFTLS_RDONE on success;  <=0 on error. */
static int tls_certs_read(fftls *t, ffstr *data, ffstr *cert)
{
	ffstr d = *data;
	int n;

	if ((n = datalen24(&d)) < 0)
		return 0;
	ffstr_shift(data, 3 + n);

	for (;;) {
		if ((n = datalen24(&d)) < 0)
			return 0;

		ffstr_set(cert, d.ptr, n);
		return FFTLS_RCERT;
	}

	return FFTLS_RDONE;
}

#define ERR(t, r) \
	(t)->err = -(r),  FFTLS_RERR

int fftls_read(fftls *t)
{
	enum { R_REC, R_HSHAKE, R_CLIHEL, R_SRVHEL, R_HEL_EXTS, R_HEL_EXT, R_CERTS, };
	int r;

	for (;;) {
	switch (t->state) {

	case R_REC:
		r = tls_rec_read(t, &t->in, &t->version, &t->buf);
		if (r == 0)
			return FFTLS_RMORE;
		else if (r < 0)
			return ERR(t, -r);

		switch (r) {
		case RT_HANDSHAKE:
			t->state = R_HSHAKE;
			break;
		default:
			return ERR(t, FFTLS_ENOTSUPP);
		}
		break;

	case R_HSHAKE:
		r = tls_hshake_read(t, &t->buf);
		if (r <= 0)
			return ERR(t, -r);
		t->hshake_type = r;

		switch (r) {
		case HS_CLIENT_HELLO:
			t->state = R_CLIHEL;
			break;

		case HS_SERVER_HELLO:
			t->state = R_SRVHEL;
			break;

		case HS_CERTIFICATE:
			t->state = R_CERTS;
			break;

		case HS_SERVER_KEY_EXCHANGE:
			t->state = R_REC;
			return FFTLS_RKEY_EXCH;

		case HS_CERTIFICATE_REQUEST:
			t->state = R_REC;
			return FFTLS_RCERT_REQ;

		case HS_SERVER_HELLO_DONE:
			t->state = R_REC;
			return FFTLS_RSERV_HELLO_DONE;

		default:
			return ERR(t, FFTLS_ENOTSUPP);
		}
		break;

	case R_CLIHEL:
		r = tls_clihello_read(t, &t->buf);
		if (r <= 0)
			return ERR(t, -r);
		t->state = R_HEL_EXTS;
		return FFTLS_RCLIENT_HELLO;

	case R_SRVHEL:
		r = tls_srvhello_read(t, &t->buf);
		if (r <= 0)
			return ERR(t, -r);
		t->state = R_HEL_EXTS;
		return FFTLS_RSERVER_HELLO;

	case R_HEL_EXTS: {
		ffstr exts;
		r = tlsexts_data(t, &t->buf, &exts);
		if (r <= 0)
			return ERR(t, -r);
		t->buf = exts;
		t->state = R_HEL_EXT;
		break;
	}

	case R_HEL_EXT:
		if (t->buf.len == 0) {
			t->state = R_REC;
			return FFTLS_RDONE;
		}

		r = tlsext_read(t, &t->buf);
		if (r <= 0)
			return ERR(t, -r);
		else if (r != FFTLS_RDONE)
			return r;
		break;

	case R_CERTS:
		r = tls_certs_read(t, &t->buf, &t->cert);
		if (r <= 0)
			return FFTLS_RERR;
		t->state = R_REC;
		return FFTLS_RCERT;

	}
	}
}


static const char tls_vers[][8] = {
	"TLSv1", "TLSv1.1", "TLSv1.2", "TLSv1.3"
};

const char* fftls_verstr(uint ver)
{
	if (ver >= 0x0301 && ver <= 0x0304)
		return tls_vers[ver];
	return "unknown";
}


int fftls_alpn_next(ffstr *buf, ffstr *proto)
{
	if (buf->len == 0)
		return 0;
	uint len = (byte)buf->ptr[0];
	if (len > buf->len)
		return 0;
	ffstr_set(proto, buf->ptr + 1, len);
	len++;
	ffstr_shift(buf, len);
	return len;
}


int fftls_alert(void *buffer, size_t cap, const void *data, size_t len)
{
	if (cap < sizeof(struct rec) + len)
		return -1;
	struct rec *r = buffer;
	r->type = 21;
	r->ver[0] = 0x03;
	r->ver[1] = 0x01;
	ffint_hton16(r->len, len);
	ffmemcpy(r->data, data, len);
	return sizeof(struct rec) + len;
}


struct cipher_map {
	byte id[2];
	char name[32];
};

static const struct cipher_map ssl_ciphers_tbl[] = {
	/* Modern compatibility.
	For services that don't need backward compatibility, the parameters below provide a higher level of security.
	This configuration is compatible with Firefox 27, Chrome 30, IE 11 on Windows 7, Edge, Opera 17, Safari 9, Android 5.0, and Java 8. */
	{ {0xc0,0x2c}, "ECDHE-ECDSA-AES256-GCM-SHA384" },
	{ {0xc0,0x30}, "ECDHE-RSA-AES256-GCM-SHA384" },
	{ {0xcc,0xa9}, "ECDHE-ECDSA-CHACHA20-POLY1305" },
	{ {0xcc,0xa8}, "ECDHE-RSA-CHACHA20-POLY1305" },
	{ {0xc0,0x2b}, "ECDHE-ECDSA-AES128-GCM-SHA256" },
	{ {0xc0,0x2f}, "ECDHE-RSA-AES128-GCM-SHA256" },
	{ {0xc0,0x24}, "ECDHE-ECDSA-AES256-SHA384" },
	{ {0xc0,0x28}, "ECDHE-RSA-AES256-SHA384" },
	{ {0xc0,0x23}, "ECDHE-ECDSA-AES128-SHA256" },
	{ {0xc0,0x27}, "ECDHE-RSA-AES128-SHA256" },

	/* Intermediate compatibility.
	For services that don't need compatibility with legacy clients (mostly WinXP),
	 but still need to support a wide range of clients, this configuration is recommended.
	It is is compatible with Firefox 1, Chrome 1, IE 7, Opera 5 and Safari 1. */
	{ {0xcc,0xa9}, "ECDHE-ECDSA-CHACHA20-POLY1305" },
	{ {0xcc,0xa8}, "ECDHE-RSA-CHACHA20-POLY1305" },
	{ {0xc0,0x2b}, "ECDHE-ECDSA-AES128-GCM-SHA256" },
	{ {0xc0,0x2f}, "ECDHE-RSA-AES128-GCM-SHA256" },
	{ {0xc0,0x2c}, "ECDHE-ECDSA-AES256-GCM-SHA384" },
	{ {0xc0,0x30}, "ECDHE-RSA-AES256-GCM-SHA384" },
	{ {0x00,0x9e}, "DHE-RSA-AES128-GCM-SHA256" },
	{ {0x00,0x9f}, "DHE-RSA-AES256-GCM-SHA384" },
	{ {0xc0,0x23}, "ECDHE-ECDSA-AES128-SHA256" },
	{ {0xc0,0x27}, "ECDHE-RSA-AES128-SHA256" },
	{ {0xc0,0x09}, "ECDHE-ECDSA-AES128-SHA" },
	{ {0xc0,0x28}, "ECDHE-RSA-AES256-SHA384" },
	{ {0xc0,0x13}, "ECDHE-RSA-AES128-SHA" },
	{ {0xc0,0x24}, "ECDHE-ECDSA-AES256-SHA384" },
	{ {0xc0,0x0a}, "ECDHE-ECDSA-AES256-SHA" },
	{ {0xc0,0x14}, "ECDHE-RSA-AES256-SHA" },
	{ {0x00,0x67}, "DHE-RSA-AES128-SHA256" },
	{ {0x00,0x33}, "DHE-RSA-AES128-SHA" },
	{ {0x00,0x6b}, "DHE-RSA-AES256-SHA256" },
	{ {0x00,0x39}, "DHE-RSA-AES256-SHA" },
	{ {0xc0,0x08}, "ECDHE-ECDSA-DES-CBC3-SHA" },
	{ {0xc0,0x12}, "ECDHE-RSA-DES-CBC3-SHA" },
	{ {0x00,0x16}, "EDH-RSA-DES-CBC3-SHA" },
	{ {0x00,0x9c}, "AES128-GCM-SHA256" },
	{ {0x00,0x9d}, "AES256-GCM-SHA384" },
	{ {0x00,0x3c}, "AES128-SHA256" },
	{ {0x00,0x3d}, "AES256-SHA256" },
	{ {0x00,0x2f}, "AES128-SHA" },
	{ {0x00,0x35}, "AES256-SHA" },
	{ {0x00,0x0a}, "DES-CBC3-SHA" },

	/* Old backward compatibility
	This is the old ciphersuite that works with all clients back to Windows XP/IE6.
	It should be used as a last resort only. */
	{ {0xcc,0xa9}, "ECDHE-ECDSA-CHACHA20-POLY1305" },
	{ {0xcc,0xa8}, "ECDHE-RSA-CHACHA20-POLY1305" },
	{ {0xc0,0x2f}, "ECDHE-RSA-AES128-GCM-SHA256" },
	{ {0xc0,0x2b}, "ECDHE-ECDSA-AES128-GCM-SHA256" },
	{ {0xc0,0x30}, "ECDHE-RSA-AES256-GCM-SHA384" },
	{ {0xc0,0x2c}, "ECDHE-ECDSA-AES256-GCM-SHA384" },
	{ {0x00,0x9e}, "DHE-RSA-AES128-GCM-SHA256" },
	{ {0x00,0xa2}, "DHE-DSS-AES128-GCM-SHA256" },
	{ {0x00,0xa3}, "DHE-DSS-AES256-GCM-SHA384" },
	{ {0x00,0x9f}, "DHE-RSA-AES256-GCM-SHA384" },
	{ {0xc0,0x27}, "ECDHE-RSA-AES128-SHA256" },
	{ {0xc0,0x23}, "ECDHE-ECDSA-AES128-SHA256" },
	{ {0xc0,0x13}, "ECDHE-RSA-AES128-SHA" },
	{ {0xc0,0x09}, "ECDHE-ECDSA-AES128-SHA" },
	{ {0xc0,0x28}, "ECDHE-RSA-AES256-SHA384" },
	{ {0xc0,0x24}, "ECDHE-ECDSA-AES256-SHA384" },
	{ {0xc0,0x14}, "ECDHE-RSA-AES256-SHA" },
	{ {0xc0,0x0a}, "ECDHE-ECDSA-AES256-SHA" },
	{ {0x00,0x67}, "DHE-RSA-AES128-SHA256" },
	{ {0x00,0x33}, "DHE-RSA-AES128-SHA" },
	{ {0x00,0x40}, "DHE-DSS-AES128-SHA256" },
	{ {0x00,0x6b}, "DHE-RSA-AES256-SHA256" },
	{ {0x00,0x38}, "DHE-DSS-AES256-SHA" },
	{ {0x00,0x39}, "DHE-RSA-AES256-SHA" },
	{ {0xc0,0x12}, "ECDHE-RSA-DES-CBC3-SHA" },
	{ {0xc0,0x08}, "ECDHE-ECDSA-DES-CBC3-SHA" },
	{ {0x00,0x16}, "EDH-RSA-DES-CBC3-SHA" },
	{ {0x00,0x9c}, "AES128-GCM-SHA256" },
	{ {0x00,0x9d}, "AES256-GCM-SHA384" },
	{ {0x00,0x3c}, "AES128-SHA256" },
	{ {0x00,0x3d}, "AES256-SHA256" },
	{ {0x00,0x2f}, "AES128-SHA" },
	{ {0x00,0x35}, "AES256-SHA" },
	{ {0x00,0x6a}, "DHE-DSS-AES256-SHA256" },
	{ {0x00,0x32}, "DHE-DSS-AES128-SHA" },
	{ {0x00,0x0a}, "DES-CBC3-SHA" },
	{ {0x00,0x9a}, "DHE-RSA-SEED-SHA" },
	{ {0x00,0x99}, "DHE-DSS-SEED-SHA" },
	{ {0xcc,0x15}, "DHE-RSA-CHACHA20-POLY1305" },
	{ {0xc0,0x77}, "ECDHE-RSA-CAMELLIA256-SHA384" },
	{ {0xc0,0x73}, "ECDHE-ECDSA-CAMELLIA256-SHA384" },
	{ {0x00,0xc4}, "DHE-RSA-CAMELLIA256-SHA256" },
	{ {0x00,0xc3}, "DHE-DSS-CAMELLIA256-SHA256" },
	{ {0x00,0x88}, "DHE-RSA-CAMELLIA256-SHA" },
	{ {0x00,0x87}, "DHE-DSS-CAMELLIA256-SHA" },
	{ {0x00,0xc0}, "CAMELLIA256-SHA256" },
	{ {0x00,0x84}, "CAMELLIA256-SHA" },
	{ {0xc0,0x76}, "ECDHE-RSA-CAMELLIA128-SHA256" },
	{ {0xc0,0x72}, "ECDHE-ECDSA-CAMELLIA128-SHA256" },
	{ {0x00,0xbe}, "DHE-RSA-CAMELLIA128-SHA256" },
	{ {0x00,0xbd}, "DHE-DSS-CAMELLIA128-SHA256" },
	{ {0x00,0x45}, "DHE-RSA-CAMELLIA128-SHA" },
	{ {0x00,0x44}, "DHE-DSS-CAMELLIA128-SHA" },
	{ {0x00,0xba}, "CAMELLIA128-SHA256" },
	{ {0x00,0x41}, "CAMELLIA128-SHA" },
	{ {0x00,0x96}, "SEED-SHA" },
};

const char* fftls_cipher_name(ushort ciph)
{
	for (uint i = 0;  i != FFCNT(ssl_ciphers_tbl);  i++) {
		const struct cipher_map *c = &ssl_ciphers_tbl[i];
		if (*(ushort*)c->id == ciph)
			return c->name;
	}
	return NULL;
}
