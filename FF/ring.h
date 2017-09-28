/** Ring buffer.
Copyright (c) 2017 Simon Zolin
*/

#pragma once
#include <FF/number.h>
#include <FFOS/atomic.h>
#include <FFOS/mem.h>


/** Circular array of pointers with fixed number of elements.
Readers and writers can run in parallel.
Empty buffer: [(r,w). . . .]
Full buffer: [(r)E1 E2 E3 (w).]
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
