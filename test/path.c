/**
Copyright (c) 2019 Simon Zolin
*/

#include <test/all.h>
#include <FF/path.h>
#include <FFOS/test.h>


static void test_path_norm()
{
	ffstr s;
	char buf[60];
	size_t n;
	s.ptr = buf;

	n = ffpath_norm(buf, FFCNT(buf), FFSTR("/path/file"), 0);
	buf[n] = '\0';
	x(0 == strcmp(buf, "/path/file"));

	n = ffpath_norm(buf, FFCNT(buf), FFSTR("//path//file//"), 0);
	buf[n] = '\0';
	x(0 == strcmp(buf, "/path/file/"));

	n = ffpath_norm(buf, FFCNT(buf), FFSTR("/path//..//path2//./file//./"), 0);
	buf[n] = '\0';
	x(0 == strcmp(buf, "/path2/file/"));

	s.len = ffpath_norm(buf, FFCNT(buf), FFSTR("/."), 0);
	x(ffstr_eqcz(&s, "/"));

	s.len = ffpath_norm(buf, FFCNT(buf), FFSTR("/.."), 0);
	x(ffstr_eqcz(&s, "/"));

	s.len = ffpath_norm(buf, FFCNT(buf), FFSTR("./1"), 0);
	x(ffstr_eqcz(&s, "1"));

	s.len = ffpath_norm(buf, FFCNT(buf), FFSTR("../1"), 0);
	x(ffstr_eqcz(&s, "../1"));

	s.len = ffpath_norm(buf, FFCNT(buf), FFSTR("./1/./2"), 0);
	x(ffstr_eqcz(&s, "1/2"));

	s.len = ffpath_norm(buf, FFCNT(buf), FFSTR("./1/.."), 0);
	x(ffstr_eqcz(&s, "."));

	s.len = ffpath_norm(buf, FFCNT(buf), FFSTR("../../2"), 0);
	x(ffstr_eqcz(&s, "../../2"));

	s.len = ffpath_norm(buf, FFCNT(buf), FFSTR("../1/../2/./3"), FFPATH_MERGEDOTS | FFPATH_TOREL);
	x(ffstr_eqcz(&s, "2/3"));

	s.len = ffpath_norm(buf, FFCNT(buf), FFSTR("/../1/../2"), FFPATH_TOREL);
	x(ffstr_eqcz(&s, "2"));

	s.len = ffpath_norm(buf, FFCNT(buf), FFSTR("/1/2"), FFPATH_TOREL);
	x(ffstr_eqcz(&s, "1/2"));

	s.len = ffpath_norm(buf, FFCNT(buf), FFSTR("c:/../1/../2"), FFPATH_WINDOWS | FFPATH_TOREL);
	x(ffstr_eqcz(&s, "2"));

	n = ffpath_norm(buf, FFCNT(buf), FFSTR("c:\\path\\\\..//..\\path2//./file//./"), FFPATH_MERGEDOTS | FFPATH_FORCESLASH | FFPATH_WINDOWS);
	buf[n] = '\0';
	x(0 == strcmp(buf, "c:/path2/file/"));

	s.len = ffpath_norm(buf, FFCNT(buf), FFSTR("c:\\path/file/"), FFPATH_MERGEDOTS | FFPATH_FORCEBKSLASH | FFPATH_WINDOWS);
	x(ffstr_eqcz(&s, "c:\\path\\file\\"));

	x(0 == ffpath_norm(buf, FFCNT(buf), FFSTR("c:\\path\\file*"), FFPATH_WINDOWS));

	n = ffpath_norm(buf, FFCNT(buf), FFSTR("//path/../.././file/./"), 0);
	buf[n] = '\0';
	x(0 == strcmp(buf, "/file/"));

	n = ffpath_norm(buf, FFCNT(buf), FFSTR("/path/.."), FFPATH_MERGEDOTS | FFPATH_STRICT_BOUNDS);
	buf[n] = '\0';
	x(0 == strcmp(buf, "/"));

	n = ffpath_norm(buf, FFCNT(buf), FFSTR("/path/."), FFPATH_MERGEDOTS | FFPATH_STRICT_BOUNDS);
	buf[n] = '\0';
	x(0 == strcmp(buf, "/path/"));

	n = ffpath_norm(buf, FFCNT(buf), FFSTR("/../a/../..//...///path/a/../path2/a/b//../.."), FFPATH_MERGEDOTS | FFPATH_STRICT_BOUNDS);
	x(0 == ffs_eqcz(buf, n, "/.../path/path2/"));

	x(0 == ffpath_norm(buf, FFCNT(buf), FFSTR("/path/../../file/./"), FFPATH_STRICT_BOUNDS));
	x(0 == ffpath_norm(buf, FFCNT(buf), FFSTR("./path/../../file/./"), FFPATH_STRICT_BOUNDS));
	x(0 == ffpath_norm(buf, FFCNT(buf), FFSTR("/.."), FFPATH_STRICT_BOUNDS));
	x(0 == ffpath_norm(buf, FFCNT(buf), FFSTR("../"), FFPATH_STRICT_BOUNDS));
	x(0 == ffpath_norm(buf, FFCNT(buf), FFSTR("/../"), FFPATH_STRICT_BOUNDS));

	x(0 == ffpath_norm(buf, 0, FFSTR("/"), 0));

	x(0 == ffpath_norm(buf, 0, FFSTR("/path/fn\0.ext"), 0)); //invalid char
}

static void test_path_cmp()
{
	ffstr s1, s2;
	ffstr_setcz(&s1, "/a/b/c");
	ffstr_setcz(&s2, "/a/b/c");
	x(ffpath_cmp(&s1, &s2, 0) == 0);

	ffstr_setcz(&s2, "/a/b/d");
	x(ffpath_cmp(&s1, &s2, 0) < 0);

	ffstr_setcz(&s2, "/a/b/b");
	x(ffpath_cmp(&s1, &s2, 0) > 0);

	ffstr_setcz(&s1, "/a/b");
	ffstr_setcz(&s2, "/a");
	x(ffpath_cmp(&s1, &s2, 0) > 0);

	ffstr_setcz(&s1, "a");
	ffstr_setcz(&s2, "a");
	x(ffpath_cmp(&s1, &s2, 0) == 0);

	ffstr_setcz(&s1, "/a/B/c");
	ffstr_setcz(&s2, "/a/b/c");
	x(ffpath_cmp(&s1, &s2, FFPATH_CASE_ISENS) == 0);

	ffstr_setcz(&s1, "/a/B/c");
	ffstr_setcz(&s2, "/a/b/c");
	x(ffpath_cmp(&s1, &s2, FFPATH_CASE_SENS) > 0);

	ffstr_setcz(&s1, "/a/b/c");
	ffstr_setcz(&s2, "/a/b+/c");
	x(ffpath_cmp(&s1, &s2, FFPATH_CASE_ISENS) < 0);

	ffstr_setcz(&s1, "/a/b");
	ffstr_setcz(&s2, "/a/C");
	x(ffpath_cmp(&s1, &s2, FFPATH_CASE_SENS) < 0);
}

static void test_fnmake()
{
	ffarr fn = {};
	ffstr idir, iname;
	ffstr odir, oext;
	ffstr_setz(&iname, "iname");
	ffstr_setz(&oext, "jpg");
	ffstr_setz(&odir, "out");

	// cmd -R ./*.bmp ./.jpg
	ffstr_setz(&idir, ".");
	ffpath_makefn_out(&fn, &idir, &iname, &odir, &oext);
	x(ffstr_eqz(&fn, "out/./iname.jpg"));

	// cmd -R ./*.bmp ./.jpg
	ffstr_setz(&idir, "./d1/d2");
	ffpath_makefn_out(&fn, &idir, &iname, &odir, &oext);
	x(ffstr_eqz(&fn, "out/d1/d2/iname.jpg"));

	// cmd -R *.bmp .jpg
	ffstr_setz(&idir, "");
	ffpath_makefn_out(&fn, &idir, &iname, &odir, &oext);
	x(ffstr_eqz(&fn, "out/iname.jpg"));

	// cmd -R in out/.jpg
	ffstr_setz(&idir, "in");
	ffpath_makefn_out(&fn, &idir, &iname, &odir, &oext);
	x(ffstr_eqz(&fn, "out/in/iname.jpg"));

	// cmd -R /in out/.jpg
	ffstr_setz(&idir, "/in");
	ffpath_makefn_out(&fn, &idir, &iname, &odir, &oext);
	x(ffstr_eqz(&fn, "out/in/iname.jpg"));

	// cmd -R ../../in out/.jpg
	ffstr_setz(&idir, "../../in");
	ffpath_makefn_out(&fn, &idir, &iname, &odir, &oext);
	x(ffstr_eqz(&fn, "out/in/iname.jpg"));

	// cmd -R /in .jpg -> cmd -R /in ./.jpg
	ffstr_setz(&idir, "/in");
	ffstr_setz(&odir, "");
	ffpath_makefn_out(&fn, &idir, &iname, &odir, &oext);
	x(ffstr_eqz(&fn, "in/iname.jpg"));

	ffarr_free(&fn);
}

int test_path()
{
	ffstr s;
	char buf[60];
	size_t n;
	s.ptr = buf;

	test_fnmake();
	test_path_norm();
	test_path_cmp();

	x(FFSLEN("filename") == ffpath_makefn(buf, FFCNT(buf), FFSTR("filename"), '_'));
	n = ffpath_makefn(buf, FFCNT(buf), FFSTR("\x00\x1f *?/\\:\""), '_');
	buf[n] = '\0';
	x(0 == strcmp(buf, "__ ______"));

#define FN "/path/file"
	x(FN + FFSLEN(FN) - FFSLEN("/file") == ffpath_rfindslash(FN, FFSLEN(FN)));
#undef FN

#define FN "file"
	x(FN + FFSLEN(FN) == ffpath_rfindslash(FN, FFSLEN(FN)));
#undef FN

	x(0 != ffpath_isvalidfn("filename", FFSLEN("filename"), FFPATH_FN_ANY));
	x(0 == ffpath_isvalidfn("filename/withslash", FFSLEN("filename/withslash"), FFPATH_FN_ANY));
	x(0 == ffpath_isvalidfn("filename\0withzero", FFSLEN("filename\0withzero"), FFPATH_FN_ANY));

	ffpath_splitname("file.txt", FFSLEN("file.txt"), NULL, &s);
	x(ffstr_eqcz(&s, "txt"));

	ffpath_splitname("qwer", FFSLEN("qwer"), NULL, &s);
	x(ffstr_eqcz(&s, ""));

	ffpath_splitname(".qwer", FFSLEN(".qwer"), NULL, &s);
	x(ffstr_eqcz(&s, ""));

	{
		ffstr in, dir, fn;
		ffstr_setcz(&in, "/path/to/file");
		x(in.ptr + FFSLEN("/path/to") == ffpath_split2(in.ptr, in.len, &dir, &fn));
		x(ffstr_eqcz(&dir, "/path/to"));
		x(ffstr_eqcz(&fn, "file"));

		fn.len = 0;
		x(NULL == ffpath_split2("file", FFSLEN("file"), &dir, &fn));
		x(ffstr_eqcz(&dir, ""));
		x(ffstr_eqcz(&fn, "file"));
	}

	return 0;
}
