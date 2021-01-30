/**
Copyright 2019 Simon Zolin. */

#include <FF/cache.h>
#include <FF/rbtree.h>
#include <FF/list.h>
#include <FF/crc.h>
#include <FF/array.h>


enum {
	MAX_KEYLEN = 64*1024,
	DATA_S_SIZE = sizeof(void*),
};

struct ffcache {
	ffrbtree items;
	fflist lastused;
	size_t memsize; //length of keys and data
	ffcache_conf conf;
	struct ffcache_stat stat;
};

/** Keys are shared within a multi-item context.
Every such item holds a reference to the instance of this struct. */
typedef struct cach_key {
	size_t len; //length of 'd[]'
	uint usage;
	char d[0];
} cach_key;

typedef struct item {
	ffcache *c;
	ffrbtl_node rbtnod;

	fflist_item lastused_li;

	cach_key *ckey;

	ffstr data;
	char data_s[DATA_S_SIZE]; //static data, saves the call to mem_alloc()

	fftmrq_entry tmr; //expiration timer
	uint usage; //the number of external references
	uint unlinked :1; //set when the item is no longer referenced by the cache
} item;

static uint item_tmrreset(item *cit, uint expire);
static int item_copydata(item *cit, const ffcache_item *ci);
static void item_fill(ffcache_item *ci, const item *cit);
static int rm_unused_one(ffcache *c);
static int rm_unused_mem(ffcache *c, size_t memneeded);
static void item_rlz(ffcache *c, item *cit);
static void item_fin(ffcache *c, item *cit);
static void item_free(item *cit);

/** Return TRUE if hash is not set. */
#define KEYHASH_EMPTY(hash)  ((hash)[0] == 0)

#define KEYHASH_SET(hash, key, len, key_icase) \
	(*(hash) = (key_icase) ? ffcrc32_iget(key, len) : ffcrc32_get(key, len))

static cach_key* key_alloc(const char *key, size_t len, int key_icase);
static ffbool key_equal(const cach_key *ckey, const char *key, size_t len, int key_icase);
static void key_ref(cach_key *ckey);
static size_t key_unref(cach_key *ckey);


/** Error strings for enum FSV_CACH_E. */
static const char *const cach_serr[] = {
	"", //FFCACHE_OK
	"system", //FFCACHE_ESYS
	"already exists", //FFCACHE_EEXISTS
	"not found", //FFCACHE_ENOTFOUND
	"key hash collision", //FFCACHE_ECOLL
	"items number limit", //FFCACHE_ENUMLIMIT
	"memory limit", //FFCACHE_EMEMLIMIT
	"size limit", //FFCACHE_ESZLIMIT
	"locked", //FFCACHE_ELOCKED
};

const char * ffcache_errstr(uint code)
{
	FF_ASSERT(code < FFCNT(cach_serr));
	return cach_serr[code];
}


static void timer_empty(fftmrq_entry *tmr, uint value_ms)
{}
static int onchange_empty(ffcache *c, ffcache_item *ci, uint flags)
{
	return 0;
}

void ffcache_conf_init(ffcache_conf *conf)
{
	ffmem_tzero(conf);

	conf->timer = &timer_empty;
	conf->onchange = &onchange_empty;

	conf->max_items = 64 * 1000;
	conf->mem_limit = 200 * 1024 * 1024;
	conf->max_data = 1 * 1024 * 1024;
	conf->def_expire = 1 * 60 * 60;
	conf->max_expire = 24 * 60 * 60;
}

ffcache* ffcache_create(const ffcache_conf *conf)
{
	ffcache *c = ffmem_new(ffcache);
	if (c == NULL)
		return NULL;
	ffrbt_init(&c->items);
	fflist_init(&c->lastused);
	c->conf = *conf;
	return c;
}

void* ffcache_udata(ffcache *c)
{
	return c->conf.udata;
}

void ffcache_stat(ffcache *c, struct ffcache_stat *stat)
{
	*stat = c->stat;
	stat->items = c->items.len;
	stat->memsize = c->memsize;
}

static void onclear(void *obj)
{
	item *cit = obj;
	item_rlz(cit->c, cit);
}

/**
The function is only safe if ffrbtl_rm() is called before ffmem_free(). */
static void ffrbtl_enumsafe(ffrbtree *tr, ffrbt_free_t func, size_t off)
{
	ffrbt_node *n, *next;
	ffrbtl_node *nl;
	fflist_item *li;

	FFTREE_FOR(tr, n) {
		nl = (void*)n;
		FFCHAIN_FOR(&nl->sib, li) {
			void *n2 = FF_PTR(ffrbtl_nodebylist(li), -(ssize_t)off);
			li = li->next;
			func(n2);
		}
		next = ffrbt_successor(tr, n);
		void *p = FF_PTR(n, -(ssize_t)off);
		func(p);
		n = next;
	}
}

void ffcache_reset(ffcache *c)
{
	ffrbtl_enumsafe(&c->items, &onclear, FFOFF(item, rbtnod));
}

static void delitem(void *obj)
{
	item *cit = obj;
	item_fin(cit->c, cit);
}

void ffcache_free(ffcache *c)
{
	ffrbtl_freeall(&c->items, &delitem, FFOFF(item, rbtnod));
	ffmem_free(c);
}

int ffcache_fetch(ffcache *c, ffcache_item *ci, uint flags)
{
	ffrbt_node *found;
	item *cit;
	enum FFCACHE_E er;

	if (flags & FFCACHE_NEXT) {

		if (!c->conf.multi
			|| ci->id == NULL) {
			fferr_set(EINVAL);
			er = FFCACHE_ESYS; //misuse
			goto fail;
		}

		cit = (item*)ci->id;
		if (cit->rbtnod.sib.next == &cit->rbtnod.sib) {
			er = FFCACHE_ENOTFOUND;
			goto fail;
		}

		cit = FF_GETPTR(item, rbtnod, ffrbtl_nodebylist(cit->rbtnod.sib.next));

	} else if (ci->id != NULL) {
		// get the item by its ID
		cit = (item*)ci->id;

	} else {
		// search for an item by name

		if (KEYHASH_EMPTY(ci->keyhash))
			KEYHASH_SET(ci->keyhash, ci->key.ptr, ci->key.len, c->conf.key_icase);

		found = ffrbt_find(&c->items, ci->keyhash[0], NULL);
		if (found == NULL) {
			c->stat.misses++;
			er = FFCACHE_ENOTFOUND;
			goto fail;
		}

		cit = FF_GETPTR(item, rbtnod, found);
		if (!key_equal(cit->ckey, ci->key.ptr, ci->key.len, c->conf.key_icase)) {
			c->stat.misses++;
			er = FFCACHE_ECOLL;
			goto fail;
		}

		c->stat.hits++;
	}

	if (flags & FFCACHE_ACQUIRE) {

		if (cit->usage != 0) {
			er = FFCACHE_ELOCKED; //the item must not have any references
			goto fail;
		}

		cit->usage++;
		item_rlz(c, cit);

	} else {

		if (ci->refs == 0) {
			fferr_set(EINVAL);
			er = FFCACHE_ESYS; //misuse
			goto fail;
		}

		cit->usage += ci->refs;
		fflist_moveback(&c->lastused, &cit->lastused_li);
	}

	item_fill(ci, cit);
	return FFCACHE_OK;

fail:
	return er;
}

int ffcache_store(ffcache *c, ffcache_item *ci, uint flags)
{
	int er;
	item *cit = NULL;
	ffrbt_node *found, *parent;

	if (ci->key.len > MAX_KEYLEN) {
		er = FFCACHE_ESZLIMIT;
		goto fail;
	}

	if (ci->data.len > c->conf.max_data) {
		er = FFCACHE_ESZLIMIT;
		goto fail;
	}

	if (c->items.len == c->conf.max_items) {
		if (0 != rm_unused_one(c)) {
			er = FFCACHE_ENUMLIMIT;
			goto fail;
		}
	}

	/* Note: we should not add 'ci->key.len' if an item with the same key already exists,
	 but that would require us to perform a tree lookup first. */
	if (c->memsize + ci->key.len + ci->data.len > c->conf.mem_limit) {
		if (0 != rm_unused_mem(c, ci->key.len + ci->data.len)) {
			er = FFCACHE_EMEMLIMIT;
			goto fail;
		}
	}

	if (KEYHASH_EMPTY(ci->keyhash))
		KEYHASH_SET(ci->keyhash, ci->key.ptr, ci->key.len, c->conf.key_icase);

	cit = ffmem_new(item);
	if (cit == NULL) {
		er = FFCACHE_ESYS;
		goto fail;
	}
	cit->c = c;

	if (0 != item_copydata(cit, ci)) {
		er = FFCACHE_ESYS;
		goto fail;
	}

	cit->usage = ci->refs;

	found = ffrbt_find(&c->items, ci->keyhash[0], &parent);
	if (found == NULL) {

		cit->ckey = key_alloc(ci->key.ptr, ci->key.len, c->conf.key_icase);
		if (cit->ckey == NULL) {
			er = FFCACHE_ESYS;
			goto fail;
		}
		c->memsize += ci->key.len;

		cit->rbtnod.key = ci->keyhash[0];
		ffrbtl_insert3(&c->items, &cit->rbtnod, parent);

	} else {

		item *fcit = FF_GETPTR(item, rbtnod, found);
		if (!key_equal(fcit->ckey, ci->key.ptr, ci->key.len, c->conf.key_icase)) {
			er = FFCACHE_ECOLL;
			goto fail;
		}

		if (!c->conf.multi) {
			er = FFCACHE_EEXISTS;
			goto fail;
		}

		key_ref(fcit->ckey);
		cit->ckey = fcit->ckey;

		ffchain_append(&cit->rbtnod.sib, fcit->rbtnod.sib.prev); //'prev' points to the last item in chain
		c->items.len++;
	}

	c->memsize += cit->data.len;
	ci->expire = item_tmrreset(cit, ci->expire);
	fflist_ins(&c->lastused, &cit->lastused_li);

	if (ci->refs != 0)
		item_fill(ci, cit);
	else
		ci->id = cit;
	ci->refs = cit->usage;
	return FFCACHE_OK;

fail:
	if (cit != NULL)
		item_free(cit);

	return er;
}

int ffcache_update(ffcache *c, ffcache_item *ci, uint flags)
{
	int er;
	item *cit;
	ssize_t memsize_delta;

	if (ci->id == NULL) {
		fferr_set(EINVAL);
		er = FFCACHE_ESYS; //item id must be set
		goto fail;
	}
	cit = (item*)ci->id;

	if (ci->data.len > c->conf.max_data) {
		er = FFCACHE_ESZLIMIT;
		goto fail;
	}

	if (cit->unlinked) {
		er = FFCACHE_ENOTFOUND; //the item was expired
		goto fail;
	}

	if (cit->usage != 1) {
		er = FFCACHE_ELOCKED; //the item must be exclusively owned by the caller
		goto fail;
	}

	memsize_delta = (ssize_t)ci->data.len - cit->data.len;
	if (c->memsize + memsize_delta > c->conf.mem_limit) {
		if (0 != rm_unused_mem(c, memsize_delta)) {
			er = FFCACHE_EMEMLIMIT;
			goto fail;
		}
	}

	//replace data
	if (0 != item_copydata(cit, ci)) {
		er = FFCACHE_ESYS;
		goto fail;
	}

	c->memsize += memsize_delta;
	fflist_moveback(&c->lastused, &cit->lastused_li);
	ci->expire = item_tmrreset(cit, ci->expire);

	item_fill(ci, cit);
	return FFCACHE_OK;

fail:
	return er;
}

int ffcache_unref(ffcache *c, void *cid, uint flags)
{
	item *cit;

	if (cid == NULL) {
		fferr_set(EINVAL);
		return FFCACHE_ESYS; //item id must be set
	}
	cit = (item*)cid;

	FF_ASSERT(cit->usage != 0);
	cit->usage--;

	if ((flags & FFCACHE_REMOVE) && !cit->unlinked) {
		item_rlz(c, cit);
		return FFCACHE_OK;
	}

	if (cit->unlinked && cit->usage == 0)
		item_fin(c, cit);

	return FFCACHE_OK;
}


/** Timer expired. */
static void item_onexpire(void *param)
{
	item *cit = param;
	item_rlz(cit->c, cit);
}

/** @expire: in sec. */
static uint item_tmrreset(item *cit, uint expire)
{
	expire = (expire == 0) ? cit->c->conf.def_expire : (uint)ffmin(expire, cit->c->conf.max_expire);
	cit->tmr.handler = &item_onexpire;
	cit->tmr.param = cit;
	cit->c->conf.timer(&cit->tmr, (int64)expire * 1000);
	return expire;
}

static void item_fill(ffcache_item *ci, const item *cit)
{
	ci->id = (void*)cit;
	ffstr_set(&ci->key, cit->ckey->d, cit->ckey->len);
	ffstr_set(&ci->data, cit->data.ptr, cit->data.len);
	ci->refs = cit->usage;
}

static int item_copydata(item *cit, const ffcache_item *ci)
{
	void *p;

	if (ci->data.len <= DATA_S_SIZE) {
		p = cit->data_s;

		if (cit->data.len > DATA_S_SIZE)
			ffstr_free(&cit->data);

	} else {

		if (cit->data.len > DATA_S_SIZE)
			p = ffmem_realloc(cit->data.ptr, ci->data.len);
		else
			p = ffmem_alloc(ci->data.len);
		if (p == NULL)
			return 1;
	}

	ffmemcpy(p, ci->data.ptr, ci->data.len);
	ffstr_set(&cit->data, p, ci->data.len);
	return 0;
}

/** Delete 1 unused item. */
static int rm_unused_one(ffcache *c)
{
	item *cit;

	_FFLIST_WALK(&c->lastused, cit, lastused_li) {

		if (cit->usage == 0) {
			item_rlz(c, cit);
			return 0;
		}
	}

	return 1;
}

/** Delete unused items until there is enough free memory. */
static int rm_unused_mem(ffcache *c, size_t memneeded)
{
	item *cit;

	_FFLIST_WALK(&c->lastused, cit, lastused_li) {

		if (cit->usage == 0) {
			item_rlz(c, cit);

			if (c->memsize + memneeded <= c->conf.mem_limit)
				return 0;
		}
	}

	return 1;
}

/** Unlink the item from the cache. */
static void item_rlz(ffcache *c, item *cit)
{
	FF_ASSERT(!cit->unlinked);

	c->conf.timer(&cit->tmr, 0);
	ffrbtl_rm(&c->items, &cit->rbtnod);
	fflist_rm(&c->lastused, &cit->lastused_li);
	cit->unlinked = 1;

	if (cit->usage == 0)
		item_fin(c, cit);
}

/** Delete the item. */
static void item_fin(ffcache *c, item *cit)
{
	if (c->conf.onchange != NULL) {
		ffcache_item ci = {};
		item_fill(&ci, cit);
		c->conf.onchange(c, &ci, FFCACHE_ONDELETE);
	}

	c->memsize -= key_unref(cit->ckey) + cit->data.len;

	item_free(cit);
}

/** Free resources owned by the item. */
static void item_free(item *cit)
{
	if (cit->data.len > DATA_S_SIZE)
		ffstr_free(&cit->data);

	ffmem_free(cit);
}


/** Create a shared key. */
static cach_key * key_alloc(const char *key, size_t len, int key_icase)
{
	cach_key *ckey = ffmem_alloc(sizeof(cach_key) + len);
	if (ckey == NULL)
		return NULL;

	if (!key_icase)
		ffmemcpy(ckey->d, key, len);
	else
		ffs_lower(ckey->d, len, key, len);

	ckey->len = len;
	ckey->usage = 1;
	return ckey;
}

static void key_ref(cach_key *ckey)
{
	ckey->usage++;
}

/** Decrease refcount and return data size if it's the last reference. */
static size_t key_unref(cach_key *ckey)
{
	if (--ckey->usage == 0) {
		size_t n = ckey->len;
		ffmem_free(ckey);
		return n;
	}
	return 0;
}

/** Return TRUE if keys are equal. */
static ffbool key_equal(const cach_key *ckey, const char *key, size_t len, int key_icase)
{
	if (ckey->len != len)
		return 0;

	if (!key_icase)
		return !ffs_cmp(ckey->d, key, len);
	return !ffs_icmp(ckey->d, key, len);
}
