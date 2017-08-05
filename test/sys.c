/**
Copyright (c) 2017 Simon Zolin
*/

#include <test/all.h>
#include <FF/sys/filemap.h>
#include <FF/sys/sendfile.h>
#include <FF/sys/dir.h>
#include <FF/net/url.h>
#include <FFOS/process.h>
#include <FFOS/thread.h>
#include <FFOS/test.h>

#define x FFTEST_BOOL


int test_fmap()
{
	fffd fd;
	fffilemap fm;
	char buf[64 * 1024];
	uint64 sz;
	ffstr d;
	const char *fn = TESTDIR "/ff_fmap.tmp";

	FFTEST_FUNC;

	fd = fffile_open(fn, O_CREAT | O_TRUNC | O_RDWR);
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
	int64 nread;
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
