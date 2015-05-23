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

#define fflist_sentl(lst)  FFLIST_END

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

#define fflist_empty(lst)  ((lst)->first == fflist_sentl(lst))

/** Traverse a list.
@p: pointer to a structure containing @member_name. */
#define FFLIST_WALK(lst, p, member_name) \
	for (p = (void*)(lst)->first \
		; p != fflist_sentl(lst) \
			&& ((p = (void*)((size_t)p - ((size_t)&p->member_name - (size_t)p))) || 1) \
		; p = (void*)p->member_name.next)

#define FFLIST_WALKSAFE(lst, p, li, nextitem) \
	for (p = (void*)(lst)->first \
		; p != fflist_sentl(lst) \
			&& ((nextitem = ((fflist_item*)p)->next) || 1) \
			&& ((p = (void*)((size_t)p - ((size_t)&p->li - (size_t)p))) || 1) \
		; p = (void*)nextitem)

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


/** Return TRUE if the item is added into the list. */
static FFINL ffbool fflist_exists(fflist *lst, fflist_item *it) {
	return it->next != FFLIST_END || it->prev != FFLIST_END
		|| lst->first == it;
}

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


typedef fflist_item* fflist_cursor;

enum FFLIST_CUR {
	FFLIST_CUR_SAME = 0
	, FFLIST_CUR_NEXT = 1
	, FFLIST_CUR_PREV = 2

	, FFLIST_CUR_RM = 0x10 //remove this
	, FFLIST_CUR_RMPREV = 0x20 //remove all previous
	, FFLIST_CUR_RMNEXT = 0x40 //remove all next
	, FFLIST_CUR_RMFIRST = 0x80 //remove if the first

	, FFLIST_CUR_BOUNCE = 0x100 //go back if FFLIST_CUR_NEXT is set and it's the last item in chain
	, FFLIST_CUR_SAMEIFBOUNCE = 0x200 //stay at the same position if a bounce occurs

	//return codes:
	, FFLIST_CUR_NONEXT = 3
	, FFLIST_CUR_NOPREV = 4
};

/** Shift cursor.
@cmd: enum FFLIST_CUR
@sentl: note: only NULL is supported
Return enum FFLIST_CUR. */
FF_EXTN uint fflist_curshift(fflist_cursor *cur, uint cmd, void *sentl);
