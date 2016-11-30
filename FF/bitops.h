/** Bit operations.
Copyright (c) 2013 Simon Zolin
*/

#pragma once

#include <FFOS/types.h>

#if !defined FF_MSVC && !defined FF_MINGW
#include <FF/bitops-gc.h>
#else
#include <FF/bitops-vc.h>
#endif

#if !defined FF_MSVC
#define ffbit_find32(n) \
({ \
	__typeof__(n) N = (n); \
	(N == 0) ? 0 : __builtin_clz(N) + 1; \
})

#if defined FF_64
#define ffbit_find64(n) \
({ \
	__typeof__(n) N = (n); \
	(N == 0) ? 0 : __builtin_clzll(N) + 1; \
})
#endif

#endif //!FF_MSVC

#if defined FF_64
/** bit: 0-63 */
#define ffbit_test(/*size_t*/ i, bit)  ffbit_test64((uint64)(i), bit)

static FFINL ffbool ffbit_set(size_t *p, uint bit) {
	return ffbit_set64((uint64*)p, bit);
}

static FFINL ffbool ffbit_reset(size_t *p, uint bit) {
	return ffbit_reset64((uint64*)p, bit);
}

/** Find the least significant 1-bit. */
#define ffbit_ffs(/*size_t*/ i)  ffbit_ffs64((uint64)(i))

/** Find the most significant 1-bit. */
#define ffbit_find(/*size_t*/ i)  ffbit_find64((uint64)(i))

#else //32 bit:
/** bit: 0-31 */
#define ffbit_test(/*size_t*/ i, bit)  ffbit_test32((uint)(i), (bit))

#define ffbit_test64(/*uint64*/ i, bit)  (((i) & FF_BIT64(bit)) != 0)

static FFINL ffbool ffbit_set(size_t *p, uint bit) {
	return ffbit_set32((uint*)p, bit);
}

static FFINL ffbool ffbit_reset(size_t *p, uint bit) {
	return ffbit_reset32((uint*)p, bit);
}

static FFINL ffbool ffbit_set64(uint64 *p, uint bit) {
	if ((*p & FF_BIT64(bit)) != 0) {
		*p |= FF_BIT64(bit);
		return 1;
	}
	return 0;
}

static FFINL ffbool ffbit_reset64(uint64 *p, uint bit) {
	if ((*p & FF_BIT64(bit)) != 0) {
		*p &= ~FF_BIT64(bit);
		return 1;
	}
	return 0;
}

#define ffbit_ffs(/*size_t*/ i)  ffbit_ffs32((int)i)

static FFINL uint ffbit_ffs64(uint64 i) {
	if ((int)i != 0)
		return ffbit_ffs((int)i);
	if ((int)(i >> 32) != 0)
		return ffbit_ffs((int)(i >> 32)) + 32;
	return 0;
}

#define ffbit_find(/*size_t*/ i)  ffbit_find32((int)(i))

#endif

static FFINL ffbool ffbit_testarr(const uint *ar, uint bit) {
	return ffbit_test32(ar[bit / 32], bit % 32);
}

static FFINL ffbool ffbit_setarr(uint *ar, uint bit) {
	return ffbit_set32(&ar[bit / 32], bit % 32);
}
