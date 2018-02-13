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

int ffdb_inputf(ffdb_stmt *s, const char *fmt, ...)
{
	int rc = -1, r;
	uint col = 0;
	va_list va;
	va_start(va, fmt);

	for (;  *fmt != '\0';  fmt++) {
		if (*fmt != '%')
			goto done;
		fmt++;
		switch (*fmt) {
		case 'd': {
			int src = va_arg(va, int);
			r = ffdb_setint(s, col, src);
			break;
		}
		case 'D': {
			int64 src = va_arg(va, int64);
			r = ffdb_setint64(s, col, src);
			break;
		}
		case 'S': {
			ffstr *src = va_arg(va, ffstr*);
			r = ffdb_settext(s, col, src->ptr, src->len);
			break;
		}
		case '\0':
			goto done;
		}

		if (r != FFDB_OK)
			goto done;
		col++;
	}

	rc = 0;

done:
	va_end(va);
	return rc;
}

int ffdb_outputf(ffdb_stmt *s, const char *fmt, ...)
{
	int rc = -1;
	uint col = 0;
	va_list va;
	va_start(va, fmt);

	for (;  *fmt != '\0';  fmt++) {
		if (*fmt != '%')
			goto done;
		fmt++;
		switch (*fmt) {
		case 'd': {
			int *dst = va_arg(va, int*);
			*dst = ffdb_getint(s, col);
			break;
		}
		case 'D': {
			int64 *dst = va_arg(va, int64*);
			*dst = ffdb_getint64(s, col);
			break;
		}
		case 'S': {
			ffstr *dst = va_arg(va, ffstr*);
			ffdb_getstr(s, col, dst);
			break;
		}
		case '\0':
			goto done;
		}

		col++;
	}

	rc = 0;

done:
	va_end(va);
	return rc;
}
