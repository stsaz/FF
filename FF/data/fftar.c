/**
Copyright (c) 2016 Simon Zolin
*/

#include <FF/data/tar.h>
#include <FF/path.h>
#include <FFOS/error.h>
#include <FFOS/file.h>
#include <FFOS/dir.h>


enum TAR_TYPE {
	TAR_FILE = '0',
	TAR_DIR = '5',
};

typedef struct tar_hdr {
	char name[100];
	char mode[8];
	char uid[8];
	char gid[8];
	char size[12];
	char mtime[12];
	char chksum[8]; //"123456\0 "
	char typeflag; //enum TAR_TYPE
	char linkname[100];
} tar_hdr;

typedef struct tar_gnu {
	char magic[8];
	char uname[32];
	char gname[32];
} tar_gnu;

#define TAR_GNU_MAGIC  "ustar  \0"

typedef struct tar_ustar {
	char magic[6];
	char version[2]; //"00"
	char uname[32];
	char gname[32];
	char unused[8];
	char unused2[8];
	char filename_prefix[155]; //file path for long names
} tar_ustar;

#define TAR_USTAR_MAGIC  "ustar\0"

enum {
	TAR_BLOCK = 512,
};


static int tar_num64(const char *d, size_t len, uint64 *dst);
static int tar_num(const char *d, size_t len, uint *dst);
static uint tar_checksum(const char *d, size_t len);


/** Parse octal number terminated with ' ' or '\0': "..000123.." */
static int _tar_num(const char *d, size_t len, void *dst, uint f)
{
	const char *p = ffs_skipof(d, len, " \0", 2);
	uint n = ffs_toint(p, (d + len) - p, dst, FFS_INTOCTAL | f);
	p += n;
	const char *p2 = ffs_skipof(p, (d + len) - p, " \0", 2);
	return (n == 0 || p == p2 || p2 != d + len) ? -1 : 0;
}

static int tar_num64(const char *d, size_t len, uint64 *dst)
{
	return _tar_num(d, len, dst, FFS_INT64);
}

static int tar_num(const char *d, size_t len, uint *dst)
{
	return _tar_num(d, len, dst, FFS_INT32);
}

/** Write tar number. */
#define tar_writenum(n, dst, cap) \
	ffs_fromint(n, dst, cap, FFINT_OCTAL | FFINT_ZEROWIDTH | FFINT_WIDTH(cap))

int fftar_hdr_parse(fftar_file *f, char *filename, const char *buf)
{
	const tar_hdr *h = (void*)buf;
	uint nlen = ffsz_nlen(h->name, sizeof(h->name));
	ffsz_fcopy(filename, h->name, nlen);
	f->name = filename;

	if (0 != tar_num(h->mode, sizeof(h->mode), &f->mode)
		|| 0 != tar_num(h->uid, sizeof(h->uid), &f->uid)
		|| 0 != tar_num(h->gid, sizeof(h->gid), &f->gid)
		|| 0 != tar_num64(h->size, sizeof(h->size), &f->size)
		|| 0 != tar_num(h->mtime, sizeof(h->mtime), &f->mtime.s))
		return FFTAR_EHDR;

	if (h->typeflag != TAR_FILE && h->typeflag != TAR_DIR)
		return FFTAR_ETYPE;
	if (h->typeflag == TAR_DIR)
		f->mode |= 0040000;

	uint hchk, chk = tar_checksum((void*)h, TAR_BLOCK);
	if (0 != tar_num(h->chksum, sizeof(h->chksum), &hchk)
		|| hchk != chk)
		return FFTAR_ECHKSUM;

	// const tar_gnu *g = (void*)(h + 1);
	// if (memcmp(g->magic, TAR_GNU_MAGIC, FFSLEN(TAR_GNU_MAGIC)))
	// 	return ;
	return 0;
}

int fftar_hdr_write(const fftar_file *f, char *buf)
{
	tar_hdr *h = (void*)buf;
	size_t namelen = ffsz_len(f->name);
	uint addslash = 0;

	uint mode = f->mode;
#ifdef FF_WIN
	mode = fffile_isdir(f->mode) ? 0755 : 0644;
#endif

	h->typeflag = TAR_FILE;
	if (fffile_isdir(f->mode)) {
		h->typeflag = TAR_DIR;
		if (!ffpath_slash(f->name[namelen - 1]))
			addslash = 1;
	}

	if (namelen + addslash > sizeof(h->name))
		return FFTAR_EBIG;
	namelen = ffpath_norm(h->name, sizeof(h->name), f->name, namelen, FFPATH_MERGEDOTS | FFPATH_FORCESLASH | FFPATH_TOREL);
	if (namelen == 0)
		return FFTAR_EFNAME;
	if (addslash)
		h->name[namelen] = '/'; //add trailing slash

	tar_writenum(mode, h->mode, sizeof(h->mode) - 1);
	tar_writenum(f->uid, h->uid, sizeof(h->uid) - 1);
	tar_writenum(f->gid, h->gid, sizeof(h->gid) - 1);

	if (f->size > 077777777777ULL)
		return FFTAR_EBIG;
	tar_writenum(f->size, h->size, sizeof(h->size) - 1);

	tar_writenum(f->mtime.s, h->mtime, sizeof(h->mtime) - 1);

	tar_gnu *g = (void*)(h + 1);
	ffmemcpy(g->magic, TAR_GNU_MAGIC, FFSLEN(TAR_GNU_MAGIC));

	const char *s;
	s = (f->uid_str != NULL) ? f->uid_str : "root";
	ffmemcpy(g->uname, s, ffsz_len(s));
	s = (f->gid_str != NULL) ? f->gid_str : "root";
	ffmemcpy(g->gname, s, ffsz_len(s));

	uint chksum = tar_checksum(buf, sizeof(tar_hdr) + sizeof(tar_gnu));
	tar_writenum(chksum, h->chksum, sizeof(h->chksum) - 2);
	h->chksum[sizeof(h->chksum) - 2] = '\0';
	return 0;
}

/** Get header checksum. */
static uint tar_checksum(const char *d, size_t len)
{
	uint c = 0, i = 0;
	for (;  i != FFOFF(tar_hdr, chksum);  i++) {
		c += (byte)d[i];
	}
	c += ' ' * 8;
	i += 8;
	for (;  i != len;  i++) {
		c += (byte)d[i];
	}
	return c;
}


static const char *const tar_errs[] = {
	"bad header",
	"unsupported file type",
	"checksum mismatch",
	"invalid padding data",
	"not ready",
	"too big name or size",
	"invalid filename",
};

const char* fftar_errstr(void *_t)
{
	fftar *t = _t;
	switch (t->err) {
	case FFTAR_ESYS:
		return fferr_strp(fferr_last());
	}
	return tar_errs[t->err - FFTAR_EHDR];
}

#define ERR(t, n) \
	(t)->err = n, FFTAR_ERR


enum {
	R_GATHER, R_HDR, R_DATA, R_DATA_BLK, R_FDONE, R_FIN, R_FIN2,
};

void fftar_init(fftar *t)
{
	t->nxstate = R_HDR;
}

void fftar_close(fftar *t)
{
	ffarr_free(&t->buf);
	ffarr_free(&t->name);
}

fftar_file* fftar_nextfile(fftar *t)
{
	return &t->file;
}

int fftar_read(fftar *t)
{
	ffstr in;
	uint64 n;
	int r;

	for (;;) {
	switch (t->state) {

	case R_GATHER:
		r = ffarr_append_until(&t->buf, t->in.ptr, t->in.len, TAR_BLOCK);
		if (r == 0)
			return FFTAR_MORE;
		else if (r < 0)
			return ERR(t, FFTAR_ESYS);
		ffstr_shift(&t->in, r);
		t->buf.len = 0;
		t->state = t->nxstate;
		continue;

	case R_HDR:
		if (t->buf.ptr[0] == '\0') {
			t->state = R_FIN;
			continue;
		}
		if (NULL == ffarr_realloc(&t->name, 100 + 1))
			return ERR(t, FFTAR_ESYS);
		ffmem_tzero(&t->file);
		if (0 != (r = fftar_hdr_parse(&t->file, t->name.ptr, t->buf.ptr)))
			return ERR(t, r);
		t->fsize = t->file.size;
		if (t->file.size != 0)
			t->state = R_DATA;
		else
			t->state = R_FDONE;
		return FFTAR_FILEHDR;

	case R_DATA:
		if (t->in.len < TAR_BLOCK) {
			t->state = R_GATHER, t->nxstate = R_DATA_BLK;
			continue;
		}
		ffstr_set(&in, t->in.ptr, ff_align_floor(t->in.len, TAR_BLOCK));
		n = ffmin64(t->fsize, in.len);
		ffstr_shift(&t->in, ff_align_ceil(n, TAR_BLOCK));
		// break

	case R_DATA_BLK:
		if (t->state == R_DATA_BLK) {
			in.ptr = t->buf.ptr;
			in.len = TAR_BLOCK;
			n = ffmin64(t->fsize, TAR_BLOCK);
		}

		ffstr_set(&t->out, in.ptr, n);
		t->fsize -= n;
		if (t->fsize == 0) {
			if (in.ptr + in.len != ffs_skip(in.ptr + n, in.len - n, 0x00))
				return ERR(t, FFTAR_EPADDING);
			t->state = R_FDONE;
		}

		return FFTAR_DATA;

	case R_FDONE:
		t->state = R_GATHER, t->nxstate = R_HDR;
		return FFTAR_FILEDONE;

	case R_FIN:
	case R_FIN2:
		if (t->buf.ptr + TAR_BLOCK != ffs_skip(t->buf.ptr, TAR_BLOCK, 0x00))
			return ERR(t, FFTAR_EPADDING);

		if (t->state == R_FIN) {
			t->state = R_GATHER, t->nxstate = R_FIN2;
			continue;
		}

		return FFTAR_DONE;
	}
	}
}


enum { W_FIRSTFILE, W_NEWFILE, W_HDR, W_DATA, W_PADDING, W_FDONE, W_FTR, W_DONE };

void fftar_wfinish(fftar_cook *t)
{
	t->state = W_FTR;
}

int fftar_newfile(fftar_cook *t, const fftar_file *f)
{
	int r;

	if (t->state != W_NEWFILE && t->state != W_FIRSTFILE)
		return ERR(t, FFTAR_ENOTREADY);

	if (t->buf == NULL && NULL == (t->buf = ffmem_alloc(2 * TAR_BLOCK)))
		return ERR(t, FFTAR_ESYS);
	ffmem_zero(t->buf, TAR_BLOCK);

	if (0 != (r = fftar_hdr_write(f, t->buf)))
		return ERR(t, r);

	t->state = W_HDR;
	return 0;
}

int fftar_write(fftar_cook *t)
{
	for (;;) {
	switch (t->state) {

	case W_HDR:
		ffstr_set(&t->out, t->buf, TAR_BLOCK);
		t->fsize = 0;
		t->state = W_DATA;
		return FFTAR_DATA;

	case W_DATA:
		if (t->fdone) {
			t->state = W_PADDING;
			continue;
		}
		ffstr_set2(&t->out, &t->in);
		t->in.len = 0;
		t->fsize += t->out.len;
		if (t->out.len == 0)
			return FFTAR_MORE;
		return FFTAR_DATA;

	case W_PADDING: {
		ffmem_zero(t->buf, TAR_BLOCK);
		uint padding = (t->fsize % TAR_BLOCK != 0) ? TAR_BLOCK - (t->fsize % TAR_BLOCK) : 0;
		ffstr_set(&t->out, t->buf, padding);
		t->state = W_FDONE;
		return FFTAR_DATA;
	}

	case W_FDONE:
		t->state = W_NEWFILE;
		return FFTAR_FILEDONE;

	case W_FTR:
		ffmem_zero(t->buf, 2 * TAR_BLOCK);
		ffstr_set(&t->out, t->buf, 2 * TAR_BLOCK);
		t->state = W_DONE;
		return FFTAR_DATA;

	case W_DONE:
		return FFTAR_DONE;

	case W_NEWFILE:
	case W_FIRSTFILE:
		t->err = FFTAR_ENOTREADY;
		return FFTAR_ERR;

	default:
		FF_ASSERT(0);
	}
	}
	//unreachable
}
