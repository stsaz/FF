/** DNS client.
Copyright 2019 Simon Zolin
*/

#include <FF/string.h>
#include <FF/rbtree.h>
#include <FF/list.h>
#include <FF/sys/timer-queue.h>
#include <FFOS/asyncio.h>


typedef struct ffdnscl_serv ffdnscl_serv;
typedef struct ffdnsclient ffdnsclient;
typedef struct ffdnsclient ffdnscl_conf;
typedef struct ffdnscl_res ffdnscl_res;

/**
status: enum FFDNS_R or other DNS response code;  -1:internal error
ffdnscl_unref() must be called on both ai[0] and ai[1] (if set). */
typedef void (*ffdnscl_onresolve)(void *udata, int status, const ffaddrinfo *ai[2]);

typedef int (*ffdnscl_oncomplete)(ffdnsclient *r, ffdnscl_res *res, const ffstr *name, uint refcount, uint ttl);

/**
level: enum FFDNSCL_LOG */
typedef void (*ffdnscl_log)(uint level, const ffstr *trxn, const char *fmt, ...);

typedef void (*ffdnscl_timer)(fftmrq_entry *tmr, uint value_ms);
typedef fftime (*ffdnscl_time)(void);

struct ffdnsclient {
	fffd kq;
	ffdnscl_oncomplete oncomplete;
	ffdnscl_log log;
	ffdnscl_timer timer;
	ffdnscl_time time;

	uint max_tries;
	uint retry_timeout; //in msec
	uint buf_size;
	uint enable_ipv6 :1;
	uint edns :1;
	uint debug_log :1;

	fflist servs; //ffdnscl_serv[]
	ffdnscl_serv *curserv;

	ffrbtree queries; //active queries by hostname.  dns_query[]
};

struct ffdnscl_serv {
	fflist_item sib;
	ffdnsclient *r;

	ffskt sk;
	ffaio_task aiotask;
	ffaddr addr;
	char saddr_s[FF_MAXIP4];
	ffstr saddr;
	char *ansbuf;
	unsigned connected :1;

	uint nqueries;
};

enum FFDNSCL_LOG {
	FFDNSCL_LOG_ERR,
	FFDNSCL_LOG_WARN,
	FFDNSCL_LOG_DBG,

	FFDNSCL_LOG_SYS = 0x10,
};

FF_EXTN ffdnsclient* ffdnscl_new(ffdnscl_conf *conf);
FF_EXTN void ffdnscl_free(ffdnsclient *r);

FF_EXTN int ffdnscl_serv_add(ffdnsclient *r, const ffstr *addr);

FF_EXTN ffdnscl_res* ffdnscl_res_by_ai(const ffaddrinfo *ai);
FF_EXTN ffaddrinfo* ffdnscl_res_ai(ffdnscl_res *res);
FF_EXTN void* ffdnscl_res_udata(ffdnscl_res *res);
FF_EXTN void ffdnscl_res_setudata(ffdnscl_res *res, void *udata);
FF_EXTN void ffdnscl_res_free(ffdnscl_res *dr);

enum FFDNSCL_F {
	FFDNSCL_CANCEL = 1,
};

/**
flags: enum FFDNSCL_F
Return 0 on success. */
FF_EXTN int ffdnscl_resolve(ffdnsclient *r, const char *name, size_t namelen, ffdnscl_onresolve ondone, void *udata, uint flags);

FF_EXTN void ffdnscl_unref(ffdnsclient *r, const ffaddrinfo *ai);
