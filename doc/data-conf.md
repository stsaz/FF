# Configuration reader

* Data format
* Reader using scheme
* Read and Copy

Include:

	#include <FF/data/conf.h>


## Data format:

	# one-line comment
	// one-line comment
	/*
	multi-line comment
	*/

	# A key or value MAY be enclosed in quotes
	#  but MUST be enclosed in quotes if it contains a non-name characters (name regexp: "a-zA-Z_0-9")
	#  or is empty ("")
	# A key may have multiple values divided by whitespace.
	# Whitespace around a key or value is trimmed,
	#  but whitespace within quotes is preserved.
	key value_1 "value-2"

	# Contexts can be nested if enclosed in {}
	# '{' MUST be on the same line
	# '}' MUST be on a new line
	key {
		key "value"
	}

	key "value" {
		key "value"
	}

A configuration reader may also support accessing nested objects via dot ('.'):

	# Set value for a 2nd-level object
	key1.key2 "value"


## Reader using scheme

See section "JSON reader using scheme" in data-json.md for details on how to set up the scheme.

Initialize:

	int r;
	ffparser_schem ps;
	ffconf conf;
	if (0 != (r = ffconf_scheminit(&ps, &conf, &ctx)))
		goto fail;

Set input data:

	ffstr data = ...;

Read all available data:

	while (data.len != 0) {

		r = ffconf_parsestr(&conf, &data);
		r = ffconf_schemrun(&ps);

		if (r == FFPARS_MORE) {
			data = ...;
			continue;
		} else if (ffpars_iserr(r))
			goto fail;
	}

Final validation check:

	r = ffconf_schemfin(&ps);
	if (ffpars_iserr(r))
		goto fail;

Print error message, if necessary:

	fail:
	if (r != 0)
		printf("%s\n", ffpars_errstr(r));

Close the reader:

	ffconf_parseclose(&conf);
	ffpars_schemfree(&ps);


## Read and Copy

	...
	context { // trigger context-copy
		// below is the data to be copied into another buffer
		key value
		...
		// end of data to copy
	} // return to normal processing
	...

It's sometimes useful to store data within the context in memory for later processing.  After the context to copy data for is found, the reader code stops to use `ffparser_schem` object and starts to fill `ffconf_ctxcopy` object with data.  It does so until `ffconf_ctxcopy` interface signals that the context is exitted.

Initialize:

	int r, copy = 0;
	ffparser_schem ps;
	ffconf conf;
	ffpars_ctx ctx;
	ffconf_ctxcopy ctxcopy = {};
	ffpars_setargs(...);
	if (0 != (r = ffconf_scheminit(&ps, &conf, &ctx)))
		goto fail;

Set input data:

	ffstr data = ...;

Read data:

	while (data.len != 0) {
		r = ffconf_parsestr(&conf, &data);
		if (ffpars_iserr(r))
			goto fail;

If we're copying data, do it:

		if (copy) {
			r = ffconf_ctx_copy(&ctxcopy, &conf);
			if (r < 0) {
				r = FFPARS_EINTL;
				goto fail;
			} else if (r > 0) {
				ffstr data = ffconf_ctxcopy_acquire(&ctxcopy);
				ffconf_ctxcopy_destroy(&ctxcopy);

				// use data

				copy = 0;
			}
			continue;
		}

Normal processing - use scheme reader:

		r = ffconf_schemrun(&ps);
		if (ffpars_iserr(r))
			goto fail;

Check if we have triggerred context-copy, and initialize copy-object if so:

		if (...) {
			ffconf_ctxcopy_init(&ctxcopy, &ps);
			copy = 1;
		}
	}

Final validation check:

	r = ffconf_schemfin(&ps);
	if (ffpars_iserr(r))
		goto fail;

Print error message, if necessary:

	fail:
	if (r != 0)
		printf("%s\n", ffpars_errstr(r));

Close the reader:

	ffconf_ctxcopy_destroy(&ctxcopy);
	ffconf_parseclose(&conf);
	ffpars_schemfree(&ps);
