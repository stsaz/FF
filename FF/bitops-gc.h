
static FFINL ffbool ffbit_test32(uint i, uint bit)
{
	int oldbit;
	__asm__ volatile("btl %2,%1\n\t"
		"sbbl %0,%0"
		: "=r" (oldbit)
		: "m" (i), "Ir" (bit));
	return oldbit != 0;
}

#ifdef FF_64
static FFINL ffbool ffbit_test64(uint64 i, uint bit)
{
	long oldbit;
	__asm__ volatile("btq %2,%1\n\t"
		"sbb %0,%0"
		: "=r" (oldbit)
		: "m" ((unsigned long)i), "Ir" ((long)bit));
	return oldbit != 0;
}
#endif

static FFINL ffbool ffbit_set32(uint *p, uint bit) {
	int oldbit;
	__asm__ volatile("btsl %2,%1\n\t"
		"sbbl %0,%0"
		: "=r" (oldbit), "+m" (*p)
		: "Ir" (bit));
	return oldbit != 0;
}

#ifdef FF_64
static FFINL ffbool ffbit_set64(uint64 *p, uint bit) {
	long oldbit;
	__asm__ volatile("btsq %2,%1\n\t"
		"sbbq %0,%0"
		: "=r" ((long)oldbit), "+m" (*(long*)p)
		: "Ir" ((long)bit));
	return oldbit != 0;
}
#endif

static FFINL ffbool ffbit_reset32(uint *p, uint bit)
{
	int oldbit;
	__asm__ volatile("btrl %2,%1\n\t"
		"sbbl %0,%0"
		: "=r" (oldbit), "+m" (*p)
		: "Ir" (bit));
	return oldbit != 0;
}

#ifdef FF_64
static FFINL ffbool ffbit_reset64(uint64 *p, uint bit)
{
	long oldbit;
	__asm__ volatile("btrq %2,%1\n\t"
		"sbbq %0,%0"
		: "=r" ((long)oldbit), "+m" (*(long*)p)
		: "Ir" ((long)bit));
	return oldbit != 0;
}
#endif

#define ffbit_ffs32(i)  __builtin_ffs(i)

#ifdef FF_64
#define ffbit_ffs64(i)  __builtin_ffsll(i)
#endif
