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
	uint reserved;
	ffrbtkey key;
};

/** fftree_node with 8-byte key. */
typedef struct fftree_node8 {
	fftree_node *left
		, *right
		, *parent;
	uint reserved;
	uint64 key;
} fftree_node8;

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

/** Get the minimum node and loop white it's valid.
User must update the node pointer manually. */
#define FFTREE_FOR(tr, nod) \
	for ((nod) = ((tr)->root != &(tr)->sentl) \
			? (void*)fftree_min((fftree_node*)(tr)->root, &(tr)->sentl) \
			: &(tr)->sentl \
		; (nod) != &(tr)->sentl \
		; )


/** Red-black tree node. */
typedef struct ffrbt_node ffrbt_node;
struct ffrbt_node {
	ffrbt_node *left
		, *right
		, *parent;
	uint color;
	ffrbtkey key;
};

typedef struct ffrbtree {
	size_t len;
	ffrbt_node *root;
	ffrbt_node sentl;
	void (*insnode)(fftree_node *nod, fftree_node **root, void *sentl);
} ffrbtree;

static FFINL void ffrbt_init(ffrbtree *tr) {
	tr->len = 0;
	tr->sentl.color = 0; //FFRBT_BLACK
	tr->root = &tr->sentl;
	tr->insnode = &fftree_insert;
}

#define ffrbt_empty(tr)  ((tr)->root == &(tr)->sentl)

/** Insert node.
'ancestor': search starting at this node (optional parameter). */
FF_EXTN void ffrbt_insert(ffrbtree *tr, ffrbt_node *nod, ffrbt_node *ancestor);
#define ffrbt_insert4(tr, nod, ancestor, node_key) \
do { \
	(nod)->key = (node_key); \
	ffrbt_insert(tr, nod, ancestor); \
} while (0)

/** Remove node. */
FF_EXTN void ffrbt_rm(ffrbtree *tr, ffrbt_node *nod);

/** Find successor of a node. */
static inline ffrbt_node* ffrbt_successor(ffrbtree *tr, ffrbt_node *nod)
{
	return (ffrbt_node*)fftree_successor((fftree_node*)nod, &tr->sentl);
}

/** Find node in red-black tree. */
static FFINL ffrbt_node * ffrbt_find(ffrbtree *tr, ffrbtkey key, ffrbt_node **parent) {
	ffrbt_node *root = tr->root;
	ffrbt_node *nod = (ffrbt_node*)fftree_findnode(key, (fftree_node**)&root, &tr->sentl);
	if (parent != NULL)
		*parent = root;
	return nod;
}

typedef void (*ffrbt_free_t)(void*);

/** Call a user's destroy/free function for each node.
off: offset of a ffrbt_node in user's structure. */
FF_EXTN void ffrbt_freeall(ffrbtree *tr, ffrbt_free_t func, size_t off);

/** Print contents of all nodes. */
FF_EXTN void ffrbt_print(ffrbtree *tr);


/** Node which holds pointers to its sibling nodes having the same key. */
typedef struct ffrbtl_node {
	ffrbt_node *left
		, *right
		, *parent;
	uint color;
	ffrbtkey key;

	fflist_item sib;
} ffrbtl_node;

/** Get RBT node by the pointer to its list item. */
#define ffrbtl_nodebylist(item)  FF_GETPTR(ffrbtl_node, sib, item)

/** Insert a new node or list-item. */
FF_EXTN void ffrbtl_insert(ffrbtree *tr, ffrbtl_node *k);

#define ffrbtl_insert3(tr, nod, parent) \
do { \
	ffrbt_insert(tr, (ffrbt_node*)(nod), parent); \
	(nod)->sib.next = (nod)->sib.prev = &(nod)->sib; \
} while (0)

/** Remove node or its sibling. */
FF_EXTN void ffrbtl_rm(ffrbtree *tr, ffrbtl_node *k);

/** Reinsert a node with a new key. */
static FFINL void ffrbtl_move(ffrbtree *tr, ffrbtl_node *k) {
	ffrbtl_rm(tr, k);
	ffrbtl_insert(tr, k);
}

/** Call a user's destroy/free function for each node (including list siblings).
off: offset of a ffrbtl_node in user's structure. */
FF_EXTN void ffrbtl_freeall(ffrbtree *tr, ffrbt_free_t func, size_t off);
