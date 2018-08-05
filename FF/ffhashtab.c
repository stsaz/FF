/**
Copyright (c) 2014 Simon Zolin
*/

#include <FFOS/mem.h>
#include <FFOS/error.h>
#include <FF/hashtab.h>


#define _FFHST_SLOT_ITEMS  1
#define _FFHST_HIWAT  75

typedef struct ffhst_item {
	uint keyhash;
	uint flags;
	void *val;
} ffhst_item;

struct ffhst_slot {
	ffhst_item item[_FFHST_SLOT_ITEMS];
};


#define hst_slot(ht, hash)  ((hash) & (ht)->slot_mask)
#define hst_slot_item(ht, slot)  (&(ht)->slots[slot].item[0])
#define hst_slot_empty(ht, slot)  ((ht)->slots[slot].item[0].flags == 0)
#define hst_slot_setbusy(ht, slot)  ((ht)->slots[slot].item[0].flags = 1)

static size_t hst_slot_collnext(const ffhstab *ht, size_t slot)
{
	return (slot + 1) & ht->slot_mask;
}

int ffhst_init(ffhstab *ht, size_t items)
{
	size_t n = items / _FFHST_SLOT_ITEMS + 1;
	ht->nslots = ff_align_power2(n);
	if (ht->nslots * _FFHST_HIWAT / 100 < n)
		ht->nslots = ff_align_power2(ht->nslots + 1);

	ht->slots = ffmem_calloc(ht->nslots, sizeof(struct ffhst_slot));
	if (ht->slots == NULL)
		return -1;

	ht->slot_mask = ht->nslots - 1;
	ht->len = 0;

#ifdef FFHST_DEBUG
	ht->ncoll = ht->maxcoll = 0;
#endif
	return 0;
}

void ffhst_free(ffhstab *ht)
{
	ffmem_free(ht->slots);
	ht->slots = NULL;
	ht->len = ht->nslots = 0;
}

int ffhst_ins(ffhstab *ht, uint hash, void *val)
{
	size_t slot = hst_slot(ht, hash);
	ffhst_item *it;
	uint r = 1;

	if (ht->len + 1 > ht->nslots)
		return -1;

	if (!hst_slot_empty(ht, slot)) {
		for (;;) {
			if (hst_slot_empty(ht, slot))
				break;
			r++;
			slot = hst_slot_collnext(ht, slot);
		}

		(void)r;
#ifdef FFHST_DEBUG
		ht->ncoll++;
		if (ht->maxcoll < r)
			ht->maxcoll = r;
#endif
	}

	it = hst_slot_item(ht, slot);
	hst_slot_setbusy(ht, slot);
	it->keyhash = hash;
	it->val = val;
	ht->len++;
	return r;
}

static int hst_cmpkey(const ffhstab *ht, const ffhst_item *it, const void *key, void *param)
{
	return ht->cmpkey(it->val, key, param);
}

void* ffhst_find_el(const ffhstab *ht, uint hash, const void *key, void *param)
{
	size_t slot = hst_slot(ht, hash);

	for (;;) {
		if (hst_slot_empty(ht, slot))
			break;

		const ffhst_item *it = hst_slot_item(ht, slot);
		if (it->keyhash == hash
			&& 0 == hst_cmpkey(ht, it, key, param))
			return (void*)it;

		slot = hst_slot_collnext(ht, slot);
	}

	return NULL;
}

void* ffhst_value(const void *element)
{
	const ffhst_item *it = element;
	return it->val;
}

int ffhst_walk(ffhstab *ht, ffhst_walk_func func, void *param)
{
	size_t slot;
	int r;

	for (slot = 0;  slot != ht->nslots;  slot++) {
		if (hst_slot_empty(ht, slot))
			continue;

		const ffhst_item *it = hst_slot_item(ht, slot);
		r = func(it->val, param);
		if (r != 0)
			return r; //user has interrupted the processing
	}

	return 0;
}

void ffhst_print(ffhstab *ht, ffarr *dst)
{
	size_t slot;
	ffarr a = {0};
	size_t ncoll = 0, maxcoll = 0, empty = 0;

	ffarr_alloc(&a, ht->nslots * FFSLEN("[000] = 00000000\n"));

	ffstr_catfmt(&a, "hst:%p  len:%L  items:\n"
		, ht, ht->len);

	for (slot = 0;  slot != ht->nslots;  slot++) {
		ffstr_catfmt(&a, "[%03u]"
			, (int)slot);
		if (hst_slot_empty(ht, slot)) {
			empty++;
			ffstr_catfmt(&a, " .\n");
			continue;
		}

		const ffhst_item *it = hst_slot_item(ht, slot);
		ffstr_catfmt(&a, " %08xu = %p\n", it->keyhash, it->val);
		if (slot != hst_slot(ht, it->keyhash))
			ncoll++;
	}

	ffstr_catfmt(&a, "used:%L/%L  ncoll:%L  maxcoll:%L\n"
		, ht->nslots - empty, ht->nslots, ncoll, maxcoll);
	if (dst == NULL) {
		FFDBG_PRINTLN(0, "%S", &a);
		ffarr_free(&a);
	} else
		ffarr_acq(dst, &a);
}
