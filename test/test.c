
/**
Copyright (c) 2013 Simon Zolin
*/

#include <FFOS/test.h>
#include <FFOS/timer.h>
#include <FF/array.h>
#include <FF/crc.h>
#include <FF/path.h>
#include <FF/bitops.h>
#include <FF/rbtree.h>
#include <FF/list.h>

#include <test/all.h>

#define x FFTEST_BOOL
#define CALL FFTEST_TIMECALL

uint _fftest_nrun;
uint _fftest_nfail;

static int test_crc()
{
	x(0x7052c01a == ffcrc32_get(FFSTR("hello, man!"), 0));
	x(0x7052c01a == ffcrc32_get(FFSTR("HELLO, MAN!"), 1));
	return 0;
}

static int test_path()
{
	ffstr s;
	char buf[60];
	size_t n;

	n = ffpath_norm(buf, FFCNT(buf), FFSTR("/path/file"), 0);
	buf[n] = '\0';
	x(0 == strcmp(buf, "/path/file"));

	n = ffpath_norm(buf, FFCNT(buf), FFSTR("//path//file//"), 0);
	buf[n] = '\0';
	x(0 == strcmp(buf, "/path/file/"));

	n = ffpath_norm(buf, FFCNT(buf), FFSTR("/path//..//path2//./file//./"), 0);
	buf[n] = '\0';
	x(0 == strcmp(buf, "/path2/file/"));

#ifdef FF_WIN
	n = ffpath_norm(buf, FFCNT(buf), FFSTR("c:\\path\\\\..//..\\path2//./file//./"), 0);
	buf[n] = '\0';
	x(0 == strcmp(buf, "c:/path2/file/"));
#endif

	n = ffpath_norm(buf, FFCNT(buf), FFSTR("//path/../.././file/./"), 0);
	buf[n] = '\0';
	x(0 == strcmp(buf, "/file/"));

	n = ffpath_norm(buf, FFCNT(buf), FFSTR("/path/.."), FFPATH_STRICT_BOUNDS);
	buf[n] = '\0';
	x(0 == strcmp(buf, "/"));

	n = ffpath_norm(buf, FFCNT(buf), FFSTR("/path/."), FFPATH_STRICT_BOUNDS);
	buf[n] = '\0';
	x(0 == strcmp(buf, "/path/"));

	x(0 == ffpath_norm(buf, FFCNT(buf), FFSTR("/path/../../file/./"), FFPATH_STRICT_BOUNDS));
	x(0 == ffpath_norm(buf, FFCNT(buf), FFSTR("./path/../../file/./"), FFPATH_STRICT_BOUNDS));
	x(0 == ffpath_norm(buf, FFCNT(buf), FFSTR("/.."), FFPATH_STRICT_BOUNDS));
	x(0 == ffpath_norm(buf, FFCNT(buf), FFSTR("../"), FFPATH_STRICT_BOUNDS));
	x(0 == ffpath_norm(buf, FFCNT(buf), FFSTR("/../"), FFPATH_STRICT_BOUNDS));

	x(FFSLEN("filename") == ffpath_makefn(buf, FFCNT(buf), FFSTR("filename"), '_'));
	n = ffpath_makefn(buf, FFCNT(buf), FFSTR("\x00\x1f *?/\\:\""), '_');
	buf[n] = '\0';
	x(0 == strcmp(buf, "__ ______"));

#define FN "/path/file"
	x(FN + FFSLEN(FN) - FFSLEN("/file") == ffpath_rfindslash(FN, FFSLEN(FN)));
	x(TEXT(FN) + FFSLEN(FN) - FFSLEN("/file") == ffpathq_rfindslash(TEXT(FN), FFSLEN(FN)));
#undef FN

#define FN "file"
	x(FN + FFSLEN(FN) == ffpath_rfindslash(FN, FFSLEN(FN)));
#undef FN

	s = ffpath_fileext(FFSTR("file.txt"));
	x(ffstr_eq(&s, FFSTR("txt")));

	s = ffpath_fileext(FFSTR("qwer"));
	x(ffstr_eq(&s, FFSTR("")));

	return 0;
}

static int test_bits()
{
	uint64 i8;
	uint i4;
	size_t i;
	uint mask[2] = { 0 };

	i8 = 1;
	x(0 != ffbit_test64(i8, 0));
	i4 = 1;
	x(0 != ffbit_test32(i4, 0));
	i = 1;
	x(0 != ffbit_test(i, 0));

	i8 = 0x8000000000000000ULL;
	x(0 != ffbit_set64(&i8, 63));
	x(i8 == 0x8000000000000000ULL);
	i4 = 0x80000000;
	x(0 != ffbit_set32(&i4, 31));
	x(i4 == 0x80000000);
	i = 0;
	x(0 == ffbit_set(&i, 31));
	x(i == 0x80000000);

	x(0 == ffbit_setarr(mask, 47));
	x(0 != ffbit_testarr(mask, 47));

	i8 = 0x8000000000000000ULL;
	x(0 != ffbit_reset64(&i8, 63) && i8 == 0);
	i4 = 0x80000000;
	x(0 != ffbit_reset32(&i4, 31) && i4 == 0);
	i = (size_t)-1;
	x(0 != ffbit_reset(&i, 31));

	i8 = 0x8000000000000000ULL;
	x(63 == ffbit_ffs64(i8)-1);
	i8 = 0;
	x(0 == ffbit_ffs64(i8));
	i4 = 0x80000000;
	x(31 == ffbit_ffs32(i4)-1);
	i4 = 0;
	x(0 == ffbit_ffs32(i4));
	i = 0;
	x(0 == ffbit_ffs(i));
	return 0;
}

#if 0
static int rbtRm(void *udata, ffrbt_listnode *nod)
{
	ffrbtree *tr = udata;
	ffrbt_listrm(tr, nod);
	return 0;
}

static int test_rbtlist()
{
	ffrbtree tr;
	enum { NUM = 100000, LNUM = 10 };
	int i;
	int n;
	ffrbt_listnode *ar;
	ffrbt_iter it;
	ffrbt_node *nod;

	FFTEST_FUNC;

	ffos_init();
	ffrbt_init(&tr);
	ar = (ffrbt_listnode*)ffmem_calloc(NUM * LNUM, sizeof(ffrbt_listnode));
	x(ar != NULL);

	n = 0;
	for (i = 0; i < NUM; i++) {
		int k;
		for (k = 0; k < LNUM; k++) {
			ffrbt_listins(i, &tr, &ar[n++]);
		}
	}

	x(tr.len == NUM * LNUM);

	i = 0;
	ffrbt_iterinit(&it, &tr);
	FFRBT_WALK(it, nod) {
		ffrbt_listnode *nl = (ffrbt_listnode*)nod;
		fflist_item *li;

		n = 1;
		x(nl->key == i);

		FFLIST_WALKNEXT(nl->sib.next, FFLIST_END, li) {
			nl = ffrbt_nodebylist(li);
			x(nl->key == i);
			n++;
		}

		x(n == LNUM);

		i++;
	}
	x(i == NUM);

	i = 0;
	ffrbt_iterinit(&it, &tr);
	FFRBT_WALK(it, nod) {
		x(i++ == nod->key);
	}
	x(i == NUM);

	{
		ffrbt_node *rt;
		ffrbt_node *found;

		found = ffrbt_find(NUM / 3, tr.root, &tr.sentl);
		x(found->key == NUM / 3);

		rt = tr.root;
		found = ffrbt_findnode(NUM, &rt, &tr.sentl);
		x(found == NULL && rt->key == NUM - 1);
	}

	ffrbt_listwalk(&tr, &rbtRm, &tr);
	x(tr.len == 0);

	ffmem_free(ar);
	return 0;
}

#else
void ffrbt_ins(ffrbtkey key, ffrbtree *tr, ffrbt_node *nod, ffrbt_node *ancestor){}
void ffrbt_rm(ffrbtree *tr, ffrbt_node *nod){}
#endif

static int test_list()
{
	fflist ls;
	fflist_item i1, i2, i3;
	fflist_item *li;
	int n;

	fflist_init(&ls);
	fflist_ins(&ls, &i1);
	fflist_ins(&ls, &i2);
	x(ls.first == &i1 && ls.last == &i2);
	x(ls.len == 2);

	fflist_makefirst(&ls, &i2);
	x(ls.first == &i2 && ls.last == &i1);
	fflist_makelast(&ls, &i2);
	x(ls.first == &i1 && ls.last == &i2);

	fflist_link(&i3, &i1);
	// i1 -> i3 -> i2
	x(i1.next == &i3 && i3.prev == &i1 && i3.next == &i2);

	n = 0;
	FFLIST_WALKNEXT(&i1, FFLIST_END, li) {
		switch (n++) {
		case 0:
			x(li == &i1);
			break;
		case 1:
			x(li == &i3);
			break;
		case 2:
			x(li == &i2);
			break;
		}
	}

	fflist_unlink(&i3);
	x(i1.next == &i2 && i2.prev == &i1);

	return 0;
}

int test_all()
{
	ffos_init();

	test_bits();
	test_str();
	test_list();
	// test_rbtlist();
	test_crc();
	test_path();
	test_url();
	test_http();

	FFTEST_TIMECALL(test_json());
	test_conf();
	test_args();

	printf("Tests run: %u.  Failed: %u.\n", _fftest_nrun, _fftest_nfail);

	return 0;
}

int main(int argc, const char **argv)
{
	return test_all();
}
