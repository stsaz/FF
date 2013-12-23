/**
Copyright (c) 2013 Simon Zolin
*/

#pragma once

#define x FFTEST_BOOL


typedef struct obj_s obj_s;

struct obj_s {
	ffstr s;
	int64 size;
	int64 i;
	int i4;
	byte b;
	byte en;
	byte bits;

	obj_s *o[2];
	int iobj;

	ffstr ar[3];
	int iar;

	unsigned arrCloseOk : 1
		, objCloseOk : 1;

	ffstr k[4];
	int ik;

	obj_s *o1;
	ffstr o1val;
};

static int newObj(ffparser_schem *ps, void *obj, ffpars_ctx *ctx);
static int any(ffparser_schem *ps, void *obj, const ffstr *val)
{
	obj_s *o = obj;
	o->k[o->ik++] = *val;
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
	o->ar[o->iar++] = ps->p->val;
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

static const ffpars_arg obj_schem[] = {
	{ "str", FFPARS_TSTR | FFPARS_FCOPY | FFPARS_FNOTEMPTY, FFPARS_DSTOFF(obj_s, s) }
	, { "size", FFPARS_TSIZE | FFPARS_F64BIT, FFPARS_DSTOFF(obj_s, size) }
	, { "int", FFPARS_TINT | FFPARS_FNOTZERO | FFPARS_FREQUIRED
		| FFPARS_F64BIT | FFPARS_FSIGN, FFPARS_DSTOFF(obj_s, i) }
	, { "i4", FFPARS_TINT, FFPARS_DSTOFF(obj_s, i4) }
	, { "bool", FFPARS_TBOOL | FFPARS_F8BIT, FFPARS_DSTOFF(obj_s, b) }
	, { "bit1", FFPARS_TBOOL | FFPARS_F8BIT | FFPARS_SETBIT(0), FFPARS_DSTOFF(obj_s, bits) }
	, { "bit2", FFPARS_TBOOL | FFPARS_F8BIT | FFPARS_SETBIT(1), FFPARS_DSTOFF(obj_s, bits) }
	, { "enum", FFPARS_TENUM | FFPARS_F8BIT, FFPARS_DST(&enumConf) }
	, { "obj", FFPARS_TOBJ | FFPARS_FNULL | FFPARS_FMULTI, FFPARS_DST(&newObj) }
	, { "arr", FFPARS_TARR, FFPARS_DST(&newArr) }
	, { "*", FFPARS_TSTR, FFPARS_DST(&any) }
	, { NULL, FFPARS_TCLOSE, FFPARS_DST(&objClose) }

	, { "list", FFPARS_TSTR | FFPARS_FLIST, FFPARS_DST(&arrItem) }
	, { "obj1", FFPARS_TOBJ | FFPARS_FOBJ1, FFPARS_DST(&newObj1) }
};

int newObj(ffparser_schem *ps, void *obj, ffpars_ctx *ctx)
{
	obj_s *o = obj;
	obj_s *subobj = (void*)-1;
	if (ctx != NULL) {
		subobj = ffmem_tcalloc1(obj_s);
		if (subobj == NULL)
			return FFPARS_ESYS;
		ffpars_setargs(ctx, subobj, obj_schem, FFCNT(obj_schem));
	}
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
	x(ffstr_eqz(&o->s, "my string"));
	x(o->size == 64 * 1024);
	x(o->i == 1000);
	x(o->i4 == 9999);
	x(o->b == 1);
	x(o->bits == (0x01|0x02));
	x(o->en == enB);

	x(o->o[0]->i == 1234);
	//x(o->o[0]->o[0] == (void*)-1);
	x(o->o[0]->objCloseOk == 1);

	x(ffstr_eqz(&o->ar[0], "11"));
	x(ffstr_eqz(&o->ar[1], "22"));
	x(ffstr_eqz(&o->ar[2], "33"));

	x(o->o[1]->i == -2345);
	x(ffstr_eqz(&o->o[1]->k[0], "k1"));
	x(ffstr_eqz(&o->o[1]->k[1], "1"));
	x(ffstr_eqz(&o->o[1]->k[2], "k2"));
	x(ffstr_eqz(&o->o[1]->k[3], "2"));
	x(o->o[1]->objCloseOk == 1);

	x(o->objCloseOk == 1);

	return 0;
}

static size_t getFileContents(const ffsyschar *fn, char *buf, size_t n)
{
	fffd f;
	f = fffile_open(fn, FFO_OPEN | O_RDONLY);
	x(f != FF_BADFD);
	n = fffile_read(f, buf, n);
	x(n != 0 && n != (size_t)-1);
	return n;
}
