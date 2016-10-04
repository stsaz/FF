/** Windows object handler - call a user function when a kernel object signals.
Copyright (c) 2015 Simon Zolin
*/

/*
       (HANDLE)
KERNEL -------> WOH-thread -> user_function()
*/

#include <FFOS/thread.h>
#include <FFOS/atomic.h>


typedef void (*handler_t)(void *udata);

struct ffwoh_item {
	handler_t handler;
	void *udata;
};

typedef struct ffwoh {
	uint count;
	HANDLE hdls[MAXIMUM_WAIT_OBJECTS]; //hdls[0] is wake_evt
	struct ffwoh_item items[MAXIMUM_WAIT_OBJECTS];

	HANDLE wake_evt
		, wait_evt;
	ffthd thd;
	ffatomic cmd;
	fflock lk;
} ffwoh;

FF_EXTN ffwoh* ffwoh_create(void);
FF_EXTN void ffwoh_free(ffwoh *oh);

/** Associate HANDLE with user-function. */
FF_EXTN int ffwoh_add(ffwoh *oh, HANDLE h, handler_t handler, void *udata);

/** Unregister HANDLE. */
FF_EXTN void ffwoh_rm(ffwoh *oh, HANDLE h);
