/**
Copyright (c) 2016 Simon Zolin
*/

#include <FF/array.h>


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
