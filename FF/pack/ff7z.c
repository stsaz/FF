/**
Copyright (c) 2017 Simon Zolin
*/

#include <FF/pack/7z.h>
#include <FF/pack/7z-fmt.h>
#include <FF/path.h>
#include <FF/crc.h>
#include <FF/number.h>
#include <FF/data/utf8.h>
#include <FFOS/error.h>

#include <zlib/zlib-ff.h>
#include <lzma/lzma-ff.h>


typedef struct z7_filter z7_filter;
struct z7_filter {
	uint64 read;
	uint64 written;
	int err;
	uint init :1;
	uint fin :1;
	uint allow_fin :1;
	ffarr buf;
	ffstr in;
	union {
		lzma_decoder *lzma;
		z_ctx *zlib;
		struct {
			uint64 off;
			uint64 size;
		} bounds;
	};
	int (*process)(z7_filter *c);
	void (*destroy)(z7_filter *c);
};


static int z7_blk_proc(ff7z *z, const struct z7_bblock *blk);
static int z7_prep_unpackhdr(ff7z *z);
static int z7_filters_create(ff7z *z, z7_stream *s);
static void z7_filters_close(ff7z *z);
static int z7_filters_call(ff7z *z);
static int z7_filter_init(z7_filter *c, const struct z7_coder *coder);
static void z7_streams_free(ffarr *stms);

static int z7_deflate_init(z7_filter *c, uint method);
static int z7_deflate_process(z7_filter *c);
static void z7_deflate_destroy(z7_filter *c);

static int z7_lzma_init(z7_filter *c, uint method, const void *props, uint nprops);
static int z7_lzma_process(z7_filter *c);
static void z7_lzma_destroy(z7_filter *c);

static int z7_bounds_process(z7_filter *c);


static FFINL int z7_readint(ffstr *d, uint64 *val)
{
	int r = z7_varint(d->ptr, d->len, val);
	ffarr_shift(d, r);
	return r;
}


static const char* errs[] = {
	"",
	"system",
	"bad signature",
	"unsupported version",
	"bad signature header CRC",
	"bad block ID",
	"unknown block ID",
	"duplicate block",
	"no required block",
	"unsupported",
	"unsupported coder method",
	"unsupported Folder flags",
	"incomplete block",
	"invalid blocks order",
	"bad data",
	"data checksum mismatch",
	"liblzma error",
	"libz error",
};

const char* ff7z_errstr(ff7z *z)
{
	uint e = z->err;
	if (e >= FFCNT(errs))
		return "";
	return errs[e];
}

#define ERR(m, r) \
	(m)->err = (r),  FF7Z_ERR

enum {
	R_START, R_GATHER, R_GHDR, R_BLKID, R_META_UNPACK,
	R_FSTART, R_FDATA, R_FDONE, R_FNEXT,
};

void ff7z_open(ff7z *z)
{
	z->st = R_START;
}

static int z7_filters_create(ff7z *z, z7_stream *s)
{
	int r;
	uint i, k = 0;

	if (NULL == (z->filters = ffmem_allocT(2 + 1, z7_filter)))
		return FF7Z_ESYS;

	for (i = 0;  i != FFCNT(s->coder);  i++) {
		if (s->coder[i].method == 0)
			break;
		if (s->coder[i].method == FF7Z_M_STORE)
			continue;
		if (0 != (r = z7_filter_init(&z->filters[k++], &s->coder[i])))
			return r;
	}
	ffmem_tzero(&z->filters[k]);
	z->filters[k].process = &z7_bounds_process;
	z->ifilter = 0;
	z->nfilters = k + 1;
	return 0;
}

static void z7_filters_close(ff7z *z)
{
	for (uint i = 0;  i != z->nfilters;  i++) {
		if (z->filters[i].init)
			z->filters[i].destroy(&z->filters[i]);
		ffarr_free(&z->filters[i].buf);
	}
	ffmem_safefree0(z->filters);
	z->nfilters = 0;
}

static void z7_streams_free(ffarr *stms)
{
	z7_stream *s;
	FFARR_WALKT(stms, s, z7_stream) {
		ff7zfile *f;
		FFARR_WALKT(&s->files, f, ff7zfile) {
			ffmem_safefree(f->name);
		}
		ffarr_free(&s->files);
	}
	ffarr_free(stms);
}

void ff7z_close(ff7z *z)
{
	ffarr_free(&z->buf);
	ffarr_free(&z->gbuf);
	z7_filters_close(z);
	z7_streams_free(&z->stms);
	ffmem_safefree0(z->blks);
}

enum {
	FILT_MORE,
	FILT_DATA,
	FILT_ERR,
	FILT_DONE,
};

static int z7_filter_init(z7_filter *c, const struct z7_coder *coder)
{
	int r;
	ffmem_tzero(c);

	switch (coder->method) {

	case FF7Z_M_DEFLATE:
		if (0 != (r = z7_deflate_init(c, coder->method)))
			return r;
		break;

	case FF7Z_M_X86:
	case FF7Z_M_LZMA1:
	case FF7Z_M_LZMA2:
		if (0 != (r = z7_lzma_init(c, coder->method, coder->props, coder->nprops)))
			return r;
		break;

	default:
		return FF7Z_EUKNCODER;
	}

	c->init = 1;
	return 0;
}


static int z7_deflate_init(z7_filter *c, uint method)
{
	int r;
	z_conf conf = {0};
	if (0 != (r = z_inflate_init(&c->zlib, &conf)))
		return FF7Z_EZLIB;

	if (NULL == ffarr_alloc(&c->buf, 64 * 1024)) {
		z_inflate_free(c->zlib);
		return FF7Z_ESYS;
	}

	c->process = &z7_deflate_process;
	c->destroy = &z7_deflate_destroy;
	return 0;
}

static void z7_deflate_destroy(z7_filter *c)
{
	FF_SAFECLOSE(c->zlib, NULL, z_inflate_free);
}

static int z7_deflate_process(z7_filter *c)
{
	int r;
	size_t n = c->in.len;
	c->buf.len = 0;
	r = z_inflate(c->zlib, c->in.ptr, &n, ffarr_end(&c->buf), ffarr_unused(&c->buf), 0);
	if (r == Z_DONE)
		return FILT_DONE;
	if (r < 0) {
		c->err = FF7Z_EZLIB;
		return FILT_ERR;
	}

	ffarr_shift(&c->in, n);
	c->read += n;
	if (r == 0)
		return FILT_MORE;

	c->buf.len += r;
	c->written += r;
	return FILT_DATA;
}


static int z7_lzma_init(z7_filter *c, uint method, const void *props, uint nprops)
{
	int r;
	lzma_filter_props fp;

	switch (method) {
	case FF7Z_M_X86:
		fp.id = LZMA_FILT_X86;
		c->allow_fin = 1;
		break;
	case FF7Z_M_LZMA1:
		fp.id = LZMA_FILT_LZMA1;
		break;
	case FF7Z_M_LZMA2:
		fp.id = LZMA_FILT_LZMA2;
		break;
	}

	fp.props = (void*)props;
	fp.prop_len = nprops;
	if (0 != (r = lzma_decode_init(&c->lzma, 0, &fp, 1)))
		return FF7Z_ELZMA;

	if (NULL == ffarr_alloc(&c->buf, lzma_decode_bufsize(c->lzma, 64 * 1024))) {
		lzma_decode_free(c->lzma);
		return FF7Z_ESYS;
	}

	c->process = &z7_lzma_process;
	c->destroy = &z7_lzma_destroy;
	return 0;
}

static void z7_lzma_destroy(z7_filter *c)
{
	FF_SAFECLOSE(c->lzma, NULL, lzma_decode_free);
}

static int z7_lzma_process(z7_filter *c)
{
	int r;
	size_t n = c->in.len;
	c->buf.len = 0;
	if (c->fin && n == 0 && c->allow_fin)
		n = (size_t)-1;
	r = lzma_decode(c->lzma, c->in.ptr, &n, ffarr_end(&c->buf), ffarr_unused(&c->buf));
	if (r == LZMA_DONE)
		return FILT_DONE;
	if (r < 0) {
		c->err = FF7Z_ELZMA;
		return FILT_ERR;
	}

	ffarr_shift(&c->in, n);
	c->read += n;
	if (r == 0)
		return FILT_MORE;

	c->buf.len += r;
	c->written += r;
	return FILT_DATA;
}


static int z7_bounds_process(z7_filter *c)
{
	ffstr s = c->in;
	size_t n = ffstr_crop_abs(&s, c->read, c->bounds.off, c->bounds.size);
	c->read += n;
	ffstr_shift(&c->in, n);
	if (s.len == 0) {
		if (c->read == c->bounds.off + c->bounds.size)
			return FILT_DONE;
		return FILT_MORE;
	}
	ffstr_set2(&c->buf, &s);
	return FILT_DATA;
}

static int z7_filters_call(ff7z *z)
{
	int r;
	const ff7zfile *f = (void*)z->curstm->files.ptr;
	f = &f[z->curstm->ifile - 1];
	z7_filter *c, *next;
	size_t inlen;

	c = &z->filters[z->ifilter];
	inlen = c->in.len;
	(void)inlen;
	r = c->process(c);

	switch (r) {

	case FILT_MORE:
		if (c->fin)
			return ERR(z, FF7Z_EDATA);

		if (z->ifilter == 0) {
			if (z->input.len == 0)
				return FF7Z_MORE;
			ffstr_set2(&c->in, &z->input);
			ffstr_crop_abs(&c->in, z->off, z->curstm->off, z->curstm->pack_size);
			size_t n = c->in.ptr + c->in.len - z->input.ptr;
			ffstr_shift(&z->input, n);
			z->off += n;
			break;
		}

		z->ifilter--;
		break;

	case FILT_DATA:
		FFDBG_PRINTLN(10, "filter#%u: %xL->%xL [%xU->%xU]"
			, z->ifilter, inlen, c->buf.len, c->read, c->written);

		if (z->ifilter + 1 == z->nfilters) {
			ffstr_set2(&z->out, &c->buf);
			z->crc = crc32((void*)z->out.ptr, z->out.len, z->crc);
			return FF7Z_DATA;
		}

		next = c + 1;
		ffstr_set2(&next->in, &c->buf);
		z->ifilter++;
		break;

	case FILT_DONE:
		if (z->ifilter + 1 == z->nfilters) {
			if (f->crc != z->crc)
				return ERR(z, FF7Z_EDATACRC);
			return FF7Z_FILEDONE;
		}

		next = c + 1;
		next->fin = 1;
		z->ifilter++;
		break;

	case FILT_ERR:
		return ERR(z, c->err);
	}

	return 0;
}

static int z7_prep_unpackhdr(ff7z *z)
{
	int r;
	z7_stream *s = (void*)z->stms.ptr;

	if (z->hdr_packed)
		return FF7Z_EDATA; //one more packed header

	if (s->files.len != 0)
		return FF7Z_EDATA; //files inside header
	if (NULL == ffarr_alloczT(&s->files, 1, ff7zfile))
		return FF7Z_ESYS;
	s->files.len = 1;
	ff7zfile *f = (void*)s->files.ptr;
	f->size = s->unpack_size;
	f->crc = s->crc;
	s->ifile = 1;

	if (NULL == ffarr_realloc(&z->buf, s->unpack_size))
		return FF7Z_ESYS;
	if (0 != (r = z7_filters_create(z, s)))
		return r;
	z->filters[z->nfilters - 1].bounds.size = f->size;
	z->hdr_packed = 1;
	z->curstm = s;
	return 0;
}

static int z7_blk_proc(ff7z *z, const struct z7_bblock *blk)
{
	int r;
	uint id = blk->flags & 0xff;
	FF_ASSERT(z->iblk + 1 != Z7_MAX_BLOCK_DEPTH);
	z->blks[++z->iblk].id = id;

	ffstr d;
	ffstr_set2(&d, &z->gdata);
	if (blk->flags & F_SIZE) {
		uint64 size;
		if (0 == (r = z7_readint(&z->gdata, &size)))
			return FF7Z_EMORE;
		FFDBG_PRINTLN(10, " block size:%xU", size);
		if (z->gdata.len < size)
			return FF7Z_EMORE;
		ffstr_set(&d, z->gdata.ptr, size);
	}

	if ((blk->flags & F_ALLOC_FILES) && !z->files_init) {
		if (id == T_CRC) {
			z7_stream *s;
			FFARR_WALKT(&z->stms, s, z7_stream) {
				if (NULL == ffarr_alloczT(&s->files, 1, ff7zfile))
					return FF7Z_ESYS;
				s->files.len = 1;
				ff7zfile *f = (void*)s->files.ptr;
				f->size = s->unpack_size;
			}
		}
		z->files_init = 1;
	}

	int (*proc)(ffarr *stms, ffstr *d);

	if (blk->flags & F_CHILDREN) {
		z->blks[z->iblk].children = blk->data;

		if (z->blks[z->iblk].children[0].flags & F_SELF) {
			proc = z->blks[z->iblk].children[0].data;
			if (0 != (r = proc(&z->stms, &d)))
				return r;
		}

	} else {
		proc = blk->data;
		if (0 != (r = proc(&z->stms, &d)))
			return r;

		ffmem_tzero(&z->blks[z->iblk]);
		z->iblk--;
	}

	z->gdata.len = ffarr_end(&z->gdata) - d.ptr;
	z->gdata.ptr = d.ptr;
	return 0;
}

/* .7z reading:
. Parse header
. Seek to meta block;  parse it:
 . Build packed streams list
 . Build file meta list, associate with packed streams
. If packed meta, seek to it;  unpack;  parse
*/
int ff7z_read(ff7z *z)
{
	int r;

	for (;;) {
	switch (z->st) {

	case R_START:
		if (NULL == (z->blks = ffmem_callocT(Z7_MAX_BLOCK_DEPTH, struct z7_block)))
			return ERR(z, FF7Z_ESYS);
		z->blks[0].children = z7_ctx_top;
		z->gsize = Z7_GHDR_LEN;
		z->st = R_GATHER,  z->gst = R_GHDR;
		// break

	case R_GATHER:
		r = ffarr_gather(&z->gbuf, z->input.ptr, z->input.len, z->gsize);
		if (r < 0)
			return ERR(z, FF7Z_ESYS);
		ffarr_shift(&z->input, r);
		z->off += r;
		if (z->gbuf.len != z->gsize)
			return FF7Z_MORE;
		ffstr_set2(&z->gdata, &z->gbuf);
		z->gbuf.len = 0;
		z->st = z->gst;
		z->gsize = 0;
		continue;

	case R_GHDR: {
		struct z7_info info;
		if (0 != (r = z7_ghdr_read(&info, z->gdata.ptr)))
			return ERR(z, r);
		z->gsize = info.hdrsize;
		z->st = R_GATHER,  z->gst = R_BLKID;
		z->off = info.hdroff;
		return FF7Z_SEEK;
	}

	case R_BLKID: {
		if (z->gdata.len == 0) {
			if (z->iblk != 0)
				return ERR(z, FF7Z_EDATA);
			z->curstm = (void*)z->stms.ptr;
			return FF7Z_FILEHDR;
		}
		uint64 blk_id;
		if (1 != z7_varint(z->gdata.ptr, 1, &blk_id))
			return ERR(z, FF7Z_EBADID);
		ffstr_shift(&z->gdata, 1);

		FFDBG_PRINTLN(10, "%*cblock:%xu  offset:%xU"
			, (size_t)z->iblk, ' ', blk_id, z->off);

		const struct z7_bblock *blk;
		if (0 != (r = z7_find_block(blk_id, &blk, &z->blks[z->iblk])))
			return ERR(z, r);

		if (blk_id == T_End) {

			if (0 != (r = z7_check_req(&z->blks[z->iblk])))
				return ERR(z, r);

			uint done_id = z->blks[z->iblk].id;
			ffmem_tzero(&z->blks[z->iblk]);
			z->iblk--;

			if (done_id == T_EncodedHeader) {
				if (0 != (r = z7_prep_unpackhdr(z)))
					return ERR(z, r);
				z->st = R_META_UNPACK;
				z->off = z->curstm->off;
				return FF7Z_SEEK;
			}
			continue;
		}

		if (0 != (r = z7_blk_proc(z, blk)))
			return ERR(z, r);
		continue;
	}

	case R_META_UNPACK:
		if (0 == (r = z7_filters_call(z)))
			continue;

		if (r == FF7Z_FILEDONE) {
			z7_filters_close(z);
			z7_streams_free(&z->stms);
			z->curstm = NULL;

			ffstr_set2(&z->gdata, &z->buf);
			z->buf.len = 0;
			z->st = R_BLKID;
			continue;

		} else if (r == FF7Z_DATA) {
			ffarr_append(&z->buf, z->out.ptr, z->out.len);
			z->out.len = 0;
			continue;
		}

		return r;

	case R_FSTART: {
		const ff7zfile *f = (void*)z->curstm->files.ptr;
		f = &f[z->curstm->ifile - 1];
		FFDBG_PRINTLN(10, "unpacking file '%s'  size:%xU  offset:%xU  CRC:%xU"
			, f->name, f->size, f->off, f->crc);

		if (z->curstm->off == 0) {
			z->st = R_FDONE;
			continue;
		}
		z->st = R_FDATA;

		z->crc = 0;
		if (z->nfilters == 0) {
			if (0 != (r = z7_filters_create(z, z->curstm)))
				return ERR(z, r);
			z->filters[z->nfilters - 1].bounds.off = f->off;
			z->filters[z->nfilters - 1].bounds.size = f->size;
			z->off = z->curstm->off;
			return FF7Z_SEEK;
		}

		z->filters[z->nfilters - 1].bounds.off = f->off;
		z->filters[z->nfilters - 1].bounds.size = f->size;
		// break
	}

	case R_FDATA:
		if (0 == (r = z7_filters_call(z)))
			continue;

		if (r == FF7Z_FILEDONE)
			z->st = R_FNEXT;
		return r;

	case R_FDONE:
		z->st = R_FNEXT;
		return FF7Z_FILEDONE;

	case R_FNEXT:
		return FF7Z_FILEHDR;

	}
	}
	return 0;
}

const ff7zfile* ff7z_nextfile(ff7z *z)
{
	if (z->curstm == NULL)
		return NULL;

	if (z->curstm->ifile == z->curstm->files.len) {
		if (z->curstm == ffarr_lastT(&z->stms, z7_stream))
			return NULL;
		z7_filters_close(z);
		z->curstm->ifile = 0;
		z->curstm++;
	}

	const ff7zfile *f = (void*)z->curstm->files.ptr;
	z->st = R_FSTART;
	return &f[z->curstm->ifile++];
}
