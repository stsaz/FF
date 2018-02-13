/**
Copyright (c) 2016 Simon Zolin
*/

/*
Empty chain:
SENTL <-> SENTL <-> ...

Chain with 2 items:
1 <-> 2 <-> SENTL <-> 1 <-> ...
*/

#pragma once


typedef struct ffchain_item ffchain_item;
struct ffchain_item {
	ffchain_item *next
		, *prev;
};

typedef ffchain_item ffchain;

#define ffchain_init(chain) \
	(chain)->next = (chain)->prev = (chain)

#define ffchain_sentl(chain)  (chain)
#define ffchain_first(chain)  ((chain)->next)
#define ffchain_last(chain)  ((chain)->prev)

#define ffchain_empty(chain)  (ffchain_first(chain) == ffchain_sentl(chain))

#define ffchain_add(chain, item)  ffchain_append(item, ffchain_last(chain))
#define ffchain_addfront(chain, item)  ffchain_prepend(item, ffchain_first(chain))

#define ffchain_rm(chain, item)  ffchain_unlink(item)

#define FFCHAIN_WALK(chain, it) \
	for (it = ffchain_first(chain);  it != ffchain_sentl(chain);  it = it->next)

#define FFCHAIN_FOR(chain, it) \
	for (it = ffchain_first(chain);  it != ffchain_sentl(chain);  )


/** L <-> R */
#define _ffchain_link2(L, R) \
do { \
	(L)->next = (R); \
	(R)->prev = (L); \
} while (0)

/** ... <-> AFTER (<-> NEW <->) 1 <-> ... */
static FFINL void ffchain_append(ffchain_item *item, ffchain_item *after)
{
	_ffchain_link2(item, after->next);
	_ffchain_link2(after, item);
}

/** ... <-> 2 (<-> NEW <->) BEFORE <-> ... */
static FFINL void ffchain_prepend(ffchain_item *item, ffchain_item *before)
{
	_ffchain_link2(before->prev, item);
	_ffchain_link2(item, before);
}

/** ... <-> 1 [<-> DEL <->] 2 <-> ... */
static FFINL void ffchain_unlink(ffchain_item *item)
{
	_ffchain_link2(item->prev, item->next);
	item->prev = item->next = NULL;
}

/** Split chain after the item.
... <-> ITEM (-> SENTL <-) 2 <-> ... */
static FFINL void ffchain_split(ffchain_item *it, void *sentl)
{
	it->next->prev = (ffchain_item*)sentl;
	it->next = (ffchain_item*)sentl;
}
