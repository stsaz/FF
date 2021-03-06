/** Timer queue.
Copyright (c) 2014 Simon Zolin
*/

#pragma once

#include <FFOS/timer.h>
#include <FF/rbtree.h>


typedef void (*fftmrq_handler)(void *param);

typedef struct fftmrq_entry {
	fftree_node8 tnode;
	int64 interval;
	fftmrq_handler handler;
	void *param;
} fftmrq_entry;

typedef struct fftimer_queue {
	fftmr tmr;
	uint64 msec_time;
	ffrbtree items; //fftmrq_entry[].  Note: 'items.sentl' is still fftree_node, not fftree_node8.
	ffkevent kev;
	uint started :1;
} fftimer_queue;

/** Initialize. */
FF_EXTN void fftmrq_init(fftimer_queue *tq);

/** Stop and destroy timer queue. */
FF_EXTN void fftmrq_stop(fftimer_queue *tq, fffd kq);
FF_EXTN void fftmrq_destroy(fftimer_queue *tq, fffd kq);

/** Return TRUE if a timer is in the queue. */
static FFINL ffbool fftmrq_active(fftimer_queue *tq, fftmrq_entry *t) {
	(void)tq;
	return (t->tnode.key != 0);
}

/** Add item to timer queue.
@interval: periodic if >0, one-shot if <0.*/
static FFINL void fftmrq_add(fftimer_queue *tq, fftmrq_entry *t, int64 interval) {
	t->tnode.key = tq->msec_time + ffabs(interval);
	t->interval = interval;
	ffrbt_insert(&tq->items, (ffrbt_node*)&t->tnode, NULL);
}

/** Remove item from timer queue. */
static FFINL void fftmrq_rm(fftimer_queue *tq, fftmrq_entry *t) {
	ffrbt_rm(&tq->items, (ffrbt_node*)&t->tnode);
	t->tnode.key = 0;
}

/** Start timer. */
FF_EXTN int fftmrq_start(fftimer_queue *tq, fffd kq, uint interval_ms);

#define fftmrq_started(tq)  ((tq)->started)

#define fftmrq_empty(tq)  ((tq)->items.len == 0)
