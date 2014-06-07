/** User task queue.
Copyright (c) 2013 Simon Zolin
*/

#pragma once

#include <FF/list.h>


typedef void (*fftask_handler)(void *param);

typedef struct fftask {
	fftask_handler handler;
	void *param;
	fflist_item sib;
} fftask;

typedef struct fftaskmgr {
	fflist tasks; //fftask[]
} fftaskmgr;

/** Return TRUE if a task is in the queue. */
static FFINL ffbool fftask_active(fftaskmgr *mgr, fftask *task) {
	return task->sib.next != FFLIST_END || task->sib.prev != FFLIST_END
		|| mgr->tasks.first == &task->sib;
}

/** Add item into task queue. */
static FFINL void fftask_post(fftaskmgr *mgr, fftask *task) {
	FF_ASSERT(!fftask_active(mgr, task));
	fflist_ins(&mgr->tasks, &task->sib);
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
	fflist_rm(&mgr->tasks, &task->sib);
}

/** Call a handler for each task. */
FF_EXTN void fftask_run(fftaskmgr *mgr);
