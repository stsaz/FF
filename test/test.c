
/**
Copyright (c) 2013 Simon Zolin
*/

#include <FFOS/test.h>
#include <FFOS/file.h>
#include <FFOS/timer.h>
#include <FFOS/thread.h>
#include <FFOS/process.h>
#include <FF/array.h>
#include <FF/crc.h>
#include <FF/path.h>
#include <FF/bitops.h>
#include <FF/net/dns.h>
#include <FF/filemap.h>
#include <FF/sendfile.h>
#include <FF/net/url.h>

#include <test/all.h>

#define x FFTEST_BOOL
#define CALL FFTEST_TIMECALL


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

	n = ffpath_norm(buf, FFCNT(buf), FFSTR("/../a/../..//...///path/a/../path2/a/b//../.."), FFPATH_STRICT_BOUNDS);
	x(0 == ffs_eqcz(buf, n, "/.../path/path2/"));

	x(0 == ffpath_norm(buf, FFCNT(buf), FFSTR("/path/../../file/./"), FFPATH_STRICT_BOUNDS));
	x(0 == ffpath_norm(buf, FFCNT(buf), FFSTR("./path/../../file/./"), FFPATH_STRICT_BOUNDS));
	x(0 == ffpath_norm(buf, FFCNT(buf), FFSTR("/.."), FFPATH_STRICT_BOUNDS));
	x(0 == ffpath_norm(buf, FFCNT(buf), FFSTR("../"), FFPATH_STRICT_BOUNDS));
	x(0 == ffpath_norm(buf, FFCNT(buf), FFSTR("/../"), FFPATH_STRICT_BOUNDS));

	x(0 == ffpath_norm(buf, 0, FFSTR("/"), 0));

	x(0 == ffpath_norm(buf, 0, FFSTR("/path/fn\0.ext"), 0)); //invalid char

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

	x(0 != ffpath_isvalidfn("filename", FFSLEN("filename")));
	x(0 == ffpath_isvalidfn("filename/withslash", FFSLEN("filename/withslash")));
	x(0 == ffpath_isvalidfn("filename\0withzero", FFSLEN("filename\0withzero")));

	s = ffpath_fileext(FFSTR("file.txt"));
	x(ffstr_eqcz(&s, "txt"));

	s = ffpath_fileext(FFSTR("qwer"));
	x(ffstr_eqcz(&s, ""));

	{
		ffstr dir, fn;
		x(FFSLEN("/path/to") == ffpath_split2(FFSTR("/path/to/file"), &dir, &fn));
		x(ffstr_eqcz(&dir, "/path/to"));
		x(ffstr_eqcz(&fn, "file"));

		fn.len = 0;
		x(-1 == ffpath_split2(FFSTR("file"), &dir, &fn));
		x(ffstr_eqcz(&dir, ""));
		x(ffstr_eqcz(&fn, "file"));
	}

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

static int test_dns()
{
	char buf[FFDNS_MAXNAME];
	const char *pos;
	ffstr d;
	ffstr s;
	s.ptr = buf;

	FFTEST_FUNC;

	s.len = ffdns_encodename(buf, FFCNT(buf), FFSTR("www.my.dot.com"));
	x(ffstr_eqz(&s, "\3www\2my\3dot\3com"));

	s.len = ffdns_encodename(buf, FFCNT(buf), FFSTR("."));
	x(ffstr_eqcz(&s, "\0"));

	s.len = ffdns_encodename(buf, FFCNT(buf), FFSTR("www.my.dot.com."));
	x(ffstr_eqcz(&s, "\3www\2my\3dot\3com\0"));

	x(0 == ffdns_encodename(buf, FFCNT(buf), FFSTR("www.my.dot.com..")));
	x(0 == ffdns_encodename(buf, FFCNT(buf), FFSTR(".www.my.dot.com.")));
	x(0 == ffdns_encodename(buf, FFCNT(buf), FFSTR("www..my.dot.com.")));
	x(0 == ffdns_encodename(buf, FFCNT(buf), FFSTR("www..my.dot.com.")));


	ffstr_setcz(&d, "\3www\2my\3dot\3com\0");
	pos = d.ptr;
	s.len = ffdns_name(buf, FFCNT(buf), d.ptr, d.len, &pos);
	x(ffstr_eqcz(&s, "www.my.dot.com."));

	ffstr_setcz(&d, "\3www\xc0\6" "\2my\3dot\3com\0");
	pos = d.ptr;
	s.len = ffdns_name(buf, FFCNT(buf), d.ptr, d.len, &pos);
	x(ffstr_eqcz(&s, "www.my.dot.com."));

	ffstr_setcz(&d, "\2my\3dot\3com\0" "\3www\xc0\0");
	pos = d.ptr + FFSLEN("\2my\3dot\3com\0");
	s.len = ffdns_name(buf, FFCNT(buf), d.ptr, d.len, &pos);
	x(ffstr_eqcz(&s, "www.my.dot.com."));

	ffstr_setcz(&d, "\2my\3dot\3com\0" "\3www\xff\xff");
	pos = d.ptr + FFSLEN("\2my\3dot\3com\0");
	x(0 == ffdns_name(buf, FFCNT(buf), d.ptr, d.len, &pos));

	ffstr_setcz(&d, "\3www\xc0\0\0");
	pos = d.ptr;
	x(0 == ffdns_name(buf, FFCNT(buf), d.ptr, d.len, &pos));

	ffstr_setcz(&d, "\0");
	pos = d.ptr;
	x(0 == ffdns_name(buf, FFCNT(buf), d.ptr, d.len, &pos));


	s.len = ffdns_addquery(buf, FFCNT(buf), FFSTR("my.dot.com"), FFDNS_AAAA);
	x(ffstr_eqcz(&s, "\2my\3dot\3com\0" "\0\x1c" "\0\1"));

	s.len = ffdns_addquery(buf, FFCNT(buf), FFSTR("my.dot.com."), FFDNS_AAAA);
	x(ffstr_eqcz(&s, "\2my\3dot\3com\0" "\0\x1c" "\0\1"));

	return 0;
}

static int test_fmap()
{
	fffd fd;
	fffilemap fm;
	char buf[64 * 1024];
	uint64 sz;
	ffstr d;
	ffsyschar fne[FF_MAXPATH];
	const ffsyschar *fn = TEXT(TMPDIR) TEXT("/ff_fmap.tmp");

	FFTEST_FUNC;

	if (0 != ffenv_expand(fn, fne, FFCNT(fne)))
		fn = fne;
	fd = fffile_openq(fn, FFO_CREATE | O_RDWR);
	x(fd != FF_BADFD);

	ffs_fill(buf, buf + FFCNT(buf), ' ', FFCNT(buf));
	buf[0] = '1';
	buf[FFCNT(buf) / 2] = '2';
	x(FFCNT(buf) == fffile_write(fd, buf, FFCNT(buf)));

	buf[0] = '3';
	x(FFCNT(buf) == fffile_write(fd, buf, FFCNT(buf)));

	sz = fffile_size(fd);

	fffile_mapinit(&fm);
	fffile_mapset(&fm, FFCNT(buf), fd, 0, sz);

	x(0 == fffile_mapbuf(&fm, &d));
	x(d.ptr[0] == '1');
	x(0 != fffile_mapshift(&fm, FFCNT(buf) / 2));
	x(0 == fffile_mapbuf(&fm, &d));
	x(d.ptr[0] == '2');
	x(0 != fffile_mapshift(&fm, FFCNT(buf) / 2));

	x(0 == fffile_mapbuf(&fm, &d));
	x(d.ptr[0] == '3');
	x(0 == fffile_mapshift(&fm, FFCNT(buf)));

	fffile_mapclose(&fm);
	fffile_close(fd);
	fffile_rmq(fn);
	return 0;
}

static int test_sendfile()
{
	ffsf sf;
	int64 n;
	ssize_t nr;
	ffskt lsn, cli, sk;
	fffd f;
	char *rbuf;
	int64 nread;
	char *iov_bufs[4];
	ffiovec hdtr[4];
	int i;
	ffaddr adr;
	ffsyschar fne[FF_MAXPATH];
	const ffsyschar *fn;

	enum { M = 1024 * 1024 };

	FFTEST_FUNC;

	{
	ffstr chunk;
	ffsf_init(&sf);
	ffiov_set(&hdtr[0], FFSTR("123"));
	ffiov_set(&hdtr[1], FFSTR("asdf"));
	ffsf_sethdtr(&sf.ht, &hdtr[0], 1, &hdtr[1], 1);
	x(0 != ffsf_nextchunk(&sf, &chunk));
	x(ffstr_eqcz(&chunk, "123"));
	ffsf_shift(&sf, chunk.len);
	x(0 == ffsf_nextchunk(&sf, &chunk));
	x(ffstr_eqcz(&chunk, "asdf"));
	}

	// prepare file
	fn = TEXT(TMPDIR) TEXT("/tmp-ff");
	if (0 != ffenv_expand(fn, fne, FFCNT(fne)))
		fn = fne;
	f = fffile_createtempq(fn, O_RDWR);
	x(f != FF_BADFD);
	x(0 == fffile_trunc(f, 4 * M + 2));
	x(2 == fffile_write(f, ".[", 2));
	fffile_seek(f, -2, SEEK_END);
	x(2 == fffile_write(f, "].", 2));

	// prepare hdtr
	for (i = 0;  i < 4;  i++) {
		iov_bufs[i] = ffmem_alloc(2 * M);
		x(iov_bufs[i] != NULL);
		iov_bufs[i][0] = 'A' + i;
		iov_bufs[i][2 * M - 1] = 'a' + i;
		ffiov_set(&hdtr[i], iov_bufs[i], 2 * M);
	}

	// prepare sf
	ffsf_init(&sf);
	fffile_mapset(&sf.fm, 64 * 1024, f, 1, 4 * M);
	ffsf_sethdtr(&sf.ht, &hdtr[0], 2, &hdtr[2], 2);
	x(!ffsf_empty(&sf));
	x(12 * M == ffsf_len(&sf));

	// prepare sockets
	ffaddr_init(&adr);
	x(0 == ffaddr_set(&adr, FFSTR("127.0.0.1"), NULL, 0));
	ffip_setport(&adr, 64000);

	ffskt_init(FFSKT_WSA);
	lsn = ffskt_create(AF_INET, SOCK_STREAM, 0);
	x(lsn != FF_BADSKT);
	x(0 == ffskt_bind(lsn, &adr.a, adr.len));
	x(0 == ffskt_listen(lsn, SOMAXCONN));

	cli = ffskt_create(AF_INET, SOCK_STREAM, 0);
	x(cli != FF_BADSKT);
	x(0 == ffskt_connect(cli, &adr.a, adr.len));

	sk = ffskt_accept(lsn, NULL, NULL, 0);
	x(sk != FF_BADSKT);
	x(0 == ffskt_nblock(sk, 1));
	x(0 == ffskt_nblock(cli, 1));

	rbuf = ffmem_alloc(12 * M);
	x(rbuf != NULL);
	nread = 0;

	for (;;) {
		n = ffsf_send(&sf, cli, 0);
		x(n != -1 || fferr_again(fferr_last()));
		if (n == -1)
			ffthd_sleep(50);
		printf("sent %d\n", (int)n);

		for (;;) {
			nr = ffskt_recv(sk, rbuf + nread, 12 * M - nread, 0);
			x(nr != -1 || fferr_again(fferr_last()));
			if (nr == -1 || nr == 0)
				break;
			nread += nr;
			printf("recvd %d\n", (int)nr);
		}

		if (n != -1 && 0 == ffsf_shift(&sf, n))
			break;
	}

	ffskt_close(lsn);
	ffskt_close(cli);
	ffskt_close(sk);

	ffsf_close(&sf);
	for (i = 0;  i < 4;  i++) {
		ffmem_free(iov_bufs[i]);
	}
	fffile_close(f);

	x(rbuf[0] == 'A' && rbuf[2 * M - 1] == 'a');
	x(rbuf[2 * M] == 'B' && rbuf[4 * M - 1] == 'b');
	x(rbuf[4 * M] == '[' && rbuf[8 * M - 1] == ']');
	x(rbuf[8 * M] == 'C' && rbuf[10 * M - 1] == 'c');
	x(rbuf[10 * M] == 'D' && rbuf[12 * M - 1] == 'd');
	ffmem_free(rbuf);

	return 0;
}

int test_all()
{
	ffos_init();

	test_bits();
	test_str();
	test_list();
	test_rbtlist();
	test_htable();
	test_crc();
	test_path();
	test_url();
	test_http();
	test_dns();
	test_fmap();
	test_time();
	test_timerq();
	test_sendfile();

	FFTEST_TIMECALL(test_json());
	test_conf();
	test_args();

	printf("%u tests were run, failed: %u.\n", fftestobj.nrun, fftestobj.nfail);

	return 0;
}

int main(int argc, const char **argv)
{
	return test_all();
}
