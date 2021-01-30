/** Linked list.
Copyright (c) 2013 Simon Zolin
*/

#pragma once

#include <FF/string.h>
#include <FF/chain.h>
#include <ffbase/list.h>


// obsolete
#define FFLIST_FOREACH(list, it)  FFCHAIN_WALK(&(list)->root, it)
typedef ffchain_item fflist_item;
#define fflist_ins  fflist_add
#define fflist_prepend  fflist_addfront
#define fflist_movetofront  fflist_movefront


typedef struct fflist1_item fflist1_item;
struct fflist1_item {
	fflist1_item *next;
};

typedef struct fflist1 {
	fflist1_item *first;
} fflist1;

static FFINL void fflist1_push(fflist1 *lst, fflist1_item *it)
{
	it->next = lst->first;
	lst->first = it;
}

static FFINL fflist1_item* fflist1_pop(fflist1 *lst)
{
	if (lst->first == NULL)
		return NULL;
	fflist1_item *it = lst->first;
	lst->first = it->next;
	return it;
}

/** Traverse a list.
@p: pointer to a structure containing @member_name. */
#define _FFLIST_WALK(lst, p, member_name) \
	for (p = (void*)(lst)->root.next \
		; p != (void*)fflist_sentl(lst) \
			&& ((p = (void*)((size_t)p - ((size_t)&p->member_name - (size_t)p))) || 1) \
		; p = (void*)p->member_name.next)

#define FFLIST_WALKSAFE(lst, p, li, nextitem) \
	for (p = (void*)(lst)->root.next \
		; p != (void*)fflist_sentl(lst) \
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
	fflist_item *li; \
	for (li = (lst)->root.next;  li != fflist_sentl(lst); ) { \
		void *p = FF_GETPTR(struct_name, member_name, li); \
		li = li->next; \
		func(p); \
	} \
} while (0)


/** Return TRUE if the item is added into the list. */
static FFINL ffbool fflist_exists(fflist *lst, fflist_item *it) {
	(void)lst;
	return it->next != NULL;
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
	// FFLIST_CUR_SAME
	// FFLIST_CUR_NEXT
	// FFLIST_CUR_PREV
	, FFLIST_CUR_NONEXT = 3
	, FFLIST_CUR_NOPREV = 4
};

/** Shift cursor.
@cmd: enum FFLIST_CUR
Return enum FFLIST_CUR. */
FF_EXTN uint fflist_curshift(fflist_cursor *cur, uint cmd, void *sentl);
