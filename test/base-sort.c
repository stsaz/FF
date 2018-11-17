/**
Copyright (c) 2018 Simon Zolin
*/

#include <FF/number.h>
#include <FF/array.h>
#include <FFOS/random.h>
#include <FFOS/test.h>

#include <test/all.h>

#define x FFTEST_BOOL
#define LIBC 1


typedef int64 sorttype;
static uint ncmp;
static int intcmp(const void *a, const void *b, void *udata)
{
	ncmp++;
	sorttype i1 = *(sorttype*)a;
	sorttype i2 = *(sorttype*)b;
	return ffint_cmp(i1, i2);
}

static void dosort(ffarr *a)
{
	ffsort(a->ptr, a->len, sizeof(sorttype), &intcmp, NULL);
}

#ifdef LIBC
static int intcmp_libc(const void *a, const void *b)
{
	ncmp++;
	sorttype i1 = *(sorttype*)a;
	sorttype i2 = *(sorttype*)b;
	return ffint_cmp(i1, i2);
}

static void dosort_libc(ffarr *a)
{
	qsort(a->ptr, a->len, sizeof(sorttype), &intcmp_libc);
}
#endif

static void test_sort_rnd(void (*func)(ffarr *a))
{
	FFTEST_FUNC;
	ffarr a = {};
	ffarr_allocT(&a, 1 << 20, sorttype);
	fftime t;
	fftime_now(&t);
	ffrnd_seed(t.sec);
	for (uint i = 0;  i != a.cap;  i++) {
		sorttype *p = ffarr_pushT(&a, sorttype);
		*p = ffrnd_get();
	}
	a.len = a.cap;

	ncmp = 0;
	FFTEST_TIMECALL(func(&a));
	fffile_fmt(ffstdout, NULL, "  ncmp: %u/%u=%.3F\n"
		, ncmp, (int)a.len, (double)(ncmp) / a.len);

	sorttype *it, prev = -1;
	FFARR_WALKT(&a, it, sorttype) {
		x(*it >= prev);
		prev = *it;
	}
	ffarr_free(&a);
}

static void test_sort_inc(void (*func)(ffarr *a))
{
	FFTEST_FUNC;
	ffarr a = {};
	ffarr_allocT(&a, 1 << 20, sorttype);
	for (uint i = 0;  i != a.cap;  i++) {
		sorttype *p = ffarr_pushT(&a, sorttype);
		*p = i;
	}
	a.len = a.cap;

	ncmp = 0;
	FFTEST_TIMECALL(func(&a));
	fffile_fmt(ffstdout, NULL, "  ncmp: %u/%u=%.3F\n"
		, ncmp, (int)a.len, (double)(ncmp) / a.len);

	sorttype *it, prev = -1;
	FFARR_WALKT(&a, it, sorttype) {
		x(*it >= prev);
		prev = *it;
	}
	ffarr_free(&a);
}

static void test_sort_dec(void (*func)(ffarr *a))
{
	FFTEST_FUNC;
	ffarr a = {};
	ffarr_allocT(&a, 1 << 20, sorttype);
	for (int i = a.cap - 1;  i >= 0;  i--) {
		sorttype *p = ffarr_pushT(&a, sorttype);
		*p = i;
	}
	a.len = a.cap;

	ncmp = 0;
	FFTEST_TIMECALL(func(&a));
	fffile_fmt(ffstdout, NULL, "  ncmp: %u/%u=%.3F\n"
		, ncmp, (int)a.len, (double)(ncmp) / a.len);

	sorttype *it, prev = -1;
	FFARR_WALKT(&a, it, sorttype) {
		x(*it >= prev);
		prev = *it;
	}
	ffarr_free(&a);
}

int test_sort(void)
{
	test_sort_inc(&dosort);
	test_sort_dec(&dosort);
	test_sort_rnd(&dosort);
#ifdef LIBC
	test_sort_inc(&dosort_libc);
	test_sort_dec(&dosort_libc);
	test_sort_rnd(&dosort_libc);
#endif
	return 0;
}
