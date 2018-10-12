/** Test PostgreSQL interface.
Copyright 2018 Simon Zolin.
*/

/* Prepare:
initdb g:\pgdata
postgres -D g:\pgdata
createdb mydb

Test with:
psql -d mydb
*/

#include <FF/array.h>
#include <FF/db/postgre.h>
#include <FFOS/test.h>

#include <test/all.h>

#define x FFTEST_BOOL

#define SQL_DROP  "DROP TABLE tbl"
#define SQL_CRE  "CREATE TABLE tbl (int INTEGER, str TEXT)"
#define SQL_DEL  "DELETE FROM tbl"
#define SQL_INS  "INSERT INTO tbl VALUES ($1, $2)"
#define SQL_SEL  "SELECT int, str FROM tbl WHERE int = $1"

int main()
{
	ffmem_init();

	fftestobj.flags |= FFTEST_STOPERR;

	ffdb *db = NULL;
	ffdb_stmt sel, ins;

	static const char *const names[] = {
		"host"
		//, "port"
		, "dbname"
		//, "user"
		//, "password"
		//, "connect_timeout"
		, NULL
	};
	static const char *const values[] = {
		"localhost"
		//, "5432"
		, "mydb"
		//, ""
		, NULL
	};
	x(FFDB_OPEN_OK == ffdb_open(&db, names, values));

	ffdb_exec(db, SQL_DROP);
	x(FFDB_OK == ffdb_exec(db, SQL_CRE));
	x(FFDB_OK == ffdb_exec(db, SQL_DEL));

	x(FFDB_OK == ffdb_prepare(db, &ins, SQL_INS));

	ffdb_reset(&ins);
	ffdb_setint(&ins, 0, 12);
	ffdb_setnull(&ins, 1);
	ffdb_settext(&ins, 1, "str-12", 6);
	x(FFDB_OK == ffdb_next(&ins));
	x(1 == ffdb_changes(&ins));

	ffdb_reset(&ins);
	ffdb_setint(&ins, 0, 123);
	ffdb_settext(&ins, 1, "str-123", 7);
	x(FFDB_OK == ffdb_next(&ins));

	x(FFDB_OK == ffdb_prepare(db, &sel, SQL_SEL));
	x(1 == ffdb_params(&sel));
	x(2 == ffdb_cols(&sel));

	// x(0 == ffdb_txn_begin(db));

	ffdb_reset(&sel);
	ffdb_setint(&sel, 0, 123);
	// ffdb_setint64(&sel, 0, 123);

	x(FFDB_ROW == ffdb_next(&sel));

	int i = ffdb_getint(&sel, 0);
	x(i == 123);
	// int64 i8 = ffdb_getint64(&sel, 0);
	// x(i8 == 123);
	x(!ffdb_getnull(&sel, 1));
	ffstr s = ffdb_getstr(&sel, 1);
	x(ffstr_eqz(&s, "str-123"));
	x(FFDB_DONE == ffdb_next(&sel));

	// x(0 == ffdb_txn_commit(db));

	ffdb_fin(&ins);
	ffdb_fin(&sel);
	x(FFDB_OK == ffdb_exec(db, SQL_DROP));
	ffdb_close(db);

	printf("%u tests were run, failed: %u.\n", fftestobj.nrun, fftestobj.nfail);
	return 0;
}
