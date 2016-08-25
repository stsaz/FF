/**
Copyright (c) 2015 Simon Zolin
*/

#include <FF/number.h>
#include <FF/string.h>


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


static int _ffint_sortfunc(FF_QSORT_PARAMS)
{
	const int *i1 = a, *i2 = b;
	return (*i1 == *i2) ? 0
		: ((*i1 < *i2) ? -1 : 0);
}

void ffint_sort(uint *arr, size_t n, uint flags)
{
	ff_qsort(arr, n, sizeof(uint), &_ffint_sortfunc, NULL);
}
