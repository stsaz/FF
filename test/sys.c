/**
Copyright (c) 2017 Simon Zolin
*/

#include <test/all.h>
#include <FF/path.h>
#include <FF/sys/filemap.h>
#include <FF/sys/sendfile.h>
#include <FF/sys/dir.h>
#include <FF/sys/fileread.h>
#include <FF/sys/filewrite.h>
#include <FF/net/url.h>
#include <FFOS/process.h>
#include <FFOS/thread.h>
#include <FFOS/sig.h>
#include <FFOS/test.h>


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
	FFTEST_FUNC;
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
	FFTEST_FUNC;
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


static void onlog(void *udata, uint level, ffstr msg)
{
	fffile_fmt(ffstdout, NULL, "%S\n", &msg);
}

uint syncvar;

static void onread()
{
	syncvar = 1;
}

int test_fileread()
{
	FFTEST_FUNC;
	syncvar = 0;
	char *fn = TESTDIR "/fftest-filerw";

	ffthpool *thpool;
	ffthpoolconf ioconf;
	ioconf.maxqueue = 4;
	ioconf.maxthreads = 2;
	x(NULL != (thpool = ffthpool_create(&ioconf)));

	fffileread *fr;
	fffileread_conf conf;
	fffileread_setconf(&conf);
	conf.thpool = thpool;
	conf.log = &onlog;
	conf.log_debug = 1;
	conf.onread = &onread;
	conf.oflags = FFO_CREATE | FFO_RDWR;
	conf.nbufs = 2;
	x(NULL != (fr = fffileread_create(fn, &conf)));

	ffarr a = {};
	ffarr_alloc(&a, 128*1024 + 9);
	ffmemcpy(a.ptr + 64*1024, "123456789", 9);
	ffmemcpy(a.ptr + 128*1024, "abcdefghi", 9);
	x(a.cap == fffile_pwrite(fffileread_fd(fr), a.ptr, a.cap, 0));
	ffarr_free(&a);

	ffstr d;
	int r;
	// read from file at 64k
	x(FFFILEREAD_RASYNC == (r = fffileread_getdata(fr, &d, 64*1024 + 1, 0)));
	ffatom_waitchange(&syncvar, 0);  syncvar = 0;
	x(FFFILEREAD_RREAD == (r = fffileread_getdata(fr, &d, 64*1024 + 1, 0)));
	x(ffstr_matchz(&d, "23456789"));

	// read from file at 128k
	x(FFFILEREAD_RASYNC == (r = fffileread_getdata(fr, &d, 128*1024 + 1, 0)));
	ffatom_waitchange(&syncvar, 0);  syncvar = 0;
	x(FFFILEREAD_RREAD == (r = fffileread_getdata(fr, &d, 128*1024 + 1, 0)));
	x(ffstr_matchz(&d, "bcdefghi"));

	// EOF
	x(FFFILEREAD_REOF == (r = fffileread_getdata(fr, &d, 128*1024 + 9, 0)));

	// read from cache at 64k
	x(FFFILEREAD_RREAD == (r = fffileread_getdata(fr, &d, 64*1024 + 2, 0)));
	x(ffstr_matchz(&d, "3456789"));

	// read from cache at 128k
	x(FFFILEREAD_RREAD == (r = fffileread_getdata(fr, &d, 128*1024 + 2, 0)));
	x(ffstr_matchz(&d, "cdefghi"));

	// check stats
	struct fffileread_stat st;
	fffileread_stat(fr, &st);
	x(st.nread == 2);
	x(st.ncached == 2);

	fffileread_free(fr);
	ffthpool_free(thpool);
	fffile_rm(fn);
	return 0;
}

static void onlogw(void *udata, uint level, ffstr msg)
{
	fffile_fmt(ffstdout, NULL, "%S\n", &msg);
}

static void onwrite()
{
	syncvar = 1;
}

int test_filewrite()
{
	FFTEST_FUNC;
	fffilewrite_stat st;
	syncvar = 0;
	char *fn = TESTDIR "/fftest-filerw";
	ffarr aread = {};
	ffstr a = {};

	ffthpool *thpool;
	ffthpoolconf ioconf;
	ioconf.maxqueue = 4;
	ioconf.maxthreads = 2;
	x(NULL != (thpool = ffthpool_create(&ioconf)));

	fffilewrite *fw;
	fffilewrite_conf conf;
	fffilewrite_setconf(&conf);
	conf.log = &onlogw;
	conf.log_debug = 1;
	conf.bufsize = 64*1024;
	conf.nbufs = 2;
	conf.onwrite = &onwrite;
	conf.prealloc = 64*1024;
	conf.prealloc_grow = 0;
	conf.thpool = thpool;
	conf.overwrite = 1;
	x(NULL != (fw = fffilewrite_create(fn, &conf)));

	ffstr data;
	ffstr_setz(&data, "0123456789");
	x(10 == fffilewrite_write(fw, data, -1, 0)); // buf0@0: "0123456789"

	ffstr_setz(&data, "abcdef");
	x(6 == fffilewrite_write(fw, data, -1, 0)); // buf0@0: "0123456789abcdef"
	fffilewrite_getstat(fw, &st); x(st.nmwrite == 2);

	ffstr_setz(&data, "-");
	x(1 == fffilewrite_write(fw, data, 13, 0)); // file: "0123456789abcdef"  buf0:aio  buf1@13: "-"
	fffilewrite_getstat(fw, &st); x(st.nmwrite == 3); x(st.nasync == 1);

	ffstr_setz(&data, "=");
	x(1 == fffilewrite_write(fw, data, -1, 0)); // file: "0123456789abcdef"  buf0:aio  buf1@13: "-="
	fffilewrite_getstat(fw, &st); x(st.nmwrite == 4);

	ffstr_alloc(&a, 128*1024);
	ffstr_addfill(&a, 128*1024, 'A', 128*1024);
	ffstr_set2(&data, &a);

	while (data.len != 0) {
		ssize_t r = fffilewrite_write(fw, data, -1, 0);
		if (r == FFFILEWRITE_RASYNC) {
			ffthd_sleep(100);
			continue;
		}
		x(r >= 0);
		// file: "0123456789abcdef-=A[64k-2]"  buf0:aio  buf1:aio
		// file: "0123456789abc-=A[64k-2+64k]"  buf1: ""
		// file: "0123456789abc-=A[64k-2+64k]"  buf1@13+2+64k-2+64k: "AA"
		ffstr_shift(&data, r);
	}

	for (;;) {
		ssize_t r = fffilewrite_write(fw, data, -1, FFFILEWRITE_FFLUSH);
		if (r == FFFILEWRITE_RASYNC) {
			ffthd_sleep(100);
			continue;
		}
		x(r == 0);
		if (r == 0) // file: "0123456789abc-=A[64k+64k]"
			break;
	}

	fffilewrite_getstat(fw, &st);
	x(st.nmwrite == 6);
	x(st.nprealloc == 3);
	x(st.nasync == 4);
	x(st.nfwrite == 4);

	fffilewrite_free(fw);

	ffstr_free(&a);
	ffstr_alloc(&a, 128*1024 + 13 + 2);
	ffstr_add(&a, -1, "0123456789abc-=", 13 + 2);
	ffstr_addfill(&a, -1, 'A', 128*1024);
	x(0 == fffile_readall(&aread, fn, -1));
	x(ffstr_eq2(&aread, &a));

	ffthpool_free(thpool);
	fffile_rm(fn);
	ffstr_free(&a);
	ffarr_free(&aread);
	return 0;
}
