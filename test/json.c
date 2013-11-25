/** Test JSON parser and deserialization with a scheme.
Copyright (c) 2013 Simon Zolin
*/

#include <FFOS/test.h>
#include <FFOS/file.h>
#include <FF/json.h>
#include "schem.h"
#include "all.h"

#define x FFTEST_BOOL

/** Parse JSON from file.
The window is just 1 byte to make the parser work harder.
Use heap memory buffer to collect data for incomplete entities.
Print the formatted data into stdout. */
int test_json_parse(const ffsyschar *testJsonFile)
{
	fffd f;
	char ch;
	ffparser json;
	int rc;
	size_t n;
	ffbool again = 0;
	ffstr3 buf = { 0 };

	f = fffile_open(testJsonFile, FFO_OPEN | O_RDONLY);
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
int test_json_schem(const ffsyschar *testJsonFile)
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
	char buf[1024];
	size_t ibuf;

	memset(&o, 0, sizeof(obj_s));
	ffjson_scheminit(&ps, &json, &top);

	ibuf = getFileContents(testJsonFile, buf, sizeof(buf));

	js = buf;
	jsend = buf + ibuf;
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

	objChk(&o);
	x(o.o[0]->o[0] == (void*)-1);
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

	test_json_parse(TESTDIR TEXT("/test.json"));
	test_json_err();
	test_json_schem(TESTDIR TEXT("/schem.json"));

	return 0;
}
