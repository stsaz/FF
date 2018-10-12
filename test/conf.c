/** Test config parser, cmd-line arguments parser.
Copyright (c) 2013 Simon Zolin
*/

#include <FFOS/test.h>
#include <FFOS/file.h>
#include <FF/data/conf.h>
#include <FF/data/psarg.h>
#include "schem.h"
#include "all.h"

#define x FFTEST_BOOL


static int test_conf_parse(const char *testConfFile)
{
	ffconf conf;
	int rc;
	size_t n;
	ffstr3 buf = { 0 };
	const char *p;
	const char *pend;
	char fbuf[1024];
	size_t ibuf;

	ibuf = _test_readfile(testConfFile, fbuf, sizeof(fbuf));
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
			x(conf.type == FFCONF_TKEY);
			ffstr_catfmt(&buf, "%*c'%*s'"
				, (size_t)4 * conf.ctxs.len, (int)' '
				, (size_t)conf.val.len, conf.val.ptr);
			break;

		case FFPARS_VAL:
			x(conf.type == FFCONF_TVAL || conf.type == FFCONF_TVALNEXT);
			ffstr_catfmt(&buf, ":  '%*s'\n"
				, (size_t)conf.val.len, conf.val.ptr);
			break;

		case FFPARS_OPEN:
			x(conf.type == FFCONF_TOBJ);
			ffstr_catfmt(&buf, "\n%*c%c\n"
				, (size_t)4 * conf.ctxs.len, (int)' '
				, (int)'{');
			break;

		case FFPARS_CLOSE:
			x(conf.type == FFCONF_TOBJ);
			ffstr_catfmt(&buf, "%*c%c\n\n"
				, (size_t)4 * (conf.ctxs.len - 1), (int)' '
				, (int)'}');
			break;

		case FFPARS_MORE:
			break;

		default:
			x(0);
			fffile_fmt(ffstdout, &buf, "\nerror: %d:%d  (%d) %s\n"
				, conf.line, conf.ch, rc, ffpars_errstr(rc));
			return 0;
		}
	}

	ffarr out = {0};
	fffile_readall(&out, TESTDATADIR "/test-out.conf", -1);
	x(ffstr_eq2(&buf, &out));
	ffarr_free(&out);

	ffconf_parseclose(&conf);
	return 0;
}

int test_conf_schem(const char *testConfFile)
{
	obj_s o;
	ffconf conf;
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

	ibuf = _test_readfile(testConfFile, buf, sizeof(buf));
	if (ibuf == (size_t)-1)
		return 1;

	p = buf;
	pend = buf + ibuf;
	while (p != pend) {
		len = pend - p;
		rc = ffconf_parse(ps.p, p, &len);
		rc = ffconf_schemrun(&ps);
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

	ffconf_parseclose(&conf);
	ffpars_schemfree(&ps);

	{
		const char *s = "int  123 456 ";
		const char *s_end = s + ffsz_len(s);
		x(0 == ffconf_scheminit(&ps, &conf, &oinf));

		len = s_end - s;
		rc = ffconf_parse(&conf, s, &len);
		s += len;
		x(FFPARS_KEY == ffconf_schemrun(&ps));

		len = s_end - s;
		rc = ffconf_parse(&conf, s, &len);
		s += len;
		x(FFPARS_VAL == ffconf_schemrun(&ps));

		len = s_end - s;
		rc = ffconf_parse(&conf, s, &len);
		x(conf.type & FFCONF_TVALNEXT);
		x(FFPARS_EVALUNEXP == ffconf_schemrun(&ps));

		x(0 == ffconf_schemfin(&ps));
		ffconf_parseclose(&conf);
		ffpars_schemfree(&ps);
	}

	return 0;
}


static int test_args_parse()
{
	static const char *const args_ar[] = {
		"-a", "-bc", "someval"
		, "--def", "--qwe=val"
		, "--zxc", "val2", "ival"
	};
	static const char *const sargs[] = {
		"a", "b", "c", "someval"
		, "def", "qwe", "val"
		, "zxc", "val2", "ival"
	};
	static const int iargs[] = {
		FFPARS_KEY, FFPARS_KEY, FFPARS_KEY, FFPARS_VAL
		, FFPARS_KEY, FFPARS_KEY, FFPARS_VAL
		, FFPARS_KEY, FFPARS_VAL, FFPARS_VAL
	};
	static const int itypes[] = {
		FFPSARG_SHORT, FFPSARG_SHORT, FFPSARG_SHORT, FFPSARG_VAL
		, FFPSARG_LONG, FFPSARG_LONG, FFPSARG_KVAL
		, FFPSARG_LONG, FFPSARG_VAL, FFPSARG_INPUTVAL
	};
	const char *const *args = args_ar;
	int len;
	ffpsarg_parser p;
	int i;

	ffpsarg_parseinit(&p);
	for (i = 0;  i < FFCNT(sargs);  i++) {
		x(iargs[i] == ffpsarg_parse(&p, *args, &len)
			&& p.type == itypes[i] && ffstr_eqz(&p.val, sargs[i]));
		args += len;
	}

	return 0;
}

static void test_args_print(void)
{
	ffpsarg a;
	ffpsarg_init(&a, NULL, 0);
	const char *s;
	for (;;) {
		s = ffpsarg_next(&a);
		if (s == NULL)
			break;
		printf("arg: '%s'\n", s);
	}
	ffpsarg_destroy(&a);
}

typedef struct Opts {
	ffstr C;
	int S;
	int D;
	int V;
	uint i;
	uint input;
} Opts;

static const char *const ssigs[] = { "valss0", "valss1", "valss2" };
static const ffpars_enumlist en = { ssigs, FFCNT(ssigs), FFPARS_DSTOFF(Opts, S) };

static const ffpars_arg cmd_args[] = {
	{ "", FFPARS_TINT,  FFPARS_DSTOFF(Opts, input) },
	{ "vv", FFPARS_TBOOL | FFPARS_SETVAL('v') | FFPARS_FALONE,  FFPARS_DSTOFF(Opts, V) }
	, { "cc", FFPARS_TSTR | FFPARS_SETVAL('c'),  FFPARS_DSTOFF(Opts, C) }
	, { "dd", FFPARS_TBOOL | FFPARS_SETVAL('d') | FFPARS_FALONE,  FFPARS_DSTOFF(Opts, D) }
	, { "ss", FFPARS_TENUM | FFPARS_SETVAL('s'),  FFPARS_DST(&en) }
	, { "int", FFPARS_TINT,  FFPARS_DSTOFF(Opts, i) }
};

static int test_args_schem()
{
	Opts o;
	const ffpars_ctx ctx = { &o, cmd_args, FFCNT(cmd_args), NULL };
	ffpsarg_parser p;
	ffparser_schem ps;
	int r = 0;
	int n;
	int i;
	static const char *cmds[] = { "--cc=valcc", "-s", "valss1", "54321", "-dv", "--int=123" };

	ffmem_tzero(&o);
	x(0 == ffpsarg_scheminit(&ps, &p, &ctx));

	for (i = 0;  i < FFCNT(cmds);  ) {
		r = ffpsarg_parse(&p, cmds[i], &n);
		i += n;
		r = ffpsarg_schemrun(&ps);
		x(!ffpars_iserr(r));
		if (ffpars_iserr(r)) {
			fffile_fmt(ffstdout, NULL, "\nerror: %u:%u near '%S' (%u) %s\n"
				, p.line, p.ch, &p.val, r, ffpars_errstr(r));
			break;
		}
	}

	x(0 == ffpsarg_schemfin(&ps));

	x(ffstr_eqcz(&o.C, "valcc"));
	x(o.S == 1); //valss1
	x(o.D == 1);
	x(o.V == 1);
	x(o.i == 123);
	x(o.input == 54321);

	ffpsarg_parseclose(&p);
	ffpars_schemfree(&ps);

	return 0;
}

static int test_args_err()
{
	Opts o;
	ffpars_ctx ctx = { &o, cmd_args, FFCNT(cmd_args), NULL };
	ffpsarg_parser p;
	ffparser_schem ps;
	int r = 0;
	int n = 0;

	ffmem_tzero(&o);
	{
		static const char *const cmds[] = { "--vv", "--somearg" };
		ffpsarg_scheminit(&ps, &p, &ctx);

		x(FFPARS_KEY == ffpsarg_parse(&p, cmds[0], &n));
		x(FFPARS_KEY == ffpsarg_schemrun(&ps));

		r = ffpsarg_parse(&p, cmds[1], &n);
		x(FFPARS_EUKNKEY == ffpsarg_schemrun(&ps)
			&& ffstr_eqcz(&p.val, "somearg")
			&& p.line == 2);

		ffpsarg_parseclose(&p);
		ffpars_schemfree(&ps);
	}

	{
		ffpsarg_scheminit(&ps, &p, &ctx);
		x(FFPARS_KEY == ffpsarg_parse(&p, "-s", &n));
		x(FFPARS_KEY == ffpsarg_schemrun(&ps));

		x(FFPARS_EVALEMPTY == ffpsarg_schemfin(&ps));

		ffpsarg_parseclose(&p);
		ffpars_schemfree(&ps);
	}

	ctx.args++;
	ctx.nargs--; //don't set o.input
	{
		static const char *const cmds[] = { "-d", "asdf" };
		ffpsarg_scheminit(&ps, &p, &ctx);

		x(FFPARS_KEY == ffpsarg_parse(&p, cmds[0], &n));
		x(FFPARS_KEY == ffpsarg_schemrun(&ps));

		r = ffpsarg_parse(&p, cmds[1], &n);
		x(FFPARS_EVALUNEXP == ffpsarg_schemrun(&ps));

		ffpsarg_parseclose(&p);
		ffpars_schemfree(&ps);
	}
	ctx.args--;
	ctx.nargs++;

	(void)r;
	return 0;
}

#define RES_STR \
"# comment-sharp\n\
ctx param {\n\
	k 1234567890 val\n\
	\"k-2\" \"v-2\"\n\
	k2 {\n\
		k3\n\
	}\n\
}\n"

static void test_conf_write(void)
{
	FFTEST_FUNC;
	ffconfw cw;
	ffconf_winit(&cw, NULL, 0);
	cw.flags |= FFCONF_PRETTY;

	ffconf_write(&cw, "comment-sharp", FFCONF_STRZ, FFCONF_TCOMMENTSHARP);

	ffconf_write(&cw, "ctx", 3, FFCONF_TKEY);
	ffconf_write(&cw, "param", FFCONF_STRZ, FFCONF_TVAL);
	ffconf_write(&cw, NULL, FFCONF_OPEN, FFCONF_TOBJ);

		ffconf_write(&cw, "k", FFCONF_STRZ, FFCONF_TKEY);
		int64 i = 1234567890;
		ffconf_write(&cw, &i, FFCONF_INT64, FFCONF_TVAL);
		ffconf_write(&cw, "val", FFCONF_STRZ, FFCONF_TVAL);

		ffconf_write(&cw, "k-2", FFCONF_STRZ, FFCONF_TKEY);
		ffconf_write(&cw, "v-2", FFCONF_STRZ, FFCONF_TVAL);

		ffconf_write(&cw, "k2", FFCONF_STRZ, FFCONF_TKEY);
		ffconf_write(&cw, NULL, FFCONF_OPEN, FFCONF_TOBJ);
			ffconf_write(&cw, "k3", FFCONF_STRZ, FFCONF_TKEY);
		ffconf_write(&cw, NULL, FFCONF_CLOSE, FFCONF_TOBJ);

	ffconf_write(&cw, NULL, FFCONF_CLOSE, FFCONF_TOBJ);
	x(0 != ffconf_write(&cw, NULL, 0, FFCONF_FIN));

	ffstr s;
	ffconf_output(&cw, &s);
	if (!x(ffstr_eqz(&s, RES_STR)))
		fffile_write(ffstdout, s.ptr, s.len);
	ffconf_wdestroy(&cw);
}

int test_conf()
{
	FFTEST_FUNC;

	test_conf_write();
	test_conf_parse(TESTDATADIR "/schem.conf");
	test_conf_schem(TESTDATADIR "/schem.conf");
	return 0;
}


// DEFERRED DATA

struct def_s {
	ffconf_ctxcopy ctx;
	ffarr data;
	uint done;
	uint active;
};
static struct def_s defer;

static int deferred_key(ffparser_schem *ps, void *obj, ffpars_ctx *ctx)
{
	ffconf_ctxcopy_init(&defer.ctx, ps);
	defer.active = 1;
	return 0;
}
static int deferred_key2(ffparser_schem *ps, void *obj, const ffstr *val)
{
	x(ffstr_eqz(val, "done"));
	defer.done = 1;
	return 0;
}
static const ffpars_arg deferred_args[] = {
	{ "key",	FFPARS_TOBJ | FFPARS_FOBJ1, FFPARS_DST(&deferred_key) },
	{ "k",	FFPARS_TSTR, FFPARS_DST(&deferred_key2) },
};

#define DEFERRED_DATA "\
key1 val1\n\
key1 val1 val2\n\
key2 arg2 {\n\
key3 val3\n\
}\n"

static void test_conf_deferred()
{
	FFTEST_FUNC;

	ffarr a = {};
	x(0 == fffile_readall(&a, TESTDATADIR "/deferred.conf", -1));

	int r;
	ffparser_schem ps;
	ffconf conf;
	ffpars_ctx ctx;
	ffpars_setargs(&ctx, NULL, deferred_args, FFCNT(deferred_args));
	ffconf_scheminit(&ps, &conf, &ctx);

	ffstr data = *(ffstr*)&a;
	while (data.len != 0) {

		r = ffconf_parsestr(&conf, &data);

		if (defer.active) {
			int r2 = ffconf_ctx_copy(&defer.ctx, &conf);
			x(r2 >= 0);
			if (r2 > 0) {
				ffstr d = ffconf_ctxcopy_acquire(&defer.ctx);
				ffarr_set3(&defer.data, d.ptr, d.len, d.len);
				defer.active = 0;
			}
		} else
			r = ffconf_schemrun(&ps);

		if (ffpars_iserr(r)) {
			x(0);
			return;
		}
	}

	r = ffconf_schemfin(&ps);
	if (ffpars_iserr(r))
		x(0);
	ffarr_free(&a);

	x(defer.done);

	x(ffstr_eqz((ffstr*)&defer.data, DEFERRED_DATA));
	ffarr_free(&defer.data);
	ffconf_ctxcopy_destroy(&defer.ctx);
}

int test_args()
{
	FFTEST_FUNC;

	test_args_parse();
	test_args_print();
	test_args_schem();
	test_args_err();
	test_conf_deferred();
	return 0;
}
