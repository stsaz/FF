/**
Copyright 2014 Simon Zolin.
*/

#include <FF/db/sqlite.h>
#include <FF/db/db.h>


int ffdb_inputv(ffdb_stmt *stmt, const byte *types, size_t ntypes, va_list va)
{
	int r = FFDB_OK;
	uint col;
	union {
		int i4;
		int64 i8;
		ffstr *s;
	} un;

	for (col = 0;  col != ntypes;  col++) {

		switch (types[col]) {

		case FFDB_TINT:
			un.i4 = va_arg(va, int);
			r = ffdb_setint(stmt, col, un.i4);
			break;

		case FFDB_TINT64:
			un.i8 = va_arg(va, int64);
			r = ffdb_setint64(stmt, col, un.i8);
			break;

		case FFDB_TSTR:
			un.s = va_arg(va, ffstr*);
			r = ffdb_settext(stmt, col, un.s->ptr, un.s->len);
			break;

		case FFDB_TNULL:
			va_arg(va, void*);
			r = ffdb_setnull(stmt, col);
			break;

		default:
			return -1;
		}

		if (r != FFDB_OK)
			break;
	}

	return r;
}

int ffdb_outputv(ffdb_stmt *stmt, const byte *types, size_t ntypes, va_list va)
{
	uint col;
	union {
		int *i4;
		int64 *i8;
		ffstr *s;
	} un;

	for (col = 0;  col != ntypes;  col++) {

		switch (types[col]) {

		case FFDB_TINT:
			un.i4 = va_arg(va, int*);
			*un.i4 = ffdb_getint(stmt, col);
			break;

		case FFDB_TINT64:
			un.i8 = va_arg(va, int64*);
			*un.i8 = ffdb_getint64(stmt, col);
			break;

		case FFDB_TSTR:
			un.s = va_arg(va, ffstr*);
			ffdb_getstr(stmt, col, un.s);
			break;

		default:
			return -1;
		}
	}

	return 0;
}
