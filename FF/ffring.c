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

/*
. Try to reserve the space for the data to add.
  If another writer has reserved this space before us, we try again.
. Write new data
. Wait for the previous writers to finish their job
. Finalize: update writer-tail pointer
*/
int _ffring_write(ffring *r, void *p, uint single)
{
	size_t head_old, head_new;

	for (;;) {
		head_old = ffatom_get(&r->whead);
		head_new = ffint_increset2(head_old, r->cap);
		if (head_new == ffatom_get(&r->r))
			return -1;
		if (single)
			break;
		if (ffatom_cmpset(&r->whead, head_old, head_new))
			break;
		// other writer has added another element
	}

	r->d[head_old] = p;

	if (!single) {
		while (ffatom_get(&r->wtail) != head_old) {
			ffcpu_pause();
		}
	}

	ffatom_fence_rel(); // the element is complete when reader sees it
	ffatom_set(&r->wtail, head_new);
	return 0;
}

int ffring_read(ffring *r, void **p)
{
	void *rc;
	size_t rr, rnew;

	for (;;) {
		rr = ffatom_get(&r->r);
		if (rr == ffatom_get(&r->wtail))
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


size_t ffringbuf_write_seq(ffringbuf *r, const void *data, size_t len)
{
	size_t n = ffmin(ffringbuf_canwrite_seq(r), len);
	ffmemcpy(r->data + r->w, data, n);
	r->w = ffint_add_reset2(r->w, n, r->cap);
	return n;
}

size_t ffringbuf_write(ffringbuf *r, const void *data, size_t len)
{
	size_t n = ffringbuf_write_seq(r, data, len);
	if (n != len)
		n += ffringbuf_write_seq(r, (byte*)data + n, len - n);
	return n;
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
	r->w = ffint_add_reset2(r->w, len, r->cap);
	r->r = ffint_add_reset2(r->r, ow, r->cap);
}

void ffringbuf_readptr(ffringbuf *r, ffstr *dst, size_t len)
{
	size_t n = ffmin(len, ffringbuf_canread_seq(r));
	ffstr_set(dst, r->data + r->r, n);
	r->r = ffint_add_reset2(r->r, n, r->cap);
}

size_t ffringbuf_read_seq(ffringbuf *r, void *dst, size_t len)
{
	ffstr d;
	ffringbuf_readptr(r, &d, len);
	if (d.len != 0)
		ffmemcpy(dst, d.ptr, d.len);
	return d.len;
}

size_t ffringbuf_read(ffringbuf *r, void *dst, size_t len)
{
	size_t n = ffringbuf_read_seq(r, dst, len);
	if (n != len)
		n += ffringbuf_read_seq(r, (byte*)dst + n, len - n);
	return n;
}
