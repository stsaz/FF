/** HTTP client.
This interface provides an easy way to retrieve a document via HTTP.
Copyright (c) 2019 Simon Zolin
*/

#pragma once

#include <FF/net/http.h>
#include <FF/sys/timer-queue.h>


/** Deinitialize recycled connection objects (on kqueue close). */
FF_EXTN void ffhttpcl_deinit();


enum FFHTTPCL_F {
	FFHTTPCL_HTTP10 = 1, /** Use HTTP ver 1.0. */
	FFHTTPCL_NOREDIRECT = 2, /** Don't follow redirections. */
};

enum FFHTTPCL_LOG {
	FFHTTPCL_LOG_ERR = 1,
	FFHTTPCL_LOG_WARN,
	FFHTTPCL_LOG_USER,
	FFHTTPCL_LOG_INFO,
	FFHTTPCL_LOG_DEBUG,

	FFHTTPCL_LOG_SYS = 0x10,
};

/** User's handler receives events when the internal state changes. */
typedef void (*ffhttpcl_handler)(void *param);

/** Logger function. */
typedef void (*ffhttpcl_log)(void *udata, uint level, const char *fmt, ...);

/** Set a one-shot timer.
value_ms: timer value in milliseconds;  0: disable. */
typedef void (*ffhttpcl_timer)(fftmrq_entry *tmr, uint value_ms);

/** HTTP client configuration. */
struct ffhttpcl_conf {
	fffd kq; /** Kernel queue used for asynchronous events.  Required. */
	ffhttpcl_log log;
	ffhttpcl_timer timer;
	uint nbuffers;
	uint buffer_size;
	uint buffer_lowat;
	uint connect_timeout; /** msec */
	uint timeout; /** msec */
	uint max_redirect; /** Maximum times to follow redirections. */
	uint max_reconnect; /** Maximum times to reconnect after I/O failure. */
	struct {
		/** Proxy hostname (static string).
		NULL: no proxy */
		const char *host;
		uint port; /** Proxy port */
	} proxy;
	uint debug_log :1; /** Log messages with FFHTTPCL_LOG_DEBUG. */
};

enum FFHTTPCL_CONF_F {
	FFHTTPCL_CONF_GET,
	FFHTTPCL_CONF_SET,
};

enum FFHTTPCL_ST {
	// all errors are <0
	FFHTTPCL_ENOADDR = -2,
	FFHTTPCL_ERR = -1,

	FFHTTPCL_DONE = 0,
	FFHTTPCL_DNS_WAIT, /** resolving hostname via DNS */
	FFHTTPCL_IP_WAIT, /** connecting to host */
	FFHTTPCL_REQ_WAIT, /** sending request */
	FFHTTPCL_RESP_WAIT, /** receiving response (HTTP headers) */
	FFHTTPCL_RESP, /** received response headers */
	FFHTTPCL_RESP_RECV, /** receiving data */
};

/** Create HTTP request.
flags: enum FFHTTPCL_F
Return connection object. */
FF_EXTN void* ffhttpcl_request(const char *method, const char *url, uint flags);

/** Close connection. */
FF_EXTN void ffhttpcl_close(void *con);

/** Set asynchronous callback function.
User function is called every time the connection status changes.
Processing is suspended until user calls send(). */
FF_EXTN void ffhttpcl_sethandler(void *con, ffhttpcl_handler func, void *udata);

/** Connect, send request, receive response.
Note: data must be NULL - sending request body isn't supported. */
FF_EXTN void ffhttpcl_send(void *con, const ffstr *data);

/** Get response data.
@data: response body
Return enum FFHTTPCL_ST. */
FF_EXTN int ffhttpcl_recv(void *con, ffhttp_response **resp, ffstr *data);

/** Add request header. */
FF_EXTN void ffhttpcl_header(void *con, const ffstr *name, const ffstr *val, uint flags);

/** Configure connection object.
May be called only before the first send().
flags: enum FFHTTPCL_CONF_F */
FF_EXTN void ffhttpcl_conf(void *con, struct ffhttpcl_conf *conf, uint flags);
