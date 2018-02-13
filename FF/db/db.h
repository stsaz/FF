/**
Copyright 2014 Simon Zolin.
*/


enum FFDB_T {
	FFDB_TBAD,

	FFDB_TINT,
	FFDB_TINT64,
	FFDB_TSTR,
	FFDB_TNULL,
};

FF_EXTN int ffdb_inputv(ffdb_stmt *stmt, const byte *types, size_t ntypes, va_list va);
FF_EXTN int ffdb_outputv(ffdb_stmt *stmt, const byte *types, size_t ntypes, va_list va);

static FFINL int ffdb_input(ffdb_stmt *stmt, const byte *types, size_t ntypes, ...)
{
	int r;
	va_list va;
	va_start(va, ntypes);
	r = ffdb_inputv(stmt, types, ntypes, va);
	va_end(va);
	return r;
}

static FFINL int ffdb_output(ffdb_stmt *stmt, const byte *types, size_t ntypes, ...)
{
	int r;
	va_list va;
	va_start(va, ntypes);
	r = ffdb_outputv(stmt, types, ntypes, va);
	va_end(va);
	return r;
}


/** Set input/output parameters using %-formatted string.
@fmt: e.g.: "%d%S%S"

    Input  Output
%d  int    int*
%D  int64  int64*
%S  ffstr* ffstr*
*/
FF_EXTN int ffdb_inputf(ffdb_stmt *s, const char *fmt, ...);
FF_EXTN int ffdb_outputf(ffdb_stmt *s, const char *fmt, ...);
