/** ffbase: conf2.h tester
2020, Simon Zolin
*/

#include <FF/data/conf2.h>
#include <FF/data/conf2-scheme.h>
#include <FFOS/file.h>
#include <test/test.h>
#include "all.h"

void test_conf2_r()
{
	ffvec data = {};
	xieq(0, fffile_readwhole(TESTDATADIR "/test.conf", &data, -1));
	ffstr d = FFSTR_INITSTR(&data);

	ffconf c;
	ffconf_init(&c);

	xieq(FFCONF_RKEY, ffconf_parse(&c, &d));
	xseq(&c.val, "key#0");

	xieq(FFCONF_RKEY, ffconf_parse(&c, &d));
	xseq(&c.val, "key0");

	xieq(FFCONF_RKEY, ffconf_parse(&c, &d));
	xseq(&c.val, "key1");
	xieq(FFCONF_RVAL, ffconf_parse(&c, &d));
	xseq(&c.val, "my str");

	xieq(FFCONF_RKEY, ffconf_parse(&c, &d));
	xseq(&c.val, "key2\n");
	xieq(FFCONF_RVAL, ffconf_parse(&c, &d));
	xseq(&c.val, "my str ing");

	xieq(FFCONF_RKEY, ffconf_parse(&c, &d));
	xseq(&c.val, "obj");
	xieq(FFCONF_ROBJ_OPEN, ffconf_parse(&c, &d));
	xieq(FFCONF_RKEY, ffconf_parse(&c, &d));
	xseq(&c.val, "key3");
	xieq(FFCONF_RVAL, ffconf_parse(&c, &d));
	xseq(&c.val, "-1234");
	xieq(FFCONF_ROBJ_CLOSE, ffconf_parse(&c, &d));

	xieq(FFCONF_RKEY, ffconf_parse(&c, &d));
	xseq(&c.val, "obj 1");
	xieq(FFCONF_RVAL, ffconf_parse(&c, &d));
	xseq(&c.val, "value1");
	xieq(FFCONF_ROBJ_OPEN, ffconf_parse(&c, &d));
	xieq(FFCONF_ROBJ_CLOSE, ffconf_parse(&c, &d));

	xieq(FFCONF_RKEY, ffconf_parse(&c, &d));
	xseq(&c.val, "list");
	xieq(FFCONF_RVAL, ffconf_parse(&c, &d));
	xseq(&c.val, "11");
	xieq(FFCONF_RVAL_NEXT, ffconf_parse(&c, &d));
	xseq(&c.val, "22");
	xieq(FFCONF_RVAL_NEXT, ffconf_parse(&c, &d));
	xseq(&c.val, "33");

	xieq(0, ffconf_fin(&c));

	ffvec_free(&data);
}

typedef struct cstruct cstruct;
struct cstruct {
	ffstr s;
	ffint64 n;
	ffbyte b;
	ffstr list[2];
	int ilist;

	cstruct *obj;
};

static const ffconf_arg cargs[];

static int cs_obj(ffconf_scheme *cs, cstruct *o)
{
	o->obj = ffmem_new(cstruct);
	ffconf_scheme_addctx(cs, cargs, o->obj);
	return 0;
}

static int cs_list(ffconf_scheme *cs, cstruct *o, ffstr *val)
{
	o->list[o->ilist++] = *val;
	return 0;
}

#define OFF(m)  FF_OFF(cstruct, m)
static const ffconf_arg cargs[] = {
	{ "str",	FFCONF_TSTR,	OFF(s) },
	{ "int",	FFCONF_TINT,	OFF(n) },
	{ "bool",	FFCONF_TBOOL,	OFF(b) },
	{ "list",	FFCONF_TSTR | FFCONF_FLIST,	(ffsize)cs_list },

	{ "obj",	FFCONF_TOBJ,	(ffsize)cs_obj },
	{},
};
#undef OFF

void test_conf2_scheme()
{
	ffstr err = {};
	ffvec d = {};
	x(0 == fffile_readwhole("/d/src/ff/test/data/object.conf", &d, -1));
	ffstr s = FFSTR_INITSTR(&d);

	cstruct o = {};
	int r = ffconf_parse_object(cargs, &o, &s, 0, &err);
	if (r != 0)
		fprintf(stderr, "%d %.*s\n", r, (int)err.len, err.ptr);
	ffstr_free(&err);
	xieq(0, r);
	xseq(&o.s, "string");
	x(o.n == 1234);
	x(o.b == 1);
	xseq(&o.list[0], "val1");
	xseq(&o.list[1], "val2");
	ffstr_free(&o.s);

	xseq(&o.obj->s, "objstring");
	x(o.obj->n == 1234);
	x(o.obj->b == 1);
	x(o.obj->obj != NULL);
	ffstr_free(&o.obj->s);
	ffmem_free(o.obj->obj);
	ffmem_free(o.obj);

	ffvec_free(&d);
}

void test_conf2()
{
	test_conf2_r();
	test_conf2_scheme();
}
