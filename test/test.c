
/**
Copyright (c) 2013 Simon Zolin
*/

#include <FFOS/test.h>
#include <FFOS/file.h>
#include <FFOS/timer.h>
#include <FF/array.h>
#include <FF/crc.h>
#include <FF/net/dns.h>
#include <FF/audio/icy.h>

#include <test/all.h>

#define x FFTEST_BOOL
#define CALL FFTEST_TIMECALL


size_t _test_readfile(const char *fn, char *buf, size_t n)
{
	fffd f;
	f = fffile_open(fn, O_RDONLY);
	x(f != FF_BADFD);
	n = fffile_read(f, buf, n);
	fffile_close(f);
	if (!x(n != 0 && n != (size_t)-1))
		return 0;
	return n;
}

static int test_crc()
{
	x(0x7052c01a == ffcrc32_get("hello, man!", FFSLEN("hello, man!")));
	x(0x7052c01a == ffcrc32_iget(FFSTR("HELLO, MAN!")));
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
FF_EXTN int test_ring(void);
FF_EXTN int test_ringbuf(void);
FF_EXTN int test_tq(void);
FF_EXTN int test_regex(void);
FF_EXTN int test_num(void);
extern int test_sort(void);
FF_EXTN int test_inchk_speed(void);
FF_EXTN int test_cue(void);
extern int test_xml(void);
extern int test_iso(void);
extern int test_tls(void);
extern int test_webskt(void);
extern int test_path(void);
extern int test_bits(void);
extern int test_sig(void);
extern void test_conf_write(void);
extern void test_dns_client(void);
extern int test_cache(void);
extern int test_ip();

struct test_s {
	const char *nm;
	int (*func)();
};

#define F(nm) { #nm, (int (*)())&test_ ## nm }
static const struct test_s _fftests[] = {
	F(str), F(regex)
	, F(num), F(sort), F(bits), F(list), F(rbtree), F(rbtlist), F(htable), F(ring), F(ringbuf), F(tq), F(crc)
	, F(file), F(fmap), F(time), F(timerq), F(sendfile), F(path), F(direxp), F(env), F(sig)
	, F(ip),
	F(url), F(http), F(dns), F(icy), F(tls), F(webskt)
	, F(json), F(conf), F(conf_write), F(args), F(cue), F(xml),
	F(iso),
	F(dns_client),
	F(cache),
};
#undef F

int main(int argc, const char **argv)
{
	size_t i, iarg;
	ffmem_init();

	fftestobj.flags |= FFTEST_STOPERR;

	if (argc == 1) {
		printf("Supported tests: all ");
		for (i = 0;  i < FFCNT(_fftests);  i++) {
			printf("%s ", _fftests[i].nm);
		}
		printf("\n");
		return 0;

	} else if (!ffsz_cmp(argv[1], "all")) {
		//run all tests
		for (i = 0;  i < FFCNT(_fftests);  i++) {
			FFTEST_TIMECALL(_fftests[i].func());
		}

	} else {
		//run the specified tests only

		for (iarg = 1;  iarg < argc;  iarg++) {
			for (i = 0;  i < FFCNT(_fftests);  i++) {

				if (!ffsz_cmp(argv[iarg], _fftests[i].nm)) {
					FFTEST_TIMECALL(_fftests[i].func());
					break;
				}
			}
		}
	}

	printf("%u tests were run, failed: %u.\n", fftestobj.nrun, fftestobj.nfail);

	return 0;
}
