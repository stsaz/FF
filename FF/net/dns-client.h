/** DNS client.
Copyright 2019 Simon Zolin
*/

#include <FF/array.h>
#include <FF/rbtree.h>
#include <FF/list.h>
#include <FF/sys/timer-queue.h>
#include <FFOS/asyncio.h>


typedef struct ffdnscl_serv ffdnscl_serv;
typedef struct ffdnsclient ffdnsclient;
typedef struct ffdnsclient ffdnscl_conf;
typedef struct ffdnscl_res ffdnscl_res;

typedef struct {
	ffstr name; // host name
	int status; // DNS response code (enum FFDNS_R);  -1:internal error
	ffarr2 ip; // ffip6[].  [IPv4..., IPv6...]
	ffstr server_addr; // server address
} ffdnscl_result;

typedef void (*ffdnscl_onresolve)(void *udata, const ffdnscl_result *res);

typedef int (*ffdnscl_oncomplete)(ffdnsclient *r, ffdnscl_res *res, const ffstr *name, uint refcount, uint ttl);

/**
level: enum FFDNSCL_LOG */
typedef void (*ffdnscl_log)(uint level, const char *fmt, ...);

typedef void (*ffdnscl_timer)(fftmrq_entry *tmr, uint value_ms);
typedef fftime (*ffdnscl_time)(void);

struct ffdnsclient {
	fffd kq; // required
	ffdnscl_oncomplete oncomplete;
	ffdnscl_log log;
	ffdnscl_timer timer; // required
	ffdnscl_time time;

	uint max_tries; // default:1
	uint retry_timeout; // in msec.  default:1000
	uint buf_size; // default:4k
	uint enable_ipv6 :1; // default:1
	uint edns :1; // default:1
	uint debug_log :1;

	fflist servs; //ffdnscl_serv[]
	ffdnscl_serv *curserv;

	ffrbtree queries; //active queries by hostname.  dns_query[]
};

enum FFDNSCL_LOG {
	FFDNSCL_LOG_ERR,
	FFDNSCL_LOG_WARN,
	FFDNSCL_LOG_DBG,

	FFDNSCL_LOG_SYS = 0x10,
};

/** Initialize configuration object. */
FF_EXTN void ffdnscl_conf_init(ffdnscl_conf *conf);

FF_EXTN ffdnsclient* ffdnscl_new(ffdnscl_conf *conf);
FF_EXTN void ffdnscl_free(ffdnsclient *r);

/** Add DNS server.
addr: "IP[:PORT]" */
FF_EXTN int ffdnscl_serv_add(ffdnsclient *r, const ffstr *addr);

enum FFDNSCL_F {
	FFDNSCL_CANCEL = 1,
};

/**
flags: enum FFDNSCL_F
Return 0 on success. */
FF_EXTN int ffdnscl_resolve(ffdnsclient *r, ffstr name, ffdnscl_onresolve ondone, void *udata, uint flags);
