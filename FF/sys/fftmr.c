/**
Copyright (c) 2017 Simon Zolin
*/

#include <FF/sys/timer-queue.h>


static void tmrq_onfire(void *t);
static void tree_instimer(fftree_node *nod, fftree_node **root, void *sentl);


static void tree_instimer(fftree_node *nod, fftree_node **root, void *sentl)
{
	if (*root == sentl) {
		*root = nod; // set root node
		nod->parent = sentl;

	} else {
		fftree_node **pchild;
		fftree_node *parent = *root;

		// find parent node and the pointer to its left/right node
		for (;;) {
			if ((int64)((fftree_node8*)nod)->key < (int64)((fftree_node8*)parent)->key)
				pchild = &parent->left;
			else
				pchild = &parent->right;

			if (*pchild == sentl)
				break;
			parent = *pchild;
		}

		*pchild = nod; // set parent's child
		nod->parent = parent;
	}

	nod->left = nod->right = sentl;
}

/** Set the current clock value. */
static void tmrq_update(fftimer_queue *tq)
{
	fftime now;
	ffclk_gettime(&now);
	tq->msec_time = fftime_ms(&now);
}

void fftmrq_init(fftimer_queue *tq)
{
	tq->tmr = FF_BADTMR;
	ffrbt_init(&tq->items);
	tq->items.insnode = &tree_instimer;
	ffkev_init(&tq->kev);
	tq->kev.oneshot = 0;
	tq->kev.handler = &tmrq_onfire;
	tq->kev.udata = tq;

	tmrq_update(tq);
}

int fftmrq_start(fftimer_queue *tq, fffd kq, uint interval_ms)
{
	if (tq->tmr == FF_BADTMR) {
		if (FF_BADTMR == (tq->tmr = fftmr_create(0)))
			return 1;
	}

	int r = fftmr_start(tq->tmr, kq, ffkev_ptr(&tq->kev), interval_ms);
	if (r == 0) {
		tq->started = 1;
		tmrq_update(tq);
	}
	return r;
}

void fftmrq_stop(fftimer_queue *tq, fffd kq)
{
	if (tq->tmr != FF_BADTMR)
		fftmr_stop(tq->tmr, kq);
	tq->started = 0;
}

void fftmrq_destroy(fftimer_queue *tq, fffd kq)
{
	ffrbt_init(&tq->items);
	if (tq->tmr != FF_BADTMR) {
		fftmr_close(tq->tmr, kq);
		tq->tmr = FF_BADTMR;
		ffkev_fin(&tq->kev);
	}
	tq->started = 0;
}

static void tmrq_onfire(void *t)
{
	fftimer_queue *tq = t;
	fftree_node *nod;
	fftmrq_entry *ent;
	fftime now;
	uint64 next;

	ffclk_gettime(&now);
	tq->msec_time = fftime_ms(&now);

	while (!ffrbt_empty(&tq->items)) {
		nod = fftree_min((fftree_node*)tq->items.root, &tq->items.sentl);
		ent = FF_GETPTR(fftmrq_entry, tnode, nod);
		uint64 key = ((fftree_node8*)nod)->key;
		if ((int64)tq->msec_time < (int64)key)
			break;

		if (ent->interval <= 0)
			fftmrq_rm(tq, ent);
		else {
			fftmrq_rm(tq, ent);
			ent->tnode.key = ffmax(key + ffabs(ent->interval), tq->msec_time + 1);
			ffrbt_insert(&tq->items, (ffrbt_node*)&ent->tnode, NULL);
		}
		next = ent->tnode.key;
		(void)next;

		FFDBG_PRINTLN(FFDBG_TIMER | 5, "%U.%06u: %p, interval:%D  key:%U  next:%U [%L]"
			, (int64)fftime_sec(&now), (int)fftime_usec(&now)
			, ent, ent->interval, key, next, tq->items.len);

		ent->handler(ent->param);
	}

	fftmr_read(tq->tmr);
}
