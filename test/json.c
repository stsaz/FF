/** Test JSON parser and deserialization with a scheme.
Copyright (c) 2013 Simon Zolin
*/

#include <FFOS/test.h>
#include <FFOS/file.h>
#include <FF/json.h>

#define x FFTEST_BOOL

/** Parse JSON from file.
The window is just 1 byte to make the parser work harder.
Use heap memory buffer to collect data for incomplete entities.
Print the formatted data into stdout. */
int test_json_dynamic()
{
	fffd f;
	char ch;
	ffparser json;
	int rc;
	size_t n;
	ffbool again = 0;
	ffstr3 buf = { 0 };

	f = fffile_open(TEXT("./test/test.json"), FFO_OPEN | O_RDONLY);
	if (!x(f != FF_BADFD))
		return 0;

	ffjson_parseinit(&json);

	for (;;) {
		if (!again) {
			n = fffile_read(f, &ch, 1);
			if (n == 0 || n == -1)
				break;
		}
		else {
			again = 0;
			n = 1;
		}

		rc = ffjson_parse(&json, &ch, &n);
		if (n == 0)
			again = 1;

		switch (rc) {
		case FFPARS_KEY:
			fffile_fmt(ffstdout, &buf, "%*c'%*s'  "
				, (size_t)4 * json.ctxs.len, (int)' '
				, (size_t)json.val.len, json.val.ptr);
			break;

		case FFPARS_VAL:
			fffile_fmt(ffstdout, &buf, "%*c(%s):  '%*s'\n"
				, ffarr_back(&json.ctxs) == FFPARS_TOBJ ? (size_t)0 : (size_t)4 * json.ctxs.len, (int)' '
				, ffjson_stype(json.type), (size_t)json.val.len, json.val.ptr);
			break;

		case FFPARS_OPEN:
			fffile_fmt(ffstdout, &buf, "\n%*c%c\n"
				, (size_t)4 * json.ctxs.len, (int)' '
				, json.type == FFJSON_TARR ? (int)'[' : (int)'{');
			break;

		case FFPARS_CLOSE:
			fffile_fmt(ffstdout, &buf, "%*c%c\n\n"
				, (size_t)4 * (json.ctxs.len - 1), (int)' '
				, json.type == FFJSON_TARR ? (int)']' : (int)'}');
			break;

		case FFPARS_MORE:
			x(0 == ffpars_savedata(&json));
			break;

		default:
			x(0);
			fffile_fmt(ffstdout, &buf, "\nerror: %d:%d  (%d) %s\n"
				, json.line, json.ch, rc, ffpars_errstr(rc));
			return 0;
		}
	}

	ffpars_free(&json);
	(void)fffile_close(f);
	return 0;
}

static int test_json_err()
{
	ffparser json;

	ffjson_parseinit(&json);
	x(FFPARS_EBADCHAR == ffjson_validate(&json, FFSTR("{,")));
	ffjson_parseinit(&json);
	x(FFPARS_EBADCHAR == ffjson_validate(&json, FFSTR("[123;")));
	ffjson_parseinit(&json);
	x(FFPARS_EBADCHAR == ffjson_validate(&json, FFSTR("\"val\n")));

	ffjson_parseinit(&json);
	x(FFPARS_EKVSEP == ffjson_validate(&json, FFSTR("{\"key\",")));

	ffjson_parseinit(&json);
	x(FFPARS_ENOVAL == ffjson_validate(&json, FFSTR("{\"key\":,")));

	ffjson_parseinit(&json);
	x(FFPARS_EBADVAL == ffjson_validate(&json, FFSTR("truE")));

	ffjson_parseinit(&json);
	x(FFPARS_EBIGVAL == ffjson_validate(&json, FFSTR("123456789123456789123456789123456789")));

	ffjson_parseinit(&json);
	x(FFPARS_EESC == ffjson_validate(&json, FFSTR("\"\\1\"")));

	ffjson_parseinit(&json);
	x(FFPARS_EBADBRACE == ffjson_validate(&json, FFSTR("[123}")));

	ffjson_parseinit(&json);
	x(FFPARS_EBADCMT == ffjson_validate(&json, FFSTR(" /z")));

	return 0;
}


typedef struct obj_s obj_s;

struct obj_s {
	ffstr s;
	int64 size;
	int64 i;
	int i4;
	byte b;
	byte en;
	obj_s *o[2];
	int iobj;
	ffstr ar[2];
	int iar;
	unsigned arrCloseOk : 1
		, objCloseOk : 1;
};

static int newObj(ffparser_schem *ps, void *obj, ffpars_ctx *ctx);
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

static int objClose(ffparser_schem *ps, void *obj)
{
	obj_s *o = obj;
	o->objCloseOk = 1;
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

static const char *const enumList[] = {
	"A", "B", "C", NULL, (void*)FFOFF(obj_s, en)
};

static const ffpars_arg obj_schem[] = {
	{ "str", FFPARS_TSTR | FFPARS_FCOPY | FFPARS_FNOTEMPTY, FFPARS_DSTOFF(obj_s, s) }
	, { "size", FFPARS_TSIZE | FFPARS_F64BIT, FFPARS_DSTOFF(obj_s, size) }
	, { "int", FFPARS_TINT | FFPARS_FNOTZERO | FFPARS_FREQUIRED
		| FFPARS_F64BIT | FFPARS_FSIGN, FFPARS_DSTOFF(obj_s, i) }
	, { "i4", FFPARS_TINT, FFPARS_DSTOFF(obj_s, i4) }
	, { "bool", FFPARS_TBOOL, FFPARS_DSTOFF(obj_s, b) }
	, { "obj", FFPARS_TOBJ | FFPARS_FNULL, FFPARS_DST(&newObj) }
	, { "arr", FFPARS_TARR, FFPARS_DST(&newArr) }
	, { "enum", FFPARS_TENUM, FFPARS_DST(enumList) }
	, { NULL, FFPARS_TCLOSE, FFPARS_DST(&objClose) }
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

#define SJSON "{\
\"str\":\"my string\"\
,\"size\":\"64k\"\
,\"int\":1000\
,\"i4\":9999\
,\"bool\":true\
,\"enum\":\"B\"\
,\"obj\":{\
	\"int\":1234\
	,\"obj\":null\
}\
,\"arr\":[\
	11,\
	\"22\",\
	{\"int\":-2345}\
]\
} "

/** Parse JSON with a predefined scheme. */
int test_json_schem()
{
	obj_s o;
	ffparser json;
	ffparser_schem ps;
	int rc;
	const char *js
		, *jsend;
	size_t len;
	const ffpars_ctx oinf = { &o, obj_schem, FFCNT(obj_schem) };
	const ffpars_arg top = { NULL, FFPARS_TOBJ | FFPARS_FPTR, FFPARS_DST(&oinf) };

	memset(&o, 0, sizeof(obj_s));
	ffjson_scheminit(&ps, &json, &top);

	js = SJSON;
	jsend = SJSON + FFSLEN(SJSON);
	while (js != jsend) {
		len = jsend - js;
		rc = ffjson_parse(ps.p, js, &len);
		rc = ffpars_schemrun(&ps, rc);
		if (!x(rc <= 0)) {
			printf("error (%d) %s\n", rc, ffpars_errstr(rc));
			break;
		}
		js += len;
	}

	x(ffstr_eqz(&o.s, "my string"));
	x(o.size == 64 * 1024);
	x(o.i == 1000);
	x(o.i4 == 9999);
	x(o.b == 1);
	x(o.en == enB);
	x(o.o[0]->i == 1234);
	x(o.o[0]->o[0] == (void*)-1);

	x(ffstr_eqz(&o.ar[0], "11"));
	x(ffstr_eqz(&o.ar[1], "22"));
	x(o.o[1]->i == -2345);
	x(o.arrCloseOk == 1);

	x(o.o[0]->objCloseOk == 1);
	x(o.o[1]->objCloseOk == 1);
	x(o.objCloseOk == 1);

	ffstr_free(&o.s);
	ffmem_free(o.o[0]);
	ffmem_free(o.o[1]);
	ffpars_free(&json);
	ffpars_schemfree(&ps);
	return 0;
}

int test_json()
{
	FFTEST_FUNC;

	test_json_dynamic();
	test_json_err();
	test_json_schem();
	return 0;
}
