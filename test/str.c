/**
Copyright (c) 2013 Simon Zolin
*/

#include <FFOS/test.h>
#include <FF/array.h>

#include <test/all.h>

#define x FFTEST_BOOL

int test_strqcat()
{
	ffsyschar buf[255];
	ffsyschar *p = buf;
	const ffsyschar *end = buf + FFCNT(buf);

	p = ffq_copyc(p, end, 'a');
	p = ffq_copyz(p, end, TEXT("sd"));
	p = ffq_copy(p, end, TEXT("faz"), 2);
	p = ffq_copyc(p, end, '\0');
	x(0 == ffq_cmp2(buf, TEXT("asdfa")));

	p = ffq_copys(buf, end, FFSTR("ASDFA"));
	x(0 == ffq_cmp2(buf, TEXT("ASDFA")));

	return 0;
}

int test_strcat()
{
	char buf[255];
	char *p = buf;
	const char *end = buf + FFCNT(buf);

	p = ffs_copyc(p, end, 'a');
	p = ffs_copyz(p, end, "sd");
	p = ffs_copy(p, end, "faz", 2);
	p = ffs_copyc(p, end, '\0');
	x(0 == strcmp(buf, "asdfa"));

	p = ffs_copyq(buf, end, FFSTRQ("ASDFA"));
	x(0 == strcmp(buf, "ASDFA"));

	p = NULL;
	end = NULL;
	p = ffs_copyc(p, end, 'a');
	p = ffs_copyz(p, end, "sd");
	p = ffs_copy(p, end, "faz", 2);
	p = ffs_copyc(p, end, '\0');
	x(p == NULL);

	return 0;
}

static int test_arr()
{
	ffarr ar;
	ffstr str;
	char *ptr = "asdf";
	int n = 4;
	char *p;
	int i;

	ffarr_null(&ar);
	ffarr_set(&ar, ptr, n);
	x('f' == ffarr_back(&ar));
	x(ptr + n == ffarr_end(&ar));
	ar.cap = n;
	x(0 == ffarr_unused(&ar));
	ffarr_shift(&ar, 1);
	x(ar.len == n - 1 && ar.ptr == ptr + 1);

	ffarr_set(&ar, ptr, n);
	i = 0;
	FFARR_WALK(&ar, p) {
		i++;
	}
	x(i == n);
	i = 0;

	FFARR_RWALK(&ar, p) {
		i++;
	}
	x(i == n);

	ffstr_set(&str, ptr, n);
	x(ffstr_eq(&str, FFSTR("asdf")));
	x(!ffstr_eq(&str, FFSTR("asd")));
	x(!ffstr_eq(&str, FFSTR("zxcv")));
	x(!ffstr_eq(&str, FFSTR("asdfz")));
	x(ffstr_ieq(&str, FFSTR("ASDF")));

	x(ffstr_eqz(&str, "asdf"));
	x(!ffstr_eqz(&str, "asdfz"));

	{
		ffstr s3 = { 0 };
		ffstr_set(&s3, FFSTR("ASDF"));
		x(ffstr_ieq2(&str, &s3));
	}

	x(ffstr_match(&str, FFSTR("asd")));
	x(!ffstr_match(&str, FFSTR("asdz")));
	x(ffstr_imatch(&str, FFSTR("ASD")));

	p = ffstr_alloc(&str, 4);
	x(p == str.ptr);
	ffstr_free(&str);

	x(NULL != ffarr_alloc(&ar, 4));
	x(NULL != ffarr_realloc(&ar, 8));
	x(NULL != ffarr_append(&ar, "asdf", 4));
	x(ffstr_eq((ffstr*)&ar, FFSTR("asdf")));
	ffarr_free(&ar);

	return 0;
}

int test_inttostr()
{
	char s[FFINT_MAXCHARS];
	ffstr ss;
	ss.ptr = s;

	ss.len = ffs_fromint((uint64)-1, s, FFCNT(s), FFINT_SIGNED);
	x(ffstr_eq(&ss, FFSTR("-1")));

	ss.len = ffs_fromint((uint64)-1, s, FFCNT(s), 0);
	x(ffstr_eq(&ss, FFSTR("18446744073709551615")));

	ss.len = ffs_fromint(-1, s, FFCNT(s), FFINT_HEXUP);
	x(ffstr_eq(&ss, FFSTR("FFFFFFFFFFFFFFFF")));

	ss.len = ffs_fromint(0xabc1, s, FFCNT(s), FFINT_HEXLOW | FFINT_ZEROWIDTH | FFINT_WIDTH(8));
	x(ffstr_eq(&ss, FFSTR("0000abc1")));

	ss.len = ffs_fromint(0xabc1, s, FFCNT(s), FFINT_HEXLOW | FFINT_SPACEWIDTH | FFINT_WIDTH(8));
	x(ffstr_eq(&ss, FFSTR("    abc1")));

	return 0;
}

int test_strtoint()
{
	union {
		int64 i8;
		uint64 ui8;
		int i4;
		uint ui4;
		short i2;
		ushort ui2;
		byte ui1;
	} u;

	x(0 == ffs_toint(FFSTR(""), &u.i8, FFS_INT64));

	x(FFSLEN("3213213213213213123") == ffs_toint(FFSTR("3213213213213213123"), &u.i8, FFS_INT64)
		&& u.ui8 == 3213213213213213123ULL);
	x(FFSLEN("18446744073709551615") == ffs_toint(FFSTR("18446744073709551615"), &u.i8, FFS_INT64)
		&& u.ui8 == 18446744073709551615ULL);
	x(FFSLEN("-123456789") == ffs_toint(FFSTR("-123456789"), &u.i8, FFS_INT64 | FFS_INTSIGN)
		&& u.i8 == -123456789);
	x(FFSLEN("-123456789") == ffs_toint(FFSTR("-123456789"), &u.i8, FFS_INT32 | FFS_INTSIGN)
		&& u.i4 == -123456789);

	x(FFSLEN("-1") == ffs_toint(FFSTR("-1"), &u.i8, FFS_INT64 | FFS_INTSIGN)
		&& u.i8 == -1);
	x(FFSLEN("+1") == ffs_toint(FFSTR("+1"), &u.i8, FFS_INT64 | FFS_INTSIGN)
		&& u.i8 == 1);

	x(FFSLEN("-3123456789") == ffs_toint(FFSTR("-3123456789"), &u.i8, FFS_INT64 | FFS_INTSIGN)
		&& u.i8 == -3123456789LL);
	x(0 == ffs_toint(FFSTR("-3123456789"), &u.i8, FFS_INT64));
	x(0 == ffs_toint(FFSTR("-3123456789"), &u.i8, FFS_INT32 | FFS_INTSIGN));

	x(FFSLEN("65535") == ffs_toint(FFSTR("65535"), &u.i8, FFS_INT16)
		&& u.ui2 == 65535);
	x(0 == ffs_toint(FFSTR("65536"), &u.i8, FFS_INT16));

	x(FFSLEN("255") == ffs_toint(FFSTR("255"), &u.i8, FFS_INT8)
		&& u.ui1 == 255);
	x(0 == ffs_toint(FFSTR("256"), &u.i8, FFS_INT8));

	x(FFSLEN("000abc1") == ffs_toint(FFSTR("000abc1"), &u.i8, FFS_INT32 | FFS_INTHEX)
		&& u.ui4 == 0xabc1);
	x(FFSLEN("abcdef") == ffs_toint(FFSTR("abcdef"), &u.i8, FFS_INT32 | FFS_INTHEX)
		&& u.ui4 == 0xabcdef);
	x(FFSLEN("abcdef") == ffs_toint(FFSTR("ABCDEF"), &u.i8, FFS_INT32 | FFS_INTHEX)
		&& u.ui4 == 0xabcdef);
	x(0 == ffs_toint(FFSTR("ffffffffffffffff"), &u.i8, FFS_INT32 | FFS_INTHEX));
	x(FFSLEN("ffffffffffffffff") == ffs_toint(FFSTR("ffffffffffffffff"), &u.i8, FFS_INT64 | FFS_INTHEX)
		&& u.ui8 == 0xffffffffffffffffLL);
	x(0 == ffs_toint(FFSTR("ABCDEg"), &u.i8, FFS_INT32 | FFS_INTHEX));
	x(0 == ffs_toint(FFSTR(":"), &u.i8, FFS_INT32 | FFS_INTHEX));

	return 0;
}

int test_str()
{
	const char *ptr = "asdfa";
	int n = FFSLEN("asdfa");
	const char *end = ptr + n;

	FFTEST_FUNC;

	x(0 == ffs_cmp(ptr, "asdff", 0));
	x(0 == ffs_cmp(ptr, "asdfaz", n));
	x(ffs_cmp(ptr, "asdff", 5) < 0);

	x(0 == ffs_icmp(ptr, "ASDFF", 4));

	x(ptr == ffs_find(ptr, n, 'a'));
	x(ptr + 3 == ffs_find(ptr, n, 'f'));
	x(end == ffs_find(ptr, n, 'z'));

	x(ptr + 4 == ffs_rfind(ptr, n, 'a'));
	x(ptr + 3 == ffs_rfind(ptr, n, 'f'));
	x(end == ffs_rfind(ptr, n, 'z'));

	x(ptr + 3 == ffs_findof(ptr, n, "zxcvf", FFSLEN("zxcvf")));
	x(end == ffs_findof(ptr, n, "zxcv", FFSLEN("zxcv")));

	x(ptr + 4 == ffs_rfindof(ptr, n, "zxcva", FFSLEN("zxcva")));
	x(end == ffs_rfindof(ptr, n, "zxcv", FFSLEN("zxcv")));

#define STR "  asdf  "
	x(STR + 2 == ffs_skip(FFSTR(STR), ' '));
	x(STR + FFSLEN(STR) - 2 == ffs_rskip(FFSTR(STR), ' '));
#undef STR

	test_strcat();
	test_strqcat();
	test_strtoint();
	test_arr();

	{
		char buf[8];
		size_t n;
		x(FFSLEN("asdfasdf") == ffs_replacechar(FFSTR("asdfasdf"), buf, FFCNT(buf), 'a', 'z', &n));
		x(n == 2);
		x(0 == ffs_cmp(buf, "zsdfzsdf", n));
	}

	{
		static const ffstr ss[] = { FFSTR_INIT("qwer"), FFSTR_INIT("asdf") };
		x(0 == ffstr_findarr(ss, FFCNT(ss), FFSTR("qwer")));
		x(1 == ffstr_findarr(ss, FFCNT(ss), FFSTR("asdf")));
		x(-1 == ffstr_findarr(ss, FFCNT(ss), FFSTR("asdfa")));
	}

	{
		ffstr s;
		ffstr v;
		size_t by;
		ffstr_set(&s, FFSTR(" , qwer , asdf , "));
		by = ffstr_nextval(s.ptr, s.len, &v, ',');
		x(by == FFSLEN(" ,"));
		ffstr_shift(&s, by);
		x(ffstr_eq(&v, FFSTR("")));

		by = ffstr_nextval(s.ptr, s.len, &v, ',');
		x(by == FFSLEN(" qwer ,"));
		ffstr_shift(&s, by);
		x(ffstr_eq(&v, FFSTR("qwer")));

		by = ffstr_nextval(s.ptr, s.len, &v, ',');
		x(by == FFSLEN(" asdf ,"));
		ffstr_shift(&s, by);
		x(ffstr_eq(&v, FFSTR("asdf")));

		by = ffstr_nextval(s.ptr, s.len, &v, ',');
		x(by == FFSLEN(" "));
		ffstr_shift(&s, by);
		x(ffstr_eq(&v, FFSTR("")));
	}

	x(ffchar_isname('A') && !ffchar_isname('\xff') && !ffchar_isname('-'));
	x(ffchar_iswhite(' ') && ffchar_iswhite('\xff') && !ffchar_iswhite('-'));

	return 0;
}
