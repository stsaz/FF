/**
Copyright (c) 2014 Simon Zolin
*/

#include <FFOS/mem.h>
#include <FFOS/error.h>
#include <FF/hashtab.h>

#define hst_slot(ht, hash)  &((ht)->slots[((hash) % (ht)->nslots)])

#define hst_ext_size(nitems)  (sizeof(ffhst_ext) + (nitems) * sizeof(ffhst_item))

#define hst_slot_empty(slot)  ((slot)->item.val == NULL)

#define hst_slot_isext(slot)  ((slot)->empty == 0)

#define hst_slot_ext(slot)  ((slot)->ext)

#define hst_slot_setext(slot, ext)  (slot)->ext = (ext)

#define hst_ext_tail(ext)  ((ext)->items + (ext)->len)

int ffhst_init(ffhstab *ht, size_t nslots)
{
	ht->slots = ffmem_calloc(nslots, sizeof(ffhst_slot));
	if (ht->slots == NULL)
		return -1;

	ht->nslots = nslots;
	ht->len = 0;

#ifdef FFHST_DEBUG
	ht->ncoll = ht->maxcoll = 0;
#endif
	return 0;
}

void ffhst_free(ffhstab *ht)
{
	ffhst_slot *slot;
	void *end;

	if (ht->slots == NULL)
		return;

	end = ht->slots + ht->len;
	for (slot = ht->slots;  slot != end;  slot++) {
		if (!hst_slot_empty(slot)
			&& hst_slot_isext(slot))
			ffmem_free(hst_slot_ext(slot));
	}

	ffmem_free(ht->slots);
	ht->slots = NULL;
	ht->len = ht->nslots = 0;
}

int ffhst_ins(ffhstab *ht, uint hash, void *val)
{
	ffhst_slot *slot = hst_slot(ht, hash);
	ffhst_item *it;
	int r = 1;

	if (hash == 0 || val == NULL) {
		fferr_set(EINVAL);
		return -1;
	}

	if (hst_slot_empty(slot))
		it = &slot->item;

	else {
		ffhst_ext *ext;

		if (!hst_slot_isext(slot)) {
			//there is one item stored directly inside the slot
			ext = ffmem_alloc(hst_ext_size(2));
			if (ext == NULL)
				return -1;

			ext->items[0] = slot->item; //move item from the slot into a separate buffer
			slot->empty = 0;
			it = &ext->items[1];
			ext->len = 2;

		} else {
			ext = hst_slot_ext(slot);
			ext = ffmem_realloc(ext, hst_ext_size(ext->len + 1));
			if (ext == NULL)
				return -1;

			it = hst_ext_tail(ext);
			ext->len++;
		}

		hst_slot_setext(slot, ext);
		r = (int)ext->len;

#ifdef FFHST_DEBUG
		ht->ncoll++;
		if (ht->maxcoll < ext->len)
			ht->maxcoll = ext->len;
#endif
	}

	it->keyhash = hash;
	it->val = val;
	ht->len++;
	return r;
}

void * ffhst_find(const ffhstab *ht, uint hash, const char *key, size_t keylen, void *param)
{
	ffhst_slot *slot;
	ffhst_item *it, *tail;

	if (ht->len == 0)
		return NULL;

	slot = hst_slot(ht, hash);
	if (hst_slot_empty(slot))
		return NULL;

	if (!hst_slot_isext(slot)) {
		it = &slot->item;
		tail = it + 1;

	} else {
		ffhst_ext *ext = hst_slot_ext(slot);
		it = ext->items;
		tail = hst_ext_tail(ext);
	}

	for (;  it != tail;  it++) {
		if (it->keyhash == hash
			&& 0 == ht->cmpkey(it->val, key, keylen, param))
			return it->val;
	}

	return NULL;
}

int ffhst_walk(ffhstab *ht, ffhst_walk_func func, void *param)
{
	ffhst_slot *slot;
	ffhst_ext *ext;
	void *tail, *end;
	ffhst_item *it;
	int r;

	end = ht->slots + ht->len;
	for (slot = ht->slots;  slot != end;  slot++) {
		if (hst_slot_empty(slot))
			continue;

		if (!hst_slot_isext(slot)) {
			r = func(slot->item.val, param);
			if (r != 0)
				return r; //user has interrupted the processing
			continue;
		}

		ext = hst_slot_ext(slot);
		tail = hst_ext_tail(ext);
		for (it = ext->items;  it != tail;  it++) {
			r = func(it->val, param);
			if (r != 0)
				return r;
		}
	}

	return 0;
}
