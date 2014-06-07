/**
Copyright (c) 2014 Simon Zolin
*/

#include <FFOS/time.h>
#include <FFOS/file.h>
#include <FF/filemap.h>
#include <FF/timer-queue.h>
#include <FF/taskqueue.h>


static void tmrq_onfire(void *t);
static void tree_instimer(fftree_node *nod, fftree_node **root, void *sentl);


#define ff_align_floor(n, bound)  ((n) & ~((bound) - 1))

void fffile_mapclose(fffilemap *fm)
{
	if (fm->map != NULL)
		(void)ffmap_unmap(fm->map, fm->mapsz);
	if (fm->hmap != 0)
		ffmap_close(fm->hmap);
	fffile_mapinit(fm);
}

int fffile_mapbuf(fffilemap *fm, ffstr *dst)
{
	size_t off = fm->foff & (fm->blocksize - 1);

	if (fm->map == NULL) {
		uint64 effoffs;
		size_t size = (size_t)ffmin64(fm->blocksize, off + fm->fsize);

		if (fm->hmap == 0) {
			fm->hmap = ffmap_create(fm->fd, 0, FFMAP_PAGEREAD);
			if (fm->hmap == 0)
				return 1;
		}

		effoffs = ff_align_floor(fm->foff, fm->blocksize);
		fm->map = ffmap_open(fm->hmap, effoffs, size, PROT_READ, MAP_SHARED);
		if (fm->map == NULL)
			return 1;
		fm->mapoff = effoffs;
		fm->mapsz = size;
	}

	dst->ptr = fm->map + off;
	dst->len = (size_t)ffmin64(fm->mapsz - off, fm->fsize);
	return 0;
}

int fffile_mapshift(fffilemap *fm, ssize_t by)
{
	FF_ASSERT(fm->fsize >= by);
	fm->fsize -= by;
	fm->foff += by;

	if (fm->fsize != 0) {
		if (fm->foff == fm->mapoff + fm->mapsz) {
			(void)ffmap_unmap(fm->map, fm->mapsz);
			fm->mapoff = 0;
			fm->mapsz = 0;
			fm->map = NULL;
		}
		return 1;
	}

	fffile_mapclose(fm);
	return 0;
}


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
	ffaio_init(&tq->task);
	tq->task.oneshot = 0;
	tq->task.rhandler = &tmrq_onfire;
	tq->task.udata = tq;

	{
		fftime now;
		fftime_now(&now);
		tq->msec_time = fftime_ms(&now);
	}
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

#ifdef FFDBG_TIMER
		ffdbg_print(0, "%s(): %u.%06u: %p, interval:%u\n"
			, FF_FUNC, now.s, now.mcs, ent, ent->interval);
#endif

		ent->handler(&now, ent->param);
	}

	fftmr_read(tq->tmr);
}


void fftask_run(fftaskmgr *mgr)
{
	while (mgr->tasks.len != 0) {
		fftask *task = FF_GETPTR(fftask, sib, mgr->tasks.first);

#ifdef FFDBG_TASKS
		ffdbg_print(0, "%s(): [%L] %p handler=%p, param=%p\n"
			, FF_FUNC, mgr->tasks.len, task, task->handler, task->param);
#endif

		fflist_rm(&mgr->tasks, &task->sib);

		task->handler(task->param);
	}
}
