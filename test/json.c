/** Test JSON parser and deserialization with a scheme.  JSON generator.
Copyright (c) 2013 Simon Zolin
*/

#include <FFOS/test.h>
#include <FFOS/file.h>
#include <FFOS/process.h>
#include <FF/data/json.h>
#include "schem.h"
#include "all.h"

#define x FFTEST_BOOL

/** Parse JSON from file.
The window is just 1 byte to make the parser work harder.
Use heap memory buffer to collect data for incomplete entities.
Print the formatted data into stdout. */
int test_json_parse(const char *testJsonFile)
{
	fffd f;
	char ch;
	ffparser json;
	int rc;
	size_t n;
	ffbool again = 0;
	ffstr3 buf = { 0 };
	ffjson_cook ck;
	int ckf = FFJSON_PRETTY4SPC;
	void *dst;

	f = fffile_open(testJsonFile, O_RDONLY);
	if (!x(f != FF_BADFD))
		return 0;

	ffjson_parseinit(&json);
	ffjson_cookinit(&ck, NULL, 0);

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
			ffjson_bufadd(&ck, FFJSON_TSTR | ckf, &json.val);
			break;

		case FFPARS_VAL:
			if (json.type == FFJSON_TINT)
				dst = &json.intval;
			else if (json.type == FFJSON_TBOOL)
				dst = json.intval != 0 ? (void*)1 : NULL;
			else
				dst = &json.val;
			ffjson_bufadd(&ck, json.type | ckf, dst);
			break;

		case FFPARS_OPEN:
			ffjson_bufadd(&ck, json.type | ckf, FFJSON_CTXOPEN);
			break;

		case FFPARS_CLOSE:
			ffjson_bufadd(&ck, json.type | ckf, FFJSON_CTXCLOSE);
			break;

		case FFPARS_MORE:
			break;

		default:
			x(0);
			fffile_fmt(ffstdout, &buf, "\nerror: %d:%d  (%d) %s\n"
				, json.line, json.ch, rc, ffpars_errstr(rc));
			return 0;
		}
	}

	ffarr out = {0};
	fffile_readall(&out, TESTDIR "/test-out.json", -1);
	x(ffstr_eq2(&ck.buf, &out));
	ffarr_free(&out);

	ffjson_cookfinbuf(&ck);
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
	x(FFPARS_EBADCHAR == ffjson_validate(&json, FFSTR("\"val\"1")));

	ffjson_parseinit(&json);
	x(FFPARS_EBADCHAR == ffjson_validate(&json, FFSTR("\"val\",")));
	ffjson_parseinit(&json);
	x(FFPARS_EBADCHAR == ffjson_validate(&json, FFSTR("\"val\"]")));
	ffjson_parseinit(&json);
	x(FFPARS_EBADCHAR == ffjson_validate(&json, FFSTR("123,")));
	ffjson_parseinit(&json);
	x(FFPARS_EBADCHAR == ffjson_validate(&json, FFSTR("123]")));
	ffjson_parseinit(&json);
	x(FFPARS_EBADCHAR == ffjson_validate(&json, FFSTR("[123],")));
	ffjson_parseinit(&json);
	x(FFPARS_EBADCHAR == ffjson_validate(&json, FFSTR("[123]]")));

	ffjson_parseinit(&json);
	x(FFPARS_EKVSEP == ffjson_validate(&json, FFSTR("{\"key\",")));

	ffjson_parseinit(&json);
	x(FFPARS_ENOVAL == ffjson_validate(&json, FFSTR("{\"key\":,")));

	ffjson_parseinit(&json);
	x(FFPARS_EBADVAL == ffjson_validate(&json, FFSTR("truE")));

	ffjson_parseinit(&json);
	x(FFPARS_VAL == ffjson_validate(&json, FFSTR("123456789123456789123456789123456789"))
		&& json.type == FFJSON_TNUM);

	ffjson_parseinit(&json);
	x(FFPARS_EESC == ffjson_validate(&json, FFSTR("\"\\1\"")));

	ffjson_parseinit(&json);
	x(FFPARS_EBADBRACE == ffjson_validate(&json, FFSTR("[123}")));

	ffjson_parseinit(&json);
	x(FFPARS_EBADCMT == ffjson_validate(&json, FFSTR(" /z")));

	return 0;
}

/** Parse JSON with a predefined scheme. */
int test_json_schem(const char *testJsonFile)
{
	obj_s o;
	ffparser json;
	ffparser_schem ps;
	int rc;
	const char *js
		, *jsend;
	size_t len;
	char buf[1024];
	size_t ibuf;

	FFTEST_FUNC;

	memset(&o, 0, sizeof(obj_s));
	ffjson_scheminit2(&ps, &json, &glob_ctx, &o);

	ibuf = _test_readfile(testJsonFile, buf, sizeof(buf));
	if (ibuf == (size_t)-1)
		return 1;

	js = buf;
	jsend = buf + ibuf;
	while (js != jsend) {
		len = jsend - js;
		rc = ffjson_parse(ps.p, js, &len);
		rc = ffjson_schemrun(&ps);
		if (!x(rc <= 0)) {
			printf("error (%d) %s\n", rc, ffpars_errstr(rc));
			break;
		}
		js += len;
	}

	x(0 == (rc = ffjson_schemfin(&ps)));

	objChk(&o);
	x(o.o[0]->o[0] == (void*)-1);
	x(o.arrCloseOk == 1);

	ffstr_free(&o.s);
	ffmem_free(o.o[0]);
	ffmem_free(o.o[1]);
	ffpars_free(&json);
	ffpars_schemfree(&ps);
	return 0;
}

/** Generate JSON file. */
int test_json_generat(const char *fn)
{
	char buf[16 * 1024];
	fffd f;
	int i;
	int jf = FFJSON_PRETTY /*| FFJSON_NOESC*/;
	ffjson_cook j;
	int r;
	int64 i64 = 123456;
	int e;

	enum {
		eStart
		, eKeyStr, eValStr
		, eKeyInt, eValInt
		, eKeyArr, eArrStart, eArrElStr, eArrElInt, eArrClose
		, eEnd
	};
	const void *srcs[] = {
		FFJSON_CTXOPEN
		, "str", "my string"
		, "int", &i64
		, "arr", FFJSON_CTXOPEN, "my string", &i64, FFJSON_CTXCLOSE
		, FFJSON_CTXCLOSE
	};
	static const int dstflags[] = {
		FFJSON_TOBJ
		, FFJSON_FKEYNAME, FFJSON_FKEYNAME
		, FFJSON_FKEYNAME, FFJSON_TINT
		, FFJSON_FKEYNAME, FFJSON_TARR, FFJSON_FKEYNAME, FFJSON_TINT, FFJSON_TARR
		, FFJSON_TOBJ
	};

	FFTEST_FUNC;

	ffjson_cookinit(&j, buf, sizeof(buf));

	f = fffile_open(fn, O_CREAT | O_TRUNC | O_WRONLY);
	if (!x(f != FF_BADFD))
		return 0;

	ffjson_add(&j, FFJSON_TARR | jf, FFJSON_CTXOPEN);

	for (i = 0; i < 10000; i++) {
		for (e = 0; e <= eEnd; e++) {
			int jflags = dstflags[e];
			const void *src = srcs[e];

			r = ffjson_add(&j, jflags | jf, src);
			if (r != FFJSON_OK) {
				x(r == FFJSON_BUFFULL);
				x(j.buf.len == fffile_write(f, j.buf.ptr, j.buf.len));
				j.buf.len = 0;
				r = ffjson_add(&j, jflags | jf, src);
			}
		}
	}

	ffjson_add(&j, FFJSON_TARR | jf, FFJSON_CTXCLOSE);

	x(j.buf.len == fffile_write(f, j.buf.ptr, j.buf.len));
	x(0 == fffile_close(f));
	x(0 == fffile_rm(fn));
	ffjson_cookfin(&j);
	return 0;
}

static const int meta[] = {
	FFJSON_TOBJ
	, FFJSON_FKEYNAME, FFJSON_FINTVAL
	, FFJSON_FKEYNAME, FFJSON_TSTR
	, FFJSON_TOBJ
};

static int test_json_cook()
{
	ffstr s;
	ffjson_cook js;
	char buf[1024];

	FFTEST_FUNC;

	ffstr_setcz(&s, "my string");
	ffjson_cookinit(&js, buf, FFCNT(buf));

	x(FFJSON_OK == ffjson_addv(&js, meta, FFCNT(meta)
		, FFJSON_CTXOPEN
		, "key1", (int64)123456789123456789ULL
		, "key2", &s
		, FFJSON_CTXCLOSE
		, NULL));

	x(js.buf.ptr == buf);
	x(ffstr_eqcz(&js.buf, "{\"key1\":123456789123456789,\"key2\":\"my string\"}"));
	ffjson_cookfin(&js);


	ffjson_cookinit(&js, NULL, 0);

	x(FFJSON_OK == ffjson_bufaddv(&js, meta, FFCNT(meta)
		, FFJSON_CTXOPEN
		, "key1", (int64)123456789123456789ULL
		, "key2", &s
		, FFJSON_CTXCLOSE
		, NULL));

	x(js.buf.cap == FFSLEN("{\"key1\":,\"key2\":\"my string\"}") + FFINT_MAXCHARS + 1);
	x(ffstr_eqcz(&js.buf, "{\"key1\":123456789123456789,\"key2\":\"my string\"}"));
	ffjson_cookfinbuf(&js);

	return 0;
}

int test_json()
{
	char buf[16];
	FFTEST_FUNC;

	x(7*2 + 1 == ffjson_escape(buf, sizeof(buf), FFSTR("\"\\\b\f\r\n\t/")));
	x(!memcmp(buf, FFSTR("\\\"\\\\\\b\\f\\r\\n\\t/")));

	test_json_parse(TESTDIR "/test.json");
	test_json_err();
	test_json_schem(TESTDIR "/schem.json");

	test_json_generat(TESTDIR "/gen.json");
	test_json_cook();
	return 0;
}
