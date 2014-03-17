/** Binary tree.  Red-black tree.
Copyright (c) 2013 Simon Zolin
*/

#pragma once

#include <FF/list.h>


typedef uint ffrbtkey;
typedef struct fftree_node fftree_node;

/** Binary tree node. */
struct fftree_node {
	fftree_node *left
		, *right
		, *parent;
	ffrbtkey key;
};

/** Insert into binary tree. */
FF_EXTN void fftree_insert(fftree_node *nod, fftree_node **root, void *sentl);

/** Search node.
Return NULL if not found.  'root' is set to the leaf node. */
FF_EXTN fftree_node * fftree_findnode(ffrbtkey key, fftree_node **root, void *sentl);

/** Find bottom left node. */
static FFINL fftree_node * fftree_min(fftree_node *nod, void *sentl) {
	FF_ASSERT(nod != sentl);
	while (nod->left != sentl)
		nod = nod->left;
	return nod;
}

/** Find successor of a node. */
FF_EXTN fftree_node * fftree_successor(fftree_node *it, void *sentl);

/** Traverse a tree. */
#define FFTREE_WALK(tr, nod) \
	if ((tr)->root != &(tr)->sentl) \
		for ((nod) = fftree_min((fftree_node*)(tr)->root, &(tr)->sentl) \
			; (nod) != (void*)&(tr)->sentl \
			; (nod) = fftree_successor((nod), &(tr)->sentl))

/** Traverse a tree safely. */
#define FFTREE_WALKSAFE(tr, nod, next) \
	if ((tr)->root != &(tr)->sentl) \
		for (nod = fftree_min((fftree_node*)(tr)->root, &(tr)->sentl) \
			; nod != (void*)&(tr)->sentl && ((next = fftree_successor(nod, &(tr)->sentl)) || 1) \
			; nod = next)


typedef struct ffrbt_node ffrbt_node;

/** Red-black tree node. */
struct ffrbt_node {
	ffrbt_node *left
		, *right
		, *parent;
	ffrbtkey key;
	uint color;
};

typedef struct ffrbtree {
	size_t len;
	ffrbt_node *root;
	ffrbt_node sentl;
} ffrbtree;

static FFINL void ffrbt_init(ffrbtree *tr) {
	tr->len = 0;
	tr->sentl.color = 0; //FFRBT_BLACK
	tr->root = &tr->sentl;
}

/** Insert node.
'ancestor': search starting at this node (optional parameter). */
FF_EXTN void ffrbt_insert(ffrbtree *tr, ffrbt_node *nod, ffrbt_node *ancestor);

/** Remove node. */
FF_EXTN void ffrbt_rm(ffrbtree *tr, ffrbt_node *nod);

/** Find node in red-black tree. */
static FFINL ffrbt_node * ffrbt_find(ffrbtree *tr, ffrbtkey key, ffrbt_node **parent) {
	ffrbt_node *root = tr->root;
	ffrbt_node *nod = (ffrbt_node*)fftree_findnode(key, (fftree_node**)&root, &tr->sentl);
	if (parent != NULL)
		*parent = root;
	return nod;
}


/** Node which holds pointers to its sibling nodes having the same key. */
typedef struct ffrbtl_node {
	ffrbt_node *left
		, *right
		, *parent;
	ffrbtkey key;
	uint color;
	fflist_item sib;
} ffrbtl_node;

/** Get RBT node by the pointer to its list item. */
#define ffrbtl_nodebylist(item)  FF_GETPTR(ffrbtl_node, sib, item)

/** Insert a new node or list-item. */
FF_EXTN void ffrbtl_insert(ffrbtree *tr, ffrbtl_node *k);

/** Remove node or its sibling. */
FF_EXTN void ffrbtl_rm(ffrbtree *tr, ffrbtl_node *k);

/** Reinsert a node with a new key. */
static FFINL void ffrbtl_move(ffrbtree *tr, ffrbtl_node *k) {
	ffrbtl_rm(tr, k);
	ffrbtl_insert(tr, k);
}

typedef int (*fftree_on_item_t)(void *obj, void *udata);

/** Call on_item() safely for every node in tree and all of its list items in chain.
If on_item() returns non-zero, break the loop and return. */
FF_EXTN int ffrbtl_enumsafe(ffrbtree *tr, fftree_on_item_t on_item, void *udata, uint off);
