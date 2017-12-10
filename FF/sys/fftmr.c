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
			if (((fftree_node8*)nod)->key < ((fftree_node8*)parent)->key)
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

void fftmrq_init(fftimer_queue *tq)
{
	tq->tmr = FF_BADTMR;
	ffrbt_init(&tq->items);
	tq->items.insnode = &tree_instimer;
	ffkev_init(&tq->kev);
	tq->kev.oneshot = 0;
	tq->kev.handler = &tmrq_onfire;
	tq->kev.udata = tq;

	fftime now;
	fftime_now(&now);
	tq->msec_time = fftime_ms(&now);
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
		fftime now;
		fftime_now(&now);
		tq->msec_time = fftime_ms(&now);
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
	fftime_now(&now);

	tq->msec_time = fftime_ms(&now);

	while (tq->items.len != 0) {
		nod = fftree_min((fftree_node*)tq->items.root, &tq->items.sentl);
		ent = FF_GETPTR(fftmrq_entry, tnode, nod);
		if (((fftree_node8*)nod)->key > tq->msec_time)
			break;

		fftmrq_rm(tq, ent);
		if (ent->interval != 0)
			fftmrq_add(tq, ent, ent->interval);

		FFDBG_PRINTLN(10, "%U.%06u: %p, interval:%u  key:%U [%L]"
			, (int64)fftime_sec(&now), (int)fftime_usec(&now)
			, ent, ent->interval, ((fftree_node8*)nod)->key, tq->items.len);

		ent->handler(&now, ent->param);
	}

	fftmr_read(tq->tmr);
}
