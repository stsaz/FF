/**
Copyright (c) 2016 Simon Zolin
*/

#include <FF/array.h>


void * _ffarr_realloc(ffarr *ar, size_t newlen, size_t elsz)
{
	void *d = NULL;

	if (newlen == 0)
		newlen = 1; //ffmem_realloc() returns NULL if requested buffer size is 0

	if (ar->cap != 0) {
		if (ar->cap == newlen)
			return ar->ptr; //nothing to do
		d = ar->ptr;
	}
	d = ffmem_realloc(d, newlen * elsz);
	if (d == NULL)
		return NULL;

	if (ar->cap == 0 && ar->len != 0)
		ffmemcpy(d, ar->ptr, ffmin(ar->len, newlen));

	ar->ptr = d;
	ar->cap = newlen;
	ar->len = ffmin(ar->len, newlen);
	return d;
}

char *_ffarr_grow(ffarr *ar, size_t by, ssize_t lowat, size_t elsz)
{
	size_t newcap = ar->len + by;
	if (ar->cap >= newcap)
		return ar->ptr;

	if (lowat == FFARR_GROWQUARTER)
		lowat = newcap / 4; //allocate 25% more
	if (by < (size_t)lowat)
		newcap = ar->len + (size_t)lowat;
	return (char*)_ffarr_realloc(ar, newcap, elsz);
}

void _ffarr_free(ffarr *ar)
{
	if (ar->cap != 0) {
		FF_ASSERT(ar->ptr != NULL);
		FF_ASSERT(ar->cap >= ar->len);
		ffmem_free(ar->ptr);
		ar->cap = 0;
	}
	ar->ptr = NULL;
	ar->len = 0;
}

void * _ffarr_push(ffarr *ar, size_t elsz)
{
	if (ar->cap < ar->len + 1
		&& NULL == _ffarr_realloc(ar, ar->len + 1, elsz))
		return NULL;

	ar->len += 1;
	return ar->ptr + (ar->len - 1) * elsz;
}

void * _ffarr_append(ffarr *ar, const void *src, size_t num, size_t elsz)
{
	if (ar->cap < ar->len + num
		&& NULL == _ffarr_realloc(ar, ar->len + num, elsz))
		return NULL;

	ffmemcpy(ar->ptr + ar->len * elsz, src, num * elsz);
	ar->len += num;
	return ar->ptr + ar->len * elsz;
}

ssize_t ffarr_append_until(ffarr *ar, const char *d, size_t len, size_t until)
{
	FF_ASSERT(ar->len <= until);

	if (ar->len == 0 && len >= until) {
		ffarr_free(ar);
		ffstr_set(ar, d, until);
		return until;

	} else if (ar->cap == 0 && ar->ptr + ar->len == d && ar->len + len >= until) {
		// 'ar' and 'd' point to the same memory region
		size_t r = until - ar->len;
		ar->len = until;
		return r;
	}

	if (ar->cap < until
		&& NULL == _ffarr_realloc(ar, until, sizeof(char)))
		return -1;

	len = ffmin(len, until - ar->len);
	ffarr_append(ar, d, len);
	return (ar->len == until) ? len : 0;
}

void _ffarr_rmleft(ffarr *ar, size_t n, size_t elsz)
{
	FF_ASSERT(ar->len >= n);
	if (ar->cap == 0)
		ar->ptr += n * elsz;
	else
		memmove(ar->ptr, ar->ptr + n * elsz, (ar->len - n) * elsz);
	ar->len -= n;
}


size_t ffbuf_add(ffstr3 *buf, const char *src, size_t len, ffstr *dst)
{
	size_t sz;

	if (buf->len == 0 && len > buf->cap) {
		// input is larger than buffer
		sz = len - (len % buf->cap);
		ffstr_set(dst, src, sz);
		return sz;
	}

	sz = ffmin(len, ffarr_unused(buf));
	ffmemcpy(buf->ptr + buf->len, src, sz);
	buf->len += sz;

	if (buf->cap != buf->len) {
		dst->len = 0;
		return sz;
	}

	//buffer is full
	ffstr_set(dst, buf->ptr, buf->len);
	buf->len = 0;
	return sz;
}

int ffbuf_gather(ffarr *buf, struct ffbuf_gather *d)
{
	enum { I_INPUT, I_INPUTDATA, I_CHK, I_CHK_COPIED };
	size_t r;

	for (;;) {
	switch (d->state) {

	case I_INPUT:
		if (buf->len != 0) {
			d->buflen = buf->len;
			r = ffmin(d->data.len, d->ctglen - 1);
			if (NULL == ffarr_append(buf, d->data.ptr, r))
				return FFBUF_ERR;

			d->state = I_CHK_COPIED;
			return FFBUF_READY;
		}
		// break

	case I_INPUTDATA:
		ffarr_free(buf);
		ffarr_set(buf, d->data.ptr, d->data.len);
		d->state = I_CHK;
		return FFBUF_READY;

	case I_CHK:
		if (d->off == 0) {
			uint n = ffmin(d->data.len, d->ctglen - 1);
			if (NULL == ffarr_copy(buf, d->data.ptr + d->data.len - n, n))
				return FFBUF_ERR;
			return FFBUF_MORE;
		}

		if (--d->off != 0)
			_ffarr_rmleft(buf, d->off, sizeof(char));
		buf->len = d->ctglen;
		ffstr_shift(&d->data, d->off + d->ctglen);
		return FFBUF_DONE;

	case I_CHK_COPIED:
		if (d->off == 0) {
			if (d->data.len > d->ctglen - d->buflen) {
				d->state = I_INPUTDATA;
				continue;
			}
			return FFBUF_MORE;
		}
		d->off--;
		_ffarr_crop(buf, d->off, d->ctglen, sizeof(char)); // "...DATA..." -> "DATA"
		ffstr_shift(&d->data, d->off + d->ctglen - d->buflen); // "ATA..." -> "..."
		return FFBUF_DONE;
	}
	}
}
