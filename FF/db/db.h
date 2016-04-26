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

typedef struct ffdb_stmtinfo {
	char *sql;
	byte input_n
		, output_n;
	byte types[]; //enum FFDB_T
	//input_types[]
	//output_types[]
} ffdb_stmtinfo;

#define ffdb_si_input(si)  ((si)->types)
#define ffdb_si_output(si)  ((si)->types + (si)->input_n)

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

static FFINL int ffdb_input2(ffdb_stmt *stmt, const ffdb_stmtinfo *si, ...)
{
	int r;
	va_list va;
	va_start(va, si);
	r = ffdb_inputv(stmt, ffdb_si_input(si), si->input_n, va);
	va_end(va);
	return r;
}

static FFINL int ffdb_output2(ffdb_stmt *stmt, const ffdb_stmtinfo *si, ...)
{
	int r;
	va_list va;
	va_start(va, si);
	r = ffdb_outputv(stmt, ffdb_si_output(si), si->output_n, va);
	va_end(va);
	return r;
}
