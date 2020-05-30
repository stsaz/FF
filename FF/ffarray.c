/**
Copyright (c) 2016 Simon Zolin
*/

#include <FF/array.h>


char *_ffarr_grow(ffarr *ar, size_t by, ssize_t lowat, size_t elsz)
{
	size_t newcap = ar->len + by;
	if (ar->cap >= newcap)
		return ar->ptr;

	if (lowat & FFARR_GROWQUARTER) {
		lowat &= ~FFARR_GROWQUARTER;
		lowat = ffmax((size_t)lowat, newcap / 4);
	}

	newcap = ar->len + ffmax((size_t)lowat, by);
	return (char*)_ffarr_realloc(ar, newcap, elsz);
}

void* ffarr_pushgrow(ffarr *ar, size_t lowat, size_t elsz)
{
	if (NULL == _ffarr_grow(ar, 1, lowat, elsz))
		return NULL;

	ar->len += 1;
	return ar->ptr + (ar->len - 1) * elsz;
}

ssize_t ffarr_gather(ffarr *ar, const char *d, size_t len, size_t until)
{
	if (ar->len >= until)
		return 0;

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
	return len;
}

void _ffarr_rm(ffarr *ar, size_t off, size_t n, size_t elsz)
{
	FF_ASSERT(ar->len >= off + n);

	if (off + n == ar->len) {
		// remove from the right side
		ar->len -= n;
		return;
	}

	if (ar->cap == 0) {
		if (off != 0)
			return; //reallocation is needed
		ar->ptr += n * elsz;
	} else
		memmove(ar->ptr + off * elsz, ar->ptr + (off + n) * elsz, (ar->len - off - n) * elsz);
	ar->len -= n;
}

void _ffarr_crop(ffarr *ar, size_t off, size_t n, size_t elsz)
{
	if (off != 0) {
		if (ar->cap != 0)
			memmove(ar->ptr, ar->ptr + off * elsz, n * elsz);
		else
			ar->ptr += off + elsz;
	}
	ar->len = n;
}

/*
....DATA....
ds  o   o+s de
*/
size_t ffstr_crop_abs(ffstr *data, uint64 start, uint64 off, uint64 size)
{
	uint64 l, r, end = start + data->len;
	l = ffmin(ffmax(start, off), end);
	r = ffmin(off + size, end);
	data->ptr += l - start;
	data->len = r - l;
	return r - start;
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
	if (ffarr_end(buf) != src)
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
		//fallthrough

	case I_INPUTDATA:
		ffarr_free(buf);
		if (d->data.len == 0)
			return FFBUF_MORE;
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


/*
CTGLEN: length of contiguous data
BUFFER: temporary buffer
INPUT: input buffer
. If BUFFER contains data (partially filled up to <CTGLEN - 1>):
  . Append up to <CTGLEN - 1> from INPUT
  . If BUFFER is less than <CTGLEN>, wait for more
  . Return data in BUFFER to user
  . If user calls ffbuf_contig_store():
    . If BUFFER is less than <(CTGLEN - 1) * 2>, store only the last <CTGLEN - 1> bytes, wait for more
    . or clear BUFFER
. Return data in INPUT to user
. If user calls ffbuf_contig_store(), store the last chunk from INPUT (<CTGLEN - 1>) and wait for more
*/
int ffbuf_contig(ffarr *buf, const ffstr *in, size_t ctglen, ffstr *s)
{
	FF_ASSERT(ctglen != 0);
	s->len = 0;

	if (buf->len != 0) {
		if (buf->len >= ctglen) {
			ffstr_set2(s, buf);
			return 0;
		}

		size_t n = ffmin(ctglen - 1, in->len);
		if (NULL == ffarr_append(buf, in->ptr, n))
			return -1;
		if (buf->len < ctglen)
			return 0;
		ffstr_set2(s, buf);
		return n;
	}

	if (in->len < ctglen)
		return 0;
	ffstr_set2(s, in);
	return 0;
}

int ffbuf_contig_store(ffarr *buf, const ffstr *in, size_t ctglen)
{
	FF_ASSERT(ctglen != 0);
	if (buf->len != 0) {
		ctglen = ffmin(ctglen - 1, buf->len);
		if (ffstr_match(in, buf->ptr + buf->len - ctglen, ctglen)) {
			buf->len = 0;
			return 0;
		}
		_ffarr_rmleft(buf, buf->len - ctglen, sizeof(char));
		return 0;
	}

	ctglen = ffmin(ctglen - 1, in->len);
	if (NULL == ffarr_append(buf, ffarr_end(in) - ctglen, ctglen))
		return -1;
	return in->len;
}


ffmblk* ffmblk_chain_push(ffchain *blocks)
{
	ffmblk *mblk;
	if (NULL == (mblk = ffmem_allocT(1, ffmblk)))
		return NULL;
	ffarr_null(&mblk->buf);
	ffchain_add(blocks, &mblk->sib);
	return mblk;
}
