/** Bit operations.
Copyright (c) 2013 Simon Zolin
*/

#pragma once

#include <FFOS/types.h>


/* 32-bit */

static FFINL ffbool ffbit_test32(const uint *p, uint bit)
{
	FF_ASSERT(bit <= 31);
	return ((*p & FF_BIT32(bit)) != 0);
}

static FFINL ffbool ffbit_set32(uint *p, uint bit)
{
	FF_ASSERT(bit <= 31);
	if ((*p & FF_BIT32(bit)) == 0) {
		*p |= FF_BIT32(bit);
		return 0;
	}
	return 1;
}

static FFINL ffbool ffbit_reset32(uint *p, uint bit)
{
	FF_ASSERT(bit <= 31);
	if ((*p & FF_BIT32(bit)) != 0) {
		*p &= ~FF_BIT32(bit);
		return 1;
	}
	return 0;
}


/* 64-bit */

static FFINL ffbool ffbit_test64(const uint64 *p, uint bit)
{
	FF_ASSERT(bit <= 63);
	return ((*p & FF_BIT64(bit)) != 0);
}

static FFINL ffbool ffbit_set64(uint64 *p, uint bit)
{
	FF_ASSERT(bit <= 63);
	if ((*p & FF_BIT64(bit)) == 0) {
		*p |= FF_BIT64(bit);
		return 0;
	}
	return 1;
}

static FFINL ffbool ffbit_reset64(uint64 *p, uint bit)
{
	FF_ASSERT(bit <= 63);
	if ((*p & FF_BIT64(bit)) != 0) {
		*p &= ~FF_BIT64(bit);
		return 1;
	}
	return 0;
}


#if defined FF_64

/* size_t -> uint64
bit: 0..63 */

static FFINL ffbool ffbit_test(const size_t *p, uint bit)
{
	return ffbit_test64((uint64*)p, bit);
}

static FFINL ffbool ffbit_set(size_t *p, uint bit)
{
	return ffbit_set64((uint64*)p, bit);
}

static FFINL ffbool ffbit_reset(size_t *p, uint bit)
{
	return ffbit_reset64((uint64*)p, bit);
}

/** Find the least significant 1-bit.
0xABCD <--
Return position +1;  0 if not found. */
#define ffbit_ffs(/*size_t*/ i)  ffbit_ffs64(i)

/** Find the most significant 1-bit.
--> 0xABCD
Return position +1;  0 if not found. */
#define ffbit_find(/*size_t*/ i)  ffbit_find64(i)

#else //32 bit:

/* size_t -> uint
bit: 0..31 */

static FFINL ffbool ffbit_test(const size_t *p, uint bit)
{
	return ffbit_test32((uint*)p, bit);
}

static FFINL ffbool ffbit_set(size_t *p, uint bit)
{
	return ffbit_set32((uint*)p, bit);
}

static FFINL ffbool ffbit_reset(size_t *p, uint bit)
{
	return ffbit_reset32((uint*)p, bit);
}

#define ffbit_ffs(/*size_t*/ i)  ffbit_ffs32(i)

#define ffbit_find(/*size_t*/ i)  ffbit_find32(i)


/* uint64 operations on 32-bit CPU */

static FFINL uint ffbit_ffs64(uint64 i)
{
	if ((int)i != 0)
		return ffbit_ffs32((int)i);
	if ((int)(i >> 32) != 0)
		return ffbit_ffs32((int)(i >> 32)) + 32;
	return 0;
}

#endif //FF_64

#define ffbit_testset32  ffbit_set32
#define ffbit_testset64  ffbit_set64
#define ffbit_testset  ffbit_set

static FFINL ffbool ffbit_testarr(const uint *ar, uint bit)
{
	return ffbit_test32(&ar[bit / 32], bit % 32);
}

static FFINL ffbool ffbit_setarr(uint *ar, uint bit)
{
	return ffbit_set32(&ar[bit / 32], bit % 32);
}

/** Get the number of bits set */
FF_EXTN size_t ffbit_count(const void *d, size_t len);

/** Get the number of bytes needed to hold the specified number of bits */
#define ffbit_nbytes(bits)  (((bits) + 7) / 8)

/** Get maximum value by bits number (16 -> 0xffff). */
#define ffbit_max(bits)  ((1ULL << (bits)) - 1)
