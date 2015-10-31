/** Operations with numbers.
Copyright (c) 2015 Simon Zolin
*/

#pragma once

#include <FFOS/types.h>


static FFINL void ffint_hton16(void *dst, ushort i)
{
	*((ushort*)dst) = ffhton16(i);
}

static FFINL void ffint_hton32(void *dst, uint i)
{
	*((uint*)dst) = ffhton32(i);
}

static FFINL ushort ffint_ntoh16(const void *p)
{
	return ffhton16(*(ushort*)p);
}
static FFINL uint ffint_ntoh32(const void *p)
{
	return ffhton32(*(uint*)p);
}



FF_EXTN ssize_t ffint_find1(const byte *arr, size_t n, int search);

FF_EXTN ssize_t ffint_find4(const uint *arr, size_t n, uint search);

/** Binary search integer. */
FF_EXTN ssize_t ffint_binfind4(const uint *arr, size_t n, uint search);
