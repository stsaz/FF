/**
Copyright (c) 2017 Simon Zolin
*/

#include <test/all.h>
#include <FF/path.h>
#include <FF/sys/filemap.h>
#include <FF/sys/sendfile.h>
#include <FF/sys/dir.h>
#include <FF/net/url.h>
#include <FFOS/process.h>
#include <FFOS/thread.h>
#include <FFOS/sig.h>
#include <FFOS/test.h>

#define x FFTEST_BOOL


int test_file(void)
{
	FFTEST_FUNC;

	const char *fn1 = TESTDIR "/f1.tmp",  *fn2 = TESTDIR "/f2.tmp";
	ffarr a = {};
	ffarr_alloc(&a, 100 * 1024);
	ffs_fill(a.ptr, ffarr_edge(&a), 'A', 100 * 1024);
	a.len = a.cap;

	ffarr_back(&a) = 'B';
	x(0 == fffile_writeall(fn1, a.ptr, a.len, 0));
	ffarr_back(&a) = 'C';
	x(0 == fffile_writeall(fn2, a.ptr, a.len, 0));

	x(fffile_cmp(fn1, fn2, 0) < 0);
	x(fffile_cmp(fn2, fn1, 0) > 0);
	x(fffile_cmp(fn2, fn1, 100 * 1024 - 1) == 0);

	fffile_rm(fn1);
	fffile_rm(fn2);
	ffarr_free(&a);
	return 0;
}

int test_fmap()
{
	fffd fd;
	fffilemap fm;
	char buf[64 * 1024];
	uint64 sz;
	ffstr d;
	const char *fn = TESTDIR "/ff_fmap.tmp";

	FFTEST_FUNC;

	fd = fffile_open(fn, FFO_CREATE | FFO_TRUNC | FFO_RDWR);
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
	fffile_rm(fn);
	return 0;
}

int test_sendfile()
{
	ffsf sf;
	int64 n;
	ssize_t nr;
	ffskt lsn, cli, sk;
	fffd f;
	char *rbuf;
	uint nsent, nread;
	char *iov_bufs[4];
	ffiovec hdtr[4];
	int i;
	ffaddr adr;
	const char *fn;

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
	fn = TESTDIR "/tmp-ff";
	f = fffile_createtemp(fn, O_RDWR);
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

	ffskt_init(FFSKT_WSA | FFSKT_WSAFUNCS);
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
	nsent = 0;

	for (;;) {
		n = ffsf_send(&sf, cli, 0);
		if (n == -1)
			x(fferr_again(fferr_last()));
		else if (n != 0) {
			nsent += n;
			if (0 == ffsf_shift(&sf, n))
				ffskt_fin(cli);
		}
		printf("sent %d [%x]\n", (int)n, nsent);

		for (;;) {
			nr = ffskt_recv(sk, rbuf + nread, 12 * M - nread, 0);
			if (nr == -1) {
				x(fferr_again(fferr_last()));
				break;
			} else if (nr == 0)
				goto done;
			nread += nr;
			printf("recvd %d [%x]\n", (int)nr, nread);
		}

		if (n == -1)
			ffthd_sleep(50);
	}

done:
	x(nsent == 12 * M);
	x(nsent == nread);

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

int test_direxp(void)
{
	uint n;
	ffdirexp dex;
	const char *name;
	char mask[64];
	static const char *const names[] = {
		"./ff-anothertmpfile.html", "./ff-tmpfile.htm"
#ifdef FF_WIN
		, "./ff-tmpfile.HTML"
#else
		, "./ff-tmpfile.html"
#endif
	};

	FFTEST_FUNC;

	for (n = 0;  n < 3;  n++) {
		fffd f = fffile_open(names[n], O_CREAT);
		x(f != FF_BADFD);
		fffile_close(f);
	}

	ffsz_copycz(mask, "./*.htm.tmp");
	x(0 != ffdir_expopen(&dex, mask, 0) && fferr_last() == ENOMOREFILES);

	ffsz_copycz(mask, "./*.htm");
	x(0 == ffdir_expopen(&dex, mask, 0));
	n = 0;
	for (;;) {
		name = ffdir_expread(&dex);
		if (name == NULL)
			break;
		x(!ffsz_cmp(name, names[1]));
		n++;
	}
	x(n == 1);
	ffdir_expclose(&dex);

	ffsz_copycz(mask, "./f*.htm*");
	x(0 == ffdir_expopen(&dex, mask, 0));
	n = 0;
	for (;;) {
		name = ffdir_expread(&dex);
		if (name == NULL)
			break;
		x(!ffsz_cmp(name, names[n++]));
	}
	x(n == 3);
	ffdir_expclose(&dex);

	ffsz_copycz(mask, "./");
	x(0 == ffdir_expopen(&dex, mask, 0));
	n = 0;
	while (NULL != (name = ffdir_expread(&dex))) {
		if (!ffsz_cmp(name, names[0])
			|| !ffsz_cmp(name, names[1])
			|| !ffsz_cmp(name, names[2]))
			n++;
	}
	x(n == 3);
	ffdir_expclose(&dex);

	for (n = 0;  n < 3;  n++) {
		fffile_rm(names[n]);
	}
	return 0;
}

int test_env(void)
{
#ifdef FF_UNIX
	ffenv env;
	char *e_s[] = { "KEY1=VAL1", "KEY=VAL", NULL };
	char **e = e_s, *p, buf[256];
	ffenv_init(&env, e);

	p = ffenv_expand(&env, NULL, 0, "asdf $KEY 1234");
	x(ffsz_eq(p, "asdf VAL 1234"));
	ffmem_free(p);

	p = ffenv_expand(&env, buf, sizeof(buf), "asdf $KEY 1234");
	x(p == buf);
	x(ffsz_eq(p, "asdf VAL 1234"));

	ffenv_destroy(&env);
#endif
	return 0;
}

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

int test_path()
{
	ffstr s;
	char buf[60];
	size_t n;
	s.ptr = buf;

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


static void sig_handler(struct ffsig_info *inf)
{
	char buf[512];
	size_t n;

	n = ffs_fmt(buf, buf + sizeof(buf), "Signal:%xu  Address:0x%p  Flags:%xu\n"
		, inf->sig, inf->addr, inf->flags);
	fffile_write(ffstderr, buf, n);
}

int sig_thdfunc(void *param)
{
	ffsig_raise(FFSIG_SEGV);
	return 0;
}

int test_sig(void)
{
	static const uint sigs_fault[] = { FFSIG_SEGV, FFSIG_STACK, FFSIG_ILL, FFSIG_FPE };
	ffsig_subscribe(&sig_handler, sigs_fault, FFCNT(sigs_fault));
	ffthd th = ffthd_create(&sig_thdfunc, NULL, 0);
	ffthd_join(th, -1, NULL);
	return 0;
}
