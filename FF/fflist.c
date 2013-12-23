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
