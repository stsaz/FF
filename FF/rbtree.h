/** Red-black tree.
Copyright (c) 2013 Simon Zolin
*/

#pragma once

#include <FF/list.h>


typedef uint ffrbtkey;
typedef struct ffrbt_node ffrbt_node;
typedef struct ffrbt_listnode ffrbt_listnode;

struct ffrbt_node {
	ffrbtkey key;
	uint color;
	ffrbt_node *left
		, *right
		, *parent;
};

/** Node which holds pointers to its sibling nodes having the same key. */
struct ffrbt_listnode {
	ffrbtkey key;
	uint color;
	ffrbt_node *left
		, *right
		, *parent;
	fflist_item sib;
};

typedef struct ffrbtree {
	size_t len;
	ffrbt_node *root;
	ffrbt_node sentl;
} ffrbtree;

FF_STRUCTPTR(ffrbt_listnode, ffrbt_nodebylist, fflist_item, sib)

enum FFRBT_COLOR {
	FFRBT_BLACK
	, FFRBT_RED
};

static FFINL void ffrbt_init(ffrbtree *tr) {
	tr->len = 0;
	tr->sentl.color = 0; //FFRBT_BLACK
	tr->root = &tr->sentl;
}

/** Insert node.
'ancestor': search starting at this node (optional parameter). */
FF_EXTN void ffrbt_ins(ffrbtkey key, ffrbtree *tr, ffrbt_node *nod, ffrbt_node *ancestor);

/** Remove node. */
FF_EXTN void ffrbt_rm(ffrbtree *tr, ffrbt_node *nod);

/** Insert a new node or list-item. */
FF_EXTN void ffrbt_listins(ffrbtkey key, ffrbtree *tr, ffrbt_listnode *k);

/** Remove node or its sibling. */
FF_EXTN void ffrbt_listrm(ffrbtree *tr, ffrbt_listnode *k);

/** Search node.
Return NULL if not found. 'root' is set to the leaf node. */
FF_EXTN ffrbt_node * ffrbt_findnode(ffrbtkey key, ffrbt_node **root, ffrbt_node *sentl);

static FFINL ffrbt_node * ffrbt_find(ffrbtkey key, ffrbt_node *root, ffrbt_node *sentl) {
	return ffrbt_findnode(key, &root, sentl);
}

/** Iterator. */
typedef struct {
	ffrbt_node *nod;
	ffrbt_node *sentl;
	int st;
	int ord;
} ffrbt_iter;

enum FFRBT_ORDER {
	FFRBT_INORD
	, FFRBT_POSTORD
};

enum FFRBT_N {
	FFRBT_LEFT = 0
	, FFRBT_RIGHT
	, FFRBT_UP
};

/** Initialize iterator. */
static FFINL void ffrbt_iterinit(ffrbt_iter *it, ffrbtree *tr) {
	it->nod = tr->root;
	it->sentl = &tr->sentl;
	it->st = FFRBT_LEFT;
	it->ord = FFRBT_INORD;
}

/** Get next minimum node (in-order)
	or next minimum node starting from leafs (post-order). */
FF_EXTN ffrbt_node * ffrbt_nextmin(ffrbt_iter *it);

/** Traverse a tree. */
#define FFRBT_WALK(iter, node) \
	for (ffrbt_nextmin(&(iter)), (node) = (iter).nod, ffrbt_nextmin(&(iter)) \
		; (node) != (iter).sentl \
		; (node) = (iter).nod, ffrbt_nextmin(&(iter)))

#define FFRBT_DESTROY(tree, on_destroy, get_struct_ptr) \
do { \
	ffrbt_iter it; \
	ffrbt_node *nod; \
	ffrbt_iterinit(&it, tree); \
	FFRBT_WALK(it, nod) { \
		on_destroy(get_struct_ptr(nod)); \
	} \
	(tree)->len = 0; \
} while (0)

typedef int (*ffrbt_on_item_t)(void *udata, ffrbt_listnode *nod);

/** Traverse a tree with nodes and list items. */
FF_EXTN int ffrbt_iterlwalk(ffrbt_iter *it, ffrbt_on_item_t on_item, void *udata);

static FFINL int ffrbt_listwalk(ffrbtree *tr, ffrbt_on_item_t on_item, void *udata) {
	ffrbt_iter it;
	ffrbt_iterinit(&it, tr);
	return ffrbt_iterlwalk(&it, on_item, udata);
}
