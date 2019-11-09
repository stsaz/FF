/**
Copyright (c) 2016 Simon Zolin
*/

#include <FF/pack/tar.h>
#include <FF/path.h>
#include <FF/number.h>
#include <FFOS/error.h>
#include <FFOS/file.h>
#include <FFOS/dir.h>


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
	char prefix[155]; //file path for long names
} tar_ustar;

#define TAR_USTAR_MAGIC  "ustar\0"

enum {
	TAR_BLOCK = 512,
	TAR_ENDDATA = 3 * TAR_BLOCK,
	TAR_MAXLONGNAME = 4 * 1024,
	TAR_MAXUID = 07777777,
	TAR_MAXSIZE = 077777777777ULL,
};


static int tar_num64(const char *d, size_t len, uint64 *dst);
static int tar_num(const char *d, size_t len, uint *dst);
static uint tar_checksum(const char *d, size_t len);
static const byte tar_ftype[];


/** Parse octal number terminated with ' ' or '\0': "..000123.." */
static int _tar_num(const char *d, size_t len, void *dst, uint f)
{
	const char *p = ffs_skipof(d, len, " \0", 2);
	uint n = ffs_toint(p, (d + len) - p, dst, FFS_INTOCTAL | f);
	p += n;
	const char *p2 = ffs_skipof(p, (d + len) - p, " \0", 2);
	if (n == 0 && p2 == d + len)
		return 0;
	return (n == 0 || p == p2 || p2 != d + len) ? -1 : 0;
}

/* Large values: 0x80 0 0 0 (int-64) */
static int tar_num64(const char *d, size_t len, uint64 *dst)
{
	if (d[0] & 0x80) {
		*dst = ffint_ntoh64(d + 4);
		return 0;
	}
	return _tar_num(d, len, dst, FFS_INT64);
}

/* Large values: 0x80 0 0 0 (int-32) */
static int tar_num(const char *d, size_t len, uint *dst)
{
	if (d[0] & 0x80) {
		*dst = ffint_ntoh32(d + 4);
		return 0;
	}
	return _tar_num(d, len, dst, FFS_INT32);
}

/** Write tar number. */
static int tar_writenum(uint64 n, char *dst, size_t cap)
{
	if (cap == 12 && n > TAR_MAXSIZE) {
		ffint_hton32(dst, 0x80000000);
		ffint_hton64(dst + 4, n);

	} else if (cap == 8 && n > TAR_MAXUID) {
		ffint_hton32(dst, 0x80000000);
		ffint_hton32(dst + 4, n);

	} else
		ffs_fromint(n, dst, cap, FFINT_OCTAL | FFINT_ZEROWIDTH | FFINT_WIDTH(cap - 1));
	return 0;
}

int fftar_hdr_parse(fftar_file *f, char *filename, const char *buf)
{
	const tar_hdr *h = (void*)buf;
	uint nlen = ffsz_nlen(h->name, sizeof(h->name));
	uint64 tt;

	if (filename != NULL) {
		ffsz_fcopy(filename, h->name, nlen);
		f->name = filename;
	}

	int t = h->typeflag;
	f->type = h->typeflag;
	if (t == FFTAR_SLINK || t == FFTAR_HLINK) {
		if (sizeof(h->linkname) == ffsz_nlen(h->linkname, sizeof(h->linkname)))
			return FFTAR_ELONGNAME;
		f->link_to = h->linkname;
	}

	if (0 != tar_num(h->mode, sizeof(h->mode), &f->mode)
		|| 0 != tar_num(h->uid, sizeof(h->uid), &f->uid)
		|| 0 != tar_num(h->gid, sizeof(h->gid), &f->gid)
		|| 0 != tar_num64(h->size, sizeof(h->size), &f->size)
		|| 0 != tar_num64(h->mtime, sizeof(h->mtime), &tt))
		return FFTAR_EHDR;
	if (t == FFTAR_DIR)
		f->size = 0;
	fftime_fromtime_t(&f->mtime, tt);

	if (!((t >= '0' && t <= '6')
		|| t == FFTAR_FILE0
		|| t == FFTAR_EXTHDR || t == FFTAR_NEXTHDR
		|| t == FFTAR_LONG)) {
		FFDBG_PRINTLN(1, "%*s: file type: '%c'", (size_t)nlen, h->name, t);
	}
	f->mode &= 0777;
	if (t >= '0' && t <= '6')
		f->mode |= (uint)tar_ftype[t - '0'] << 12;

	uint hchk, chk = tar_checksum((void*)h, TAR_BLOCK);
	if (0 != tar_num(h->chksum, sizeof(h->chksum), &hchk)
		|| hchk != chk)
		return FFTAR_ECHKSUM;

	const tar_gnu *g = (void*)(h + 1);
	if (!memcmp(g->magic, TAR_GNU_MAGIC, FFSLEN(TAR_GNU_MAGIC))) {
		f->uid_str = g->uname;
		f->gid_str = g->gname;
	}

	const tar_ustar *us = (void*)(h + 1);
	if (!memcmp(us, TAR_USTAR_MAGIC "00", FFSLEN(TAR_USTAR_MAGIC "00"))) {
		f->uid_str = us->uname;
		f->gid_str = us->gname;
		if (filename != NULL && us->prefix[0] != '\0') {
			ffs_fmt2(filename, -1, "%*s/%*s%Z"
				, ffsz_nlen(us->prefix, sizeof(us->prefix)), us->prefix
				, ffsz_nlen(h->name, sizeof(h->name)), h->name);
		}
	}

	return 0;
}

int fftar_hdr_write(const fftar_file *f, ffarr *buf)
{
	int r;
	size_t cap = 512;
	size_t namelen = ffsz_len(f->name);
	ffbool dir = (f->mode & FFUNIX_FILE_TYPEMASK) == FFUNIX_FILE_DIR;
	if (dir && ffpath_slash(f->name[namelen - 1]))
		namelen--;
	if (namelen + dir > FF_SIZEOF(tar_hdr, name))
		cap = 512 + ff_align_ceil2(namelen + dir, 512) + 512;
	buf->len = 0;
	if (NULL == ffarr_realloc(buf, cap))
		return FFTAR_ESYS;
	ffmem_zero(buf->ptr, cap);

	if (namelen + dir > FF_SIZEOF(tar_hdr, name)) {
		fftar_file fl = {};
		fl.name = "././@LongLink";
		fl.mode = 0644;
		fl.size = namelen + dir;
		fftime_fromtime_t(&fl.mtime, 0);
		r = _fftar_hdr_write(&fl, buf->ptr, FFTAR_LONG);
		if (r != 0)
			return r;
		buf->len += 512;

		namelen = ffpath_norm(ffarr_end(buf), ffarr_unused(buf) - dir, f->name, namelen, FFPATH_MERGEDOTS | FFPATH_FORCESLASH | FFPATH_TOREL);
		if (namelen == 0)
			return FFTAR_EFNAME;
		if (dir)
			buf->ptr[512 + namelen] = '/';
		buf->len += ff_align_ceil2(namelen + dir, 512);
	}

	uint t = f->mode & FFUNIX_FILE_TYPEMASK;
	uint type = FFTAR_FILE;
	for (uint i = 0;  i != 7;  i++) {
		if (t == ((uint)tar_ftype[i] << 12)) {
			type = FFTAR_FILE + i;
			break;
		}
	}

	r = _fftar_hdr_write(f, ffarr_end(buf), type);
	if (r != 0 && r != FFTAR_ELONGNAME)
		return r;
	buf->len += 512;
	return r;
}

int _fftar_hdr_write(const fftar_file *f, char *buf, uint type)
{
	int rc = 0;
	tar_hdr *h = (void*)buf;
	ffbool dir = (f->mode & FFUNIX_FILE_TYPEMASK) == FFUNIX_FILE_DIR;

	h->typeflag = type;
	if (type >= '0' && type <= '6') {
		size_t namelen = ffsz_len(f->name);
		if (dir && ffpath_slash(f->name[namelen - 1]))
			namelen--;

		if (namelen + dir > sizeof(h->name)) {
			// normalize path then trim its tail to the max. available space
			ffarr fn = {};
			if (NULL == ffarr_alloc(&fn, namelen))
				return FFTAR_ESYS;
			namelen = ffpath_norm(fn.ptr, fn.cap, f->name, namelen, FFPATH_MERGEDOTS | FFPATH_FORCESLASH | FFPATH_TOREL);
			if (namelen == 0) {
				ffarr_free(&fn);
				return FFTAR_EFNAME;
			}
			char *end = ffs_copy(h->name, h->name + sizeof(h->name) - dir, fn.ptr, namelen);
			if ((size_t)(end - h->name) != namelen)
				rc = FFTAR_ELONGNAME;
			namelen = end - h->name;
			ffarr_free(&fn);

		} else {
			namelen = ffpath_norm(h->name, sizeof(h->name), f->name, namelen, FFPATH_MERGEDOTS | FFPATH_FORCESLASH | FFPATH_TOREL);
			if (namelen == 0)
				return FFTAR_EFNAME;
		}

		if (dir)
			h->name[namelen] = '/';

	} else {
		ffs_copyz(h->name, h->name + sizeof(h->name), f->name);
	}

	uint e = 0;
	e |= tar_writenum(f->mode & 0777, h->mode, sizeof(h->mode));
	e |= tar_writenum(f->uid, h->uid, sizeof(h->uid));
	e |= tar_writenum(f->gid, h->gid, sizeof(h->gid));

	if (dir)
		tar_writenum(0, h->size, sizeof(h->size));
	else
		e |= tar_writenum(f->size, h->size, sizeof(h->size));

	time_t t = fftime_to_time_t(&f->mtime);
	e |= tar_writenum(t, h->mtime, sizeof(h->mtime));

	if (e)
		return FFTAR_EBIG;

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
	return rc;
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
	"bad header", //FFTAR_EHDR
	"unsupported file type", //FFTAR_ETYPE
	"checksum mismatch", //FFTAR_ECHKSUM
	"invalid padding data", //FFTAR_EPADDING
	"not ready", //FFTAR_ENOTREADY
	"too big name or size", //FFTAR_EBIG
	"invalid filename", //FFTAR_EFNAME
	"too long filename", //FFTAR_ELONGNAME
	"file has non-zero size", //FFTAR_EHAVEDATA
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
	R_INIT, R_GATHER, R_HDR, R_LONG_NAME, R_SKIP, R_DATA, R_DATA_BLK, R_FDONE, R_FIN, R_FIN2,
};

void fftar_init(fftar *t)
{
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

	case R_INIT:
		if (NULL == ffarr_alloc(&t->name, FF_SIZEOF(tar_hdr, name) + FFSLEN("/") + FF_SIZEOF(tar_ustar, prefix) + 1))
			return ERR(t, FFTAR_ESYS);
		t->state = R_GATHER,  t->nxstate = R_HDR;
		continue;

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

	case R_HDR: {
		char *namebuf = t->name.ptr;

		if (t->buf.ptr[0] == '\0') {
			t->state = R_FIN;
			continue;
		}

		ffmem_tzero(&t->file);

		if (t->long_name) {
			t->long_name = 0;
			namebuf = NULL;
			t->file.name = t->name.ptr;
		}

		if (0 != (r = fftar_hdr_parse(&t->file, namebuf, t->buf.ptr)))
			return ERR(t, r);
		t->fsize = t->file.size;

		FFDBG_PRINTLN(10, "hdr: name:%s  type:%c  size:%U"
			, t->file.name, t->file.type, t->file.size);

		switch (t->file.type) {

		case FFTAR_DIR:
		case FFTAR_HLINK:
		case FFTAR_SLINK:
			if (t->file.size != 0)
				return ERR(t, FFTAR_EHAVEDATA);
			break;

		case FFTAR_LONG:
			if (t->file.size > TAR_MAXLONGNAME)
				return ERR(t, FFTAR_ELONGNAME);
			t->state = R_LONG_NAME;
			continue;
		case FFTAR_EXTHDR:
		case FFTAR_NEXTHDR:
			if (t->file.size > TAR_MAXLONGNAME)
				return ERR(t, FFTAR_ELONGNAME);
			t->state = R_GATHER,  t->nxstate = R_SKIP;
			continue;
		}

		if (t->file.size != 0)
			t->state = R_DATA;
		else
			t->state = R_FDONE;
		return FFTAR_FILEHDR;
	}

	case R_LONG_NAME:
		r = ffarr_append_until(&t->name, t->in.ptr, t->in.len, ff_align_ceil2(t->file.size, TAR_BLOCK));
		if (r == 0)
			return FFTAR_MORE;
		else if (r < 0)
			return ERR(t, FFTAR_ESYS);

		t->name.len = t->file.size;
		if (NULL == ffarr_append(&t->name, "\0", 1))
			return ERR(t, FFTAR_ESYS);
		t->name.len = 0;
		t->long_name = 1;

		ffstr_shift(&t->in, r);
		t->state = R_GATHER, t->nxstate = R_HDR;
		continue;

	case R_SKIP:
		t->file.size -= ffmin(TAR_BLOCK, t->file.size);
		if (t->file.size != 0) {
			t->state = R_GATHER,  t->nxstate = R_SKIP;
			continue;
		}
		t->state = R_GATHER,  t->nxstate = R_HDR;
		continue;

	case R_DATA:
		if (t->in.len == 0)
			return FFTAR_MORE;
		else if (t->in.len < TAR_BLOCK) {
			t->state = R_GATHER, t->nxstate = R_DATA_BLK;
			continue;
		}
		ffstr_set(&in, t->in.ptr, ff_align_floor2(t->in.len, TAR_BLOCK));
		n = ffmin64(t->fsize, in.len);
		ffstr_shift(&t->in, ff_align_ceil2(n, TAR_BLOCK));
		in.len = ff_align_ceil2(n, TAR_BLOCK);
		// break

	case R_DATA_BLK:
		if (t->state == R_DATA_BLK) {
			in.ptr = t->buf.ptr;
			in.len = TAR_BLOCK;
			n = ffmin64(t->fsize, TAR_BLOCK);
			t->state = R_DATA;
		}

		ffstr_set(&t->out, in.ptr, n);
		t->fsize -= n;
		if (t->fsize == 0) {
			if (in.ptr + in.len != ffs_skip(in.ptr + n, in.len - n, 0x00))
				return ERR(t, FFTAR_EPADDING);
			t->state = R_FDONE;
		}

		FFDBG_PRINTLN(10, "data block:+%L (%U left)", t->out.len, t->fsize);
		return FFTAR_DATA;

	case R_FDONE:
		t->state = R_GATHER, t->nxstate = R_HDR;
		return FFTAR_FILEDONE;

	case R_FIN:
		if (t->buf.ptr + TAR_BLOCK != ffs_skip(t->buf.ptr, TAR_BLOCK, 0x00))
			return ERR(t, FFTAR_EPADDING);
		t->state = R_FIN2;
		// break

	case R_FIN2:
		if (t->in.len == 0) {
			if (t->fin)
				return FFTAR_DONE;
			return FFTAR_MORE;
		}

		t->state = R_GATHER, t->nxstate = R_FIN;
		continue;
	}
	}
}

static const byte tar_ftype[] = {
	FFUNIX_FILE_REG >> 12,
	FFUNIX_FILE_LINK >> 12,
	FFUNIX_FILE_LINK >> 12,
	FFUNIX_FILE_CHAR >> 12,
	FFUNIX_FILE_BLOCK >> 12,
	FFUNIX_FILE_DIR >> 12,
	FFUNIX_FILE_FIFO >> 12,
};

uint fftar_mode(const fftar_file *f)
{
	uint t = f->type;
	if (t == '\0')
		t = FFTAR_FILE;
	if (t >= '0' && t <= '6')
		t = (uint)tar_ftype[ffchar_tonum(t)] << 12;
	else
		t = FFUNIX_FILE_REG;
	return t | (f->mode & 0777);
}


enum { W_NEWFILE, W_HDR, W_DATA, W_PADDING, W_FDONE, W_FTR, W_DONE };

int fftar_create(fftar_cook *t)
{
	if (NULL == ffarr_alloc(&t->buf, TAR_ENDDATA))
		return ERR(t, FFTAR_ESYS);
	return 0;
}

void fftar_wfinish(fftar_cook *t)
{
	t->state = W_FTR;
}

int fftar_newfile(fftar_cook *t, const fftar_file *f)
{
	int r;

	if (t->state != W_NEWFILE)
		return ERR(t, FFTAR_ENOTREADY);

	r = fftar_hdr_write(f, &t->buf);
	if (r != 0 && r != FFTAR_ELONGNAME)
		return ERR(t, r);

	t->fdone = 0;
	t->state = W_HDR;
	return 0;
}

int fftar_write(fftar_cook *t)
{
	for (;;) {
	switch (t->state) {

	case W_HDR:
		ffstr_set2(&t->out, &t->buf);
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
		ffmem_zero(t->buf.ptr, 512);
		uint padding = (t->fsize % TAR_BLOCK != 0) ? TAR_BLOCK - (t->fsize % TAR_BLOCK) : 0;
		ffstr_set(&t->out, t->buf.ptr, padding);
		t->state = W_FDONE;
		return FFTAR_DATA;
	}

	case W_FDONE:
		t->state = W_NEWFILE;
		return FFTAR_FILEDONE;

	case W_FTR:
		ffmem_zero(t->buf.ptr, TAR_ENDDATA);
		ffstr_set(&t->out, t->buf.ptr, TAR_ENDDATA);
		t->state = W_DONE;
		return FFTAR_DATA;

	case W_DONE:
		return FFTAR_DONE;

	case W_NEWFILE:
		t->err = FFTAR_ENOTREADY;
		return FFTAR_ERR;

	default:
		FF_ASSERT(0);
	}
	}
	//unreachable
}
