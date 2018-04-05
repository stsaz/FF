/**
Copyright (c) 2017 Simon Zolin
*/

#include <FF/ring.h>


int ffring_create(ffring *r, size_t size, uint align)
{
	FF_ASSERT(0 == (size & (size - 1)));
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


size_t ffringbuf_write(ffringbuf *r, const void *data, size_t len)
{
	len = ffmin(len, ffringbuf_canwrite(r));
	size_t n = ffmin(ffringbuf_canwrite_seq(r), len);
	ffmemcpy(r->data + r->w, data, n);
	if (len != n)
		ffmemcpy(r->data, (byte*)data + n, len - n);
	r->w = (r->w + len) & (r->cap - 1);
	return len;
}

void ffringbuf_overwrite(ffringbuf *r, const void *data, size_t len)
{
	if (len >= r->cap) {
		data = (byte*)data + len - (r->cap - 1);
		len = r->cap - 1;
	}
	size_t n = ffmin(r->cap - r->w, len);
	ffmemcpy(r->data + r->w, data, n);
	if (len != n)
		ffmemcpy(r->data, (byte*)data + n, len - n);
	size_t ow = ffmax((ssize_t)(len - ffringbuf_canwrite(r)), 0);
	r->w = (r->w + len) & (r->cap - 1);
	r->r = (r->r + ow) & (r->cap - 1);
}
