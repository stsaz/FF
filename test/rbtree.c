/**
Copyright (c) 2014 Simon Zolin
*/

#include <test/all.h>
#include <FF/rbtree.h>
#include <FFOS/mem.h>
#include <FFOS/test.h>

#define x FFTEST_BOOL


int test_rbtree(void)
{
	ffrbtree tr;
	enum { NUM = 10000, OFF = 1000, LNUM = 1 };
	int i;
	int n;
	ffrbt_node *ar;
	fftree_node *nod;

	FFTEST_FUNC;

	ffrbt_init(&tr);
	ar = (ffrbt_node*)ffmem_calloc(NUM * LNUM, sizeof(ffrbt_node));
	x(ar != NULL);

	n = 0;
	for (i = OFF; i < NUM; i++) {
		ar[n].key = i;
		ffrbt_insert(&tr, &ar[n++], NULL);
	}
	for (i = 0; i < OFF; i++) {
		ar[n].key = i;
		ffrbt_insert(&tr, &ar[n++], NULL);
	}

	x(tr.len == NUM * LNUM);

	i = 0;
	FFTREE_WALK(&tr, nod) {
		n = 1;
		x(nod->key == i);
		i++;
	}
	x(i == NUM);

	i = 0;
	FFTREE_WALK(&tr, nod) {
		x(i++ == nod->key);
	}
	x(i == NUM);

	{
		ffrbt_node *rt;
		ffrbt_node *found;

		found = ffrbt_find(&tr, NUM / 3, NULL);
		x(found->key == NUM / 3);

		found = ffrbt_find(&tr, NUM, &rt);
		x(found == NULL && rt->key == NUM - 1);
	}

	for (i = (NUM / 2) * LNUM;  i != (NUM / 2 + OFF) * LNUM;  i++) {
		ffrbt_rm(&tr, &ar[i]);
	}
	ffrbt_node *nod2, *next;
	FFTREE_FOR(&tr, nod2) {
		next = ffrbt_successor(&tr, nod2);
		ffrbt_rm(&tr, (void*)nod2);
		nod2 = next;
	}
	x(tr.len == 0);

	ffmem_free(ar);
	return 0;
}

static void rbtl_free(void *p)
{
}

int test_rbtlist()
{
	ffrbtree tr;
	enum { NUM = 10000, OFF = 1000, LNUM = 10 };
	int i;
	int n;
	ffrbtl_node *ar;
	fftree_node *nod;

	FFTEST_FUNC;

	ffrbt_init(&tr);
	ar = (ffrbtl_node*)ffmem_calloc(NUM * LNUM, sizeof(ffrbtl_node));
	x(ar != NULL);

	n = 0;
	for (i = OFF; i < NUM; i++) {
		int k;
		for (k = 0; k < LNUM; k++) {
			ar[n].key = i;
			ffrbtl_insert(&tr, &ar[n++]);
		}
	}
	for (i = 0; i < OFF; i++) {
		int k;
		for (k = 0; k < LNUM; k++) {
			ar[n].key = i;
			ffrbtl_insert(&tr, &ar[n++]);
		}
	}

	x(tr.len == NUM * LNUM);

	i = 0;
	FFTREE_WALK(&tr, nod) {
		ffrbtl_node *nl = (ffrbtl_node*)nod;
		fflist_item *li;

		n = 1;
		x(nl->key == i);

		for (li = nl->sib.next;  li != &((ffrbtl_node*)nod)->sib;  li = li->next) {
			nl = ffrbtl_nodebylist(li);
			x(nl->key == i);
			n++;
		}

		x(n == LNUM);

		i++;
	}
	x(i == NUM);

	i = 0;
	FFTREE_WALK(&tr, nod) {
		x(i++ == nod->key);
	}
	x(i == NUM);

	{
		ffrbt_node *rt;
		ffrbt_node *found;

		found = ffrbt_find(&tr, NUM / 3, NULL);
		x(found->key == NUM / 3);

		found = ffrbt_find(&tr, NUM, &rt);
		x(found == NULL && rt->key == NUM - 1);
	}

	for (i = (NUM / 2) * LNUM;  i != (NUM / 2 + OFF) * LNUM;  i++) {
		ffrbtl_rm(&tr, &ar[i]);
	}
	ffrbtl_freeall(&tr, &rbtl_free, 0);
	x(tr.len == 0);

	ffmem_free(ar);
	return 0;
}
