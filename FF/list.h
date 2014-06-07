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

/** Traverse a list.
@p: pointer to a structure containing @member_name. */
#define FFLIST_WALK(lst, p, member_name) \
	for (p = (void*)(lst)->first \
		; p != FFLIST_END && NULL != (p = (void*)((size_t)p - ((size_t)&p->member_name - (size_t)p))) \
		; p = (void*)p->member_name.next)

/** Call func() for every item in list.
A list item pointer is translated (by offset in a structure) into an object.
Example:
fflist mylist;
struct mystruct_t {
	...
	fflist_item sibling;
};
FFLIST_ENUMSAFE(&mylist, ffmem_free, struct mystruct_t, sibling); */
#define FFLIST_ENUMSAFE(lst, func, struct_name, member_name) \
do { \
	fflist_item *iter, *li; \
	FFLIST_WALKNEXTSAFE((lst)->first, li, iter) { \
		func(FF_GETPTR(struct_name, member_name, li)); \
	} \
} while (0)


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
