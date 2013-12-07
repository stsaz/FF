/**
Copyright (c) 2013 Simon Zolin
*/

#include <FFOS/test.h>
#include <FFOS/timer.h>
#include <FF/array.h>
#include <FF/crc.h>
#include <FF/path.h>
#include <FF/bitops.h>

#include <test/all.h>

#define x FFTEST_BOOL
#define CALL FFTEST_TIMECALL

uint _fftest_nrun;
uint _fftest_nfail;

static int test_crc()
{
	x(0x7052c01a == ffcrc32_get(FFSTR("hello, man!"), 0));
	x(0x7052c01a == ffcrc32_get(FFSTR("HELLO, MAN!"), 1));
	return 0;
}

static int test_path()
{
	ffstr s;
	char buf[60];
	size_t n;

	n = ffpath_norm(buf, FFCNT(buf), FFSTR("/path/file"), 0);
	buf[n] = '\0';
	x(0 == strcmp(buf, "/path/file"));

	n = ffpath_norm(buf, FFCNT(buf), FFSTR("//path//file//"), 0);
	buf[n] = '\0';
	x(0 == strcmp(buf, "/path/file/"));

	n = ffpath_norm(buf, FFCNT(buf), FFSTR("/path//..//path2//./file//./"), 0);
	buf[n] = '\0';
	x(0 == strcmp(buf, "/path2/file/"));

#ifdef FF_WIN
	n = ffpath_norm(buf, FFCNT(buf), FFSTR("c:\\path\\\\..//..\\path2//./file//./"), 0);
	buf[n] = '\0';
	x(0 == strcmp(buf, "c:/path2/file/"));
#endif

	n = ffpath_norm(buf, FFCNT(buf), FFSTR("//path/../.././file/./"), 0);
	buf[n] = '\0';
	x(0 == strcmp(buf, "/file/"));

	n = ffpath_norm(buf, FFCNT(buf), FFSTR("/path/.."), FFPATH_STRICT_BOUNDS);
	buf[n] = '\0';
	x(0 == strcmp(buf, "/"));

	n = ffpath_norm(buf, FFCNT(buf), FFSTR("/path/."), FFPATH_STRICT_BOUNDS);
	buf[n] = '\0';
	x(0 == strcmp(buf, "/path/"));

	x(0 == ffpath_norm(buf, FFCNT(buf), FFSTR("/path/../../file/./"), FFPATH_STRICT_BOUNDS));
	x(0 == ffpath_norm(buf, FFCNT(buf), FFSTR("./path/../../file/./"), FFPATH_STRICT_BOUNDS));
	x(0 == ffpath_norm(buf, FFCNT(buf), FFSTR("/.."), FFPATH_STRICT_BOUNDS));
	x(0 == ffpath_norm(buf, FFCNT(buf), FFSTR("../"), FFPATH_STRICT_BOUNDS));
	x(0 == ffpath_norm(buf, FFCNT(buf), FFSTR("/../"), FFPATH_STRICT_BOUNDS));

	x(FFSLEN("filename") == ffpath_makefn(buf, FFCNT(buf), FFSTR("filename"), '_'));
	n = ffpath_makefn(buf, FFCNT(buf), FFSTR("\x00\x1f *?/\\:\""), '_');
	buf[n] = '\0';
	x(0 == strcmp(buf, "__ ______"));

#define FN "/path/file"
	x(FN + FFSLEN(FN) - FFSLEN("/file") == ffpath_rfindslash(FN, FFSLEN(FN)));
	x(TEXT(FN) + FFSLEN(FN) - FFSLEN("/file") == ffpathq_rfindslash(TEXT(FN), FFSLEN(FN)));
#undef FN

#define FN "file"
	x(FN + FFSLEN(FN) == ffpath_rfindslash(FN, FFSLEN(FN)));
#undef FN

	s = ffpath_fileext(FFSTR("file.txt"));
	x(ffstr_eq(&s, FFSTR("txt")));

	s = ffpath_fileext(FFSTR("qwer"));
	x(ffstr_eq(&s, FFSTR("")));

	return 0;
}

static int test_bits()
{
	uint64 i8;
	uint i4;
	size_t i;
	uint mask[2] = { 0 };

	i8 = 1;
	x(0 != ffbit_test64(i8, 0));
	i4 = 1;
	x(0 != ffbit_test32(i4, 0));
	i = 1;
	x(0 != ffbit_test(i, 0));

	i8 = 0x8000000000000000ULL;
	x(0 != ffbit_set64(&i8, 63));
	x(i8 == 0x8000000000000000ULL);
	i4 = 0x80000000;
	x(0 != ffbit_set32(&i4, 31));
	x(i4 == 0x80000000);
	i = 0;
	x(0 == ffbit_set(&i, 31));
	x(i == 0x80000000);

	x(0 == ffbit_setarr(mask, 47));
	x(0 != ffbit_testarr(mask, 47));

	i8 = 0x8000000000000000ULL;
	x(0 != ffbit_reset64(&i8, 63) && i8 == 0);
	i4 = 0x80000000;
	x(0 != ffbit_reset32(&i4, 31) && i4 == 0);
	i = (size_t)-1;
	x(0 != ffbit_reset(&i, 31));

	i8 = 0x8000000000000000ULL;
	x(63 == ffbit_ffs64(i8)-1);
	i8 = 0;
	x(0 == ffbit_ffs64(i8));
	i4 = 0x80000000;
	x(31 == ffbit_ffs32(i4)-1);
	i4 = 0;
	x(0 == ffbit_ffs32(i4));
	i = 0;
	x(0 == ffbit_ffs(i));
	return 0;
}

int test_all()
{
	ffos_init();

	test_bits();
	test_str();
	test_crc();
	test_path();
	test_url();
	test_http();
	CALL(test_json());

	printf("Tests run: %u.  Failed: %u.\n", _fftest_nrun, _fftest_nfail);

	return 0;
}

int main(int argc, const char **argv)
{
	return test_all();
}
