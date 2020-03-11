/**
Copyright (c) 2020 Simon Zolin
*/

#include <FF/sys/filewrite.h>
#include <FF/array.h>
#include <FFOS/number.h>
#include <FFOS/dir.h>


#define dbglog(f, fmt, ...)  fw_log(f, 1, fmt, __VA_ARGS__)
#define syserrlog(f, fmt, ...)  fw_log(f, 0, "%s: " fmt, (f)->name, __VA_ARGS__)

struct fffilewrite {
	fffilewrite_conf conf;
	fffilewrite_stat stat;
	fffd fd;
	char *name;
	uint64 off; // current offset
	uint64 size; // file size
	uint completed :1;

	uint uflags;
	ffarr buf; // bufferred data
	ffstr buf_input; // new data to write
	uint64 buf_off; // offset at which 'buf_input' should be written

	uint64 prealloc_size; // preallocated size
};

void fffilewrite_setconf(fffilewrite_conf *conf)
{
	ffmem_tzero(conf);
	conf->kq = FF_BADFD;
	conf->align = 4096;
	conf->bufsize = 64 * 1024;
	conf->create = 1;
	conf->mkpath = 1;
	conf->prealloc = 128 * 1024;
	conf->prealloc_grow = 1;
}

static void fw_log(fffilewrite *f, uint level, const char *fmt, ...)
{
	if (f->conf.log == NULL)
		return;

	ffarr a = {};
	va_list va;
	va_start(va, fmt);
	ffstr_catfmtv(&a, fmt, va);
	va_end(va);

	ffstr s;
	ffstr_set2(&s, &a);
	f->conf.log(f->conf.udata, level, s);
	ffarr_free(&a);
}

fffilewrite* fffilewrite_create(const char *fn, fffilewrite_conf *conf)
{
	fffilewrite *f = ffmem_new(fffilewrite);
	if (f == NULL)
		return NULL;
	f->conf = *conf;
	if (NULL == (f->name = ffsz_alcopyz(fn)))
		goto end;
	if (NULL == (f->buf.ptr = ffmem_align(conf->bufsize, conf->align)))
		goto end;
	f->buf.cap = conf->bufsize;
	f->fd = FF_BADFD;
	return f;

end:
	fffilewrite_free(f);
	return NULL;
}

void fffilewrite_free(fffilewrite *f)
{
	if (f == NULL)
		return;

	if (f->fd != FF_BADFD) {
		if (f->prealloc_size != 0
				&& 0 != fffile_trunc(f->fd, f->size))
			syserrlog(f, "fffile_trunc", 0);

		if (0 != fffile_close(f->fd))
			syserrlog(f, "%s", fffile_close_S, 0);

		if (f->conf.del_on_err && !f->completed) {
			if (0 != fffile_rm(f->name))
				syserrlog(f, "%s", fffile_rm_S);
			else
				dbglog(f, "removed file", 0);
		}
	}

	ffmem_free(f->name);
	ffmem_alignfree(f->buf.ptr);
	ffmem_free(f);
}

static int fw_open(fffilewrite *f)
{
	uint errmask = 0;
	uint flags = 0;
	if (f->conf.create)
		flags = (f->conf.overwrite) ? FFO_CREATE : FFO_CREATENEW;
	else
		f->conf.prealloc = 0;
	flags |= f->conf.oflags;
	flags |= FFO_WRONLY;
	while (FF_BADFD == (f->fd = fffile_open(f->name, flags))) {
		if (fferr_nofile(fferr_last()) && f->conf.mkpath) {
			if (ffbit_testset32(&errmask, 0))
				goto err;
			if (0 != ffdir_make_path(f->name, 0)) {
				syserrlog(f, "%s", ffdir_make_S);
				goto err;
			}
		} else {
			syserrlog(f, "%s", fffile_open_S);
			goto err;
		}
	}

	return 0;

err:
	return FFFILEWRITE_RERR;
}

/** Store data from 'buf_input' in internal buffer until the output data chunk is ready. */
static int fw_buf(fffilewrite *f, ffstr *dst)
{
	if (f->buf_off != f->off) {
		if (f->buf.len != 0) {
			ffstr_set2(dst, &f->buf);
			f->buf.len = 0;
			return 0; // return previously bufferred data
		}
		f->off = f->buf_off; // update next file write offset
	}

	if (f->uflags & FFFILEWRITE_FFLUSH) {
		if (f->buf.len != 0) {
			ffstr_set2(dst, &f->buf);
			f->buf.len = 0;
			return 0; // return previously bufferred data
		}

		if (f->buf_input.len == 0) {
			f->completed = 1;
			return FFFILEWRITE_RWRITTEN;
		}

		*dst = f->buf_input;
		f->buf_off += f->buf_input.len;
		f->buf_input.len = 0;
		return 0; // return new data
	}

	if (f->buf_input.len == 0)
		return FFFILEWRITE_RWRITTEN;

	size_t n = ffbuf_add(&f->buf, f->buf_input.ptr, f->buf_input.len, dst);
	ffstr_shift(&f->buf_input, n);
	f->buf_off += n;
	if (dst->len != 0)
		return 0; // return full buffer

	f->stat.nmwrite++;
	return FFFILEWRITE_RWRITTEN;
}

/** Preallocate disk space. */
static void fw_prealloc(fffilewrite *f, ffstr data, uint64 off)
{
	uint64 roff = off + data.len;
	if (f->conf.prealloc == 0 || roff <= f->prealloc_size)
		return;

	uint64 n = ff_align_ceil(roff, f->conf.prealloc);
	if (0 != fffile_trunc(f->fd, n))
		return;

	if (f->conf.prealloc_grow)
		f->conf.prealloc *= 2;

	f->prealloc_size = n;
	f->stat.nprealloc++;
	dbglog(f, "prealloc: %Uk", n / 1024);
}

/** Write data to disk. */
static int fw_write(fffilewrite *f, ffstr d, uint64 off)
{
	ssize_t n = fffile_pwrite(f->fd, d.ptr, d.len, off);
	if (n < 0) {
		syserrlog(f, "%s", fffile_write_S);
		return FFFILEWRITE_RERR;
	}
	FF_ASSERT((size_t)n == d.len);
	f->stat.nfwrite++;
	dbglog(f, "%s: written %L bytes at offset %Uk"
		, f->name, n, off / 1024);
	f->size = ffmax(f->size, off + n);
	f->off = off + n;
	return 0;
}

/** buffer -> prealloc -> write */
int fffilewrite_write(fffilewrite *f, ffstr data, int64 off, uint flags)
{
	int r;

	if (f->fd == FF_BADFD)
		if (0 != (r = fw_open(f)))
			return r;

	f->buf_input = data;
	f->buf_off = off;
	if (off == -1)
		f->buf_off = f->off + f->buf.len;
	f->uflags = flags;

	for (;;) {
		ffstr s;
		if (0 != (r = fw_buf(f, &s)))
			return r;

		fw_prealloc(f, s, f->off);

		if (0 != (r = fw_write(f, s, f->off)))
			return r;
	}

	//unreachable
}

fffd fffilewrite_fd(fffilewrite *f)
{
	return f->fd;
}

void fffilewrite_getstat(fffilewrite *f, fffilewrite_stat *stat)
{
	*stat = f->stat;
}
