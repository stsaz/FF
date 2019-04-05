/** DNS client.
Copyright 2019 Simon Zolin
*/

#include <FF/net/dns-client.h>
#include <FF/net/dns.h>
#include <FF/net/proto.h>
#include <FF/net/url.h>
#include <FF/crc.h>
#include <FF/time.h>
#include <FFOS/random.h>


typedef struct dns_quser {
	ffdnscl_onresolve ondone;
	void *udata;
} dns_quser;

typedef struct dns_query {
	ffdnsclient *r;
	ffrbt_node rbtnod;
	fftmrq_entry tmr;
	uint tries_left;
	ffstr name; //hostname to be resolved

	ffdnscl_res *res[2];
	uint ttl[2];
	int status; //aggregated status of both IPv4/6 queries
	fftime firstsend;

	struct { FFARR(dns_quser) } users; //requestors waiting for this query
	ushort txid4;
	ushort txid6;
	unsigned need4 :1
		, need6 :1;
	byte nres; //number of elements in res[2]
	ushort ques_len4;
	ushort ques_len6;
	char question[0];
} dns_query;

typedef struct dns_a {
	ffaddrinfo ainfo;
	struct sockaddr_in addr;
} dns_a;

typedef struct dns_a6 {
	ffaddrinfo ainfo;
	struct sockaddr_in6 addr;
} dns_a6;

struct ffdnscl_res {
	void *cached_id;
	uint usage; //reference count.  0 if stored in cache.

	uint naddrs;
	union {
		dns_a addrs[0];
		dns_a6 addrs6[0];
	};
};


#define syserrlog_x(r, ...) \
	(r)->log(FFDNSCL_LOG_ERR | FFDNSCL_LOG_SYS, NULL, __VA_ARGS__)
#define errlog_x(r, ...) \
	(r)->log(FFDNSCL_LOG_ERR, NULL, __VA_ARGS__)

#define syserrlog_srv(serv, ...) \
	(serv)->r->log(FFDNSCL_LOG_ERR | FFDNSCL_LOG_SYS, &(serv)->saddr, __VA_ARGS__)
#define errlog_srv(serv, ...) \
	(serv)->r->log(FFDNSCL_LOG_ERR, &(serv)->saddr, __VA_ARGS__)
#define dbglog_srv(serv, lev, ...) \
do { \
	if ((serv)->r->debug_log) \
		(serv)->r->log(FFDNSCL_LOG_DBG, &(serv)->saddr, __VA_ARGS__); \
} while (0)

#define errlog_q(q, ...)  (q)->r->log(FFDNSCL_LOG_ERR, &(q)->name, __VA_ARGS__)
#define warnlog_q(q, ...)  (q)->r->log(FFDNSCL_LOG_WARN, &(q)->name, __VA_ARGS__)
#define log_checkdbglevel(q, lev)  (q->r->debug_log)
#define dbglog_q(q, lev, ...) \
do { \
	if (log_checkdbglevel(q, lev)) \
		(q)->r->log(FFDNSCL_LOG_DBG, &(q)->name, __VA_ARGS__); \
} while (0)


// SERVER
static int serv_init(ffdnscl_serv *serv);
static ffdnscl_serv * serv_next(ffdnsclient *r);
static void serv_fin(ffdnscl_serv *serv);

// QUERY
#define query_sib(pnod)  FF_GETPTR(dns_query, rbtnod, pnod)
static int query_addusr(dns_query *q, ffdnscl_onresolve ondone, void *udata);
static int query_rmuser(ffdnsclient *r, const ffstr *host, ffdnscl_onresolve ondone, void *udata);
static size_t query_prep(ffdnsclient *r, char *buf, size_t cap, uint txid, const ffstr *nm, int type);
static void query_send(dns_query *q, int resend);
static int query_send1(dns_query *q, ffdnscl_serv *serv, int resend);
static void query_onexpire(void *param);
static void query_fin(dns_query *q, int status);
static void query_free(void *param);

// ANSWER
static void ans_read(void *udata);
static void ans_proc(ffdnscl_serv *serv, const ffstr *resp);
static dns_query * ans_find_query(ffdnscl_serv *serv, ffdns_hdr_host *h, const ffstr *resp);
static uint ans_nrecs(dns_query *q, ffdns_hdr_host *h, const ffstr *resp, const char *pbuf, int is4);
static ffdnscl_res* ans_proc_resp(dns_query *q, ffdns_hdr_host *h, const ffstr *resp, int is4);

// DUMMY CALLBACKS
static int oncomplete_dummy(ffdnsclient *r, ffdnscl_res *res, const ffstr *name, uint refcount, uint ttl)
{
	return 0;
}
static void log_dummy(uint level, const ffstr *trxn, const char *fmt, ...)
{}
static fftime time_dummy(void)
{
	fftime t = {};
	return t;
}

ffdnsclient* ffdnscl_new(ffdnscl_conf *conf)
{
	ffdnsclient *r = ffmem_new(ffdnsclient);
	if (r == NULL)
		return NULL;
	ffmemcpy(r, conf, sizeof(*conf));

	if (r->oncomplete == NULL)
		r->oncomplete = &oncomplete_dummy;

	if (r->log == NULL)
		r->log = &log_dummy;

	if (r->time == NULL)
		r->time = &time_dummy;

	fflist_init(&r->servs);
	r->curserv = NULL;
	ffrbt_init(&r->queries);
	return r;
}

int ffdnscl_resolve(ffdnsclient *r, const char *name, size_t namelen, ffdnscl_onresolve ondone, void *udata, uint flags)
{
	uint namecrc;
	char buf4[FFDNS_MAXMSG], buf6[FFDNS_MAXMSG];
	size_t ibuf4, ibuf6 = 0;
	ffrbt_node *found_query, *parent;
	dns_query *q = NULL;
	ffstr host;
	ushort txid4, txid6 = 0;

	ffstr_set(&host, name, namelen);

	if (flags & FFDNSCL_CANCEL)
		return query_rmuser(r, &host, ondone, udata);

	namecrc = ffcrc32_iget(name, namelen);

	// determine whether the needed query is already pending and if so, attach to it
	found_query = ffrbt_find(&r->queries, namecrc, &parent);
	if (found_query != NULL) {
		q = query_sib(found_query);

		if (!ffstr_eq2(&q->name, &host)) {
			errlog_x(r, "%S: CRC collision with %S", &host, &q->name);
			goto fail;
		}

		dbglog_q(q, LOG_DBGFLOW, "query hit");
		if (0 != query_addusr(q, ondone, udata))
			goto nomem;

		return 0;
	}

	// prepare DNS queries: A and AAAA
	txid4 = ffrnd_get() & 0xffff;
	ibuf4 = query_prep(r, buf4, FFCNT(buf4), txid4, &host, FFDNS_A);
	if (ibuf4 == 0) {
		errlog_x(r, "invalid hostname: %S", &host);
		goto fail;
	}

	if (r->enable_ipv6) {
		txid6 = ffrnd_get() & 0xffff;
		ibuf6 = query_prep(r, buf6, FFCNT(buf6), txid6, &host, FFDNS_AAAA);
	}

	// initialize DNS query object
	q = ffmem_alloc(sizeof(dns_query) + ibuf4 + ibuf6);
	if (q == NULL)
		goto nomem;
	ffmem_zero(q, sizeof(dns_query));
	q->r = r;

	if (0 != query_addusr(q, ondone, udata))
		goto nomem;

	if (NULL == ffstr_copy(&q->name, name, namelen))
		goto nomem;

	q->need4 = 1;
	ffmemcpy(q->question, buf4, ibuf4);
	q->ques_len4 = (ushort)ibuf4;
	q->txid4 = txid4;

	if (r->enable_ipv6) {
		q->need6 = 1;
		ffmemcpy(q->question + ibuf4, buf6, ibuf6);
		q->ques_len6 = (ushort)ibuf6;
		q->txid6 = txid6;
	}

	q->rbtnod.key = namecrc;
	ffrbt_insert(&r->queries, &q->rbtnod, parent);
	q->tries_left = r->max_tries;
	q->firstsend = r->time();

	query_send(q, 0);
	return 0;

nomem:
	syserrlog_x(r, "%e", FFERR_BUFALOC);

fail:
	if (q != NULL)
		query_free(q);

	ondone(udata, -1, NULL);
	return 0;
}

void ffdnscl_unref(ffdnsclient *r, const ffaddrinfo *ai)
{
	dns_a *paddrs = FF_GETPTR(dns_a, ainfo, ai);
	ffdnscl_res *res = FF_GETPTR(ffdnscl_res, addrs, paddrs);

	FF_ASSERT(res->usage != 0);
	if (--res->usage == 0)
		ffdnscl_res_free(res);
}

void ffdnscl_free(ffdnsclient *r)
{
	if (r == NULL)
		return;

	ffrbt_freeall(&r->queries, &query_free, FFOFF(dns_query, rbtnod));
	FFLIST_ENUMSAFE(&r->servs, serv_fin, ffdnscl_serv, sib);
	ffmem_free(r);
}

static void query_free(void *param)
{
	dns_query *q = param;
	q->r->timer(&q->tmr, 0);
	ffstr_free(&q->name);
	ffarr_free(&q->users);
	ffmem_free(q);
}

/** Timer expired. */
static void query_onexpire(void *param)
{
	dns_query *q = param;

	if (q->tries_left == 0) {
		errlog_q(q, "reached max_tries limit", 0);
		query_fin(q, -1);
		return;
	}

	query_send(q, 1);
}

/** One more user wants to send the same query. */
static int query_addusr(dns_query *q, ffdnscl_onresolve ondone, void *udata)
{
	dns_quser *quser;

	if (NULL == ffarr_grow(&q->users, 1, FFARR_GROWQUARTER))
		return 1;
	quser = ffarr_push(&q->users, dns_quser);
	quser->ondone = ondone;
	quser->udata = udata;
	return 0;
}

/** User doesn't want to wait for this query anymore. */
static int query_rmuser(ffdnsclient *r, const ffstr *host, ffdnscl_onresolve ondone, void *udata)
{
	ffrbt_node *found;
	dns_query *q;
	dns_quser *quser;
	uint namecrc;

	namecrc = ffcrc32_iget(host->ptr, host->len);
	found = ffrbt_find(&r->queries, namecrc, NULL);
	if (found == NULL) {
		errlog_x(r, "cancel: no query for %S", host);
		return 1;
	}

	q = query_sib(found);

	if (!ffstr_eq2(&q->name, host)) {
		errlog_x(r, "%S: CRC collision with %S", host, &q->name);
		return 1;
	}

	FFARR_WALK(&q->users, quser) {

		if (udata == quser->udata && ondone == quser->ondone) {
			ffarr_rmswap(&q->users, quser);
			dbglog_q(q, LOG_DBGFLOW, "cancel: unref query");
			return 0;
		}
	}

	errlog_q(q, "cancel: no matching reference for the query", 0);
	return 1;
}

static void query_send(dns_query *q, int resend)
{
	ffdnscl_serv *serv;

	for (;;) {

		if (q->tries_left == 0) {
			errlog_q(q, "reached max_tries limit", 0);
			query_fin(q, -1);
			return;
		}

		q->tries_left--;
		serv = serv_next(q->r);
		if (0 == query_send1(q, serv, resend))
			return;
	}
}

/** Send query to server. */
static int query_send1(dns_query *q, ffdnscl_serv *serv, int resend)
{
	ssize_t rc;
	int er;
	ffdnsclient *r = q->r;

	if (!serv->connected) {
		if (0 != serv_init(serv))
			return 1;

		if (0 != ffskt_connect(serv->sk, &serv->addr.a, serv->addr.len)) {
			er = FFERR_SKTCONN;
			goto fail;
		}
		serv->connected = 1;
		ans_read(serv);
	}

	if (q->need6) {
		rc = ffskt_send(serv->sk, q->question + q->ques_len4, q->ques_len6, 0);
		if (rc != q->ques_len6) {
			er = FFERR_WRITE;
			goto fail;
		}

		serv->nqueries++;

		dbglog_q(q, LOG_DBGNET, "%ssent %s query #%u (%u).  [%L]"
			, (resend ? "re" : ""), "AAAA", (int)q->txid6, serv->nqueries, (size_t)r->queries.len);
	}

	if (q->need4) {
		rc = ffskt_send(serv->sk, q->question, q->ques_len4, 0);
		if (rc != q->ques_len4) {
			er = FFERR_WRITE;
			goto fail;
		}

		serv->nqueries++;

		dbglog_q(q, LOG_DBGNET, "%ssent %s query #%u (%u).  [%L]"
			, (resend ? "re" : ""), "A", (int)q->txid4, serv->nqueries, (size_t)r->queries.len);
	}

	q->tmr.handler = &query_onexpire;
	q->tmr.param = q;
	r->timer(&q->tmr, r->retry_timeout);
	return 0;

fail:
	syserrlog_srv(serv, "%e", er);
	if (er == FFERR_WRITE) {
		ffskt_close(serv->sk);
		serv->sk = FF_BADSKT;
		ffaio_fin(&serv->aiotask);
		serv->connected = 0;
	}
	return 1;
}

/** Prepare DNS query. */
static size_t query_prep(ffdnsclient *r, char *buf, size_t cap, uint txid, const ffstr *host, int type)
{
	char *pbuf;
	uint n;
	ffdns_hdr *h = (ffdns_hdr*)buf;

	ffdns_initquery(h, txid, 1);
	pbuf = buf + sizeof(ffdns_hdr);

	n = ffdns_addquery(pbuf, (buf + cap) - pbuf, host->ptr, host->len, type);
	if (n == 0)
		return 0;
	pbuf += n;

	if (r->edns) {
		h->arcount[1] = 1;
		*pbuf++ = '\x00';
		ffdns_optinit((ffdns_opt*)pbuf, r->buf_size);
		pbuf += sizeof(ffdns_opt);
	}

	return pbuf - buf;
}


/** Receive data from DNS server. */
static void ans_read(void *udata)
{
	ffdnscl_serv *serv = udata;
	ssize_t r;
	ffstr resp;

	for (;;) {
		r = ffaio_recv(&serv->aiotask, &ans_read, serv->ansbuf, serv->r->buf_size);
		if (r == FFAIO_ASYNC)
			return;
		else if (r == FFAIO_ERROR) {
			syserrlog_srv(serv, "%e", FFERR_READ);
			return;
		}

		dbglog_srv(serv, LOG_DBGNET, "received response (%L bytes)", r);

		ffstr_set(&resp, serv->ansbuf, r);
		ans_proc(serv, &resp);
	}
}

/** Process response and notify users waiting for it. */
static void ans_proc(ffdnscl_serv *serv, const ffstr *resp)
{
	ffdns_hdr_host h;
	dns_query *q;
	int is4, i;
	ffdnsclient *r = serv->r;

	q = ans_find_query(serv, &h, resp);
	if (q == NULL)
		return;

	if (q->need4 && h.id == q->txid4) {
		q->need4 = 0;
		is4 = 1;

	} else if (q->need6 && h.id == q->txid6) {
		q->need6 = 0;
		is4 = 0;

	} else {
		errlog_q(q, "request/response IDs don't match.  Response ID: #%u", h.id);
		return;
	}

	if (h.rcode != FFDNS_NOERROR) {
		errlog_q(q, "#%u: DNS response: (%u) %s"
			, h.id, h.rcode, ffdns_errstr(h.rcode));
		if (q->nres == 0)
			q->status = h.rcode; //set error only from the first response

	} else if (NULL != ans_proc_resp(q, &h, resp, is4))
		q->status = FFDNS_NOERROR;
	else if (q->nres == 0)
		q->status = -1;

	if (log_checkdbglevel(q, LOG_DBGNET)) {
		fftime t = r->time();
		fftime_diff(&q->firstsend, &t);
		dbglog_q(q, LOG_DBGNET, "resolved IPv%u in %u.%03us"
			, (is4) ? 4 : 6, (int)fftime_sec(&t), (int)fftime_msec(&t));
	}

	if (q->need4 || q->need6)
		return; //waiting for the second response

	q->r->timer(&q->tmr, 0);

	for (i = 0;  i < q->nres;  i++) {
		ffdnscl_res *res = q->res[i];
		res->usage = (uint)q->users.len;
	}

	uint ttl = (uint)ffmin(q->ttl[0], q->ttl[q->nres - 1]);
	for (uint i = 0;  i < q->nres;  i++) {
		q->r->oncomplete(q->r, q->res[i], &q->name, q->users.len, ttl);
	}

	query_fin(q, q->status);
}

/** Find query by a response from DNS server. */
static dns_query * ans_find_query(ffdnscl_serv *serv, ffdns_hdr_host *h, const ffstr *resp)
{
	dns_query *q;
	const char *end = resp->ptr + resp->len;
	const char *pbuf;
	ffdns_hdr *hdr;
	char qname[FFDNS_MAXNAME];
	const char *errmsg = NULL;
	uint namecrc;
	ffstr name;
	ffrbt_node *found_query;
	uint resp_id = 0;

	if (resp->len < sizeof(ffdns_hdr)) {
		errmsg = "too small response";
		goto fail;
	}

	hdr = (ffdns_hdr*)resp->ptr;
	ffdns_hdrtohost(h, hdr);
	if (hdr->qr != 1) {
		errmsg = "received invalid response";
		goto fail;
	}

	resp_id = h->id;

	if (h->qdcount != 1) {
		errmsg = "number of questions in response is not 1";
		goto fail;
	}

	pbuf = resp->ptr + sizeof(ffdns_hdr);

	name.len = ffdns_name(qname, sizeof(qname), resp->ptr, resp->len, &pbuf);
	if (name.len == 0) {
		errmsg = "invalid name in question";
		goto fail;
	}
	name.len--;
	name.ptr = qname;

	serv->r->log(FFDNSCL_LOG_DBG /*LOG_DBGNET*/, &name
		, "DNS response #%u.  Status: %u.  AA: %u, RA: %u.  Q: %u, A: %u, N: %u, R: %u."
		, h->id, hdr->rcode, hdr->aa, hdr->ra
		, h->qdcount, h->ancount, h->nscount, h->arcount);

	if (pbuf + sizeof(ffdns_ques) > end) {
		errmsg = "too small response";
		goto fail;
	}

	{
		ffdns_ques_host qh;
		ffdns_questohost(&qh, pbuf);
		if (qh.clas != FFDNS_IN) {
			serv->r->log(FFDNSCL_LOG_ERR, &name
				, "#%u: invalid class %u in DNS response"
				, h->id, qh.clas);
			goto fail;
		}
	}

	namecrc = ffcrc32_get(qname, name.len);

	found_query = ffrbt_find(&serv->r->queries, namecrc, NULL);
	if (found_query == NULL) {
		errmsg = "unexpected DNS response";
		goto fail;
	}

	q = query_sib(found_query);
	if (!ffstr_eq2(&q->name, &name)) {
		errmsg = "unexpected DNS response";
		goto fail;
	}

	return q;

fail:
	if (errmsg != NULL)
		errlog_srv(serv, "%s. ID: #%u. Name: %S", errmsg, resp_id, &name);

	return NULL;
}

/** Get the number of useful records.  Print debug info about the records in response. */
static uint ans_nrecs(dns_query *q, ffdns_hdr_host *h, const ffstr *resp, const char *pbuf, int is4)
{
	uint nrecs = 0;
	uint ir;
	char qname[FFDNS_MAXNAME];
	const char *end = resp->ptr + resp->len;
	ffdns_ans_host ans;
	ffstr name;

	for (ir = 0;  ir < h->ancount;  ir++) {
		name.len = ffdns_name(qname, sizeof(qname), resp->ptr, resp->len, &pbuf);
		if (name.len == 0) {
			warnlog_q(q, "#%u: invalid name in answer", h->id);
			break;
		}
		name.ptr = qname;
		name.len--;

		if (pbuf + sizeof(ffdns_ans) > end) {
			dbglog_q(q, LOG_DBGNET, "#%u: incomplete response", h->id);
			break;
		}

		ffdns_anstohost(&ans, (ffdns_ans*)pbuf);
		pbuf += sizeof(ffdns_ans) + ans.len;
		if (pbuf > end) {
			dbglog_q(q, LOG_DBGNET, "#%u: incomplete response", h->id);
			break;
		}

		switch (ans.type) {

		case FFDNS_A:
			if (ans.clas != FFDNS_IN) {
				errlog_q(q, "#%u: invalid class in %s record: %u", h->id, "A", ans.clas);
				continue;
			}
			if (ans.len != sizeof(ffip4)) {
				errlog_q(q, "#%u: invalid %s address length: %u", h->id, "A", ans.len);
				continue;
			}

			if (log_checkdbglevel(q, LOG_DBGFLOW)) {
				char ip[FFIP4_STRLEN];
				size_t iplen = ffip4_tostr(ip, FFCNT(ip), (void*)ans.data);
				dbglog_q(q, LOG_DBGFLOW, "%s for %S : %*s, TTL:%u"
					, "A", &name, (size_t)iplen, ip, ans.ttl);
			}

			if (is4)
				nrecs++;
			break;

		case FFDNS_AAAA:
			if (ans.clas != FFDNS_IN) {
				errlog_q(q, "#%u: invalid class in %s record: %u", h->id, "AAAA", ans.clas);
				continue;
			}
			if (ans.len != sizeof(ffip6)) {
				errlog_q(q, "#%u: invalid %s address length: %u", h->id, "AAAA", ans.len);
				continue;
			}

			if (log_checkdbglevel(q, LOG_DBGFLOW)) {
				char ip[FFIP6_STRLEN];
				size_t iplen = ffip6_tostr(ip, FFCNT(ip), (void*)ans.data);
				dbglog_q(q, LOG_DBGFLOW, "%s for %S : %*s, TTL:%u"
					, "AAAA", &name, (size_t)iplen, ip, ans.ttl);
			}

			if (!is4)
				nrecs++;
			break;

		case FFDNS_CNAME:
			if (log_checkdbglevel(q, LOG_DBGFLOW)) {
				ffstr scname;
				char cname[NI_MAXHOST];
				const char *tbuf = pbuf;

				scname.len = ffdns_name(cname, sizeof(cname), resp->ptr, resp->len, &tbuf);
				if (scname.len == 0 || tbuf > pbuf + ans.len) {
					errlog_q(q, "invalid CNAME");
					continue;
				}
				scname.ptr = cname;
				scname.len--;

				dbglog_q(q, LOG_DBGFLOW, "CNAME for %S : %S", &name, &scname);
			}
			break;

		default:
			dbglog_q(q, LOG_DBGFLOW, "record of type %u, length %u", ans.type, ans.len);
			break;
		}
	}

	return nrecs;
}

/** Set the linked list of addresses. */
static void resv_addr_setlist4(dns_a *a, dns_a *end)
{
	for (;;) {
		if (a + 1 == end) {
			a->ainfo.ai_next = NULL;
			break;
		}
		a->ainfo.ai_next = &(a + 1)->ainfo;
		a++;
	}
}

static void resv_addr_setlist6(dns_a6 *a6, dns_a6 *end)
{
	for (;;) {
		if (a6 + 1 == end) {
			a6->ainfo.ai_next = NULL;
			break;
		}
		a6->ainfo.ai_next = &(a6 + 1)->ainfo;
		a6++;
	}
}

/** Create DNS resource object. */
static ffdnscl_res* ans_proc_resp(dns_query *q, ffdns_hdr_host *h, const ffstr *resp, int is4)
{
	const char *end = resp->ptr + resp->len;
	const char *pbuf;
	uint nrecs = 0;
	uint ir;
	ffdnscl_res *res = NULL;
	dns_a *acur;
	dns_a6 *a6cur;
	uint minttl = (uint)-1;

	pbuf = resp->ptr + sizeof(ffdns_hdr);
	ffdns_skipname(resp->ptr, resp->len, &pbuf);
	pbuf += sizeof(ffdns_ques);

	nrecs = ans_nrecs(q, h, resp, pbuf, is4);

	if (nrecs == 0) {
		dbglog_q(q, LOG_DBGFLOW, "#%u: no useful records in response", h->id);
		return NULL;
	}

	{
		uint adr_sz = (is4 ? sizeof(dns_a) : sizeof(dns_a6));
		res = ffmem_alloc(sizeof(ffdnscl_res) + adr_sz * nrecs);
		if (res == NULL) {
			syserrlog_x(q->r, "%e", FFERR_BUFALOC);
			return NULL;
		}
		res->naddrs = nrecs;
		acur = res->addrs;
		a6cur = res->addrs6;
	}

	// set addresses and get the minimum TTL value
	for (ir = 0;  ir < h->ancount;  ir++) {
		ffdns_ans_host ans;

		ffdns_skipname(resp->ptr, resp->len, &pbuf);

		if (pbuf + sizeof(ffdns_ans) > end)
			break;

		ffdns_anstohost(&ans, (ffdns_ans*)pbuf);
		pbuf += sizeof(ffdns_ans) + ans.len;
		if (pbuf > end)
			break;

		if (is4 && ans.type == FFDNS_A) {
			if (ans.clas != FFDNS_IN || ans.len != sizeof(struct in_addr))
				continue;

			acur->addr.sin_family = AF_INET;
			acur->ainfo.ai_family = AF_INET;
			acur->ainfo.ai_addr = (struct sockaddr*)&acur->addr;
			acur->ainfo.ai_addrlen = sizeof(struct sockaddr_in);
			ffmemcpy(&acur->addr.sin_addr, ans.data, sizeof(struct in_addr));
			acur++;

			minttl = (uint)ffmin(minttl, ans.ttl);

		} else if (!is4 && ans.type == FFDNS_AAAA) {
			if (ans.clas != FFDNS_IN || ans.len != sizeof(struct in6_addr))
				continue;

			a6cur->addr.sin6_family = AF_INET6;
			a6cur->ainfo.ai_family = AF_INET6;
			a6cur->ainfo.ai_addr = (struct sockaddr*)&a6cur->addr;
			a6cur->ainfo.ai_addrlen = sizeof(struct sockaddr_in6);
			ffmemcpy(&a6cur->addr.sin6_addr, ans.data, sizeof(struct in6_addr));
			a6cur++;

			minttl = (uint)ffmin(minttl, ans.ttl);
		}
	}

	if (is4)
		resv_addr_setlist4(res->addrs, acur);
	else
		resv_addr_setlist6(res->addrs6, a6cur);

	ir = q->nres++;
	q->res[ir] = res;
	q->ttl[ir] = minttl;
	return res;
}

/** Notify users, waiting for this question.  Free query object. */
static void query_fin(dns_query *q, int status)
{
	dns_quser *quser;
	const ffaddrinfo *ai[2] = {0};
	uint i;

	ffrbt_rm(&q->r->queries, &q->rbtnod);

	for (i = 0;  i < q->nres;  i++) {
		ai[i] = &q->res[i]->addrs[0].ainfo;
	}

	FFARR_WALK(&q->users, quser) {
		dbglog_q(q, LOG_DBGFLOW, "calling user function %p, udata:%p"
			, quser->ondone, quser->udata);
		quser->ondone(quser->udata, status, ai);
	}

	dbglog_q(q, LOG_DBGFLOW, "query done [%L]"
		, q->r->queries.len);
	query_free(q);
}


int ffdnscl_serv_add(ffdnsclient *r, const ffstr *saddr)
{
	ffdnscl_serv *serv = ffmem_new(ffdnscl_serv);
	if (serv == NULL)
		return -1;
	fflist_ins(&r->servs, &serv->sib);

	serv->r = r;
	serv->ansbuf = ffmem_alloc(r->buf_size);
	if (serv->ansbuf == NULL)
		goto err;
	r->curserv = FF_GETPTR(ffdnscl_serv, sib, r->servs.first);

	ffip4 a4;
	if (0 != ffip4_parse(&a4, saddr->ptr, saddr->len))
		goto err;
	ffaddr_init(&serv->addr);
	ffip4_set(&serv->addr, (void*)&a4);
	ffip_setport(&serv->addr, FFDNS_PORT);

	char *s = ffs_copy(serv->saddr_s, serv->saddr_s + FFCNT(serv->saddr_s), saddr->ptr, saddr->len);
	ffstr_set(&serv->saddr, serv->saddr_s, s - serv->saddr_s);
	return 0;

err:
	ffmem_free(serv);
	return -1;
}

/** Prepare socket to connect to a DNS server. */
static int serv_init(ffdnscl_serv *serv)
{
	int er;
	ffskt sk;

	sk = ffskt_create(ffaddr_family(&serv->addr), SOCK_DGRAM | SOCK_NONBLOCK, IPPROTO_UDP);
	if (sk == FF_BADSKT) {
		er = FFERR_SKTCREAT;
		goto fail;
	}

	{
		ffaddr la;
		ffaddr_init(&la);
		ffaddr_setany(&la, ffaddr_family(&serv->addr));
		if (0 != ffskt_bind(sk, &la.a, la.len)) {
			er = FFERR_SKTLISTEN;
			goto fail;
		}
	}

	ffaio_init(&serv->aiotask);
	serv->aiotask.udata = serv;
	serv->aiotask.sk = sk;
	serv->aiotask.udp = 1;
	if (0 != ffaio_attach(&serv->aiotask, serv->r->kq, FFKQU_READ)) {
		er = FFERR_KQUATT;
		goto fail;
	}

	serv->sk = sk;
	return 0;

fail:
	syserrlog_srv(serv, "%e", er);

	if (sk != FF_BADSKT)
		ffskt_close(sk);

	return 1;
}

static void serv_fin(ffdnscl_serv *serv)
{
	FF_SAFECLOSE(serv->sk, FF_BADSKT, ffskt_close);
	FF_SAFECLOSE(serv->ansbuf, NULL, ffmem_free);
	ffmem_free(serv);
}

/** Round-robin balancer. */
static ffdnscl_serv * serv_next(ffdnsclient *r)
{
	ffdnscl_serv *serv = r->curserv;
	fflist_item *next = ((serv->sib.next != fflist_sentl(&r->servs)) ? serv->sib.next : r->servs.first);
	r->curserv = FF_GETPTR(ffdnscl_serv, sib, next);
	return serv;
}


ffdnscl_res* ffdnscl_res_by_ai(const ffaddrinfo *ai)
{
	dns_a *paddrs = FF_GETPTR(dns_a, ainfo, ai);
	ffdnscl_res *res = FF_GETPTR(ffdnscl_res, addrs, paddrs);
	return res;
}

ffaddrinfo* ffdnscl_res_ai(ffdnscl_res *res)
{
	return &res->addrs[0].ainfo;
}

void* ffdnscl_res_udata(ffdnscl_res *res)
{
	return res->cached_id;
}

void ffdnscl_res_setudata(ffdnscl_res *res, void *udata)
{
	res->cached_id = udata;
	res->usage = 0;
}

void ffdnscl_res_free(ffdnscl_res *dr)
{
	ffmem_free(dr);
}
