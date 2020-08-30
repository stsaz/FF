/** Test SQLite interface.
Copyright 2018 Simon Zolin.
*/

#include <FF/string.h>
#include <FF/db/sqlite.h>
#include <FFOS/test.h>

#include <test/all.h>

#define SQL_CRE  "CREATE TABLE tbl (int INTEGER, str TEXT)"
#define SQL_INS  "INSERT INTO tbl VALUES (?, ?)"
#define SQL_SEL  "SELECT int, str FROM tbl WHERE int = ?"

int main()
{
	ffmem_init();

	fftestobj.flags |= FFTEST_STOPERR;

	const char *fn = "fftest.db";
	ffdb *db = NULL;
	ffdb_stmt *sel, *ins;
	fffile_rm(fn);
	x(FFDB_OPEN_OK == ffdb_open(&db, fn, SQLITE_OPEN_CREATE | SQLITE_OPEN_READWRITE));

	x(0 == ffdb_exec(db, SQL_CRE));

	x(0 == ffdb_prepare(db, &ins, SQL_INS));

	ffdb_reset_clear(ins);
	ffdb_setint(ins, 0, 12);
	ffdb_setnull(ins, 1);
	ffdb_settext(ins, 1, "str-12", 6);
	x(FFDB_DONE == ffdb_next(ins));
	x(1 == ffdb_changes(db));

	ffdb_reset_clear(ins);
	ffdb_setint(ins, 0, 123);
	ffdb_setblob(ins, 1, "str-123", 7);
	x(FFDB_DONE == ffdb_next(ins));

	x(0 == ffdb_prepare(db, &sel, SQL_SEL));
	x(1 == ffdb_params(sel));
	x(2 == ffdb_cols(sel));

	x(0 == ffdb_txn_begin(db));

	ffdb_reset_clear(sel);
	ffdb_setint(sel, 0, 123);
	ffdb_setint64(sel, 0, 123);

	x(FFDB_ROW == ffdb_next(sel));
	int i = ffdb_getint(sel, 0);
	x(i == 123);
	int64 i8 = ffdb_getint64(sel, 0);
	x(i8 == 123);
	ffstr s;
	ffdb_getstr(sel, 1, &s);
	x(ffstr_eqz(&s, "str-123"));
	x(FFDB_DONE == ffdb_next(sel));

	x(0 == ffdb_txn_commit(db));

	ffdb_fin(ins);
	ffdb_fin(sel);
	ffdb_close(db);
	fffile_rm(fn);

	printf("%u tests were run, failed: %u.\n", fftestobj.nrun, fftestobj.nfail);
	return 0;
}
