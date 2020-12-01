/** ff: cmdarg.h tester
2020, Simon Zolin
*/

#include <FF/data/cmdarg.h>
#include <FF/data/cmdarg-scheme.h>
#include <FFOS/std.h>
#include <test/test.h>

struct obj_s {
	ffstr val1, val23, val4;
	ffint64 intval;
	ffbyte _switch;
};

int arg_val(const ffcmdarg_scheme *as, void *obj, ffstr *s)
{
	struct obj_s *o = obj;
	o->val1 = *s;
	return 0;
}

int arg_func1(const ffcmdarg_scheme *as, void *obj, ffstr *s)
{
	struct obj_s *o = obj;
	o->val23 = *s;
	return 0;
}

int arg_func2(const ffcmdarg_scheme *as, void *obj, ffstr *s)
{
	struct obj_s *o = obj;
	o->val4 = *s;
	return 0;
}

int arg_switch(const ffcmdarg_scheme *as, void *obj)
{
	struct obj_s *o = obj;
	o->_switch = 1;
	return 0;
}

int arg_int(const ffcmdarg_scheme *as, void *obj, ffint64 i)
{
	struct obj_s *o = obj;
	o->intval = i;
	return 0;
}

void test_cmdarg_scheme()
{
	static const ffcmdarg_arg args[] = {
		{ 0, "", FFCMDARG_TSTR, (ffsize)arg_val },
		{ 's', "long1", FFCMDARG_TSTR, (ffsize)arg_func1 },
		{ 0, "long2", FFCMDARG_TSTR, (ffsize)arg_func2 },
		{ 0, "switch", FFCMDARG_TSWITCH, (ffsize)arg_switch },
		{ 0, "int", FFCMDARG_TINT, (ffsize)arg_int },
		{},
	};

	const char *argv[] = {
		"",
		"val1",
		"-s", "val2",
		"--long1", "val3",
		"--long2=val4",
		"--switch",
		"--int=1234",
	};

	struct obj_s o = {};
	ffstr err = {};
	int r = ffcmdarg_parse_object(args, &o, argv, FF_COUNT(argv), 0, &err);
	if (r != 0)
		fflog("ffcmdarg_parse_object: %S", &err);
	ffstr_free(&err);
	xieq(0, r);

	xseq(&o.val1, "val1");
	xseq(&o.val23, "val3");
	xseq(&o.val4, "val4");
	xieq(1234, o.intval);
	xieq(1, o._switch);
}

void test_cmdarg()
{
	ffstr s;
	ffcmdarg a;
	const char *argv[] = {
		"",
		"val1",
		"-s", "val2",
		"--long1", "val3",
		"--long2=val4",
	};
	ffcmdarg_init(&a, argv, FF_COUNT(argv));

	xieq(FFCMDARG_RVAL, ffcmdarg_parse(&a, &s));
	xseq(&s, "val1");

	xieq(FFCMDARG_RKEYSHORT, ffcmdarg_parse(&a, &s));
	xseq(&s, "s");
	xieq(FFCMDARG_RVAL, ffcmdarg_parse(&a, &s));
	xseq(&s, "val2");

	xieq(FFCMDARG_RKEYLONG, ffcmdarg_parse(&a, &s));
	xseq(&s, "long1");
	xieq(FFCMDARG_RVAL, ffcmdarg_parse(&a, &s));
	xseq(&s, "val3");

	xieq(FFCMDARG_RKEYLONG, ffcmdarg_parse(&a, &s));
	xseq(&s, "long2");
	xieq(FFCMDARG_RKEYVAL, ffcmdarg_parse(&a, &s));
	xseq(&s, "val4");

	xieq(0, ffcmdarg_fin(&a));

	test_cmdarg_scheme();
}
