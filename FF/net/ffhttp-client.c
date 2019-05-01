/**
Copyright (c) 2019 Simon Zolin
*/

#include <FF/net/http-client.h>
#include <FF/list.h>
#include <FFOS/asyncio.h>


static fflist1 recycled_cons;

struct filter {
	const struct ffhttp_filter *iface;
	void *p;
};

typedef struct http {
	uint state;

	char *method;
	fflist1_item recycled;
	ffstr orig_target_url; // original target URL
	ffarr target_url; // target URL (possibly relocated)
	ffarr hostname; // server hostname (may be a proxy)
	uint hostport; // server port (may be a proxy)
	ffurl url;
	ffiplist iplist;
	ffip6 ip;
	ffaddrinfo *addr;
	ffip_iter curaddr;
	ffskt sk;
	ffaio_task aio;
	uint reconnects;
	fftmrq_entry tmr;
	ffhttp_cook hdrs;

	ffstr *bufs;
	uint rbuf;
	uint wbuf;
	size_t curtcp_len;
	uint lowat; //low-watermark number of filled bytes for buffer
	ffstr data, outdata;

	ffstr hbuf;
	ffhttp_response resp;
	uint nredirect;
	struct ffhttpcl_conf conf;

	uint flags;
	uint buflock :1
		, iowait :1 //waiting for I/O, all input data is consumed
		, async :1
		, preload :1 //fill all buffers
		;

	ffhttpcl_handler handler;
	void *udata;
	uint status;
	struct filter f;
} http;


#define dbglog(...) \
do { \
	if (c->conf.debug_log) \
		c->conf.log(c->udata, FFHTTPCL_LOG_DEBUG, __VA_ARGS__); \
} while (0)
#define infolog(...)  c->conf.log(c->udata, FFHTTPCL_LOG_INFO, __VA_ARGS__)
#define warnlog(...)  c->conf.log(c->udata, FFHTTPCL_LOG_WARN, __VA_ARGS__)
#define syswarnlog(...)  c->conf.log(c->udata, FFHTTPCL_LOG_WARN | FFHTTPCL_LOG_SYS, __VA_ARGS__)
#define errlog(...)  c->conf.log(c->udata, FFHTTPCL_LOG_ERR, __VA_ARGS__)
#define syserrlog(...)  c->conf.log(c->udata, FFHTTPCL_LOG_ERR | FFHTTPCL_LOG_SYS, __VA_ARGS__)

enum {
	R_ASYNC = 1,
	R_MORE,
	R_DATA,
	R_DONE,
	R_ERR,
};

static int httpcl_prep_url(http *c, const char *url);
static void httpcl_process(http *c);

static int ip_resolve(http *c);

static int tcp_alloc(http *c, size_t size);
static int tcp_prepare(http *c, ffaddr *a);
static int tcp_connect(http *c, const struct sockaddr *addr, socklen_t addr_size);
static int tcp_recv(http *c);
static void tcp_ontmr(void *param);
static int tcp_recvhdrs(http *c);
static int tcp_send(http *c);
static int tcp_ioerr(http *c);


static const struct ffhttp_filter*const http_filters[] = {
	&ffhttp_chunked_filter, &ffhttp_contlen_filter, &ffhttp_connclose_filter
};
static int http_prepreq(http *c, ffstr *dst);
static int http_parse(http *c);
static int http_recvbody(http *c, uint tcpfin);


void ffhttpcl_deinit()
{
	http *c;
	while (NULL != (c = (void*)fflist1_pop(&recycled_cons))) {
		c = FF_GETPTR(http, recycled, c);
		ffmem_free(c);
	}
}


/** Prepare a normal request URL. */
static int httpcl_prep_url(http *c, const char *url)
{
	int r;
	ffurl u;
	ffstr s;
	ffstr_setz(&s, url);
	ffurl_init(&u);
	if (0 != (r = ffurl_parse(&u, s.ptr, s.len))) {
		errlog("URL parse: %S: %s"
			, &s, ffurl_errstr(r));
		return -1;
	}
	if (!ffurl_has(&u, FFURL_HOST)) {
		errlog("incorrect URL: %S", &s);
		return -1;
	}
	ffstr scheme = ffurl_get(&u, s.ptr, FFURL_SCHEME);
	if (scheme.len == 0)
		ffstr_setz(&scheme, "http");
	if (u.port == FFHTTP_PORT && ffstr_eqz(&scheme, "http"))
		u.port = 0;
	ffstr host = ffurl_get(&u, s.ptr, FFURL_HOST);
	ffstr path = ffurl_get(&u, s.ptr, FFURL_PATH);
	ffstr qs = ffurl_get(&u, s.ptr, FFURL_QS);
	if (0 == ffurl_joinstr(&c->orig_target_url, &scheme, &host, u.port, &path, &qs))
		return -1;

	return 0;
}

static void log_empty(void *udata, uint level, const char *fmt, ...)
{
}

static void timer_empty(fftmrq_entry *tmr, uint value_ms)
{
}

void* ffhttpcl_request(const char *method, const char *url, uint flags)
{
	http *c;
	if (NULL != (c = (void*)fflist1_pop(&recycled_cons)))
		c = FF_GETPTR(http, recycled, c);
	else if (NULL == (c = ffmem_new(http)))
		return NULL;
	c->sk = FF_BADSKT;
	ffhttp_respinit(&c->resp);
	ffhttp_cookinit(&c->hdrs, NULL, 0);
	c->conf.log = &log_empty;
	c->conf.timer = &timer_empty;

	if (0 != httpcl_prep_url(c, url))
		goto done;
	ffstr_set2(&c->target_url, &c->orig_target_url);

	if (!ffhttp_check_method(method, ffsz_len(method))) {
		errlog("invalid HTTP method: %s", method);
		goto done;
	}
	if (NULL == (c->method = ffsz_alcopyz(method)))
		goto done;

	c->tmr.handler = &tcp_ontmr;
	c->tmr.param = c;
	c->flags = flags;

	c->conf.kq = FF_BADFD;
	c->conf.nbuffers = 2;
	c->conf.buffer_size = 16 * 1024;
	c->conf.connect_timeout = 1500;
	c->conf.timeout = 5000;
	c->conf.max_redirect = 10;
	c->conf.max_reconnect = 3;

	return c;

done:
	ffhttpcl_close(c);
	return NULL;
}

void ffhttpcl_close(void *con)
{
	if (con == NULL)
		return;

	http *c = con;
	c->conf.timer(&c->tmr, 0);

	if (c->f.p != NULL)
		c->f.iface->close(c->f.p);

	FF_SAFECLOSE(c->addr, NULL, ffaddr_free);
	if (c->sk != FF_BADSKT) {
		ffskt_fin(c->sk);
		ffskt_close(c->sk);
		c->sk = FF_BADSKT;
	}
	ffarr_free(&c->target_url);
	ffstr_free(&c->orig_target_url);
	ffmem_free(c->method);
	ffaio_fin(&c->aio);
	ffhttp_respfree(&c->resp);
	ffhttp_cookdestroy(&c->hdrs);

	if (c->bufs != NULL) {
		uint i;
		for (i = 0;  i != c->conf.nbuffers;  i++) {
			ffstr_free(&c->bufs[i]);
		}
	}
	ffmem_safefree(c->bufs);

	ffstr_free(&c->hbuf);

	uint inst = c->aio.instance;
	ffmem_tzero(c);
	c->aio.instance = inst;
	fflist1_push(&recycled_cons, &c->recycled);
}

void ffhttpcl_conf(void *con, struct ffhttpcl_conf *conf, uint flags)
{
	http *c = con;
	if (flags == FFHTTPCL_CONF_GET)
		*conf = c->conf;
	else if (flags == FFHTTPCL_CONF_SET)
		c->conf = *conf;
}

void ffhttpcl_sethandler(void *con, ffhttpcl_handler func, void *udata)
{
	http *c = con;
	c->handler = func;
	c->udata = udata;
}

enum {
	I_START, I_ADDR, I_NEXTADDR, I_CONN,
	I_HTTP_REQ, I_HTTP_REQ_SEND, I_HTTP_RESP, I_HTTP_RESP_PARSE, I_HTTP_RECVBODY, I_HTTP_RESPBODY,
	I_DONE, I_ERR, I_ERR2, I_NOOP,
};

static void call_handler(http *c, uint status)
{
	c->status = status;
	dbglog("calling user handler.  status:%d", status);
	c->handler(c->udata);
}

static void httpcl_process(http *c)
{
	int r;
	ffaddr a = {};

	for (;;) {
	switch (c->state) {

	case I_START:
		if (NULL == (c->bufs = ffmem_callocT(c->conf.nbuffers, ffstr))) {
			c->state = I_ERR;
			continue;
		}
		if (0 != tcp_alloc(c, c->conf.buffer_size)) {
			c->state = I_ERR;
			continue;
		}
		c->state = I_ADDR;
		call_handler(c, FFHTTPCL_DNS_WAIT);
		return;

	case I_ADDR:
		if (0 != ip_resolve(c)) {
			c->state = I_ERR;
			continue;
		}
		c->state = I_NEXTADDR;
		call_handler(c, FFHTTPCL_IP_WAIT);
		return;

	case I_NEXTADDR:
		if (0 != (r = tcp_prepare(c, &a))) {
			c->state = I_ERR2;
			continue;
		}
		c->state = I_CONN;
		//fallthrough

	case I_CONN:
		r = tcp_connect(c, &a.a, a.len);
		if (r == R_ASYNC)
			return;
		else if (r == R_MORE) {
			c->state = I_NEXTADDR;
			continue;
		}
		c->state = I_HTTP_REQ;
		call_handler(c, FFHTTPCL_REQ_WAIT);
		return;

	case I_HTTP_REQ:
		http_prepreq(c, &c->data);
		c->state = I_HTTP_REQ_SEND;
		//fallthrough

	case I_HTTP_REQ_SEND:
		r = tcp_send(c);
		if (r == R_ASYNC)
			return;
		else if (r == R_ERR) {
			tcp_ioerr(c);
			continue;
		}

		dbglog("receiving response...");
		ffstr_set(&c->data, c->bufs[0].ptr, c->conf.buffer_size);
		c->state = I_HTTP_RESP;
		call_handler(c, FFHTTPCL_RESP_WAIT);
		return;

	case I_HTTP_RESP:
		r = tcp_recvhdrs(c);
		if (r == R_ASYNC)
			return;
		else if (r == R_MORE) {
			c->state = I_ERR;
			continue;
		} else if (r == R_ERR) {
			tcp_ioerr(c);
			continue;
		}
		c->state = I_HTTP_RESP_PARSE;
		//fall through

	case I_HTTP_RESP_PARSE:
		r = http_parse(c);
		switch (r) {
		case 0:
			break;
		case -1:
			c->state = I_ERR;
			continue;
		case 1:
			c->state = I_HTTP_RESP;
			continue;
		case 2:
			if (c->flags & FFHTTPCL_NOREDIRECT)
				break;
			c->state = I_ADDR;
			continue;
		}

		if (c->resp.h.has_body) {
			const struct ffhttp_filter *const *f;
			FFARRS_FOREACH(http_filters, f) {
				void *d = (*f)->open(&c->resp.h);
				if (d == NULL)
				{}
				else if (d == (void*)-1) {
					warnlog("filter #%u open error", (int)(f - http_filters));
					c->state = I_ERR;
					continue;
				} else {
					dbglog("opened filter #%u", (int)(f - http_filters));
					c->f.iface = *f;
					c->f.p = d;
					break;
				}
			}
		}

		ffstr_set2(&c->data, &c->bufs[0]);
		ffstr_shift(&c->data, c->resp.h.len);
		c->bufs[0].len = 0;
		if (c->resp.h.has_body)
			c->state = I_HTTP_RESPBODY;
		else
			c->state = I_DONE;
		call_handler(c, FFHTTPCL_RESP);
		return;

	case I_HTTP_RECVBODY:
		r = tcp_recv(c);
		if (r == R_ASYNC)
			return;
		else if (r == R_ERR) {
			tcp_ioerr(c);
			continue;
		} else if (r == R_DONE) {
			r = http_recvbody(c, 1);
			switch (r) {
			case -1:
				c->state = I_ERR;
				continue;
			}
			c->state = I_DONE;
			call_handler(c, FFHTTPCL_RESP_RECV);
			continue;
		} else if (r == R_MORE) {
			c->state = I_ERR;
			continue;
		}
		c->data = c->bufs[c->rbuf];
		c->bufs[c->rbuf].len = 0;
		c->rbuf = ffint_cycleinc(c->rbuf, c->conf.nbuffers);
		c->state = I_HTTP_RESPBODY;
		//fallthrough

	case I_HTTP_RESPBODY:
		r = http_recvbody(c, 0);
		switch (r) {
		case -1:
			c->state = I_ERR;
			continue;
		case 0:
			c->state = I_DONE;
			call_handler(c, FFHTTPCL_RESP_RECV);
			continue;
		}
		if (c->data.len == 0)
			c->state = I_HTTP_RECVBODY;
		call_handler(c, FFHTTPCL_RESP_RECV);
		return;

	case I_ERR:
	case I_DONE: {
		uint r = (c->state == I_ERR) ? FFHTTPCL_ERR : FFHTTPCL_DONE;
		c->state = I_NOOP;
		call_handler(c, r);
		return;
	}
	case I_ERR2:
		c->state = I_NOOP;
		call_handler(c, r);
		return;

	case I_NOOP:
		return;
	}
	}
}

void ffhttpcl_send(void *con, const ffstr *data)
{
	FF_ASSERT(data == NULL);
	httpcl_process(con);
}

int ffhttpcl_recv(void *con, ffhttp_response **resp, ffstr *data)
{
	http *c = con;
	*resp = &c->resp;
	ffstr_set2(data, &c->outdata);
	c->outdata.len = 0;
	return c->status;
}

void ffhttpcl_header(void *con, const ffstr *name, const ffstr *val, uint flags)
{
	http *c = con;
	ffhttp_addhdr_str(&c->hdrs, name, val);
}


static int ip_resolve(http *c)
{
	char *hostz;
	int r;

	ffurl_init(&c->url);
	if (0 != (r = ffurl_parse(&c->url, c->target_url.ptr, c->target_url.len))) {
		errlog("URL parse: %S: %s"
			, &c->target_url, ffurl_errstr(r));
		goto done;
	}
	if (c->url.port == 0)
		c->url.port = FFHTTP_PORT;

	if (c->conf.proxy.host == NULL) {
		ffstr host = ffurl_get(&c->url, c->target_url.ptr, FFURL_HOST);
		ffstr_set2(&c->hostname, &host);
		c->hostport = c->url.port;
		r = ffurl_parse_ip(&c->url, c->target_url.ptr, &c->ip);
	} else {
		ffstr_setz(&c->hostname, c->conf.proxy.host);
		c->hostport = c->conf.proxy.port;
		r = ffip_parse(c->hostname.ptr, c->hostname.len, &c->ip);
		c->flags |= FFHTTPCL_NOREDIRECT;
	}

	if (r < 0) {
		errlog("bad IP address: %S", &c->hostname);
		goto done;
	} else if (r != 0) {
		ffip_list_set(&c->iplist, r, &c->ip);
		ffip_iter_set(&c->curaddr, &c->iplist, NULL);
		return 0;
	}

	if (NULL == (hostz = ffsz_alcopystr(&c->hostname))) {
		syserrlog("%s", ffmem_alloc_S);
		goto done;
	}

	infolog("resolving host %S...", &c->hostname);
	r = ffaddr_info(&c->addr, hostz, NULL, 0);
	ffmem_free(hostz);
	if (r != 0) {
		syserrlog("%s", ffaddr_info_S);
		goto done;
	}
	ffip_iter_set(&c->curaddr, NULL, c->addr);

	if (c->conf.debug_log) {
		size_t n;
		char buf[FF_MAXIP6];
		ffip_iter it;
		ffip_iter_set(&it, NULL, c->addr);
		uint fam;
		void *ip;
		while (0 != (fam = ffip_next(&it, &ip))) {
			n = ffip_tostr(buf, sizeof(buf), fam, ip, 0);
			dbglog("%*s", n, buf);
		}
	}

	return 0;

done:
	return -1;
}


static int tcp_alloc(http *c, size_t size)
{
	uint i;
	for (i = 0;  i != c->conf.nbuffers;  i++) {
		if (NULL == (ffstr_alloc(&c->bufs[i], size))) {
			syserrlog("%s", ffmem_alloc_S);
			return -1;
		}
	}
	return 0;
}

static int tcp_prepare(http *c, ffaddr *a)
{
	void *ip;
	int family;
	while (0 != (family = ffip_next(&c->curaddr, &ip))) {

		ffaddr_setip(a, family, ip);
		ffip_setport(a, c->hostport);

		char saddr[FF_MAXIP6];
		size_t n = ffaddr_tostr(a, saddr, sizeof(saddr), FFADDR_USEPORT);
		infolog("connecting to %S (%*s)...", &c->hostname, n, saddr);

		if (FF_BADSKT == (c->sk = ffskt_create(family, SOCK_STREAM | SOCK_NONBLOCK, IPPROTO_TCP))) {
			syswarnlog("%s", ffskt_create_S);
			ffskt_close(c->sk);
			c->sk = FF_BADSKT;
			continue;
		}

		if (0 != ffskt_setopt(c->sk, IPPROTO_TCP, TCP_NODELAY, 1))
			syswarnlog("%s", ffskt_setopt_S);

		ffaio_init(&c->aio);
		c->aio.sk = c->sk;
		c->aio.udata = c;
		if (0 != ffaio_attach(&c->aio, c->conf.kq, FFKQU_READ | FFKQU_WRITE)) {
			syserrlog("%s", ffkqu_attach_S);
			return FFHTTPCL_ERR;
		}
		return 0;
	}

	errlog("no next address to connect");
	return FFHTTPCL_ENOADDR;
}

static void tcp_aio(void *udata)
{
	http *c = udata;
	c->async = 0;
	c->conf.timer(&c->tmr, 0);
	httpcl_process(c);
}

static int tcp_connect(http *c, const struct sockaddr *addr, socklen_t addr_size)
{
	int r;
	r = ffaio_connect(&c->aio, &tcp_aio, addr, addr_size);
	if (r == FFAIO_ERROR) {
		syswarnlog("%s", ffskt_connect_S);
		ffskt_close(c->sk);
		c->sk = FF_BADSKT;
		ffaio_fin(&c->aio);
		return R_MORE;

	} else if (r == FFAIO_ASYNC) {
		c->async = 1;
		c->conf.timer(&c->tmr, c->conf.connect_timeout);
		return R_ASYNC;
	}

	dbglog("%s ok", ffskt_connect_S);
	FF_SAFECLOSE(c->addr, NULL, ffaddr_free);
	ffmem_tzero(&c->curaddr);
	return 0;
}

static void tcp_ontmr(void *param)
{
	http *c = param;
	warnlog("I/O timeout", 0);
	c->async = 0;
	tcp_ioerr(c);
	httpcl_process(c);
}

static int tcp_recvhdrs(http *c)
{
	ssize_t r;

	if (c->bufs[0].len == c->conf.buffer_size) {
		errlog("too large response headers [%L]"
			, c->bufs[0].len);
		return R_MORE;
	}

	r = ffaio_recv(&c->aio, &tcp_aio, ffarr_end(&c->bufs[0]), c->conf.buffer_size - c->bufs[0].len);
	if (r == FFAIO_ASYNC) {
		dbglog("async recv...");
		c->async = 1;
		c->conf.timer(&c->tmr, c->conf.timeout);
		return R_ASYNC;
	}

	if (r <= 0) {
		if (r == 0)
			errlog("server has closed connection");
		else
			syserrlog("%s", ffskt_recv_S);
		return R_ERR;
	}

	c->bufs[0].len += r;
	dbglog("recv: +%L [%L]", r, c->bufs[0].len);
	return 0;
}

static int tcp_ioerr(http *c)
{
	if (c->reconnects++ == c->conf.max_reconnect) {
		errlog("reached max number of reconnections", 0);
		c->state = I_ERR;
		return 1;
	}

	ffskt_fin(c->sk);
	ffskt_close(c->sk);
	c->sk = FF_BADSKT;
	ffaio_fin(&c->aio);

	ffarr_free(&c->target_url);
	ffstr_set2(&c->target_url, &c->orig_target_url);
	ffmem_tzero(&c->url);
	ffmem_tzero(&c->iplist);
	ffmem_tzero(&c->ip);

	for (uint i = 0;  i != c->conf.nbuffers;  i++) {
		c->bufs[i].len = 0;
	}
	c->curtcp_len = 0;
	c->wbuf = c->rbuf = 0;
	c->lowat = 0;

	ffhttp_respfree(&c->resp);
	ffhttp_respinit(&c->resp);
	c->state = I_ADDR;
	dbglog("reconnecting...", 0);
	return 0;
}

static int tcp_recv(http *c)
{
	ssize_t r;

	if (c->curtcp_len == c->conf.buffer_size) {
		errlog("buffer #%u is full", c->wbuf);
		return R_MORE;
	}

	for (;;) {

		dbglog("buf #%u recv...  rpending:%u  size:%u"
			, c->wbuf, c->aio.rpending
			, (int)c->conf.buffer_size - (int)c->curtcp_len);
		r = ffaio_recv(&c->aio, &tcp_aio, c->bufs[c->wbuf].ptr + c->curtcp_len, c->conf.buffer_size - c->curtcp_len);
		if (r == FFAIO_ASYNC) {
			dbglog("buf #%u async recv...", c->wbuf);
			c->async = 1;
			c->conf.timer(&c->tmr, c->conf.timeout);
			return R_ASYNC;
		}

		if (r == 0) {
			dbglog("server has closed connection");
			return R_DONE;
		} else if (r < 0) {
			syserrlog("%s", ffskt_recv_S);
			return R_ERR;
		}

		c->curtcp_len += r;
		dbglog("buf #%u recv: +%L [%L]", c->wbuf, r, c->curtcp_len);
		if (c->curtcp_len < c->lowat)
			continue;

		c->bufs[c->wbuf].len = c->curtcp_len;
		c->curtcp_len = 0;
		c->wbuf = ffint_cycleinc(c->wbuf, c->conf.nbuffers);
		if (c->preload && c->bufs[c->wbuf].len == 0) {
			// the next buffer is free, so start filling it
			continue;
		}

		break;
	}

	return R_DATA;
}

static int tcp_send(http *c)
{
	int r;

	for (;;) {
		r = ffaio_send(&c->aio, &tcp_aio, c->data.ptr, c->data.len);
		if (r == FFAIO_ERROR) {
			syserrlog("%s", ffskt_send_S);
			return R_ERR;

		} else if (r == FFAIO_ASYNC) {
			c->async = 1;
			c->conf.timer(&c->tmr, c->conf.timeout);
			return R_ASYNC;
		}

		dbglog("send: +%u", r);
		ffstr_shift(&c->data, r);
		if (c->data.len == 0)
			return 0;
	}
}


static int http_prepreq(http *c, ffstr *dst)
{
	ffstr s;
	ffhttp_cook ck;

	ffhttp_cookinit(&ck, NULL, 0);

	if (c->flags & FFHTTPCL_HTTP10)
		ffstr_setcz(&ck.proto, "HTTP/1.0");

	if (c->conf.proxy.host == NULL)
		s = ffurl_get(&c->url, c->target_url.ptr, FFURL_PATHQS);
	else
		ffstr_set2(&s, &c->target_url);
	ffhttp_addrequest(&ck, c->method, ffsz_len(c->method), s.ptr, s.len);

	s = ffurl_get(&c->url, c->target_url.ptr, FFURL_FULLHOST);
	ffhttp_addihdr(&ck, FFHTTP_HOST, s.ptr, s.len);

	ffarr_append(&ck.buf, c->hdrs.buf.ptr, c->hdrs.buf.len);

	ffhttp_cookfin(&ck);
	ffstr_acqstr3(&c->hbuf, &ck.buf);
	ffstr_set2(dst, &c->hbuf);
	ffhttp_cookdestroy(&ck);
	dbglog("sending request: %S", dst);
	return 0;
}

/**
Return 0 on success;  1 - need more data;  2 - redirect;  -1 on error. */
static int http_parse(http *c)
{
	ffstr s;
	int r = ffhttp_respparse_all(&c->resp, c->bufs[0].ptr, c->bufs[0].len, FFHTTP_IGN_STATUS_PROTO);
	switch (r) {
	case FFHTTP_DONE:
		break;
	case FFHTTP_MORE:
		return 1;
	default:
		errlog("parse HTTP response: %s", ffhttp_errstr(r));
		return -1;
	}

	dbglog("HTTP response: %*s", c->resp.h.len, c->bufs[0].ptr);

	if ((c->resp.code == 301 || c->resp.code == 302)
		&& c->nredirect++ != c->conf.max_redirect
		&& 0 != ffhttp_findihdr(&c->resp.h, FFHTTP_LOCATION, &s)) {

		infolog("HTTP redirect: %S", &s);
		if (c->sk != FF_BADSKT) {
			ffskt_fin(c->sk);
			ffskt_close(c->sk);
			c->sk = FF_BADSKT;
		}
		ffaio_fin(&c->aio);
		ffarr_free(&c->target_url);
		if (0 == ffstr_fmt(&c->target_url, "%S", &s)) {
			syserrlog("%s", ffmem_alloc_S);
			return -1;
		}
		ffurl_init(&c->url);
		c->bufs[0].len = 0;
		ffhttp_respfree(&c->resp);
		ffhttp_respinit(&c->resp);
		return 2;
	}

	return 0;
}

static int http_recvbody(http *c, uint tcpfin)
{
	int r;
	ffstr s;
	if (!tcpfin)
		r = c->f.iface->process(c->f.p, &c->data, &s);
	else
		r = c->f.iface->process(c->f.p, NULL, &s);
	if (ffhttp_iserr(r)) {
		warnlog("filter error: (%d) %s", r, ffhttp_errstr(r));
		return -1;
	}
	c->outdata = s;
	if (r == FFHTTP_DONE)
		return 0;
	return 1;
}
