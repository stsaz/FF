/** Operations with numbers.
Copyright (c) 2015 Simon Zolin
*/

#pragma once

#include <FFOS/types.h>

#ifdef FF_64
#include <emmintrin.h> //SSE2
#endif


#define FF_SAFEDIV(val, by) \
	((by) != 0 ? (val) / (by) : 0)


static FFINL ushort ffint_ltoh16(const void *p)
{
	return *(ushort*)p;
}

static FFINL uint ffint_ltoh32(const void *p)
{
	return *(uint*)p;
}

static FFINL uint64 ffint_ltoh64(const void *p)
{
	return *(uint64*)p;
}

static FFINL void ffint_htol16(void *dst, ushort i)
{
	*((ushort*)dst) = i;
}

static FFINL void ffint_htol32(void *dst, uint i)
{
	*((uint*)dst) = i;
}

static FFINL void ffint_htol64(void *dst, uint64 i)
{
	*((uint64*)dst) = i;
}


static FFINL void ffint_hton16(void *dst, ushort i)
{
	*((ushort*)dst) = ffhton16(i);
}

static FFINL void ffint_hton24(void *dst, uint i)
{
	byte *b = dst;
	b[0] = (byte)(i >> 16);
	b[1] = (byte)(i >> 8);
	b[2] = (byte)i;
}

static FFINL void ffint_hton32(void *dst, uint i)
{
	*((uint*)dst) = ffhton32(i);
}

static FFINL void ffint_hton64(void *dst, uint64 i)
{
	*((uint64*)dst) = ffhton64(i);
}

static FFINL ushort ffint_ntoh16(const void *p)
{
	return ffhton16(*(ushort*)p);
}

static FFINL uint ffint_ntoh24(const void *p)
{
	const byte *b = p;
	return ((uint)b[0] << 16) | ((uint)b[1] << 8) | (uint)b[2];
}

static FFINL uint ffint_ntoh32(const void *p)
{
	return ffhton32(*(uint*)p);
}

static FFINL uint64 ffint_ntoh64(const void *p)
{
	return ffhton64(*(uint64*)p);
}

static FFINL int ffint_24(const void *p)
{
	return (((int)((char*)p)[2]) << 16) | *(ushort*)p;
}


static FFINL int ffint_ftoi(double d)
{
	int r;

#ifdef FF_64
	r = _mm_cvtsd_si32(_mm_load_sd(&d));

#elif !defined FF_MSVC
	asm volatile("fistpl %0"
		: "=m"(r)
		: "t"(d)
		: "st");

#else
	r = (int)((d < 0) ? d - 0.5 : d + 0.5);
#endif

	return r;
}


FF_EXTN ssize_t ffint_find1(const byte *arr, size_t n, int search);

FF_EXTN ssize_t ffint_find4(const uint *arr, size_t n, uint search);

/** Binary search integer. */
FF_EXTN ssize_t ffint_binfind4(const uint *arr, size_t n, uint search);


FF_EXTN void ffint_sort(uint *arr, size_t n, uint flags);
