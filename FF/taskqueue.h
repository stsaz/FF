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

typedef struct fftaskmgr {
	fflist tasks; //fftask[]
	fflock lk;
} fftaskmgr;

/** Return TRUE if a task is in the queue. */
static FFINL ffbool fftask_active(fftaskmgr *mgr, fftask *task) {
	return task->sib.next != FFLIST_END || task->sib.prev != FFLIST_END
		|| mgr->tasks.first == &task->sib;
}

/** Add item into task queue.  Thread-safe. */
static FFINL void fftask_post(fftaskmgr *mgr, fftask *task) {
	FF_ASSERT(!fftask_active(mgr, task));
	fflk_lock(&mgr->lk);
	fflist_ins(&mgr->tasks, &task->sib);
	fflk_unlock(&mgr->lk);
}

#define fftask_post4(mgr, task, func, _param) \
do { \
	(task)->handler = func; \
	(task)->param = _param; \
	fftask_post(mgr, task); \
} while (0)

/** Remove item from task queue. */
static FFINL void fftask_del(fftaskmgr *mgr, fftask *task) {
	FF_ASSERT(fftask_active(mgr, task));
	fflk_lock(&mgr->lk);
	fflist_rm(&mgr->tasks, &task->sib);
	fflk_unlock(&mgr->lk);
}

/** Call a handler for each task. */
FF_EXTN void fftask_run(fftaskmgr *mgr);
