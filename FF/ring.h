/** Ring buffer.
Copyright (c) 2017 Simon Zolin
*/

#pragma once
#include <FF/array.h>
#include <FF/number.h>
#include <FFOS/atomic.h>
#include <FFOS/mem.h>


/** Circular array of pointers with fixed number of elements.
Readers and writers can run in parallel.
Empty buffer: [(r,w). . . .]
Full buffer: [(r)E1 E2 E3 (w).]
Full buffer: [(w). (r)E1 E2 E3]
*/
struct ffring {
	void **d;
	size_t cap;
	ffatomic r, w;
};
typedef struct ffring ffring;

/**
@size: power of 2. */
FF_EXTN int ffring_create(ffring *r, size_t size, uint align);

static FFINL void ffring_destroy(ffring *r)
{
	ffmem_alignfree(r->d);
}

/** Add element to ring buffer.
Return 0 on success;  <0 if full. */
FF_EXTN int ffring_write(ffring *r, void *p);

/** Read element from ring buffer.
Return 0 on success;  <0 if empty. */
FF_EXTN int ffring_read(ffring *r, void **p);

/** Get the number of filled elements. */
static FFINL size_t ffring_unread(ffring *r)
{
	return (ffatom_get(&r->w) - ffatom_get(&r->r)) & (r->cap - 1);
}

static FFINL int ffring_full(ffring *r)
{
	size_t wnew = ffint_increset2(ffatom_get(&r->w), r->cap);
	return (wnew == ffatom_get(&r->r));
}

static FFINL int ffring_empty(ffring *r)
{
	return (ffatom_get(&r->r) == ffatom_get(&r->w));
}


typedef struct ffringbuf {
	char *data;
	size_t cap;
	size_t r, w;
} ffringbuf;

/**
@cap: power of 2. */
static FFINL void ffringbuf_init(ffringbuf *r, void *p, size_t cap)
{
	FF_ASSERT(0 == (cap & (cap - 1)));
	r->data = p;
	r->cap = cap;
	r->r = r->w = 0;
}

static FFINL void ffringbuf_reset(ffringbuf *r)
{
	r->r = r->w = 0;
}

#define ffringbuf_data(r)  ((r)->data)
#define ffringbuf_cap(r)  ((r)->cap)

static FFINL int ffringbuf_full(ffringbuf *r)
{
	size_t wnew = (r->w + 1) & (r->cap - 1);
	return wnew == r->r;
}

static FFINL int ffringbuf_empty(ffringbuf *r)
{
	return r->r == r->w;
}

/** Return # of bytes available to read. */
static FFINL size_t ffringbuf_canread(ffringbuf *r)
{
	return (r->w - r->r) & (r->cap - 1);
}

/** Return # of sequential bytes available to read. */
static FFINL size_t ffringbuf_canread_seq(ffringbuf *r)
{
	return (r->w >= r->r) ? r->w - r->r : r->cap - r->r;
}

/** Return # of bytes available to write. */
static FFINL size_t ffringbuf_canwrite(ffringbuf *r)
{
	return (r->r - r->w - 1) & (r->cap - 1);
}

/** Return # of sequential bytes available to write. */
static FFINL size_t ffringbuf_canwrite_seq(ffringbuf *r)
{
	if (r->r > r->w)
		return r->r - r->w - 1;
	return (r->r == 0) ? r->cap - r->w - 1 : r->cap - r->w;
}

/** Append data until out of available sequential space.
Return # of bytes written. */
FF_EXTN size_t ffringbuf_write_seq(ffringbuf *r, const void *data, size_t len);

/** Append data until buffer is full.
Return # of bytes written. */
FF_EXTN size_t ffringbuf_write(ffringbuf *r, const void *data, size_t len);

/** Append (overwrite) data. */
FF_EXTN void ffringbuf_overwrite(ffringbuf *r, const void *data, size_t len);

/** Get chunk of sequential data. */
FF_EXTN void ffringbuf_readptr(ffringbuf *r, ffstr *dst, size_t len);

/** Read from buffer until out of available sequential data.
Return bytes copied. */
FF_EXTN size_t ffringbuf_read_seq(ffringbuf *r, void *dst, size_t len);

/** Read from buffer.
Return bytes copied. */
FF_EXTN size_t ffringbuf_read(ffringbuf *r, void *dst, size_t len);
