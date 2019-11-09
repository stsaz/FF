/** .tar r/w
Copyright (c) 2019 Simon Zolin
*/

#include <test/all.h>
#include <FF/pack/tar.h>
#include <FF/time.h>
#include <FFOS/test.h>

#define x FFTEST_BOOL


int test_tar()
{
	char fn[255];
	ffarr a = {};
	fftar_file f = {}, f2 = {};

	// write/read file
	f.name = "/name";
	f.size = 12345;
	f.mode = FFUNIX_FILE_REG | 0644;
	fftime_fromtime_t(&f.mtime, 1);
	x(0 == fftar_hdr_write(&f, &a) && a.len == 512);
	x(0 == fftar_hdr_parse(&f2, fn, a.ptr));
	x(ffsz_eq(fn, "name"));
	x(ffsz_eq(f2.name, "name"));
	x(f2.size == f.size);
	x(f2.mode == f.mode);
	x(f2.type == FFTAR_FILE);
	x(!fftime_cmp(&f2.mtime, &f.mtime));
	x(f2.uid == f.uid);
	x(f2.gid == f.gid);
	x(ffsz_eq(f2.uid_str, "root"));
	x(ffsz_eq(f2.gid_str, "root"));

	// write/read directory
	f.name = "/name";
	f.mode = FFUNIX_FILE_DIR | 0755;
	f.size = 12345;
	x(0 == fftar_hdr_write(&f, &a));
	x(0 == fftar_hdr_parse(&f2, fn, a.ptr));
	x(ffsz_eq(f2.name, "name/"));
	x(f2.mode == f.mode);
	x(f2.type == FFTAR_DIR);
	x(f2.size == 0);

	// write/read directory with a long name
	f.name = "/namenamenamenamenamenamenamenamenamenamenamenamenamenamenamenamenamenamenamenamenamenamenamenamenamenamenamenamenamenamenamenamenamenamenamenamenamenamenamenamenamenamenamenamenamenamenamenamenamenamenamenamenamenamenamenamename";
	f.mode = FFUNIX_FILE_DIR | 0755;
	x(FFTAR_ELONGNAME == fftar_hdr_write(&f, &a));
	x(0 == fftar_hdr_parse(&f2, fn, a.ptr));
	x(ffsz_eq(f2.name, "namenamenamenamenamenamenamenamenamenamenamenamenamenamenamenamenamenamenamenamenamenamenamenamenam/"));

	ffarr_free(&a);
	return 0;
}
