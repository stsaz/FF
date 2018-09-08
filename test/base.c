/**
Copyright (c) 2014 Simon Zolin
*/

#include <FF/list.h>
#include <FF/rbtree.h>
#include <FF/number.h>
#include <FF/ring.h>
#include <FF/sys/taskqueue.h>
#include <FF/array.h>
#include <FFOS/thread.h>
#include <FFOS/mem.h>
#include <FFOS/test.h>

#include <test/all.h>

#define x FFTEST_BOOL


static void test_endian(void)
{
	uint64 n;

#if defined FF_LITTLE_ENDIAN
	n = 0x123456,  x(0x123456 == ffint_ltoh24(&n));
	n = 0x00f23456,  x(0xfff23456 == ffint_ltoh24s(&n));
	n = 0x1234567890abcdefULL,  x(0x1234567890abcdefULL == ffint_ltoh64(&n));

	ffint_htol24(&n, 0x123456),  x((n & 0xffffff) == 0x123456);
	ffint_htol64(&n, 0x1234567890abcdefULL),  x(n == 0x1234567890abcdefULL);

	n = 0x563412,  x(0x123456 == ffint_ntoh24(&n));
	n = 0xefcdab9078563412ULL,  x(0x1234567890abcdefULL == ffint_ntoh64(&n));

	ffint_hton24(&n, 0x563412),  x((n & 0xffffff) == 0x123456);
	ffint_hton64(&n, 0xefcdab9078563412ULL),  x(n == 0x1234567890abcdefULL);
#endif
}

static const uint iarr[] = { 0,1,2,3,4,5 };

int test_num(void)
{
	uint i;

	test_endian();

	FFTEST_FUNC;

	for (i = 0;  i != FFCNT(iarr);  i++) {
		x(i == ffint_binfind4(iarr, FFCNT(iarr), i));
	}

	for (i = 0;  i != FFCNT(iarr) - 1;  i++) {
		x(i == ffint_binfind4(iarr, FFCNT(iarr) - 1, i));
	}

	x(-1 == ffint_binfind4(iarr, FFCNT(iarr), 6));
	return 0;
}


int test_rbt(void)
{
	ffrbtree tr;
	enum { NUM = 10000, OFF = 1000, LNUM = 1 };
	int i;
	int n;
	ffrbt_node *ar;
	fftree_node *nod;

	FFTEST_FUNC;

	ffrbt_init(&tr);
	ar = (ffrbt_node*)ffmem_calloc(NUM * LNUM, sizeof(ffrbt_node));
	x(ar != NULL);

	n = 0;
	for (i = OFF; i < NUM; i++) {
		ar[n].key = i;
		ffrbt_insert(&tr, &ar[n++], NULL);
	}
	for (i = 0; i < OFF; i++) {
		ar[n].key = i;
		ffrbt_insert(&tr, &ar[n++], NULL);
	}

	x(tr.len == NUM * LNUM);

	i = 0;
	FFTREE_WALK(&tr, nod) {
		n = 1;
		x(nod->key == i);
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
		ffrbt_rm(&tr, &ar[i]);
	}
	fftree_node *nod2, *next;
	FFTREE_WALKSAFE(&tr, nod2, next) {
		ffrbt_rm(&tr, (void*)nod2);
	}
	x(tr.len == 0);

	ffmem_free(ar);
	return 0;
}

static int rbtl_rm(ffrbtl_node *nod, void *udata)
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
	ffrbtl_enumsafe(&tr, (fftree_on_item_t)&rbtl_rm, &tr, 0);
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

	ffchain_append(&i3, &i1);
	// i1 -> i3 -> i2
	x(i1.next == &i3 && i3.prev == &i1 && i3.next == &i2);

	n = 0;
	for (li = &i1;  li != fflist_sentl(&ls);  li = li->next) {
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

	ffchain_unlink(&i3);
	x(i1.next == &i2 && i2.prev == &i1);

	return 0;
}


enum {
	RING_CNT = 64 * 1024,
};

static int FFTHDCALL ring_wr(void *param)
{
	ffring *r = param;
	uint i, skip = 0;
	for (i = 0;  i != 16 * RING_CNT;  ) {
		if (0 == ffring_write(r, (void*)(size_t)i))
			i++;
		else
			skip++;
	}
	fffile_fmt(ffstdout, NULL, "wskip: %u\n", skip);
	return 0;
}

int test_ring(void)
{
	ffring r = {0};
	uint i;
	void *val;

	FFTEST_FUNC;

	x(0 == ffring_create(&r, RING_CNT, 4096) && ffring_empty(&r));

	// single-thread
	for (i = 0;  i != RING_CNT - 1;  i++) {
		x(0 == ffring_write(&r, (void*)(size_t)i));
	}
	x(0 != ffring_write(&r, (void*)(size_t)i) && ffring_full(&r));
	x(RING_CNT - 1 == ffring_unread(&r));

	for (i = 0;  i != RING_CNT - 1;  i++) {
		x(0 == ffring_read(&r, &val) && i == (size_t)val);
	}
	x(0 != ffring_read(&r, &val) && ffring_empty(&r));
	x(0 == ffring_unread(&r));

	// multi-thread
	ffthd th[4];
	for (i = 0;  i != 4;  i++)
		th[i] = ffthd_create(&ring_wr, &r, 0);

	uint skip = 0;
	for (i = 0;  i != 4 * 16 * RING_CNT;  ) {
		if (0 == ffring_read(&r, &val))
			i++;
		else
			skip++;
	}
	x(0 != ffring_read(&r, &val));
	fffile_fmt(ffstdout, NULL, "rskip: %u\n", skip);

	for (i = 0;  i != 4;  i++)
		ffthd_join(th[i], -1, NULL);

	ffring_destroy(&r);
	return 0;
}

int test_ringbuf(void)
{
	FFTEST_FUNC;
	char buf[8];
	ffstr s;
	ffringbuf rb;
	ffringbuf_init(&rb, buf, 8);
	x(ffringbuf_empty(&rb));

	// write until full, read all
	ffringbuf_reset(&rb);
	x(3 == ffringbuf_write(&rb, "123", 3));
	x(3 == ffringbuf_canread(&rb));
	x(4 == ffringbuf_write(&rb, "45678", 5));
	x(7 == ffringbuf_canread(&rb));
	x(ffringbuf_full(&rb));
	ffringbuf_readptr(&rb, &s, rb.cap);
	x(ffstr_eqz(&s, "1234567"));
	x(ffringbuf_empty(&rb));

	// write (overwrite) by chunks, read all
	ffringbuf_reset(&rb);
	ffringbuf_overwrite(&rb, "1", 1);
	ffringbuf_readptr(&rb, &s, rb.cap);
	x(ffstr_eqz(&s, "1"));
	ffringbuf_overwrite(&rb, "234", 3);
	ffringbuf_overwrite(&rb, "56789", 5);
	x(ffringbuf_full(&rb));
	ffringbuf_readptr(&rb, &s, rb.cap);
	x(ffstr_eqz(&s, "345678"));
	ffringbuf_readptr(&rb, &s, rb.cap);
	x(ffstr_eqz(&s, "9"));
	x(ffringbuf_empty(&rb));

	// write (overwrite) in 1 chunk, read all
	ffringbuf_reset(&rb);
	x(ffringbuf_empty(&rb));
	ffringbuf_overwrite(&rb, "12345678", 8);
	x(ffringbuf_full(&rb));
	ffringbuf_readptr(&rb, &s, rb.cap);
	x(ffstr_eqz(&s, "2345678"));

	// ffringbuf_read()
	char rbuf[8];
	ffringbuf_reset(&rb);
	x(3 == ffringbuf_write(&rb, "123", 3));
	x(3 == ffringbuf_read(&rb, rbuf, sizeof(rbuf)));
	x(!ffs_cmp(rbuf, "123", 3));
	x(ffringbuf_empty(&rb));
	x(5 == ffringbuf_canwrite_seq(&rb));
	x(7 == ffringbuf_canwrite(&rb));
	x(7 == ffringbuf_write(&rb, "4567890", 7));
	x(0 == ffringbuf_canwrite_seq(&rb));
	x(5 == ffringbuf_canread_seq(&rb));
	x(7 == ffringbuf_canread(&rb));
	x(7 == ffringbuf_read(&rb, rbuf, sizeof(rbuf)));
	x(!ffs_cmp(rbuf, "4567890", 7));
	x(ffringbuf_empty(&rb));

	return 0;
}


struct tq {
	fftaskmgr tq;
	uint q;
	uint cnt;
	fftask *tsk;
};

static int FFTHDCALL tq_wr(void *param)
{
	struct tq *t = param;
	uint i = 0;
	for (;;) {
		if (FF_READONCE(&t->q))
			break;
		int r = fftask_post(&t->tq, &t->tsk[i]);
		i = ffint_cycleinc(i, 1000);
		(void)r;
	}
	return 0;
}

static void tq_func(void *param)
{
	struct tq *t = param;
	t->cnt++;
	if (t->cnt == 10 * 1000000)
		FF_WRITEONCE(&t->q, 1);
}

int test_tq(void)
{
	FFTEST_FUNC;
	struct tq t_s, *t = &t_s;
	ffthd th;

	ffmem_tzero(&t_s);

	t->tsk = ffmem_callocT(1000, fftask);
	fftask_init(&t->tq);

	for (uint i = 0;  i != 1000;  i++) {
		t->tsk[i].handler = &tq_func;
		t->tsk[i].param = t;
	}

	th = ffthd_create(&tq_wr, t, 0);

	for (;;) {
		if (FF_READONCE(&t->q))
			break;
		int r = fftask_run(&t->tq);
		(void)r;
	}

	ffthd_join(th, -1, NULL);
	ffmem_safefree(t->tsk);
	return 0;
}
