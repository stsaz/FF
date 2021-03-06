
/**
Copyright (c) 2013 Simon Zolin
*/

#include <FFOS/test.h>
#include <FFOS/file.h>
#include <FFOS/timer.h>
#include <FFOS/dir.h>
#include <FF/array.h>
#include <FF/crc.h>
#include <FF/net/dns.h>
#include <FF/audio/icy.h>

#include <test/all.h>

#define CALL FFTEST_TIMECALL


size_t _test_readfile(const char *fn, char *buf, size_t n)
{
	fffd f;
	f = fffile_open(fn, O_RDONLY);
	x(f != FF_BADFD);
	n = fffile_read(f, buf, n);
	fffile_close(f);
	x(n != 0 && n != (size_t)-1);
	return n;
}

static int test_crc()
{
	x(0x7052c01a == ffcrc32_get("hello, man!", FFSLEN("hello, man!")));
	x(0x7052c01a == ffcrc32_iget(FFSTR("HELLO, MAN!")));
	return 0;
}

#define ICY_META "\x03StreamTitle='artist - track';StreamUrl='';\0\0\0\0\0\0"

static int test_icy(void)
{
	FFTEST_FUNC;

	fficymeta icymeta;
	ffstr d, v, artist, title;
	fficy_metaparse_init(&icymeta);
	ffstr_setz(&d, ICY_META + 1);

	x(FFPARS_KEY == fficy_metaparse_str(&icymeta, &d, &v));
	x(ffstr_eqz(&v, "StreamTitle"));
	x(FFPARS_VAL == fficy_metaparse_str(&icymeta, &d, &v));
	x(ffstr_eqz(&v, "artist - track"));
	fficy_streamtitle(v.ptr, v.len, &artist, &title);
	x(ffstr_eqz(&artist, "artist"));
	x(ffstr_eqz(&title, "track"));

	x(FFPARS_KEY == fficy_metaparse_str(&icymeta, &d, &v));
	x(ffstr_eqz(&v, "StreamUrl"));
	x(FFPARS_VAL == fficy_metaparse_str(&icymeta, &d, &v));
	x(ffstr_eqz(&v, ""));
	x(d.len == 0);

	char meta[FFICY_MAXMETA];
	ffarr m;
	fficy_initmeta(&m, meta, sizeof(meta));
	x(FFSLEN("StreamTitle='artist - track';")
		== fficy_addmeta(&m, FFSTR("StreamTitle"), FFSTR("artist - track")));
	x(FFSLEN("StreamUrl='';")
		== fficy_addmeta(&m, FFSTR("StreamUrl"), FFSTR("")));
	x(FFSLEN(ICY_META) == fficy_finmeta(&m));
	x(ffstr_eq(&m, ICY_META, FFSLEN(ICY_META)));

	return 0;
}


extern int test_file(void);
extern int test_fileread();
extern int test_filewrite();
FF_EXTN int test_ring(void);
FF_EXTN int test_ringbuf(void);
FF_EXTN int test_tq(void);
FF_EXTN int test_regex(void);
FF_EXTN int test_num(void);
FF_EXTN int test_inchk_speed(void);
FF_EXTN int test_cue(void);
extern int test_xml(void);
extern int test_tls(void);
extern int test_webskt(void);
extern int test_path(void);
extern int test_bits(void);
extern void test_conf_write(void);
extern void test_dns(void);
extern int test_domain();
extern void test_dns_client(void);
extern int test_cache(void);
extern int test_ip();
extern int test_cmdarg();
extern int test_conf2();

struct test_s {
	const char *nm;
	int (*func)();
};

#define F(nm) { #nm, (int (*)())&test_ ## nm }
static const struct test_s _fftests[] = {
	F(str), F(regex),
	F(num), F(bits), F(rbtree), F(rbtlist), F(htable), F(ring), F(ringbuf), F(tq), F(crc),
	F(file), F(fmap), F(time), F(timerq), F(sendfile), F(path), F(direxp),
	F(ip), F(url), F(http), F(dns), F(icy), F(tls), F(webskt),
	F(domain),
	F(json),
	F(cmdarg),
	F(conf2), F(conf), F(conf_write), F(args), F(cue), F(xml),
	F(dns_client),
	F(cache),
};
#undef F

int main(int argc, const char **argv)
{
	size_t i, iarg;
	ffmem_init();

	ffdir_make(TESTDIR);

	if (argc == 1) {
		ffarr a = {};
		ffstr_catfmt(&a, "Supported tests: all ", 0);
		for (i = 0;  i < FFCNT(_fftests);  i++) {
			ffstr_catfmt(&a, "%s ", _fftests[i].nm);
		}
		ff_printf("%S\n", &a);
		ffarr_free(&a);
		return 0;

	} else if (!ffsz_cmp(argv[1], "all")) {
		//run all tests
		for (i = 0;  i < FFCNT(_fftests);  i++) {
			ffstdout_fmt("%s\n", _fftests[i].nm);
			_fftests[i].func();
			ffstdout_fmt("  OK\n");
		}

	} else {
		//run the specified tests only

		for (iarg = 1;  iarg < argc;  iarg++) {
			for (i = 0;  i < FFCNT(_fftests);  i++) {

				if (!ffsz_cmp(argv[iarg], _fftests[i].nm)) {
					ffstdout_fmt("%s\n", _fftests[i].nm);
					_fftests[i].func();
					ffstdout_fmt("  OK\n");
					break;
				}

				if (ffsz_matchz(argv[iarg], _fftests[i].nm)
					&& ffsz_eq(argv[iarg] + ffsz_len(_fftests[i].nm), "...")) {
					// run all next tests
					for (uint k = i;  k < FFCNT(_fftests);  k++) {
						ffstdout_fmt("%s\n", _fftests[k].nm);
						_fftests[k].func();
						ffstdout_fmt("  OK\n");
					}
					break;
				}
			}
		}
	}

	return 0;
}
