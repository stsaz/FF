/**
Copyright (c) 2019 Simon Zolin
*/

#include <FF/sys/fileread.h>
#include <FF/array.h>
#include <FF/number.h>


static int fr_read_off(fffileread *f, uint64 off);
static int fr_read(fffileread *f);


struct fffileread {
	fffd fd;
	ffaio_filetask aio;
	uint state; //enum FI_ST
	uint64 eof; // end-of-file position set after the last block has been read
	uint64 async_off; // last user request's offset for which async operation is scheduled

	ffarr2 bufs; //struct buf[]
	uint wbuf;
	uint locked;

	fffileread_conf conf;
	struct fffileread_stat stat;
};

static void fr_log(fffileread *f, uint level, const char *fmt, ...)
{
	if (f->conf.log == NULL)
		return;

	ffarr a = {};
	va_list va;
	va_start(va, fmt);
	ffstr_catfmtv(&a, fmt, va);
	va_end(va);
	f->conf.log(f->conf.udata, level, (ffstr*)&a);
}

struct buf {
	size_t len;
	char *ptr;
	uint64 offset;
};

/** Create buffers aligned to system pagesize. */
static int bufs_create(fffileread *f, const fffileread_conf *conf)
{
	if (NULL == ffarr2_callocT(&f->bufs, conf->nbufs, struct buf))
		goto err;
	f->bufs.len = conf->nbufs;
	struct buf *b;
	FFARR_WALKT(&f->bufs, b, struct buf) {
		if (NULL == (b->ptr = ffmem_align(conf->bufsize, conf->bufalign)))
			goto err;
		b->offset = (uint64)-1;
	}
	return 0;

err:
	fr_log(f, 0, "%s", ffmem_alloc_S);
	return -1;
}

static void bufs_free(ffarr2 *bufs)
{
	struct buf *b;
	FFARR_WALKT(bufs, b, struct buf) {
		ffmem_alignfree(b->ptr);
	}
	ffarr2_free(bufs);
}

/** Find buffer containing file offset. */
static struct buf* bufs_find(fffileread *f, uint64 offset)
{
	struct buf *b;
	FFARR_WALKT(&f->bufs, b, struct buf) {
		if (ffint_within(offset, b->offset, b->offset + b->len))
			return b;
	}
	return NULL;
}

/** Prepare buffer for reading. */
static void buf_prepread(struct buf *b, uint64 off)
{
	b->len = 0;
	b->offset = off;
}


enum R {
	R_ASYNC,
	R_DONE,
	R_DATA,
	R_ERR,
};

enum FI_ST {
	FI_OK,
	FI_ERR,
	FI_ASYNC,
	FI_EOF,
};

fffileread* fffileread_create(const char *fn, fffileread_conf *conf)
{
	if (conf->nbufs == 0
		|| conf->bufalign == 0
		|| conf->bufsize == 0
		|| conf->bufalign != ff_align_power2(conf->bufalign)
		|| conf->bufsize != ff_align_floor2(conf->bufsize, conf->bufalign)
		|| (conf->directio && conf->onread == NULL))
		return NULL;

	fffileread *f;
	if (NULL == (f = ffmem_new(fffileread)))
		return NULL;
	f->fd = FF_BADFD;
	f->async_off = (uint64)-1;

	if (0 != bufs_create(f, conf))
		goto err;

	uint flags = conf->oflags;

	if (conf->kq != FF_BADFD)
		flags |= (conf->directio) ? FFO_DIRECT : 0;

	while (FF_BADFD == (f->fd = fffile_open(fn, flags))) {

#ifdef FF_LINUX
		if (fferr_last() == EINVAL && (flags & FFO_DIRECT)) {
			flags &= ~FFO_DIRECT;
			continue;
		}
#endif

		fr_log(f, 0, "%s: %s", fffile_open_S, fn);
		goto err;
	}

	ffaio_finit(&f->aio, f->fd, f);
	if (0 != ffaio_fattach(&f->aio, conf->kq, !!(flags & FFO_DIRECT))) {
		fr_log(f, 0, "%s: %s", ffkqu_attach_S, fn);
		goto err;
	}
	f->conf = *conf;

	conf->directio = !!(flags & FFO_DIRECT);
	return f;

err:
	fffileread_unref(f);
	return NULL;
}

void fffileread_unref(fffileread *f)
{
	FF_SAFECLOSE(f->fd, FF_BADFD, fffile_close);
	if (f->state == FI_ASYNC)
		return; //wait until AIO is completed

	bufs_free(&f->bufs);
	ffmem_free(f);
}

int fffileread_getdata(fffileread *f, ffstr *dst, uint64 off, uint flags)
{
	int r, cachehit = 0;
	struct buf *b;
	uint64 next;

	f->locked = (uint)-1;

	if (NULL != (b = bufs_find(f, off))) {
		if (f->async_off != off) {
			cachehit = 1;
			f->stat.ncached++;
		}
		f->async_off = (uint64)-1;
		goto done;
	}

	if (f->state == FI_ASYNC)
		return FFFILEREAD_RASYNC;
	else if (f->state == FI_EOF) {
		if (off > f->eof) {
			fr_log(f, 0, "seek offset %U is bigger than file size %U", off, f->eof);
			return FFFILEREAD_RERR;
		} else if (off == f->eof)
			return FFFILEREAD_REOF;
		f->state = FI_OK;
	}

	r = fr_read_off(f, ff_align_floor2(off, f->conf.bufalign));
	if (r == R_ASYNC) {
		f->async_off = off;
		return FFFILEREAD_RASYNC;
	} else if (r == R_ERR)
		return FFFILEREAD_RERR;

	//R_READ or R_DONE
	b = bufs_find(f, off);
	if (r == R_DONE && b == NULL)
		return FFFILEREAD_REOF;
	FF_ASSERT(b != NULL);

done:
	next = b->offset + b->len;
	if ((flags & FFFILEREAD_FREADAHEAD)
		&& ((flags & FFFILEREAD_FBACKWARD) || next != f->eof) // don't read past eof
		&& f->conf.directio && f->conf.nbufs != 1) {

		if (flags & FFFILEREAD_FBACKWARD)
			next = b->offset - f->conf.bufsize;

		if (NULL == bufs_find(f, next)
			&& f->state != FI_ASYNC) {
			fr_read_off(f, next);
		}
	}

	uint ibuf = b - (struct buf*)f->bufs.ptr;
	f->locked = ibuf;
	fr_log(f, 1, "returning buf#%u  off:%Uk  cache-hit:%u"
		, ibuf, b->offset / 1024, cachehit);

	ffarr_setshift(dst, b->ptr, b->len, off - b->offset);
	return FFFILEREAD_RREAD;
}

/** Async read has signalled.  Notify consumer about new events. */
static void fr_read_a(void *param)
{
	fffileread *f = param;
	int r;

	FF_ASSERT(f->state == FI_ASYNC);
	f->state = FI_OK;

	if (f->fd == FF_BADFD) {
		//chain was closed while AIO is pending
		fffileread_unref(f);
		return;
	}

	r = fr_read(f);
	if (r == R_ASYNC)
		return;

	f->conf.onread(f->conf.udata);
}

/** Start reading at the specified aligned offset. */
static int fr_read_off(fffileread *f, uint64 off)
{
	struct buf *b = ffarr_itemT(&f->bufs, f->wbuf, struct buf);
	FF_ASSERT(f->wbuf != f->locked);
	buf_prepread(b, off);
	return fr_read(f);
}

/** Read from file. */
static int fr_read(fffileread *f)
{
	int r;
	struct buf *b;

	b = ffarr_itemT(&f->bufs, f->wbuf, struct buf);
	r = (int)ffaio_fread(&f->aio, b->ptr, f->conf.bufsize, b->offset, &fr_read_a);
	if (r < 0) {
		if (fferr_again(fferr_last())) {
			fr_log(f, 1, "buf#%u: async read, offset:%Uk", f->wbuf, b->offset / 1024);
			f->state = FI_ASYNC;
			f->stat.nasync++;
			return R_ASYNC;
		}

		fr_log(f, 0, "%s: buf#%u offset:%Uk"
			, fffile_read_S, f->wbuf, b->offset / 1024);
		f->state = FI_ERR;
		return R_ERR;
	}

	b->len = r;
	f->stat.nread++;
	fr_log(f, 1, "buf#%u: read %L bytes at offset %Uk"
		, f->wbuf, b->len, b->offset / 1024);

	f->wbuf = ffint_cycleinc(f->wbuf, f->conf.nbufs);

	if ((uint)r != f->conf.bufsize) {
		fr_log(f, 1, "read the last block", 0);
		f->eof = b->offset + b->len;
		f->state = FI_EOF;
		return R_DONE;
	}
	return R_DATA;
}

fffd fffileread_fd(fffileread *f)
{
	return f->fd;
}

void fffileread_stat(fffileread *f, struct fffileread_stat *st)
{
	*st = f->stat;
}
