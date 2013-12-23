/** Linked list.
Copyright (c) 2013 Simon Zolin
*/

#pragma once

#include <FFOS/types.h>


/** Get structure pointer by the pointer to its member. */
#define FF_STRUCTPTR(struct_type, func_name, item_type, item_name) \
	static FFINL struct_type * func_name(item_type *item) { \
		return (struct_type *)((byte *)item - FFOFF(struct_type, item_name)); \
	}

typedef struct fflist_item fflist_item;
struct fflist_item {
	fflist_item *next
		, *prev;
};

/** Insert a new item into the chain. */
FF_EXTN void fflist_link(fflist_item *it, fflist_item *after);

/** Remove item from the chain. */
FF_EXTN void fflist_unlink(fflist_item *it);

typedef struct {
	size_t len;
	fflist_item *first
		, *last;
} fflist;

#define FFLIST_END  NULL

/** Initialize container. */
static FFINL void fflist_init(fflist *ls) {
	ls->len = 0;
	ls->first = ls->last = FFLIST_END;
}

#define FFLIST_WALKNEXT(begin, end, it) \
	for ((it) = (begin);  (it) != (end);  (it) = (it)->next)

/** Traverse a list. */
#define FFLIST_WALK(lst, li) \
	FFLIST_WALKNEXT((lst)->first, FFLIST_END, li)

/** Safely get the next item. */
static FFINL void _fflist_next(fflist_item **it, const fflist_item *end) {
	if (*it != end)
		*it = (*it)->next;
}

#define FFLIST_WALKSAFE(begin, end, iter, li) \
	for ((li) = (iter) = (begin), _fflist_next(&(iter), end) \
		; (li) != (end) \
		; (li) = (iter), _fflist_next(&(iter), end))

#define FFLIST_DESTROY(lst, on_destroy, get_struct_ptr) \
do { \
	fflist_item *iter, *li; \
	FFLIST_WALKSAFE((lst)->first, FFLIST_END, iter, li) { \
		on_destroy(get_struct_ptr(li)); \
	} \
	(lst)->len = 0; \
} while (0)

/** Add item to the end. */
FF_EXTN void fflist_ins(fflist *lst, fflist_item *it);

/** Add item to the beginning. */
FF_EXTN void fflist_prepend(fflist *lst, fflist_item *it);

/** Remove item. */
FF_EXTN void fflist_rm(fflist *lst, fflist_item *it);

/** Move the item to the beginning. */
static FFINL void fflist_makefirst(fflist *lst, fflist_item *it) {
	fflist_rm(lst, it);
	fflist_prepend(lst, it);
}

/** Move the item to the end. */
static FFINL void fflist_makelast(fflist *lst, fflist_item *it) {
	fflist_rm(lst, it);
	fflist_ins(lst, it);
}
