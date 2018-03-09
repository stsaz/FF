/**
Copyright (c) 2015 Simon Zolin
*/

#include <FF/data/cue.h>
#include <test/all.h>
#include <FFOS/test.h>

#define x FFTEST_BOOL


static const uint idx_prev[] = {
	1, 3,
	3, 4,
};

static const uint idx_prev1[] = {
	0, 3,
	3, 4,
};

static const uint idx_curr[] = {
	0, 2,
	2, 4,
};


static const uint idx_skip[] = {
	1, 2,
	3, 4,
};

static const uint *const idxs[] = {
	idx_prev, idx_prev1, idx_curr, idx_skip
};

int test_cue(void)
{
	ffcue cu;
	ffcuep p;
	ffcuetrk *trk;
	char buf[4096];
	ffstr s, s1;
	int r;
	uint i, k, last;

	FFTEST_FUNC;

	s.len = _test_readfile(TESTDIR "/1.cue", buf, sizeof(buf));
	s.ptr = buf;

	for (i = 0;  i != FFCNT(idxs);  i++) {
		k = 0;
		ffmem_tzero(&p);
		ffcue_init(&p);
		ffmem_tzero(&cu);
		cu.options = i;
		s1 = s;
		last = 0;

		FFDBG_PRINT(5, "%s(): ffcue.options=%u\n", FF_FUNC, cu.options);

		for (;;) {
			size_t n = s1.len;
			r = ffcue_parse(&p, s1.ptr, &n);
			if (r == FFPARS_MORE) {
				last = 1;
				n = 1;
				r = ffcue_parse(&p, "\n", &n);
			}
			ffstr_shift(&s1, n);

			if (NULL == (trk = ffcue_index(&cu, r, p.intval)))
				continue;
			x(trk->from == idxs[i][k]);
			x(trk->to == idxs[i][k + 1]);
			k += 2;

			if (last)
				break;
		}
		x(k == FFCNT(idx_prev));
	}

	return 0;
}
