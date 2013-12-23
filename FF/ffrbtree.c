/**
Copyright (c) 2013 Simon Zolin
*/

#include <FF/rbtree.h>


ffrbt_node * ffrbt_findnode(ffrbtkey key, ffrbt_node **root, ffrbt_node *sentl)
{
	ffrbt_node *nod = *root
		, *prev = *root;

	while (nod != sentl) {
		prev = nod;
		if (key < nod->key)
			nod = nod->left;
		else if (key > nod->key)
			nod = nod->right;
		else //key == nod->key
			return (ffrbt_node*)nod;
	}

	*root = prev;
	return NULL;
}

void ffrbt_listins(ffrbtkey key, ffrbtree *tr, ffrbt_listnode *k)
{
	ffrbt_node *n = tr->root;
	ffrbt_listnode *found = (ffrbt_listnode*)ffrbt_findnode(key, &n, &tr->sentl);

	if (found == NULL)
		ffrbt_ins(key, tr, (ffrbt_node*)k, n);
	else {
		k->key = key;
		fflist_link(&k->sib, &found->sib);
		tr->len++;
	}
}

/** Replace nold with nnew. */
static void ffrbt_replace(ffrbtree *tr, ffrbt_node *nold, ffrbt_node *nnew)
{
	//rebind parent
	ffrbt_node **n = &tr->root;
	if (nold->parent != &tr->sentl) {
		n = &nold->parent->left;
		if (*n != (ffrbt_node*)nold)
			n = &nold->parent->right;
	}
	*n = nnew; // link parent with the node's sibling to the right

	//rebind left
	if (nold->left != &tr->sentl)
		nold->left->parent = nnew;

	//rebind right
	if (nold->right != &tr->sentl)
		nold->right->parent = nnew;
}

/*
     50
    /  \
   25   75
  /  \
15    35[0] <-> 35[1] <-> 35[2] ->|
     /  \
   30    45
*/
void ffrbt_listrm(ffrbtree *tr, ffrbt_listnode *k)
{
	ffrbt_node *next;

	FF_ASSERT(tr->len != 0);

	if (k->sib.prev != NULL) {
		// the node is just a list item
		fflist_unlink(&k->sib);
		tr->len--;
		return;
	}

	// leftmost node
	if (k->sib.next == NULL) {
		// the node has no siblings
		ffrbt_rm(tr, (ffrbt_node*)k);
		return;
	}

	// the node has a sibling
	next = (ffrbt_node*)ffrbt_nodebylist(k->sib.next);
	*next = *(ffrbt_node*)k;
	k->sib.next->prev = NULL;
	k->sib.next = k->sib.prev = NULL;

	ffrbt_replace(tr, (ffrbt_node*)k, next);
	tr->len--;
}

ffrbt_node * ffrbt_nextmin(ffrbt_iter *it)
{
	ffrbt_node *n = it->nod;
	int st = it->st;

	if (n == it->sentl)
		return n;

	for (;;) {
		if (st == FFRBT_LEFT) {
			if (n->left != it->sentl) {
				n = n->left; //move to the bottom left

			} else {
				st = FFRBT_RIGHT;
				if (it->ord == FFRBT_INORD)
					break; //no left node, return this one
			}

		} else if (st == FFRBT_RIGHT) {
			if (n->right != it->sentl) {
				n = n->right; //move once to the right
				st = FFRBT_LEFT;

			} else {
				st = FFRBT_UP;
				if (it->ord == FFRBT_POSTORD)
					break; //no right node, return this one
			}

		} else /*if (st == FFRBT_UP)*/ {
			if (n == it->sentl)
				break;

			if (n->parent->left == n) {
				n = n->parent;
				st = FFRBT_RIGHT;
				if (it->ord == FFRBT_INORD)
					break; //moved from the left up to the parent

			} else /*if (n->parent->right == n)*/ {
				n = n->parent;
				if (it->ord == FFRBT_POSTORD)
					break; //moved from the right up to the parent
			}
		}
	}

	it->st = st;
	it->nod = n;
	return n;
}

int ffrbt_iterlwalk(ffrbt_iter *it, ffrbt_on_item_t on_item, void *udata)
{
	int rc;
	ffrbt_node *_nod;
	ffrbt_listnode *nod;
	fflist_item *listiter
		, *li;

	FFRBT_WALK(*it, _nod) {
		nod = (ffrbt_listnode*)_nod;

		FFLIST_WALKSAFE(nod->sib.next, FFLIST_END, listiter, li) {
			rc = on_item(udata, ffrbt_nodebylist(li));
			if (rc != 0)
				return rc;
		}

		rc = on_item(udata, nod);
		if (rc != 0)
			return rc;
	}

	return 0;
}
