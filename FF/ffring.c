/**
Copyright (c) 2017 Simon Zolin
*/

#include <FF/ring.h>


int ffring_create(ffring *r, size_t size, uint align)
{
	if (NULL == (r->d = ffmem_align(size * sizeof(void*), align)))
		return -1;
	r->cap = size;
	return 0;
}

int ffring_write(ffring *r, void *p)
{
	size_t ww, wnew;

	for (;;) {
		ww = ffatom_get(&r->w);
		wnew = ffint_increset2(ww, r->cap);
		if (wnew == ffatom_get(&r->r))
			return -1;
		r->d[ww] = p;
		ffatom_fence_rel(); // the element is complete when reader sees it
		if (ffatom_cmpset(&r->w, ww, wnew))
			break;
		// other writer has added another element
	}
	return 0;
}

int ffring_read(ffring *r, void **p)
{
	void *rc;
	size_t rr, rnew;

	for (;;) {
		rr = ffatom_get(&r->r);
		if (rr == ffatom_get(&r->w))
			return -1;
		ffatom_fence_acq(); // if we see an unread element, it's complete
		rc = r->d[rr];
		rnew = ffint_increset2(rr, r->cap);
		if (ffatom_cmpset(&r->r, rr, rnew))
			break;
		// other reader has read this element
	}

	*p = rc;
	return 0;
}
