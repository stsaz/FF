/**
Copyright (c) 2019 Simon Zolin
*/

#include <FF/net/dns-client.h>
#include <FF/net/dns.h>
#include <FFOS/random.h>
#include <FFOS/test.h>

#define x FFTEST_BOOL


static ffdnsclient *ctx;
static fftmrq_entry gtmr;
static fftimer_queue tq;
static uint gflags;
static int oncomplete(ffdnsclient *r, ffdnscl_res *res, const ffstr *name, uint refcount, uint ttl);
static void onresolve(void *udata, int status, const ffaddrinfo *ai[2]);
static void dnslog(uint level, const ffstr *trxn, const char *fmt, ...);
static void dnstimer(fftmrq_entry *tmr, uint value_ms);
static fftime dnstime(void);
static void tmr_exit(void *param);

void test_dns_client(void)
{
	fffd kq = FF_BADFD;

	fftime now;
	fftime_now(&now);
	ffrnd_seed(now.sec);

	// ffkqu_init();
	kq = ffkqu_create();
	fftmrq_init(&tq);
	fftmrq_start(&tq, kq, 100);
	gtmr.handler = &tmr_exit;
	fftmrq_add(&tq, &gtmr, -2000);

	ffdnscl_conf conf = {};
	conf.kq = kq;
	conf.oncomplete = &oncomplete;
	conf.log = &dnslog;
	conf.time = &dnstime;
	conf.timer = &dnstimer;

	conf.max_tries = 3;
	conf.retry_timeout = 500;
	conf.buf_size = 4096;
	conf.enable_ipv6 = 1;
	conf.edns = 1;
	conf.debug_log = 1;

	ctx = ffdnscl_new(&conf);
	ffstr s;
	ffstr_setz(&s, "8.8.8.8");
	x(0 == ffdnscl_serv_add(ctx, &s));

	x(0 == ffdnscl_resolve(ctx, FFSTR("google.com"), &onresolve, (void*)1, 0));

	x(0 == ffdnscl_resolve(ctx, FFSTR("apple.com"), &onresolve, (void*)2, 0));
	x(0 == ffdnscl_resolve(ctx, FFSTR("apple.com"), &onresolve, (void*)2, FFDNSCL_CANCEL));

	ffkqu_time tm;
	ffkqu_settm(&tm, 1000);
	for (;;) {
		if (gflags & 2)
			break;
		ffkqu_entry ev;
		int n = ffkqu_wait(kq, &ev, 1, &tm);
		if (n == 1)
			ffkev_call(&ev);
	}

	x(gflags & 1);

	ffkqu_close(kq);
	ffdnscl_free(ctx);
	fftmrq_destroy(&tq, kq);
}

static void onresolve(void *udata, int status, const ffaddrinfo *ai[2])
{
	if (udata == (void*)1) {
		x(status == FFDNS_NOERROR);
		gflags |= 1;
		x(ai[0] != NULL);
		ffdnscl_unref(ctx, ai[0]);
	} else {
		x(0);
	}
	if (ai[1] != NULL)
		ffdnscl_unref(ctx, ai[1]);
}

static int oncomplete(ffdnsclient *r, ffdnscl_res *res, const ffstr *name, uint refcount, uint ttl)
{
	return 0;
}

static void dnslog(uint level, const ffstr *trxn, const char *fmt, ...)
{
	ffarr a = {};
	va_list args;
	va_start(args, fmt);
	ffstr_catfmt(&a, "%S: ", trxn);
	ffstr_catfmtv(&a, fmt, args);
	va_end(args);
	fffile_write(ffstdout, a.ptr, a.len);
	fffile_write(ffstdout, "\r\n", 2);
}

static void dnstimer(fftmrq_entry *tmr, uint value_ms)
{
	if (value_ms == 0) {
		if (fftmrq_active(&tq, tmr))
			fftmrq_rm(&tq, tmr);
		return;
	}
	fftmrq_add(&tq, tmr, -(int)value_ms);
}

static fftime dnstime(void)
{
	fftime t;
	fftime_now(&t);
	return t;
}

static void tmr_exit(void *param)
{
	gflags |= 2;
}
