# JSON

Include:

	#include <FF/data/json.h>


## JSON reader using scheme

The most robust and effective way to process JSON text is when a small chunk of data is read just once and immediately processed.  This way the parser can handle a huge amount of data very fast.  Depending on how the parser is used, it requires either a small fixed memory size or a memory size equal to the largest JSON string length.  Initially this parser was written for use in a server environment, so in theory it's possible to instantiate millions of `ffjson` objects to simultaneously process millions of different JSON files of any size.

`ffparser_schem` is a front-end for `ffjson` parser.  The method shown here uses a predefined scheme to automatically convert JSON elements to C structure.  A statically defined table must contain elements' names, types and target offset within a C structure.  After the whole input data is processed, there's a complete C structure object that contains the values from JSON.  The input JSON data is validated very strictly: the parser handles syntax errors, type checks, integer overflows.

Include:

	#include <FF/data/parse.h>

Suppose we need to parse data like this:

	{
		"strkey": string,
		"intkey": integer
	}

Declare C structure:

	struct foo {
		ffstr s;
		uint64 i;
	};

Define JSON -> C-struct scheme:

	static const ffpars_arg json_scheme[] = {
		{ "strkey",	FFPARS_TSTR | FFPARS_FCOPY, FFPARS_DSTOFF(struct foo, s) },
		{ "intkey",	FFPARS_TINT64, FFPARS_DSTOFF(struct foo, i) },
	};

	static int set_object_ctx(ffparser_schem *ps, void *obj, ffpars_ctx *ctx)
	{
		ffpars_setargs(ctx, obj, json_scheme, FFCNT(json_scheme));
		return 0;
	}
	static const ffpars_arg global_ctx = { NULL, FFPARS_TOBJ, FFPARS_DST(&set_object_ctx) };

Note that we need 2 contexts here because the first context is a global one - a JSON object enclosed in `{}` brackets.  The second context is the table of the object's elements.

Initialize:

	int r;
	struct foo obj = {};
	ffparser_schem ps;
	ffjson json;
	ffjson_scheminit2(&ps, &json, &global_ctx, &obj);

Set input data:

	ffstr data;
	ffstr_setz(&data, "{ \"strkey\":\"value\", \"intkey\":1234 }");

Process input data:

	while (data.len != 0) {

		r = ffjson_parsestr(&json, &data);
		r = ffjson_schemrun(&ps);

		if (r == FFPARS_MORE) {
			data = ...;
			continue;
		} else if (ffpars_iserr(r))
			return;
	}

Final validation check:

	r = ffjson_schemfin(&ps);
	if (ffpars_iserr(r))
		return;

Use the parsed data:

	// obj.s contains "value"
	// obj.i == 1234
	ffstr_free(&obj.s); // this is needed if FFPARS_FCOPY is used


## JSON writer

Create:

	ffjson_cook js;
	ffjson_cookinit(&js, NULL, 0);
	js.gflags = FFJSON_PRETTY;

Add elements one by one:

	ffjson_bufadd(&js, FFJSON_TOBJ, FFJSON_CTXOPEN);
	ffjson_bufadd(&js, FFJSON_FSTRZ, "key");
	ffjson_bufadd(&js, FFJSON_FINTVAL, 1234);
	ffjson_bufadd(&js, FFJSON_TOBJ, FFJSON_CTXCLOSE);

Add elements using scheme:

	static const int types[] = {
		FFJSON_TOBJ,
		FFJSON_FSTRZ,
		FFJSON_FINTVAL,
		FFJSON_TOBJ,
	};
	ffjson_bufaddv(&js, types
		, FFJSON_CTXOPEN
		, "key", 1234
		, FFJSON_CTXCLOSE
		);

Get JSON data:

	ffstr json_data;
	ffstr_set(&json_data, js.buf.ptr, js.buf.len);

	/*
	'json_data' now contains:

	{
		"key": 1234
	}
	*/

Destroy:

	ffjson_cookfinbuf(&js);
