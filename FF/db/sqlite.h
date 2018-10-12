/**
Copyright 2014 Simon Zolin.
*/

#pragma once

#include <FF/array.h>

#ifndef FF_HAVE_SQLITE
#include <sqlite/sqlite-ff.h>
#endif


#define FFDB_Q(n)  "?"

enum FFDB_E {
	FFDB_OK = SQLITE_OK,
	FFDB_OPEN_OK = SQLITE_OK,
	FFDB_ROW = SQLITE_ROW,
	FFDB_DONE = SQLITE_DONE,
};

typedef sqlite3 ffdb;
typedef sqlite3_stmt ffdb_stmt;

#define FFDB_OPEN_TEMP  ""
#define FFDB_OPEN_MEM  ":memory:"

/**
@fn: filename or FFDB_OPEN_*
@flags: SQLITE_OPEN_*
*/
#define ffdb_open(pdb, fn, flags)  sqlite3_open_v2(fn, pdb, flags, NULL)

static FFINL int ffdb_prepare(ffdb *db, ffdb_stmt **pstmt, const char *sql)
{
	return sqlite3_prepare_v2(db, sql, (int)ffsz_len(sql), pstmt, NULL);
}

#define ffdb_err(db)  sqlite3_errcode(db)
#define ffdb_errstr(db)  sqlite3_errmsg(db)
#define ffdb_close(db)  sqlite3_close(db)

/** Execute a simple query.
Return FFDB_OK on success. */
#define ffdb_exec(db, sql)  sqlite3_exec(db, sql, NULL, NULL, NULL)

/** Execute prepared statement.
Return FFDB_ROW, SQLITE_DONE or error. */
#define ffdb_next(stmt)  sqlite3_step(stmt)

/** Reset statement, but keep bindings. */
#define ffdb_reset(stmt)  sqlite3_reset(stmt)

/** Clear bindings on SQL statement. */
#define ffdb_clear_bindings(stmt)  sqlite3_clear_bindings(stmt)

static FFINL void ffdb_reset_clear(ffdb_stmt *stmt)
{
	ffdb_reset(stmt);
	ffdb_clear_bindings(stmt);
}

#define ffdb_changes(db)  sqlite3_changes(db)
#define ffdb_fin(stmt)  sqlite3_finalize(stmt)

#define ffdb_params(stmt)  sqlite3_bind_parameter_count(stmt)
#define ffdb_setint(stmt, col, val)  sqlite3_bind_int(stmt, (col) + 1, val)
#define ffdb_setint64(stmt, col, val)  sqlite3_bind_int64(stmt, (col) + 1, val)
#define ffdb_setnull(stmt, col)  sqlite3_bind_null(stmt, (col) + 1)

#define ffdb_settext(stmt, col, s, len) \
	sqlite3_bind_text(stmt, (col) + 1, s, (int)(len), SQLITE_STATIC)

#define ffdb_setblob(stmt, col, s, len) \
	sqlite3_bind_blob(stmt, (col) + 1, s, (int)(len), SQLITE_STATIC)


#define ffdb_cols(stmt)  sqlite3_column_count(stmt)
#define ffdb_colname(stmt, col)  sqlite3_column_name(stmt, col)
#define ffdb_coltype(stmt, col)  sqlite3_column_type(stmt, col)
#define ffdb_getint(stmt, col)  sqlite3_column_int(stmt, col)
#define ffdb_getint64(stmt, col)  sqlite3_column_int64(stmt, col)

static FFINL void ffdb_getstr(ffdb_stmt *stmt, int col, ffstr *s)
{
	s->ptr = (char*)sqlite3_column_text(stmt, col);
	s->len = sqlite3_column_bytes(stmt, col);
}

#define ffdb_txn_begin(db)  sqlite3_exec(db, "BEGIN", NULL, NULL, NULL)
#define ffdb_txn_commit(db)  sqlite3_exec(db, "COMMIT", NULL, NULL, NULL)
#define ffdb_txn_rollback(db)  sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL)
