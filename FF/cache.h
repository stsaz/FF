/** In-memory cache.
Copyright 2019 Simon Zolin. */

#include <FF/sys/timer-queue.h>
#include <FF/array.h>


typedef struct ffcache ffcache;
typedef struct ffcache_item ffcache_item;

/** Set a one-shot timer.
value_ms: timer value in milliseconds;  0: disable. */
typedef void (*ffcache_timer)(fftmrq_entry *tmr, uint value_ms);

enum FFCACHE_CB {
	FFCACHE_ONDELETE = 1,
};

/**
flags: enum FFCACHE_CB
*/
typedef int (*ffcache_onchange)(ffcache *c, ffcache_item *ci, uint flags);

typedef struct ffcache_conf {
	ffcache_timer timer;
	ffcache_onchange onchange;
	void *udata;

	char sname[FFINT_MAXCHARS];
	ffstr name;
	uint max_items;
	uint max_data;
	uint mem_limit;
	uint def_expire;
	uint max_expire;
	uint key_icase :1
		, multi :1;
} ffcache_conf;

struct ffcache_stat {
	uint hits;
	uint misses;
	size_t items;
	size_t memsize;
};

FF_EXTN const char* ffcache_errstr(uint code);

FF_EXTN void ffcache_conf_init(ffcache_conf *conf);
FF_EXTN ffcache* ffcache_create(const ffcache_conf *conf);
FF_EXTN void ffcache_free(ffcache *c);
FF_EXTN void ffcache_stat(ffcache *c, struct ffcache_stat *stat);
FF_EXTN void ffcache_reset(ffcache *c);
FF_EXTN void* ffcache_udata(ffcache *c);

enum FFCACHE_E {
	FFCACHE_OK,
	FFCACHE_ESYS,
	FFCACHE_EEXISTS,
	FFCACHE_ENOTFOUND,
	FFCACHE_ECOLL,
	FFCACHE_ENUMLIMIT,
	FFCACHE_EMEMLIMIT,
	FFCACHE_ESZLIMIT,
	FFCACHE_ELOCKED,
};

struct ffcache_item {
	void *id;
	uint keyhash[1];
	ffstr key, data;
	uint refs;
	uint expire; //max-age, in sec
};

enum FFCACHE_FETCH {
	FFCACHE_ACQUIRE = 1, //acquire item (fetch and remove from cache)
	FFCACHE_NEXT = 2, //fetch the next item with the same key, for FFCACHE_MULTI
};

/** Fetch data.
ci: receives item data and properties
 ci.id: If not NULL, return item by this ID (don't perform lookup).
 ci.keyhash: If not 0, use this key hash (don't call hash function).
flags: enum FFCACHE_FETCH
Return enum FFCACHE_E
 FFCACHE_OK: 'ci' is filled with item data and properties.
  User must unref 'ci.id' with ffcache_unref().
*/
FF_EXTN int ffcache_fetch(ffcache *c, ffcache_item *ci, uint flags);

/** Store data.
Return enum FFCACHE_E
 FFCACHE_OK:
  ci.refs != 0: 'ci' is filled with item data and properties.
   User must unref 'ci.id' with ffcache_unref().
  ci.refs == 0: 'ci.id' is set
*/
FF_EXTN int ffcache_store(ffcache *c, ffcache_item *ci, uint flags);

/** Update data.
ca:
 ca.id: Item ID
Return enum FFCACHE_E */
FF_EXTN int ffcache_update(ffcache *c, ffcache_item *ci, uint flags);

enum FFCACHE_UNREF {
	FFCACHE_REMOVE = 1, //Unref and remove item
};

/** Unref data.
cid: ci.id returned by ffcache_fetch() or ffcache_store().
flags: enum FFCACHE_UNREF
Return enum FFCACHE_E */
FF_EXTN int ffcache_unref(ffcache *c, void *cid, uint flags);
