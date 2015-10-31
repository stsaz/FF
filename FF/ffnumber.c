/**
Copyright (c) 2015 Simon Zolin
*/

#include <FF/number.h>


ssize_t ffint_find1(const byte *arr, size_t n, int search)
{
	size_t i;
	for (i = 0;  i != n;  i++) {
		if ((uint)search == arr[i])
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
