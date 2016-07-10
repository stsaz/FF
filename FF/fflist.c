/**
Copyright (c) 2013 Simon Zolin
*/

#include <FF/list.h>


uint fflist_curshift(fflist_cursor *cur, uint cmd, void *sentl)
{
	const fflist_cursor c = *cur;
	uint r;
	FF_ASSERT((cmd & (FFLIST_CUR_NEXT | FFLIST_CUR_PREV)) != (FFLIST_CUR_NEXT | FFLIST_CUR_PREV));

	if (cmd & FFLIST_CUR_RMPREV)
		c->prev = sentl;
	if (cmd & FFLIST_CUR_RMNEXT)
		c->next = sentl;

	if (cmd & FFLIST_CUR_NEXT) {
		if (c->next != sentl) {
			*cur = c->next;
			r = FFLIST_CUR_NEXT;
		} else if ((cmd & (FFLIST_CUR_BOUNCE | FFLIST_CUR_SAMEIFBOUNCE)) == (FFLIST_CUR_BOUNCE | FFLIST_CUR_SAMEIFBOUNCE)) {
			r = FFLIST_CUR_SAME;
		} else if ((cmd & FFLIST_CUR_BOUNCE) && c->prev != sentl) {
			*cur = c->prev;
			r = FFLIST_CUR_PREV;
		} else
			r = FFLIST_CUR_NONEXT;

	} else if (cmd & FFLIST_CUR_PREV) {
		if (c->prev != sentl) {
			*cur = c->prev;
			r = FFLIST_CUR_PREV;
		} else
			r = FFLIST_CUR_NOPREV;

	} else
		r = FFLIST_CUR_SAME;

	if ((cmd & FFLIST_CUR_RM)
		|| ((cmd & FFLIST_CUR_RMFIRST) && c->prev == sentl))
		ffchain_unlink(c);

	return r;
}
