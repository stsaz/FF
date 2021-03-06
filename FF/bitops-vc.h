
static FFINL ffbool ffbit_test32(uint i, uint bit) {
	return BitTest((long *)&i, bit) != 0;
}

#ifdef FF_64
static FFINL ffbool ffbit_test64(int64 i, uint bit) {
	return BitTest64(&i, bit) != 0;
}
#endif

static FFINL ffbool ffbit_set32(uint *i, uint bit) {
	return BitTestAndSet((long *)i, bit) != 0;
}

#ifdef FF_64
#define ffbit_set64(p, bit)  BitTestAndSet64((int64*)p, bit)
#endif

static FFINL ffbool ffbit_reset32(uint *i, uint bit) {
	return BitTestAndReset((long *)i, bit) != 0;
}

#ifdef FF_64
#define ffbit_reset64(p, bit)  BitTestAndReset64((int64*)p, bit)
#endif

static FFINL uint ffbit_ffs32(uint i) {
	DWORD idx;
	if (!BitScanForward(&idx, i))
		return 0;
	return idx + 1;
}

#ifdef FF_64
static FFINL uint ffbit_ffs64(uint64 i) {
	DWORD idx;
	if (!BitScanForward64(&idx, i))
		return 0;
	return idx + 1;
}
#endif


static FFINL uint ffbit_find32(uint i)
{
	DWORD idx;
	if (!BitScanReverse(&idx, i))
		return 0;
	return 32 - idx;
}

#ifdef FF_64
static FFINL uint ffbit_find64(uint64 i)
{
	DWORD idx;
	if (!BitScanReverse64(&idx, i))
		return 0;
	return 64 - idx;
}
#endif
