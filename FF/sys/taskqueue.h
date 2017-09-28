/** User task queue.
Copyright (c) 2013 Simon Zolin
*/

#pragma once

#include <FF/list.h>
#include <FFOS/atomic.h>


typedef void (*fftask_handler)(void *param);

typedef struct fftask {
	fftask_handler handler;
	void *param;
	fflist_item sib;
} fftask;

#define fftask_set(tsk, func, udata) \
	(tsk)->handler = (func),  (tsk)->param = (udata)

/** Queue of arbitrary length containing tasks - user callback functions.
First in, first out.
One reader/deleter, multiple writers.
*/
typedef struct fftaskmgr {
	fflist tasks; //fftask[]
	fflock lk;
	uint max_run; //max. tasks to execute per fftask_run()
} fftaskmgr;

static FFINL void fftask_init(fftaskmgr *mgr)
{
	fflist_init(&mgr->tasks);
	fflk_init(&mgr->lk);
	mgr->max_run = 64;
}

/** Return TRUE if a task is in the queue. */
#define fftask_active(mgr, task)  ((task)->sib.next != NULL)

/** Add item into task queue.  Thread-safe.
Return 1 if the queue was empty. */
FF_EXTN uint fftask_post(fftaskmgr *mgr, fftask *task);

#define fftask_post4(mgr, task, func, _param) \
do { \
	(task)->handler = func; \
	(task)->param = _param; \
	fftask_post(mgr, task); \
} while (0)

/** Remove item from task queue. */
FF_EXTN void fftask_del(fftaskmgr *mgr, fftask *task);

/** Call a handler for each task.
Return the number of tasks executed. */
FF_EXTN uint fftask_run(fftaskmgr *mgr);
