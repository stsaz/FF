/** XML.
Copyright (c) 2014 Simon Zolin
*/

/*
<TAG [ATTR="VAL"]...>TEXT</TAG>
<TAG [ATTR="VAL"].../>

Attribute value must be enclosed in single quotes or double quotes.
*/

#pragma once

#include <FF/data/parse.h>


/** Escape special XML characters: <>&"
Return the number of bytes written. */
FF_EXTN size_t ffxml_escape(char *dst, size_t cap, const char *s, size_t len);


enum FFXML_O {
	/* Enforce strict rules.
	If not set:
	. attribute values can be not enclosed in quotes: ATTR=VAL
	*/
	FFXML_OSTRICT = 1,
};

typedef struct ffxml {
	uint st;
	uint options;
	ffarr buf;

	uint type; // enum FFXML_T
	uint line; // absolute line number
	uint line_byte; // byte number on the line
	ffstr val;
} ffxml;

FF_EXTN void ffxml_close(ffxml *x);

enum FFXML_T {
	FFXML_TEXT,
	FFXML_TAG_OPEN, // "<TAG..."
	FFXML_TAG, // "<TAG>..."
	FFXML_TAG_CLOSE, // "<TAG.../>..."
	FFXML_TAG_CLOSE_NAME, // "</TAG>..."
	FFXML_TAG_ATTR, // "<TAG...ATTR=..."
	FFXML_TAG_ATTR_VAL, // "<TAG...ATTR="VAL"..."
};

/** Parse XML.
Return enum FFPARS_E, 'len' is the number of processed bytes. */
int ffxml_parse(ffxml *x, const char *data, size_t *len);

static inline int ffxml_parsestr(ffxml *x, ffstr *data)
{
	size_t n = data->len;
	int r = ffxml_parse(x, data->ptr, &n);
	ffstr_shift(data, n);
	return r;
}
