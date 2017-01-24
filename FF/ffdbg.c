/**
Copyright (c) 2017 Simon Zolin
*/


#ifdef _DEBUG
#include <FF/string.h>
#include <FFOS/file.h>
#include <FFOS/atomic.h>

static ffatomic counter;
int ffdbg_mask = 1;
int ffdbg_print(int t, const char *fmt, ...)
{
	char buf[4096];
	size_t n;
	va_list va;
	va_start(va, fmt);

	n = ffs_fmt(buf, buf + FFCNT(buf), "%p#%L "
		, &counter, (size_t)ffatom_incret(&counter));

	n += ffs_fmtv(buf + n, buf + FFCNT(buf), fmt, va);
	fffile_write(ffstdout, buf, n);

	va_end(va);
	return 0;
}
#endif
