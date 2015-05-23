/**
Copyright (c) 2013 Simon Zolin
*/

#include <FF/list.h>


void fflist_link(fflist_item *it, fflist_item *after)
{
	if (after->next != FFLIST_END) {
		after->next->prev = it;
		it->next = after->next;

	} else
		it->next = FFLIST_END;
	after->next = it;
	it->prev = after;
}

void fflist_unlink(fflist_item *it)
{
	if (it->prev != FFLIST_END)
		it->prev->next = it->next; // rebind left
	if (it->next != FFLIST_END)
		it->next->prev = it->prev; // rebind right
	it->prev = it->next = FFLIST_END;
}

void fflist_ins(fflist *lst, fflist_item *it)
{
	if (lst->first != FFLIST_END) {
		lst->last->next = it;
		it->prev = lst->last;

	} else {
		it->prev = FFLIST_END;
		lst->first = it;
	}
	lst->last = it;
	it->next = FFLIST_END;
	lst->len++;
}

void fflist_prepend(fflist *lst, fflist_item *it)
{
	if (lst->last != FFLIST_END) {
		lst->first->prev = it;
		it->next = lst->first;

	} else {
		it->next = FFLIST_END;
		lst->last = it;
	}
	lst->first = it;
	it->prev = FFLIST_END;
	lst->len++;
}

void fflist_rm(fflist *lst, fflist_item *it)
{
	FF_ASSERT(lst->len != 0);
	if (it->prev != FFLIST_END)
		it->prev->next = it->next;
	else
		lst->first = it->next;

	if (it->next != FFLIST_END)
		it->next->prev = it->prev;
	else
		lst->last = it->prev;
	it->next = it->prev = FFLIST_END;
	lst->len--;
}


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
		fflist_unlink(c);

	return r;
}
