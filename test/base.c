/**
Copyright (c) 2014 Simon Zolin
*/

#include <FFOS/mem.h>
#include <FFOS/test.h>
#include <FF/list.h>
#include <FF/rbtree.h>
#include <FF/crc.h>
#include <FF/hashtab.h>

#include <test/all.h>

#define x FFTEST_BOOL


static int rbtRm(ffrbtl_node *nod, void *udata)
{
	ffrbtree *tr = udata;
	ffrbtl_rm(tr, nod);
	return 0;
}

int test_rbtlist()
{
	ffrbtree tr;
	enum { NUM = 10000, OFF = 1000, LNUM = 10 };
	int i;
	int n;
	ffrbtl_node *ar;
	fftree_node *nod;

	FFTEST_FUNC;

	ffrbt_init(&tr);
	ar = (ffrbtl_node*)ffmem_calloc(NUM * LNUM, sizeof(ffrbtl_node));
	x(ar != NULL);

	n = 0;
	for (i = OFF; i < NUM; i++) {
		int k;
		for (k = 0; k < LNUM; k++) {
			ar[n].key = i;
			ffrbtl_insert(&tr, &ar[n++]);
		}
	}
	for (i = 0; i < OFF; i++) {
		int k;
		for (k = 0; k < LNUM; k++) {
			ar[n].key = i;
			ffrbtl_insert(&tr, &ar[n++]);
		}
	}

	x(tr.len == NUM * LNUM);

	i = 0;
	FFTREE_WALK(&tr, nod) {
		ffrbtl_node *nl = (ffrbtl_node*)nod;
		fflist_item *li;

		n = 1;
		x(nl->key == i);

		for (li = nl->sib.next;  li != &((ffrbtl_node*)nod)->sib;  li = li->next) {
			nl = ffrbtl_nodebylist(li);
			x(nl->key == i);
			n++;
		}

		x(n == LNUM);

		i++;
	}
	x(i == NUM);

	i = 0;
	FFTREE_WALK(&tr, nod) {
		x(i++ == nod->key);
	}
	x(i == NUM);

	{
		ffrbt_node *rt;
		ffrbt_node *found;

		found = ffrbt_find(&tr, NUM / 3, NULL);
		x(found->key == NUM / 3);

		found = ffrbt_find(&tr, NUM, &rt);
		x(found == NULL && rt->key == NUM - 1);
	}

	for (i = (NUM / 2) * LNUM;  i != (NUM / 2 + OFF) * LNUM;  i++) {
		ffrbtl_rm(&tr, &ar[i]);
	}
	ffrbtl_enumsafe(&tr, (fftree_on_item_t)&rbtRm, &tr, 0);
	x(tr.len == 0);

	ffmem_free(ar);
	return 0;
}


int test_list()
{
	fflist ls;
	fflist_item i1, i2, i3;
	fflist_item *li;
	int n;

	FFTEST_FUNC;

	fflist_init(&ls);
	fflist_ins(&ls, &i1);
	fflist_ins(&ls, &i2);
	x(ls.first == &i1 && ls.last == &i2);
	x(ls.len == 2);

	fflist_movetofront(&ls, &i2);
	x(ls.first == &i2 && ls.last == &i1);
	fflist_moveback(&ls, &i2);
	x(ls.first == &i1 && ls.last == &i2);

	fflist_link(&i3, &i1);
	// i1 -> i3 -> i2
	x(i1.next == &i3 && i3.prev == &i1 && i3.next == &i2);

	n = 0;
	FFLIST_WALKNEXT(&i1, li) {
		switch (n++) {
		case 0:
			x(li == &i1);
			break;
		case 1:
			x(li == &i3);
			break;
		case 2:
			x(li == &i2);
			break;
		}
	}

	fflist_unlink(&i3);
	x(i1.next == &i2 && i2.prev == &i1);

	return 0;
}


typedef struct svc_table_t {
	int port;
	char *svc;
} svc_table_t;

static const svc_table_t svc_table[] = {
	{ 80, "http" }
	, { 8080, "http-alt" }
	, { 443, "https" }
	, { 20, "ftp-data" }
	, { 21, "ftp" }
	, { 22, "ssh" }
	, { 23, "telnet" }
	, { 25, "smtp" }
	, { 110, "pop3" }
	, { 53, "dns" }
	, { 123, "ntp" }
};

static int cmpkey(void *udata, const char *key, size_t klen, void *param) {
	svc_table_t *t = udata;
	return strncmp(t->svc, key, klen);
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
		uint hash = ffcrc32_get(t->svc, strlen(t->svc), 0);
		x(ffhst_ins(&ht, hash, (void*)t) >= 0);
	}

	for (i = 0;  i < FFCNT(svc_table);  i++) {
		const svc_table_t *t = &svc_table[i];
		uint hash = ffcrc32_get(t->svc, strlen(t->svc), 0);
		t = ffhst_find(&ht, hash, t->svc, strlen(t->svc), NULL);
		x(t == &svc_table[i]);
	}

	x(0 == ffhst_walk(&ht, &walk, &n));
	x(n == ht.len);

	ffhst_free(&ht);
	return 0;
}
