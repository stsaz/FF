/** Hash table test.
Copyright (c) 2018 Simon Zolin
*/

#include <FF/hashtab.h>
#include <FF/crc.h>
#include <FFOS/random.h>
#include <FFOS/test.h>


struct route {
	byte ip[4];
	byte mac[6];
};

enum { COUNT = 3000000 };

static int cmpkey2(void *udata, const void *key, void *param)
{
	const struct route *r = udata;
	return memcmp(&r->ip, key, 4);
}

static void hst_speed(ffhstab *ht, ffarr *a)
{
	struct route *r;
	x(0 == ffhst_init(ht, COUNT));
	ht->cmpkey = &cmpkey2;

	FFARR_WALKT(a, r, struct route) {
		uint hash = ffcrc32_get((char*)&r->ip, 4);
		ffhst_ins(ht, hash, r);
	}

	FFARR_WALKT(a, r, struct route) {
		uint hash = ffcrc32_get((char*)&r->ip, 4);
		x(r == ffhst_find(ht, hash, &r->ip, NULL));
	}

	// for (uint i = 0;  i != COUNT;  i++) {
	// 	ffhst_find(ht, i, (char*)&i, 4, NULL);
	// }
}

static void test_htable_large()
{
	ffhstab ht;
	FFTEST_FUNC;
	ffarr a = {};

	ffarr_allocT(&a, COUNT, struct route);
	a.len = COUNT;
	struct route *r;
	fftime t;
	fftime_now(&t);
	ffrnd_seed(t.sec);
	uint rnd = ffrnd_get();
	uint iter = rnd;
	FFARR_WALKT(&a, r, struct route) {
		uint n = ffint_bswap32(iter);
		memcpy(r->ip, &n, 4);
		r->ip[0] = ffrnd_get();
		memcpy(r->mac, &iter, 4);
		memcpy(r->mac + 4, &iter, 2);
		iter += 1;
	}

	FFTEST_TIMECALL(hst_speed(&ht, &a));

	fffile_fmt(ffstdout, NULL, "hst size:%L/%L  coll:%L  maxcoll:%L\n"
		, ht.len, ht.nslots, ht.ncoll, ht.maxcoll);

	ffhst_free(&ht);
	ffarr_free(&a);
}

typedef struct svc_table_t {
	int port;
	char *svc;
} svc_table_t;

static const svc_table_t svc_table[] = {
	{ 80, "http" },
	{ 8080, "http-alt" },
	{ 443, "https" },
	{ 20, "ftp-data" },
	{ 21, "ftp" },
	{ 22, "ssh" },
	{ 23, "telnet" },
	{ 25, "smtp" },
	{ 110, "pop3" },
	{ 53, "dns" },
	{ 123, "ntp" },
};

static int cmpkey(void *udata, const void *key, void *param)
{
	svc_table_t *t = udata;
	const ffstr *k = key;
	return !ffstr_eqz(k, t->svc);
}

static int walk(void *udata, void *param) {
	int *n = param;
	(*n)++;
	return 0;
}

int test_htable()
{
	ffhstab ht;
	size_t i;
	int n = 0;

	FFTEST_FUNC;

	x(0 == ffhst_init(&ht, FFCNT(svc_table)));
	ht.cmpkey = &cmpkey;

	for (i = 0;  i < FFCNT(svc_table);  i++) {
		const svc_table_t *t = &svc_table[i];
		uint hash = ffcrc32_get(t->svc, strlen(t->svc));
		x(ffhst_ins(&ht, hash, (void*)t) >= 0);
	}

	for (i = 0;  i < FFCNT(svc_table);  i++) {
		const svc_table_t *t = &svc_table[i];
		uint hash = ffcrc32_get(t->svc, strlen(t->svc));
		ffstr k;
		ffstr_setz(&k, t->svc);
		t = ffhst_find(&ht, hash, &k, NULL);
		x(t == &svc_table[i]);
	}

	x(0 == ffhst_walk(&ht, &walk, &n));
	x(n == ht.len);

	ffarr a = {0};
	ffhst_print(&ht, &a);
	ffarr_free(&a);
	ffhst_print(&ht, NULL);

	ffhst_free(&ht);

	test_htable_large();
	return 0;
}
