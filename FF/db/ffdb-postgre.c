/**
Copyright 2018 Simon Zolin.
*/

#include <FF/db/postgre.h>
#include <FFOS/atomic.h>


static ffatomic _ffdb_postgre_stmt_cntr;

int ffdb_prepare(ffdb *db, ffdb_stmt *q, const char *sql)
{
	ffmem_tzero(q);

	int rc;

	size_t i = ffatom_incret(&_ffdb_postgre_stmt_cntr);
	int n = ffs_fromint(i, q->name, sizeof(q->name), 0);
	q->name[n] = '\0';

	q->stmt = PQprepare(db, q->name, sql, 0, NULL);
	if (q->stmt == NULL) {
		q->errmsg = PQerrorMessage(db);
		return FFDB_ERR;
	}

	rc = PQresultStatus(q->stmt);
	if (rc != FFDB_OK) {
		q->errmsg = PQresultErrorMessage(q->stmt);
		PQclear(q->stmt);
		q->stmt = NULL;
		return rc;
	}

	PGresult *res = PQdescribePrepared(db, q->name);
	q->nparams = PQnparams(res);
	q->ncols = PQnfields(res);
	PQclear(res);

	if (q->nparams != 0) {
		char *p = ffmem_alloc(q->nparams * (sizeof(void*) + sizeof(int) * 3 + sizeof(int64)));
		if (p == NULL) {
			PQclear(q->stmt);
			q->stmt = NULL;
			q->errmsg = "memory allocation";
			return FFDB_ERR;
		}

		q->data = (const char**)p;
		q->lens = (int*)(p + q->nparams * sizeof(void*));
		q->fmts = (int*)(p + q->nparams * (sizeof(void*) + sizeof(int)));
		q->data_int4 = (int*)(p + q->nparams * (sizeof(void*) + sizeof(int) * 2));
		q->data_int8 = (int64*)(p + q->nparams * (sizeof(void*) + sizeof(int) * 3));
	}

	q->db = db;
	return FFDB_OK;
}

void ffdb_fin(ffdb_stmt *q)
{
	ffdb_reset(q);
	if (q->data != NULL)
		ffmem_free((char*)q->data);
	if (q->stmt != NULL)
		PQclear(q->stmt);
}

int ffdb_next(ffdb_stmt *q)
{
	if (q->res == NULL) {
		PQsendQueryPrepared(q->db, q->name, q->nparams, q->data, q->lens, q->fmts
			, /*resultFormat=*/ 1 /*binary*/);
		PQsetSingleRowMode(q->db);
	} else {
		PQclear(q->res);
		q->res = NULL;
	}

	q->res = PQgetResult(q->db);
	if (q->res == NULL) {
		q->errmsg = PQerrorMessage(q->db);
		return FFDB_ERR;
	}

	int r = PQresultStatus(q->res);
	if (r == PGRES_TUPLES_OK)
		return FFDB_DONE;
	q->errmsg = PQresultErrorMessage(q->res);
	return r;
}

void ffdb_reset(ffdb_stmt *q)
{
	if (q->res != NULL) {
		PQclear(q->res);
		q->res = NULL;
	}

	for (;;) {
		q->res = PQgetResult(q->db);
		if (q->res == NULL)
			break;
		PQclear(q->res);
	}
}
