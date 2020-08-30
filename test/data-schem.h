/**
Copyright (c) 2013 Simon Zolin
*/

#pragma once


typedef struct obj_s obj_s;

struct obj_s {
	ffstr snull;
	int inull;
	byte bnull;
	byte have_objnull;

	ffstr s;
	int64 size;
	int64 i;
	int i4;
	byte b;
	byte en;
	uint bits;

	obj_s *o[2];
	int iobj;

	ffstr ar[3];
	int iar;

	unsigned arrCloseOk : 1
		, objCloseOk : 1
		, any : 1;

	ffstr k[4];
	int ik;

	obj_s *o1;
	ffstr o1val;
};

static int new_objnull(ffparser_schem *ps, void *obj, ffpars_ctx *ctx);
static int newObj(ffparser_schem *ps, void *obj, ffpars_ctx *ctx);
static int any(ffparser_schem *ps, void *obj, const ffstr *val)
{
	obj_s *o = obj;
	o->k[o->ik++] = *val;
	return 0;
}
static int any2(ffparser_schem *ps, void *obj)
{
	obj_s *o = obj;
	o->any = 1;
	return 0;
}
static int objClose(ffparser_schem *ps, void *obj)
{
	obj_s *o = obj;
	o->objCloseOk = 1;
	return 0;
}
static int newObj1(ffparser_schem *ps, void *obj, ffpars_ctx *ctx);

static int newArr(ffparser_schem *ps, void *obj, ffpars_ctx *ctx);
static int arrItem(ffparser_schem *ps, void *obj, const ffstr *val)
{
	obj_s *o = obj;
	o->ar[o->iar++] = *val;
	return 0;
}
static int arrItemInt(ffparser_schem *ps, void *obj, const int64 *val)
{
	obj_s *o = obj;
	if (*val != 11)
		return FFPARS_EBADVAL;
	ffstr_setz(&o->ar[o->iar], "11");
	o->iar++;
	return 0;
}
static int arrClose(ffparser_schem *ps, void *obj)
{
	obj_s *o = obj;
	o->arrCloseOk = 1;
	return 0;
}

static const ffpars_arg arr_schem[] = {
	{ NULL, FFPARS_TINT, FFPARS_DST(&arrItemInt) }
	, { NULL, FFPARS_TSTR | FFPARS_FNOTEMPTY, FFPARS_DST(&arrItem) }
	, { NULL, FFPARS_TOBJ, FFPARS_DST(&newObj) }
	, { NULL, FFPARS_TCLOSE, FFPARS_DST(&arrClose) }
};

enum {
	enA
	, enB
	, enC
};

static const char *const enumStr[] = {
	"A", "B", "C"
};

static const ffpars_enumlist enumConf = {
	enumStr, FFCNT(enumStr), FFPARS_DSTOFF(obj_s, en)
};

static int glob_setctx(ffparser_schem *ps, void *obj, ffpars_ctx *ctx);
static const ffpars_arg glob_ctx = { NULL, FFPARS_TOBJ, FFPARS_DST(&glob_setctx) };

static const ffpars_arg obj_schem[] = {
#ifdef TEST_JSON_SCHEME
	{ "objnull", FFPARS_TOBJ | FFPARS_FNULL | FFPARS_FMULTI, FFPARS_DST(&new_objnull) },
	{ "snull", FFPARS_TSTR | FFPARS_FNULL, FFPARS_DSTOFF(obj_s, snull) },
	{ "inull", FFPARS_TINT | FFPARS_FNULL, FFPARS_DSTOFF(obj_s, inull) },
	{ "bnull", FFPARS_TBOOL8 | FFPARS_FNULL, FFPARS_DSTOFF(obj_s, bnull) },
#endif

	{ "str", FFPARS_TSTR | FFPARS_FCOPY | FFPARS_FNOTEMPTY, FFPARS_DSTOFF(obj_s, s) }
	, { "size", FFPARS_TSIZE | FFPARS_F64BIT, FFPARS_DSTOFF(obj_s, size) }
	, { "int", FFPARS_TINT | FFPARS_FNOTZERO | FFPARS_FREQUIRED
		| FFPARS_F64BIT | FFPARS_FSIGN, FFPARS_DSTOFF(obj_s, i) }
	, { "i4", FFPARS_TINT, FFPARS_DSTOFF(obj_s, i4) }
	, { "bool", FFPARS_TBOOL | FFPARS_F8BIT, FFPARS_DSTOFF(obj_s, b) }
	, { "bit1", FFPARS_TBOOL | FFPARS_SETBIT(0), FFPARS_DSTOFF(obj_s, bits) }
	, { "bit2", FFPARS_TBOOL | FFPARS_SETBIT(1), FFPARS_DSTOFF(obj_s, bits) }
	, { "enum", FFPARS_TENUM | FFPARS_F8BIT, FFPARS_DST(&enumConf) }
	, { "obj", FFPARS_TOBJ | FFPARS_FMULTI, FFPARS_DST(&newObj) }
	, { "arr", FFPARS_TARR, FFPARS_DST(&newArr) }
	, { "any", FFPARS_TANYTHING, FFPARS_DST(&any2) }

	, { "list", FFPARS_TSTR | FFPARS_FLIST, FFPARS_DST(&arrItem) }
	, { "obj1", FFPARS_TOBJ | FFPARS_FOBJ1, FFPARS_DST(&newObj1) }

	, { "*", FFPARS_TSTR, FFPARS_DST(&any) }
	, { NULL, FFPARS_TCLOSE, FFPARS_DST(&objClose) }
};

static int glob_setctx(ffparser_schem *ps, void *obj, ffpars_ctx *ctx)
{
	ffpars_setargs(ctx, obj, obj_schem, FFCNT(obj_schem));
	return 0;
}

static int new_objnull(ffparser_schem *ps, void *obj, ffpars_ctx *ctx)
{
	obj_s *o = obj;
	if (ctx == NULL)
		o->have_objnull = 1;
	return 0;
}

int newObj(ffparser_schem *ps, void *obj, ffpars_ctx *ctx)
{
	(void)new_objnull;

	obj_s *o = obj;
	obj_s *subobj;
	subobj = ffmem_tcalloc1(obj_s);
	if (subobj == NULL)
		return FFPARS_ESYS;
	ffpars_setargs(ctx, subobj, obj_schem, FFCNT(obj_schem));
#ifdef TEST_JSON_SCHEME
	if (o->iobj == 0) {
		subobj->have_objnull = 0;
		subobj->inull = 1;
		subobj->bnull = 1;
		subobj->snull.len = 1;
	}
#endif
	o->o[o->iobj++] = subobj;
	return 0;
}

int newArr(ffparser_schem *ps, void *obj, ffpars_ctx *ctx)
{
	ffpars_setargs(ctx, obj, arr_schem, FFCNT(arr_schem));
	return 0;
}

int newObj1(ffparser_schem *ps, void *obj, ffpars_ctx *ctx)
{
	obj_s *o = obj;
	obj_s *subobj = ffmem_tcalloc1(obj_s);
	if (subobj == NULL)
		return FFPARS_ESYS;
	o->o1 = subobj;
	ffpars_setargs(ctx, subobj, obj_schem, FFCNT(obj_schem));
	o->o1val = ps->vals[0];
	return 0;
}

static int objChk(const obj_s *o)
{
	x(ffstr_eqcz(&o->s, "my string"));
	x(o->size == 64 * 1024);
	x(o->i == 1000);
	x(o->i4 == 9999);
	x(o->b == 1);
	x(o->bits == (0x01|0x02));
	x(o->en == enB);

	x(o->o[0]->i == 1234);
	x(o->o[0]->objCloseOk == 1);

	x(ffstr_eqcz(&o->ar[0], "11"));
	x(ffstr_eqcz(&o->ar[1], "22"));
	x(ffstr_eqcz(&o->ar[2], "33"));

	x(o->o[1]->i == -2345);
	x(ffstr_eqcz(&o->o[1]->k[0], "k1"));
	x(ffstr_eqcz(&o->o[1]->k[1], "1"));
	x(ffstr_eqcz(&o->o[1]->k[2], "k2"));
	x(ffstr_eqcz(&o->o[1]->k[3], "2"));
	x(o->o[1]->objCloseOk == 1);

	x(o->objCloseOk == 1);
	x(o->any == 1);

	return 0;
}
