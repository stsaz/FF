/** Hash table.
Copyright (c) 2014 Simon Zolin
*/

#pragma once

#include <FFOS/types.h>


typedef struct ffhst_item {
	uint keyhash;
	void *val;
} ffhst_item;

/** Storage for items in the same slot. */
typedef struct ffhst_ext {
	size_t len; //number of items
	ffhst_item items[0];
} ffhst_ext;

/** Slot in a hash table. */
typedef union ffhst_slot {
	struct {
		uint empty;
		ffhst_ext *ext;
	};
	ffhst_item item; //inplace item
} ffhst_slot;

/** Hash table. */
typedef struct ffhstab {
	size_t len; //number of items
	size_t nslots; //number of slots
	ffhst_slot *slots;

	/** Compare key.
	@param: opaque data passed to ffhst_find()
	Return 0 if equal. */
	int (*cmpkey)(void *val, const char *key, size_t keylen, void *param);

#ifdef _DEBUG
	size_t ncoll; //total number of collisions
	size_t maxcoll; //maximum collisions in one slot
#endif
} ffhstab;

/** Init hash table.
Return 0 on success. */
FF_EXTN int ffhst_init(ffhstab *ht, size_t nslots);

/** Free hash table. */
FF_EXTN void ffhst_free(ffhstab *ht);

/** Add new item.
Return the total number of items in the same slot.
Return -1 on error. */
FF_EXTN int ffhst_ins(ffhstab *ht, uint hash, void *val);

/** Find an item.
Return 0 if not found. */
FF_EXTN void * ffhst_find(const ffhstab *ht, uint hash, const char *key, size_t keylen, void *param);

/** Return 0 to continue the walk. */
typedef int (*ffhst_walk_func)(void *val, void *param);

/** Walk through all items in hash table. */
FF_EXTN int ffhst_walk(ffhstab *ht, ffhst_walk_func func, void *param);
