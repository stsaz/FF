/** Test config parser, cmd-line arguments parser.
Copyright (c) 2013 Simon Zolin
*/

#include <FFOS/test.h>
#include <FFOS/file.h>
#include <FF/conf.h>
#include <FF/psarg.h>
#include "schem.h"
#include "all.h"

#define x FFTEST_BOOL


static int test_conf_parse(const ffsyschar *testConfFile)
{
	ffparser conf;
	int rc;
	size_t n;
	ffstr3 buf = { 0 };
	const char *p;
	const char *pend;
	char fbuf[1024];
	size_t ibuf;

	ibuf = getFileContents(testConfFile, fbuf, sizeof(fbuf));
	if (ibuf == (size_t)-1)
		return 1;

	ffconf_parseinit(&conf);

	p = fbuf;
	pend = fbuf + ibuf;
	for (;;) {
		n = pend - p;
		if (n == 0)
			break;
		rc = ffconf_parse(&conf, p, &n);
		if (n != 0)
			p += n;

		switch (rc) {
		case FFPARS_KEY:
			fffile_fmt(ffstdout, &buf, "%*c'%*s'  "
				, (size_t)4 * conf.ctxs.len, (int)' '
				, (size_t)conf.val.len, conf.val.ptr);
			break;

		case FFPARS_VAL:
			fffile_fmt(ffstdout, &buf, ":  '%*s'\n"
				, (size_t)conf.val.len, conf.val.ptr);
			break;

		case FFPARS_OPEN:
			fffile_fmt(ffstdout, &buf, "\n%*c%c\n"
				, (size_t)4 * conf.ctxs.len, (int)' '
				, (int)'{');
			break;

		case FFPARS_CLOSE:
			fffile_fmt(ffstdout, &buf, "%*c%c\n\n"
				, (size_t)4 * (conf.ctxs.len - 1), (int)' '
				, (int)'}');
			break;

		case FFPARS_MORE:
			x(0 == ffpars_savedata(&conf));
			break;

		default:
			x(0);
			fffile_fmt(ffstdout, &buf, "\nerror: %d:%d  (%d) %s\n"
				, conf.line, conf.ch, rc, ffpars_errstr(rc));
			return 0;
		}
	}

	ffpars_free(&conf);
	return 0;
}

int test_conf_schem(const ffsyschar *testConfFile)
{
	obj_s o;
	ffparser conf;
	ffparser_schem ps;
	int rc;
	const char *p
		, *pend;
	size_t len;
	const ffpars_ctx oinf = { &o, obj_schem, FFCNT(obj_schem) };
	char buf[1024];
	size_t ibuf;

	memset(&o, 0, sizeof(obj_s));
	x(0 == ffconf_scheminit(&ps, &conf, &oinf));

	ibuf = getFileContents(testConfFile, buf, sizeof(buf));
	if (ibuf == (size_t)-1)
		return 1;

	p = buf;
	pend = buf + ibuf;
	while (p != pend) {
		len = pend - p;
		rc = ffconf_parse(ps.p, p, &len);
		rc = ffpars_schemrun(&ps, rc);
		if (!x(rc <= 0)) {
			fffile_fmt(ffstdout, NULL, "\nerror: %u:%u near '%S' (%u) %s\n"
				, conf.line, conf.ch, &conf.val, rc, ffpars_errstr(rc));
			break;
		}
		p += len;
	}

	x(0 == ffconf_schemfin(&ps));

	objChk(&o);

	x(o.o1->i == 1111);
	x(ffstr_eqcz(&o.o1val, "my value"));

	ffstr_free(&ps.vals[0]);
	ffstr_free(&o.s);
	ffmem_free(o.o[0]);
	ffmem_free(o.o[1]);
	ffmem_free(o.o1);

	ffpars_free(&conf);
	ffpars_schemfree(&ps);
	return 0;
}


static int test_args_parse()
{
	static const char *const args_ar[] = {
		"-a", "-bc", "someval"
		, "--def", "--qwe=val"
		, "--zxc", "val2"
	};
	static const char *const sargs[] = {
		"a", "b", "c", "someval"
		, "def", "qwe", "val"
		, "zxc", "val2"
	};
	static const int iargs[] = {
		FFPARS_KEY, FFPARS_KEY, FFPARS_KEY, FFPARS_VAL
		, FFPARS_KEY, FFPARS_KEY, FFPARS_VAL
		, FFPARS_KEY, FFPARS_VAL
	};
	static const int itypes[] = {
		FFPSARG_SHORT, FFPSARG_SHORT, FFPSARG_SHORT, FFPSARG_VAL
		, FFPSARG_LONG, FFPSARG_LONG, FFPSARG_VAL
		, FFPSARG_LONG, FFPSARG_VAL
	};
	const char *const *args = args_ar;
	int len;
	ffparser p;
	int i;

	ffpars_init(&p);
	for (i = 0;  i < FFCNT(sargs);  i++) {
		x(iargs[i] == ffpsarg_parse(&p, *args, &len)
			&& p.type == itypes[i] && ffstr_eqz(&p.val, sargs[i]));
		args += len;
	}

	return 0;
}

typedef struct Opts {
	ffstr C;
	int S;
	int D;
	int V;
} Opts;

static const char *const ssigs[] = { "valss0", "valss1", "valss2" };
static const ffpars_enumlist en = { ssigs, FFCNT(ssigs), FFPARS_DSTOFF(Opts, S) };

static const ffpars_arg cmd_args[] = {
	{ "vv", FFPARS_TBOOL | FFPARS_SETVAL('v') | FFPARS_FALONE,  FFPARS_DSTOFF(Opts, V) }
	, { "cc", FFPARS_TSTR | FFPARS_SETVAL('c'),  FFPARS_DSTOFF(Opts, C) }
	, { "dd", FFPARS_TBOOL | FFPARS_SETVAL('d') | FFPARS_FALONE,  FFPARS_DSTOFF(Opts, D) }
	, { "ss", FFPARS_TENUM | FFPARS_SETVAL('s'),  FFPARS_DST(&en) }
};

static int test_args_schem()
{
	Opts o;
	const ffpars_ctx ctx = { &o, cmd_args, FFCNT(cmd_args), NULL };
	ffparser p;
	ffparser_schem ps;
	int r = 0;
	int n;
	int i;
	static const char *cmds[] = { "--cc=valcc", "-s", "valss1", "-dv" };

	ffmem_tzero(&o);
	x(0 == ffpsarg_scheminit(&ps, &p, &ctx));

	for (i = 0;  i < FFCNT(cmds);  ) {
		r = ffpsarg_parse(&p, cmds[i], &n);
		i += n;
		r = ffpars_schemrun(&ps, r);
		if (r > 0)
			break;
	}

	x(0 == ffpsarg_schemfin(&ps));

	x(ffstr_eqcz(&o.C, "valcc"));
	x(o.S == 1); //valss1
	x(o.D == 1);
	x(o.V == 1);

	ffpars_free(&p);
	ffpars_schemfree(&ps);

	return 0;
}

static int test_args_err()
{
	Opts o;
	const ffpars_ctx ctx = { &o, cmd_args, FFCNT(cmd_args), NULL };
	ffparser p;
	ffparser_schem ps;
	int r = 0;
	int n = 0;

	ffmem_tzero(&o);
	{
		static const char *const cmds[] = { "--vv", "--somearg" };
		ffpsarg_scheminit(&ps, &p, &ctx);

		r = ffpsarg_parse(&p, cmds[0], &n);
		x(FFPARS_KEY == ffpars_schemrun(&ps, r));

		r = ffpsarg_parse(&p, cmds[1], &n);
		x(FFPARS_EUKNKEY == ffpars_schemrun(&ps, r)
			&& ffstr_eqcz(&p.val, "somearg")
			&& p.line == 2);

		ffpars_free(&p);
		ffpars_schemfree(&ps);
	}

	{
		ffpsarg_scheminit(&ps, &p, &ctx);
		r = ffpsarg_parse(&p, "-s", &n);
		x(FFPARS_KEY == ffpars_schemrun(&ps, r));

		x(FFPARS_EVALEMPTY == ffpsarg_schemfin(&ps));

		ffpars_free(&p);
		ffpars_schemfree(&ps);
	}

	{
		static const char *const cmds[] = { "-d", "asdf" };
		ffpsarg_scheminit(&ps, &p, &ctx);

		r = ffpsarg_parse(&p, cmds[0], &n);
		x(FFPARS_KEY == ffpars_schemrun(&ps, r));

		r = ffpsarg_parse(&p, cmds[1], &n);
		x(FFPARS_EVALUNEXP == ffpars_schemrun(&ps, r));

		ffpars_free(&p);
		ffpars_schemfree(&ps);
	}

	return 0;
}

int test_conf()
{
	FFTEST_FUNC;

	test_conf_parse(TESTDIR TEXT("/schem.conf"));
	test_conf_schem(TESTDIR TEXT("/schem.conf"));
	return 0;
}

int test_args()
{
	FFTEST_FUNC;

	test_args_parse();
	test_args_schem();
	test_args_err();
	return 0;
}
