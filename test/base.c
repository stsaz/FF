/**
Copyright (c) 2014 Simon Zolin
*/

#include <FF/list.h>
#include <FF/number.h>
#include <FF/ring.h>
#include <FF/sys/taskqueue.h>
#include <FF/array.h>
#include <FF/bitops.h>
#include <FFOS/thread.h>
#include <FFOS/mem.h>
#include <FFOS/test.h>

#include <test/all.h>


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
		if (FF_READONCE(t->q))
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
		FF_WRITEONCE(t->q, 1);
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
		if (FF_READONCE(t->q))
			break;
		int r = fftask_run(&t->tq);
		(void)r;
	}

	ffthd_join(th, -1, NULL);
	ffmem_safefree(t->tsk);
	return 0;
}


int test_bits()
{
	uint64 i8;
	uint i4;
	size_t i;
	uint mask[2] = { 0 };

	i8 = 1;
	x(0 != ffbit_test64(&i8, 0));
	i4 = 1;
	x(0 != ffbit_test32(&i4, 0));
	i = 1;
	x(0 != ffbit_test(&i, 0));

	x(!ffbit_ntest("\x7f\xff\xff\xff", 0));
	x(ffbit_ntest("\x01\x00\x00\x00", 7));
	x(ffbit_ntest("\x00\x00\x00\x80", 3*8));
	x(!ffbit_ntest("\x01\x01\x01\x80", 3*8+1));

	i8 = 0x8000000000000000ULL;
	x(0 != ffbit_set64(&i8, 63));
	x(i8 == 0x8000000000000000ULL);
	i8 = 0;
	x(0 == ffbit_set64(&i8, 63) && i8 == 0x8000000000000000ULL);
	i4 = 0x80000000;
	x(0 != ffbit_set32(&i4, 31));
	x(i4 == 0x80000000);
	i4 = 0;
	x(0 == ffbit_set32(&i4, 31) && i4 == 0x80000000);
	i = 0;
	x(0 == ffbit_set(&i, 31));
	x(i == 0x80000000);

	x(0 == ffbit_setarr(mask, 47));
	x(0 != ffbit_testarr(mask, 47));

	i8 = 0x8000000000000000ULL;
	x(0 != ffbit_reset64(&i8, 63) && i8 == 0);
	i4 = 0x80000000;
	x(0 != ffbit_reset32(&i4, 31) && i4 == 0);
	i = (size_t)-1;
	x(0 != ffbit_reset(&i, 31));

#ifdef FF_64
	i8 = 0x8800000000000000ULL;
	x(63-4 == ffbit_ffs64(i8)-1);
	i8 = 0;
	x(0 == ffbit_ffs64(i8));
#endif
	i4 = 0x88000000;
	x(31-4 == ffbit_ffs32(i4)-1);
	i4 = 0;
	x(0 == ffbit_ffs32(i4));
	i = 0;
	x(0 == ffbit_ffs(i));

	x(4 == ffbit_find64(0x0880000000000000ULL) - 1);
	x(4 == ffbit_find32(0x08800000) - 1);
	x(31 == ffbit_find32(0x00000001) - 1);

	char d[] = {"\xf0\xf0\xf0\xf0\xf0\xf0\xf0\xf0\xf0\xf0\xf0\xf0"};
	x(11*4 == ffbit_count(d, 11));

	x(0x1ffff == ffbit_max(17));
	return 0;
}
