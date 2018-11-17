/**
Copyright (c) 2015 Simon Zolin
*/

#include <FF/number.h>
#include <FF/string.h>
#include <FFOS/cpu.h>


ssize_t ffint_find1(const byte *arr, size_t n, int search)
{
	size_t i;
	for (i = 0;  i != n;  i++) {
		if ((uint)search == arr[i])
			return i;
	}
	return -1;
}

ssize_t ffint_find2(const ushort *arr, size_t n, uint search)
{
	size_t i;
	for (i = 0;  i != n;  i++) {
		if (search == arr[i])
			return i;
	}
	return -1;
}

ssize_t ffint_find4(const uint *arr, size_t n, uint search)
{
	size_t i;
	for (i = 0;  i != n;  i++) {
		if (search == arr[i])
			return i;
	}
	return -1;
}

ssize_t ffint_binfind1(const byte *arr, size_t n, uint search)
{
	size_t i, start = 0;
	while (start != n) {
		i = start + (n - start) / 2;
		if (search == arr[i])
			return i;
		else if (search < arr[i])
			n = i;
		else
			start = i + 1;
	}
	return -1;
}

ssize_t ffint_binfind4(const uint *arr, size_t n, uint search)
{
	size_t i, start = 0;
	while (start != n) {
		i = start + (n - start) / 2;
		if (search == arr[i])
			return i;
		else if (search < arr[i])
			n = i;
		else
			start = i + 1;
	}
	return -1;
}


static int _ffint_sortfunc(const void *a, const void *b, void *udata)
{
	const int *i1 = a, *i2 = b;
	return ffint_cmp(*i1, *i2);
}

void ffint_sort(uint *arr, size_t n, uint flags)
{
	ffsort(arr, n, sizeof(uint), &_ffint_sortfunc, NULL);
}

struct sortdata {
	void *tmp;
	size_t sz;
	ffsortcmp cmp;
	void *udata;
};

static void insertion_sort(struct sortdata *sd, void *d, size_t n)
{
	union u {
		char *i1;
		int *i4;
		int64 *i8;
	};
	union u D, T;
	D.i1 = d;
	T.i1 = sd->tmp;

	switch (sd->sz) {
	case 4:
		for (size_t i = 0;  i != n;  i++) {
			ssize_t j;
			*T.i4 = D.i4[i];
			for (j = i - 1;  j >= 0;  j--) {
				if (sd->cmp(&D.i4[j], T.i4, sd->udata) <= 0)
					break;
				D.i4[j + 1] = D.i4[j];
			}
			D.i4[j + 1] = *T.i4;
		}
		break;

	case 8:
		for (size_t i = 0;  i != n;  i++) {
			ssize_t j;
			*T.i8 = D.i8[i];
			for (j = i - 1;  j >= 0;  j--) {
				if (sd->cmp(&D.i8[j], T.i8, sd->udata) <= 0)
					break;
				D.i8[j + 1] = D.i8[j];
			}
			D.i8[j + 1] = *T.i8;
		}
		break;

	default:
		for (size_t i = 0;  i != n;  i++) {
			ssize_t j;
			ffmemcpy(T.i1, D.i1, sd->sz);
			for (j = i - 1;  j >= 0;  j--) {
				if (sd->cmp(D.i1 + j * sd->sz, T.i1, sd->udata) <= 0)
					break;
				ffmemcpy(D.i1 + (j + 1) * sd->sz, D.i1 + j * sd->sz, sd->sz);
			}
			ffmemcpy(D.i1 + (j + 1) * sd->sz, T.i1, sd->sz);
		}
		break;
	}
}

/*
{ n0 n1 nMid ... nLast }
. if the number of elements is small, use insertion sort algorithm
. sort elements in range [n0..nMid)
. sort elements in range [nMid..nLast]
. merge both sorted arrays together into temporary storage
. copy data from temp storage to the original buffer
*/
static void merge_sort(struct sortdata *sd, void *data, size_t n)
{
	if (n <= 1)
		return;
	if (n * sd->sz <= FFCPU_CACHELINE) {
		insertion_sort(sd, data, n);
		return;
	}

	size_t nL = n / 2
		, nR = n - nL
		, sz = sd->sz;
	union u {
		char *i1;
		int *i4;
		int64 *i8;
	};
	union u L, R, T;
	L.i1 = data;
	R.i1 = (char*)data + nL * sz;
	T.i1 = sd->tmp;

	merge_sort(sd, L.i1, nL);
	merge_sort(sd, R.i1, nR);

	switch (sz) {
	case 4:
		while (nL != 0 && nR != 0) {
			if (sd->cmp(L.i4, R.i4, sd->udata) <= 0) {
				*T.i4++ = *L.i4++;
				nL--;
			} else {
				*T.i4++ = *R.i4++;
				nR--;
			}
		}
		break;

	case 8:
		while (nL != 0 && nR != 0) {
			if (sd->cmp(L.i8, R.i8, sd->udata) <= 0) {
				*T.i8++ = *L.i8++;
				nL--;
			} else {
				*T.i8++ = *R.i8++;
				nR--;
			}
		}
		break;

	default:
		while (nL != 0 && nR != 0) {
			if (sd->cmp(L.i1, R.i1, sd->udata) <= 0) {
				T.i1 = ffmem_copy(T.i1, L.i1, sz);
				L.i1 += sz;
				nL--;
			} else {
				T.i1 = ffmem_copy(T.i1, R.i1, sz);
				R.i1 += sz;
				nR--;
			}
		}
		break;
	}

	/* copy the left tail to temp storage */
	if (nL != 0)
		ffmemcpy(T.i1, L.i1, nL * sz);

	/* copy all but the right tail to the original buffer */
	ffmemcpy(data, sd->tmp, (n - nR) * sz);
}

enum {
	MAXSTACKTMP = 4096,
};

void ffsort(void *data, size_t n, size_t sz, ffsortcmp cmp, void *udata)
{
	void *tmp;
	if (n * sz <= MAXSTACKTMP)
		tmp = ffmem_stack(n * sz);
	else if (NULL == (tmp = ffmem_alloc(n * sz)))
		return;

	struct sortdata sd = {
		.tmp = tmp,
		.sz = sz,
		.cmp = cmp,
		.udata = udata,
	};
	merge_sort(&sd, data, n);

	if (n * sz > MAXSTACKTMP)
		ffmem_free(tmp);
}
