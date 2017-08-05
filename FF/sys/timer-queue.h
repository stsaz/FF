/** Timer queue.
Copyright (c) 2014 Simon Zolin
*/

#pragma once

#include <FFOS/timer.h>
#include <FF/rbtree.h>


typedef void (*fftmrq_handler)(const fftime *now, void *param);

typedef struct fftmrq_entry {
	fftree_node8 tnode;
	uint64 interval;
	fftmrq_handler handler;
	void *param;
} fftmrq_entry;

typedef struct fftimer_queue {
	fftmr tmr;
	uint64 msec_time;
	ffrbtree items; //fftmrq_entry[].  Note: 'items.sentl' is still fftree_node, not fftree_node8.
	ffkevent kev;
} fftimer_queue;

/** Initialize. */
FF_EXTN void fftmrq_init(fftimer_queue *tq);

/** Stop and destroy timer queue. */
FF_EXTN void fftmrq_destroy(fftimer_queue *tq, fffd kq);

/** Return TRUE if a timer is in the queue. */
static FFINL ffbool fftmrq_active(fftimer_queue *tq, fftmrq_entry *t) {
	return (t->tnode.key != 0);
}

/** Add item to timer queue.
@interval: periodic if >0, one-shot if <0.*/
static FFINL void fftmrq_add(fftimer_queue *tq, fftmrq_entry *t, int64 interval) {
	t->tnode.key = tq->msec_time + ffabs(interval);
	t->interval = (interval > 0) ? interval : 0;
	ffrbt_insert(&tq->items, (ffrbt_node*)&t->tnode, NULL);
}

/** Remove item from timer queue. */
static FFINL void fftmrq_rm(fftimer_queue *tq, fftmrq_entry *t) {
	ffrbt_rm(&tq->items, (ffrbt_node*)&t->tnode);
	t->tnode.key = 0;
}

/** Start timer. */
static FFINL int fftmrq_start(fftimer_queue *tq, fffd kq, uint interval_ms) {
	tq->tmr = fftmr_create(0);
	if (tq->tmr == FF_BADTMR)
		return 1;
	return fftmr_start(tq->tmr, kq, ffkev_ptr(&tq->kev), interval_ms);
}

#define fftmrq_started(tq)  ((tq)->tmr != FF_BADTMR)

#define fftmrq_empty(tq)  ((tq)->items.len == 0)
