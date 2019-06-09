/**
Copyright (c) 2019 Simon Zolin
*/

#include <FF/cache.h>
#include <test/all.h>
#include <FFOS/test.h>

#define x FFTEST_BOOL

static void setci(ffcache_item *ci, const char *k, const char *v)
{
	ffmem_tzero(ci);
	ffstr_setz(&ci->key, k);
	ffstr_setz(&ci->data, v);
	ci->refs = 1;
}

static int gstate;
static int gstatus;

static int onchange(ffcache *c, ffcache_item *ci, uint flags)
{
	x(flags & FFCACHE_ONDELETE);
	switch (gstate) {
	case 0:
		x(ffstr_eqz(&ci->key, "key") && ffstr_eqz(&ci->data, "val2"));
		break;
	}
	gstatus++;
	return 0;
}

static void test_cache_general(ffcache *c)
{
	ffcache_item ci, ci2;

	// failed fetch
	setci(&ci, "key", "");
	x(FFCACHE_ENOTFOUND == ffcache_fetch(c, &ci, 0));

	// store
	setci(&ci, "key", "val");
	ci.expire = 1;
	x(0 == ffcache_store(c, &ci, 0)); // ref=1
	x(ci.id != NULL);
	x(ffstr_eqz(&ci.key, "key") && ffstr_eqz(&ci.data, "val"));
	x(ci.refs == 1);
	x(ci.expire == 1);
	x(0 == ffcache_unref(c, ci.id, 0)); // ref=0

	// fetch after store
	setci(&ci, "KEY", "val"); // check key_icase
	x(0 == ffcache_fetch(c, &ci, 0)); // ref=1
	ci2 = ci;
	x(ci.id != NULL);
	x(ffstr_eqz(&ci.key, "key") && ffstr_eqz(&ci.data, "val"));

	// failed store
	setci(&ci, "KEY", "");
	x(FFCACHE_EEXISTS == ffcache_store(c, &ci, 0));

	// failed update due to refs==2
	setci(&ci, "key", "val");
	x(0 == ffcache_fetch(c, &ci, 0)); // ref=2
	x(ci.id != NULL);
	setci(&ci, "key", "val2");
	ci.id = ci2.id;
	x(FFCACHE_ELOCKED == ffcache_update(c, &ci, 0));
	x(0 == ffcache_unref(c, ci2.id, 0)); // ref=1

	// successful update
	setci(&ci, "key", "val2");
	ci.id = ci2.id;
	x(0 == ffcache_update(c, &ci, 0));
	x(ci.id != NULL);
	x(ffstr_eqz(&ci.key, "key") && ffstr_eqz(&ci.data, "val2"));

	// unref fetched
	x(0 == ffcache_unref(c, ci.id, 0)); // ref=0

	// fetch after update
	setci(&ci, "key", "");
	x(0 == ffcache_fetch(c, &ci, 0)); // ref=1
	x(ci.id != NULL);
	x(ffstr_eqz(&ci.key, "key") && ffstr_eqz(&ci.data, "val2"));
	x(0 == ffcache_unref(c, ci.id, FFCACHE_REMOVE)); // ref=0
	x(gstatus == 1);

	// failed fetch after remove
	setci(&ci, "key", "");
	x(FFCACHE_ENOTFOUND == ffcache_fetch(c, &ci, 0));

	// stats
	struct ffcache_stat stat;
	ffcache_stat(c, &stat);
}

static void test_cache_acquire(ffcache *c)
{
	ffcache_item ci;
	gstate = 1;
	gstatus = 0;

	// store
	setci(&ci, "key", "val");
	x(0 == ffcache_store(c, &ci, 0)); // ref=1
	x(0 == ffcache_unref(c, ci.id, 0)); // ref=0

	// fetch with FFCACHE_ACQUIRE
	setci(&ci, "key", "val");
	x(0 == ffcache_fetch(c, &ci, FFCACHE_ACQUIRE));
	x(ci.id != NULL);
	x(ffstr_eqz(&ci.key, "key") && ffstr_eqz(&ci.data, "val"));
	x(gstatus == 0);

	// unref (delete)
	x(0 == ffcache_unref(c, ci.id, FFCACHE_REMOVE));
	x(gstatus == 1);
}

static void test_cache_multi()
{
	ffcache_item ci, ci2, ci3;
	ffcache *c;
	ffcache_conf conf;
	gstate = 2;

	ffcache_conf_init(&conf);
	conf.onchange = &onchange;
	conf.multi = 1;
	c = ffcache_create(&conf);

	// store 1
	setci(&ci, "key", "val1");
	x(0 == ffcache_store(c, &ci, 0)); // ref=1
	x(0 == ffcache_unref(c, ci.id, 0)); // ref=0

	// store 2
	setci(&ci, "key", "val2");
	x(0 == ffcache_store(c, &ci, 0)); // ref=1
	x(0 == ffcache_unref(c, ci.id, 0)); // ref=0

	// fetch 1
	setci(&ci, "key", "");
	x(0 == ffcache_fetch(c, &ci, 0));
	x(ci.id != NULL);
	x(ffstr_eqz(&ci.key, "key") && ffstr_eqz(&ci.data, "val1"));

	// fetch 2
	setci(&ci2, "key", "");
	ci2.id = ci.id;
	x(0 == ffcache_fetch(c, &ci2, FFCACHE_NEXT));
	x(ci2.id != NULL);
	x(ffstr_eqz(&ci2.key, "key") && ffstr_eqz(&ci2.data, "val2"));

	// fetch 1
	setci(&ci3, "key", "");
	ci3.id = ci2.id;
	x(ci3.id != NULL);
	x(ffstr_eqz(&ci.key, "key") && ffstr_eqz(&ci.data, "val1"));
	x(0 == ffcache_unref(c, ci3.id, 0));

	// remove 1
	x(0 == ffcache_unref(c, ci.id, FFCACHE_REMOVE));

	// fetch 2
	setci(&ci2, "key", "");
	x(0 == ffcache_fetch(c, &ci2, 0));
	x(ci2.id != NULL);
	x(ffstr_eqz(&ci2.key, "key") && ffstr_eqz(&ci2.data, "val2"));
	x(0 == ffcache_unref(c, ci2.id, 0));

	ffcache_free(c);
}

static void test_cache_limits()
{
	ffcache_item ci, ci2, ci3;
	ffcache *c;
	ffcache_conf conf;
	gstate = 3;
	gstatus = 0;

	ffcache_conf_init(&conf);
	conf.onchange = &onchange;
	conf.max_items = 2;
	x(NULL != (c = ffcache_create(&conf)));

// "max_items"
	// store 1
	setci(&ci, "key1", "val1");
	x(0 == ffcache_store(c, &ci, 0));

	// store 2
	setci(&ci2, "key2", "val2");
	x(0 == ffcache_store(c, &ci2, 0));

	// failed store
	setci(&ci3, "key3", "val2");
	x(FFCACHE_ENUMLIMIT == ffcache_store(c, &ci3, 0));

	// unref old item
	x(0 == ffcache_unref(c, ci.id, 0));

	// store 3
	x(0 == ffcache_store(c, &ci3, 0)); //"key1" was deleted automatically
	x(gstatus == 1);

	ffcache_free(c);
}

int test_cache(void)
{
	ffcache *c;
	ffcache_conf conf;

	ffcache_conf_init(&conf);
	conf.onchange = &onchange;
	conf.key_icase = 1;
	x(NULL != (c = ffcache_create(&conf)));

	test_cache_general(c);
	test_cache_acquire(c);

	ffcache_reset(c);
	ffcache_free(c);

	test_cache_multi();
	test_cache_limits();
	return 0;
}
