/** Linked list.
Copyright (c) 2013 Simon Zolin
*/

#pragma once

#include <FFOS/types.h>


#define FFLIST_END  NULL

typedef struct fflist_item fflist_item;
struct fflist_item {
	fflist_item *next
		, *prev;
};

/** Insert a new item into the chain. */
FF_EXTN void fflist_link(fflist_item *it, fflist_item *after);

/** Remove item from the chain. */
FF_EXTN void fflist_unlink(fflist_item *it);

/** Walk through a chain. */
#define FFLIST_WALKNEXT(begin, li) \
	for (li = (begin);  li != FFLIST_END;  li = li->next)

/** Walk through a chain safely. */
#define FFLIST_WALKNEXTSAFE(begin, li, nextitem) \
	for (li = (begin) \
		; li != FFLIST_END && ((nextitem = li->next) || 1) \
		; li = nextitem)


typedef struct fflist {
	size_t len;
	fflist_item *first
		, *last;
} fflist;

/** Initialize container. */
static FFINL void fflist_init(fflist *ls) {
	ls->len = 0;
	ls->first = ls->last = FFLIST_END;
}

/** Traverse a list. */
#define FFLIST_WALK(lst, li) \
	FFLIST_WALKNEXT((lst)->first, li)

/** Add item to the end. */
FF_EXTN void fflist_ins(fflist *lst, fflist_item *it);

/** Add item to the beginning. */
FF_EXTN void fflist_prepend(fflist *lst, fflist_item *it);

/** Remove item. */
FF_EXTN void fflist_rm(fflist *lst, fflist_item *it);

/** Move the item to the beginning. */
static FFINL void fflist_movetofront(fflist *lst, fflist_item *it) {
	fflist_rm(lst, it);
	fflist_prepend(lst, it);
}

/** Move the item to the end. */
static FFINL void fflist_moveback(fflist *lst, fflist_item *it) {
	fflist_rm(lst, it);
	fflist_ins(lst, it);
}
