/** Hash table.
Open addressing (one solid memory region).  Slots number is a power of 2.
Resolve collisions via linear probing.
Copyright (c) 2014 Simon Zolin
*/

/*
[0] .
[1] .
[2] item1 keyhash=0x..2
[3] item2 keyhash=0x..2 (collision)
[4] item3 keyhash=0x..3
...
[7] item4 keyhash=0x..7
*/

#pragma once

#include <FF/array.h>


struct ffhst_slot;

/** Hash table. */
typedef struct ffhstab {
	size_t len; //number of items
	size_t nslots; //number of slots
	size_t slot_mask;
	struct ffhst_slot *slots;

	/** Compare key.
	@param: opaque data passed to ffhst_find()
	Return 0 if equal. */
	int (*cmpkey)(void *val, const void *key, void *param);

#ifdef FFHST_DEBUG
	size_t ncoll; //total number of collisions
	size_t maxcoll; //maximum collisions in one slot
#endif
} ffhstab;

/** Init hash table.
Return 0 on success. */
FF_EXTN int ffhst_init(ffhstab *ht, size_t items);

/** Free hash table. */
FF_EXTN void ffhst_free(ffhstab *ht);

/** Add new item.
Return the total number of items in the same slot.
Return -1 on error. */
FF_EXTN int ffhst_ins(ffhstab *ht, uint hash, void *val);

/**
Return the element pointer;  NULL if not found. */
FF_EXTN void* ffhst_find_el(const ffhstab *ht, uint hash, const void *key, void *param);

/**
Return the element's value. */
FF_EXTN void* ffhst_value(const void *element);

/** Find an item.
After an element with the same hash is found, ffhstab.cmpkey() is called to compare the keys.
Return the element's value;  NULL if not found. */
static FFINL void* ffhst_find(const ffhstab *ht, uint hash, const void *key, void *param)
{
	const void *it = ffhst_find_el(ht, hash, key, param);
	if (it == NULL)
		return NULL;
	return ffhst_value(it);
}

/** Return 0 to continue the walk. */
typedef int (*ffhst_walk_func)(void *val, void *param);

/** Walk through all items in hash table. */
FF_EXTN int ffhst_walk(ffhstab *ht, ffhst_walk_func func, void *param);

/** Print hash table content.
@dst: optional */
FF_EXTN void ffhst_print(ffhstab *ht, ffarr *dst);
