# Base algorithms

* Dynamic array
* Double-linked list
* Red-black tree
* Hash table
* Ring queue (thread-safe)


## Dynamic array

Include:

	#include <FF/array.h>

Initialize and allocate array of bytes:

	ffarr a = {};
	if (NULL == ffarr_alloc(&a, 1024))
		return;

Initialize and allocate array of objects:

	ffarr a = {};
	if (NULL == ffarr_allocT(&a, 1024, uint))
		return;

Append data:

	char data[] = ...;
	if (NULL == ffarr_append(&a, data, 100))
		return;

Append an object:

	uint *new_obj = ffarr_pushT(&a, uint);
	if (new_obj == NULL)
		return;
	*new_obj = ...;

Traverse:

	uint *it;
	FFARR_WALKT(&a, it, uint) {
		// use object '*it'
	}

Destroy:

	ffarr_free(&a);


## Double-linked list

Include:

	#include <FF/chain.h>

Initialize:

	ffchain chain;
	ffchain_init(&chain);

Insert:

	struct foo {
		/*
		...struct members...
		*/

		ffchain_item sib;
	};
	struct foo obj1 = ...;
	struct foo obj2 = ...;

	ffchain_add(&chain, &obj1, NULL, /*key=*/ 0x1234);
	ffchain_addfront(&chain, &obj1, NULL, /*key=*/ 0x1234);

Traverse:

	ffchain_item *it;
	FFCHAIN_WALK(&chain, it) {
		struct foo *obj = FF_GETPTR(struct foo, sib, it);
		...
	}

Traverse safely:

	ffchain_item *it;
	FFCHAIN_FOR(&chain, it) {
		struct foo *obj = FF_GETPTR(struct foo, sib, it);
		it = it->next;
		ffchain_rm(&chain, &obj->sib);
	}

Remove element:

	ffchain_rm(&chain, &obj->sib);


## Red-black tree

Include:

	#include <FF/rbtree.h>

Initialize:

	ffrbtree tree;
	ffrbt_init(&tree);

Insert:

	struct foo {
		/*
		...struct members...
		*/

		ffrbt_node node;
	};
	struct foo obj1 = ...;
	struct foo obj2 = ...;

	ffrbt_insert4(&tree, &obj1.node, NULL, /*key=*/ 0x1234);
	ffrbt_insert4(&tree, &obj2.node, NULL, /*key=*/ 0x2345);

Search:

	ffrbt_node *found_node = ffrbt_find(&tree, /*key=*/ 0x1234, NULL);
	if (found_node == NULL)
		return; // there's no node with key = 0x1234
	struct foo *obj = FF_GETPTR(struct foo, node, found_node);
	... // use 'obj'

Traverse:

	ffrbt_node *it;
	FFTREE_WALK(&tree, it) {
		struct foo *obj = FF_GETPTR(struct foo, node, it);
		...
	}

Traverse safely:

	ffrbt_node *it, *next;
	FFTREE_WALKSAFE(&tree, it, next) {
		struct foo *obj = FF_GETPTR(struct foo, node, it);
		ffrbt_rm(&tree, &obj->node);
	}

Remove element:

	ffrbt_rm(&tree, &obj->node);


## Hash table

Include:

	#include <FF/hashtab.h>

Initialize:

	int foo_cmpkey(void *val, const void *key, void *param);

	ffhstab hst = {};
	hst.cmpkey = &foo_cmpkey;
	ffhst_init(&hst, /*items=*/ N);

Insert:

	struct foo {
		char key[...];
		/*
		...struct members...
		*/
	};

	struct foo obj1 = ...;
	struct foo obj2 = ...;

	ffhst_ins(&hst, /*hash=*/ 0x1234, &obj1);
	ffhst_ins(&hst, /*hash=*/ 0x2345, &obj2);

Search:

	int foo_cmpkey(void *val, const void *key, void *param)
	{
		struct foo *obj = val;
		return !ffsz_eq(key, obj->key);
	}

	struct foo *obj = ffhst_find(&hst, /*hash=*/ 0x1234, "keyname", param);
	if (obj == NULL)
		return; // there's no element with key = "keyname"

Destroy:

	ffhst_free(&hst);


## Ring queue (thread-safe)

Include:

	#include <FF/ring.h>

Create:

	ffring r;
	ffring_create(&r, /*size=*/ 2, /*align=*/ 16);

Insert:

	struct foo *obj = ...;
	if (0 != ffring_write(&r, obj)) {
		// queue is full
	}

Read:

	struct foo *obj;
	if (0 != ffring_read(&r, &obj)) {
		// queue is empty
	}

Destroy:

	ffring_destroy(&r);
