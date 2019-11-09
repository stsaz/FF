/**
Copyright (c) 2017 Simon Zolin
*/

#include <FF/pack/iso.h>
#include <FF/pack/iso-fmt.h>
#include <FF/data/utf8.h>
#include <FF/crc.h>
#include <FF/path.h>


static int iso_pathtab_count(ffiso_cook *c, uint flags);
static int iso_pathtab_countall(ffiso_cook *c);
static int iso_pathtab_write(ffiso_cook *c, uint flags);

static struct dir* iso_dir_new(ffiso_cook *c, ffstr *name);
static int iso_dirs_count(ffarr *dirs, uint64 *off, uint flags);
static int iso_dirs_copy(ffarr *dst, ffarr *src);
static int iso_dirs_countall(ffiso_cook *c, uint start_off);
static void iso_files_setoffsets(ffarr *dirs, uint64 off);
static int iso_dir_write(ffiso_cook *c, uint joliet);


static const char* const iso_errs[] = {
	"unsupported logical block size", //FFISO_ELOGBLK
	"no primary volume descriptor", //FFISO_ENOPRIM
	"empty root directory in primary volume descriptor", //FFISO_EPRIMEMPTY
	"primary volume: bad id", //FFISO_EPRIMID
	"primary volume: bad version", //FFISO_EPRIMVER
	"unsupported feature", //FFISO_EUNSUPP
	"too large value", //FFISO_ELARGE
	"not ready", //FFISO_ENOTREADY
	"invalid order of directories", //FFISO_EDIRORDER
};

const char* ffiso_errstr(ffiso *c)
{
	switch (c->err) {
	case FFISO_ESYS:
		return fferr_strp(fferr_last());
	}
	uint e = c->err - 1;
	if (e > FFCNT(iso_errs))
		return "";
	return iso_errs[e];
}

const char* ffiso_werrstr(ffiso_cook *c)
{
	ffiso iso = {};
	iso.err = c->err;
	return ffiso_errstr(&iso);
}

#define ERR(c, n) \
	(c)->err = (n),  FFISO_ERR

#define GATHER(c, st) \
	(c)->state = R_GATHER,  (c)->nxstate = st

void ffiso_init(ffiso *c)
{
	ffchain_init(&c->files);
}

void ffiso_close(ffiso *c)
{
	ffchain_item *it;
	FFCHAIN_FOR(&c->files, it) {
		ffiso_file *f = FF_GETPTR(ffiso_file, sib, it);
		it = it->next;
		ffstr_free(&f->name);
		ffmem_free(f);
	}
	ffarr_free(&c->fn);
	ffarr_free(&c->fullfn);
	ffarr_free(&c->buf);
}

enum {
	R_SEEK_PRIM, R_PRIM, R_VOLDESC,
	R_ENT_SEEK, R_ENT,
	R_FDATA_SEEK, R_FDATA, R_FDONE,
	R_GATHER,
};

/**
Return 0 if no more entries. */
static int read_next_dir(ffiso *c)
{
	ffiso_file *f;
	for (;;) {
		if (NULL == (f = ffiso_nextfile(c)))
			return 0;
		if (!ffiso_file_isdir(f))
			continue;
		c->curdir = f;
		c->inoff = f->off;
		c->fsize = f->size;
		c->state = R_ENT_SEEK;
		return 1;
	}
}

/* ISO-9660 read:
. Seek to 17th sector
. Read primary volume descriptor
. Read other volume descriptors (FFISO_HDR)
. Seek to root dir entry
. Read entries from root dir and its subdirectories (FFISO_FILEMETA, FFISO_LISTEND)

After user has requested file data:
. Seek to the needed sector
. Read file data (FFISO_DATA, FFISO_FILEDONE)
*/
int ffiso_read(ffiso *c)
{
	int r;

	for (;;) {
	switch (c->state) {

	case R_GATHER:
		r = ffarr_append_until(&c->buf, c->in.ptr, c->in.len, FFISO_SECT);
		if (r == 0)
			return FFISO_MORE;
		else if (r < 0)
			return ERR(c, FFISO_ESYS);
		ffstr_shift(&c->in, r);
		FF_ASSERT((c->inoff % FFISO_SECT) == 0);
		c->inoff += FFISO_SECT;
		c->buf.len = 0;
		ffstr_set(&c->d, c->buf.ptr, FFISO_SECT);
		c->state = c->nxstate;
		continue;

	case R_SEEK_PRIM:
		GATHER(c, R_PRIM);
		c->inoff = 16 * FFISO_SECT;
		return FFISO_SEEK;

	case R_PRIM: {
		if ((byte)c->d.ptr[0] != ISO_T_PRIM)
			return ERR(c, FFISO_ENOPRIM);
		if (0 != (r = iso_voldesc_parse_prim(c->d.ptr)))
			return ERR(c, -r);

		const struct iso_voldesc_prim *prim = (void*)(c->d.ptr + sizeof(struct iso_voldesc));
		ffiso_file f;
		r = iso_ent_parse(prim->root_dir, sizeof(prim->root_dir), &f, c->inoff - FFISO_SECT);
		if (r < 0)
			return ERR(c, -r);
		else if (r == 0)
			return ERR(c, FFISO_EPRIMEMPTY);

		c->root_start = f.off;
		c->fsize = f.size;

		GATHER(c, R_VOLDESC);
		break;
	}

	case R_VOLDESC: {
		uint t = (byte)c->d.ptr[0];
		FFDBG_PRINTLN(5, "Vol Desc: %xu", t);

		if (t == ISO_T_JOLIET && !(c->options & FFISO_NOJOLIET)
			&& 0 == iso_voldesc_parse_prim(c->d.ptr)) {
			const struct iso_voldesc_prim *jlt = (void*)(c->d.ptr + sizeof(struct iso_voldesc));
			ffiso_file f;
			r = iso_ent_parse(jlt->root_dir, sizeof(jlt->root_dir), &f, c->inoff - FFISO_SECT);
			if (r > 0) {
				if (NULL == ffarr_alloc(&c->fn, 255 * 4))
					return ERR(c, FFISO_ESYS);
				c->root_start = f.off;
				c->fsize = f.size;
				c->joliet = 1;
			}

		} else if (t == ISO_T_TERM) {
			c->inoff = c->root_start;
			c->state = R_ENT_SEEK;
			return FFISO_HDR;
		}

		GATHER(c, R_VOLDESC);
		break;
	}


	case R_ENT_SEEK:
		GATHER(c, R_ENT);
		return FFISO_SEEK;

	case R_ENT:
		if (c->d.len == 0) {
			if (c->fsize == 0) {
				r = read_next_dir(c);
				if (r == 0) {
					c->curdir = NULL;
					c->fcursor = NULL;
					return FFISO_LISTEND;
				}
				continue;
			}
			GATHER(c, R_ENT);
			continue;
		}

		r = iso_ent_parse(c->d.ptr, c->d.len, &c->curfile, c->inoff - FFISO_SECT);
		if (r < 0)
			return ERR(c, -r);
		else if (r == 0) {
			c->fsize = ffmax((int64)(c->fsize - c->d.len), 0);
			c->d.len = 0;
			continue;
		}
		ffarr_shift(&c->d, r);
		c->fsize -= r;

		if (c->joliet) {
			uint n = ffutf8_encodewhole(c->fn.ptr, c->fn.cap, c->curfile.name.ptr, c->curfile.name.len, FFU_UTF16BE);
			ffstr_set(&c->curfile.name, c->fn.ptr, n);
		} else {
			c->curfile.name = iso_ent_name(&c->curfile.name);
		}

		if (!(c->options & FFISO_NORR)) {
			void *ent = c->d.ptr - r;
			ffstr rr;
			ffarr_setshift(&rr, (char*)ent, r, iso_ent_len(ent));
			iso_rr_parse(rr.ptr, rr.len, &c->curfile);
		}

		if (c->curfile.name.len == 0)
			continue;

		// "name" -> "curdir/name"
		if (c->curdir != NULL) {
			c->fullfn.len = 0;
			ffstr_catfmt(&c->fullfn, "%S/%S", &c->curdir->name, &c->curfile.name);
			ffstr_set2(&c->curfile.name, &c->fullfn);
		}

		return FFISO_FILEMETA;


	case R_FDATA_SEEK:
		GATHER(c, R_FDATA);
		return FFISO_SEEK;

	case R_FDATA: {
		uint n = ffmin(c->d.len, c->fsize);
		if (c->fsize <= c->d.len)
			c->state = R_FDONE;
		else
			GATHER(c, R_FDATA);
		ffstr_set(&c->out, c->d.ptr, n);
		c->fsize -= n;
		return FFISO_DATA;
	}

	case R_FDONE:
		return FFISO_FILEDONE;

	}
	}
}

ffiso_file* ffiso_nextfile(ffiso *c)
{
	if (c->fcursor == NULL)
		c->fcursor = ffchain_first(&c->files);
	else
		c->fcursor = c->fcursor->next;
	if (c->fcursor == ffchain_sentl(&c->files))
		return NULL;
	ffiso_file *f = FF_GETPTR(ffiso_file, sib, c->fcursor);
	return f;
}

int ffiso_storefile(ffiso *c)
{
	ffiso_file *f;
	if (NULL == (f = ffmem_new(ffiso_file)))
		return -1;
	*f = c->curfile;
	if (NULL == ffstr_alcopystr(&f->name, &c->curfile.name)) {
		ffmem_free(f);
		return -1;
	}
	ffchain_add(&c->files, &f->sib);
	return 0;
}

void ffiso_readfile(ffiso *c, ffiso_file *f)
{
	if (ffiso_file_isdir(f)) {
		c->state = R_FDONE;
		return;
	}
	c->inoff = f->off;
	c->fsize = f->size;
	c->state = R_FDATA_SEEK;
}


struct dir {
	struct ffiso_file info;
	uint parent_dir;
	uint ifile; //index in parent's files array representing this directory
	ffarr files; //ffiso_file[]
};
struct dir_node {
	ffrbt_node nod;
	uint idir;
};

int ffiso_wcreate(ffiso_cook *c, uint flags)
{
	if (NULL == ffarr_alloc(&c->buf, 16 * FFISO_SECT))
		return -1;
	c->ifile = -1;
	c->options = flags;
	ffrbt_init(&c->dirnames);
	c->name = "CDROM";

	ffstr s = {};
	if (NULL == iso_dir_new(c, &s))
		return FFISO_ESYS;
	return 0;
}

void ffiso_wclose(ffiso_cook *c)
{
	struct dir *d;
	struct ffiso_file *f;
	FFARR_WALKT(&c->dirs, d, struct dir) {
		FFARR_WALKT(&d->files, f, struct ffiso_file) {
			ffstr_free(&f->name);
		}
		ffarr_free(&d->files);
		ffstr_free(&d->info.name);
	}
	ffarr_free(&c->dirs);

	FFARR_WALKT(&c->dirs_jlt, d, struct dir) {
		ffarr_free(&d->files);
	}
	ffarr_free(&c->dirs_jlt);

	ffrbt_freeall(&c->dirnames, ffmem_free, FFOFF(struct dir_node, nod));
	ffarr_free(&c->buf);
}

enum W {
	W_DIR_WAIT, W_EMPTY, W_EMPTY_VD,
	W_PATHTAB, W_PATHTAB_BE, W_PATHTAB_JLT, W_PATHTAB_JLT_BE,
	W_DIR, W_DIR_JLT, W_FILE_NEXT, W_FILE, W_FILE_DONE,
	W_VOLDESC_SEEK, W_VOLDESC_PRIM, W_VOLDESC_JLT, W_VOLDESC_TERM, W_DONE, W_ERR,
};

/** Set offset and size for each directory. */
static int iso_dirs_count(ffarr *dirs, uint64 *off, uint flags)
{
	int r;
	uint sectsize = 0;
	uint64 size;
	struct dir *d, *parent;
	struct ffiso_file *f;

	FFARR_WALKT(dirs, d, struct dir) {

		size = iso_ent_len2(1) * 2;

		FFARR_WALKT(&d->files, f, struct ffiso_file) {
			uint flags2 = ((void*)d != dirs->ptr) ? flags : flags | ENT_WRITE_RR_SP;
			r = iso_ent_write(NULL, 0, f, 0, flags2);
			if (r < 0)
				return -r;
			sectsize += r;
			if (sectsize > FFISO_SECT) {
				size += FFISO_SECT - (sectsize - r);
				sectsize = r;
			}
			size += r;
		}

		if (size > (uint)-1)
			return FFISO_ELARGE;

		d->info.size = ff_align_ceil2(size, FFISO_SECT);
		d->info.off = *off;
		if ((void*)d != (void*)dirs->ptr) {
			// set info in the parent's file array
			parent = ffarr_itemT(dirs, d->parent_dir, struct dir);
			f = ffarr_itemT(&parent->files, d->ifile, struct ffiso_file);
			f->size = d->info.size;
			f->off = *off;
		}
		*off += d->info.size;
	}

	return 0;
}

/** Copy meta data of directories and files.  Names are not copied. */
static int iso_dirs_copy(ffarr *dst, ffarr *src)
{
	if (NULL == ffarr_allocT(dst, src->len, struct dir))
		return -1;

	struct dir *d, *jd = (void*)dst->ptr;
	FFARR_WALKT(src, d, struct dir) {
		*jd = *d;
		if (NULL == ffarr_allocT(&jd->files, d->files.len, struct ffiso_file))
			return -1;
		ffmemcpy(jd->files.ptr, d->files.ptr, d->files.len * sizeof(struct ffiso_file));
		jd->files.len = d->files.len;
		jd++;
		dst->len++;
	}

	return 0;
}

/** Set offset for each file. */
static void iso_files_setoffsets(ffarr *dirs, uint64 off)
{
	struct dir *d;
	struct ffiso_file *f;
	FFARR_WALKT(dirs, d, struct dir) {
		FFARR_WALKT(&d->files, f, struct ffiso_file) {
			if (!ffiso_file_isdir(f)) {
				f->off = off;
				off += ff_align_ceil2(f->size, FFISO_SECT);
			}
		}
	}
}

/** Set offset and size for each directory.
Set offset for each file. */
static int iso_dirs_countall(ffiso_cook *c, uint start_off)
{
	int r;
	uint64 off = start_off;
	uint flags = !(c->options & FFISO_NORR) ? ENT_WRITE_RR : 0;

	if (0 != (r = iso_dirs_count(&c->dirs, &off, flags)))
		return r;

	if (!(c->options & FFISO_NOJOLIET)) {
		if (0 != iso_dirs_copy(&c->dirs_jlt, &c->dirs))
			return FFISO_ESYS;

		if (0 != (r = iso_dirs_count(&c->dirs_jlt, &off, ENT_WRITE_JLT)))
			return r;
	}

	iso_files_setoffsets(&c->dirs, off);

	if (!(c->options & FFISO_NOJOLIET))
		iso_files_setoffsets(&c->dirs_jlt, off);

	return 0;
}

static int iso_pathtab_count(ffiso_cook *c, uint flags)
{
	int r;
	uint size = 0;
	ffstr name;

	struct dir *d;
	FFARR_WALKT(&c->dirs, d, struct dir) {
		ffpath_split2(d->info.name.ptr, d->info.name.len, NULL, &name);
		r = iso_pathent_write(NULL, 0, &name, 0, 0, flags);
		if (r < 0)
			return -r;
		size += r;
	}

	return size;
}

static int iso_pathtab_countall(ffiso_cook *c)
{
	uint size, size_jlt = 0;
	size = iso_pathtab_count(c, 0);
	size = ff_align_ceil2(size, FFISO_SECT);

	if (!(c->options & FFISO_NOJOLIET)) {
		size_jlt = iso_pathtab_count(c, PATHENT_WRITE_JLT);
		size_jlt = ff_align_ceil2(size_jlt, FFISO_SECT);
	}

	return 2 * size + 2 * size_jlt;
}

/** Write path table. */
static int iso_pathtab_write(ffiso_cook *c, uint flags)
{
	int r;
	uint size = 0, size_al;
	const ffarr *dirs = (flags & PATHENT_WRITE_JLT) ? &c->dirs_jlt : &c->dirs;
	const struct dir *d;
	ffstr name, empty = { 1, "" };

	FFARR_WALKT(dirs, d, struct dir) {
		ffpath_split2(d->info.name.ptr, d->info.name.len, NULL, &name);
		const ffstr *nm = (name.len != 0) ? &name : &empty;
		r = iso_pathent_write(NULL, 0, nm, 0, 0, flags);
		if (r < 0)
			return -r;
		size += r;
	}

	size_al = ff_align_ceil2(size, FFISO_SECT);
	c->buf.len = 0;
	if (NULL == ffarr_realloc(&c->buf, size_al))
		return FFISO_ESYS;
	ffmem_zero(c->buf.ptr, size_al);

	FFARR_WALKT(dirs, d, struct dir) {
		ffpath_split2(d->info.name.ptr, d->info.name.len, NULL, &name);
		const ffstr *nm = (name.len != 0) ? &name : &empty;
		r = iso_pathent_write(ffarr_end(&c->buf), ffarr_unused(&c->buf)
			, nm, d->info.off / FFISO_SECT, d->parent_dir + 1, flags);
		if (r < 0)
			return -r;
		c->buf.len += r;
	}

	uint off = c->off / FFISO_SECT;
	struct ffiso_pathtab *pt = (flags & PATHENT_WRITE_JLT) ? &c->pathtab_jlt : &c->pathtab;
	pt->size = size;
	if (flags & PATHENT_WRITE_BE)
		pt->off_be = off;
	else
		pt->off_le = off;

	c->buf.len = size_al;
	return 0;
}

/** Write directory contents. */
static int iso_dir_write(ffiso_cook *c, uint joliet)
{
	int r;
	ffarr *dirs = (joliet) ? &c->dirs_jlt : &c->dirs;
	const struct dir *d = ffarr_itemT(dirs, c->idir++, struct dir);
	uint flags = !(c->options & FFISO_NORR) ? ENT_WRITE_RR : 0;
	if (joliet)
		flags = ENT_WRITE_JLT;
	c->buf.len = 0;
	if (NULL == ffarr_realloc(&c->buf, d->info.size))
		return FFISO_ESYS;
	ffmem_zero(c->buf.ptr, d->info.size);

	struct ffiso_file f = {0};

	f.off = d->info.off;
	f.size = d->info.size;
	f.attr = FFUNIX_FILE_DIR;
	ffstr_set(&f.name, "\x00", 1);
	uint flags2 = (c->idir != 1) ? flags : flags | ENT_WRITE_RR_SP;
	r = iso_ent_write(ffarr_end(&c->buf), ffarr_unused(&c->buf), &f, c->off, flags2);
	c->buf.len += r;

	ffstr_set(&f.name, "\x01", 1);
	const struct dir *parent = ffarr_itemT(dirs, d->parent_dir, struct dir);
	f.off = parent->info.off;
	f.size = parent->info.size;
	f.attr = FFUNIX_FILE_DIR;
	r = iso_ent_write(ffarr_end(&c->buf), ffarr_unused(&c->buf), &f, c->off, flags);
	c->buf.len += r;

	uint sectsize = c->buf.len;
	struct ffiso_file *pf;
	FFARR_WALKT(&d->files, pf, struct ffiso_file) {
		r = iso_ent_write(NULL, 0, pf, c->off, flags);
		sectsize += r;
		if (sectsize > FFISO_SECT) {
			c->buf.len += FFISO_SECT - (sectsize - r);
			sectsize = r;
		}

		r = iso_ent_write(ffarr_end(&c->buf), ffarr_unused(&c->buf), pf, c->off, flags);
		c->buf.len += r;
	}

	c->buf.len = d->info.size;
	return 0;
}

/* ISO-9660 write:
. Get the complete and sorted file list from user
 . ffiso_wfile()
. Write empty 16 sectors (FFISO_DATA)
. Write 3 empty sectors for volume descriptors
. Calculate the length of path tables
. For each directory calculate offset and size of its contents
  Directory size is always a multiple of sector size.
  If size is larger than sector size, data is split into multiple blocks:
    #1: (file-entry)... (zero padding)
    #2: (file-entry)... (zero padding)
. Write path tables
. Write all directories contents
. Write files data
 . ffiso_wfilenext()
 . Write file data
. ffiso_wfinish()
. Seek to the beginning
. Write "primary" volume descriptor
. Write "joliet" volume descriptor
. Write "terminate" volume descriptor (FFISO_DATA, FFISO_DONE)

Example of placement of directory contents and file data:

primary-VD -> /
joliet-primary-VD -> JOLIET-/
term-VD

path-tables:
  "" -> /
  "d" -> /d
    parent=""

(JOLIET PATH TABLES)

/:
  "." -> /
    RR:SP RR:RR
  ".." -> /
    RR:RR
  "a" -> /a
    RR:RR RR:NM
  "d" -> /d
  "z" -> /z
  [zero padding up to sector size]

/d:
  "." -> /d
  ".." -> /
  "a" -> /d/a

(JOLIET DIR CONTENTS)

/a: <data>
/z: <data>
/d/a: <data>
*/
int ffiso_write(ffiso_cook *c)
{
	int r;

	for (;;) {
	switch ((enum W)c->state) {

	case W_DIR_WAIT:
		if (c->dirs.len != 0) {
			c->state = W_EMPTY;
			continue;
		}
		return FFISO_MORE;

	case W_EMPTY:
		ffmem_zero(c->buf.ptr, 16 * FFISO_SECT);
		ffstr_set(&c->out, c->buf.ptr, 16 * FFISO_SECT);
		c->off += c->out.len;
		c->state = W_EMPTY_VD;
		return FFISO_DATA;

	case W_EMPTY_VD:
		ffstr_set(&c->out, c->buf.ptr, 3 * FFISO_SECT);
		c->off += c->out.len;
		r = iso_pathtab_countall(c);
		if (r < 0)
			return r;
		if (0 != (r = iso_dirs_countall(c, c->off + r)))
			return r;
		c->idir = 0;
		c->state = W_PATHTAB;
		return FFISO_DATA;

	case W_PATHTAB_JLT:
		if (c->options & FFISO_NOJOLIET) {
			c->state = W_DIR;
			continue;
		}
		//fallthrough
	case W_PATHTAB:
	case W_PATHTAB_BE:
	case W_PATHTAB_JLT_BE: {
		uint f = (c->state == W_PATHTAB_BE || c->state == W_PATHTAB_JLT_BE)
			? PATHENT_WRITE_BE : 0;
		f |= (c->state == W_PATHTAB_JLT || c->state == W_PATHTAB_JLT_BE)
			? PATHENT_WRITE_JLT : 0;
		if (0 != (r = iso_pathtab_write(c, f)))
			return ERR(c, r);
		ffstr_set(&c->out, c->buf.ptr, c->buf.len);
		c->off += c->out.len;
		c->state++;
		return FFISO_DATA;
	}

	case W_DIR:
	case W_DIR_JLT:
		if (c->idir == c->dirs.len) {
			c->idir = 0;
			if (c->state == W_DIR && !(c->options & FFISO_NOJOLIET)) {
				c->state = W_DIR_JLT;
				continue;
			}
			c->state = W_FILE_NEXT;
			return FFISO_MORE;
		}
		if (0 != (r = iso_dir_write(c, (c->state == W_DIR_JLT))))
			return ERR(c, r);
		ffstr_set(&c->out, c->buf.ptr, c->buf.len);
		c->off += c->out.len;
		return FFISO_DATA;

	case W_FILE: {
		ffstr_set2(&c->out, &c->in);
		c->off += c->out.len;
		c->curfile_size += c->out.len;
		const struct dir *d = ffarr_itemT(&c->dirs, c->idir, struct dir);
		const struct ffiso_file *f = ffarr_itemT(&d->files, c->ifile, struct ffiso_file);
		if (c->curfile_size > f->size)
			return ERR(c, FFISO_ELARGE);
		if (c->curfile_size == f->size)
			c->state = W_FILE_DONE;
		else if (c->in.len == 0)
			return FFISO_MORE;
		c->in.len = 0;
		return FFISO_DATA;
	}

	case W_FILE_DONE:
		c->state = W_FILE_NEXT;
		if (!(c->curfile_size % FFISO_SECT))
			continue;
		ffmem_zero(c->buf.ptr, FFISO_SECT);
		ffstr_set(&c->out, c->buf.ptr, FFISO_SECT - (c->curfile_size % FFISO_SECT));
		c->off += c->out.len;
		return FFISO_DATA;

	case W_FILE_NEXT:
		return FFISO_MORE;


	case W_VOLDESC_SEEK:
		c->nsectors = c->off / FFISO_SECT;
		c->off = 16 * FFISO_SECT;
		c->state = W_VOLDESC_PRIM;
		return FFISO_SEEK;

	case W_VOLDESC_PRIM: {
		const struct dir *d = (void*)c->dirs.ptr;
		struct iso_voldesc_prim_host info = {
			.type = ISO_T_PRIM,
			.name = c->name,
			.root_dir_off = d->info.off,
			.root_dir_size = d->info.size,
			.vol_size = c->nsectors,
			.path_tbl_size = c->pathtab.size,
			.path_tbl_off = c->pathtab.off_le,
			.path_tbl_off_be = c->pathtab.off_be,
		};
		iso_voldesc_prim_write(c->buf.ptr, &info);

		ffstr_set(&c->out, c->buf.ptr, FFISO_SECT);
		c->off += FFISO_SECT;
		c->state = !(c->options & FFISO_NOJOLIET) ? W_VOLDESC_JLT : W_VOLDESC_TERM;
		return FFISO_DATA;
	}

	case W_VOLDESC_JLT: {
		const struct dir *jd = (void*)c->dirs_jlt.ptr;
		ffmem_zero(c->buf.ptr, FFISO_SECT);
		struct iso_voldesc_prim_host info = {
			.type = ISO_T_JOLIET,
			.name = c->name,
			.root_dir_off = jd->info.off,
			.root_dir_size = jd->info.size,
			.vol_size = c->nsectors,
			.path_tbl_size = c->pathtab_jlt.size,
			.path_tbl_off = c->pathtab_jlt.off_le,
			.path_tbl_off_be = c->pathtab_jlt.off_be,
		};
		iso_voldesc_prim_write(c->buf.ptr, &info);

		ffstr_set(&c->out, c->buf.ptr, FFISO_SECT);
		c->off += FFISO_SECT;
		c->state = W_VOLDESC_TERM;
		return FFISO_DATA;
	}

	case W_VOLDESC_TERM:
		ffmem_zero(c->buf.ptr, FFISO_SECT);
		iso_voldesc_write(c->buf.ptr, ISO_T_TERM);

		ffstr_set(&c->out, c->buf.ptr, FFISO_SECT);
		c->off += FFISO_SECT;
		c->state = W_DONE;
		return FFISO_DATA;

	case W_DONE:
		return FFISO_DONE;

	case W_ERR:
		return ERR(c, FFISO_ENOTREADY);
	}
	}
	return 0;
}

static struct dir* iso_dir_new(ffiso_cook *c, ffstr *name)
{
	struct dir *d = ffarr_pushgrowT(&c->dirs, 64, struct dir);
	if (d == NULL)
		return NULL;
	ffmem_tzero(d);
	d->info.name = *name;

	struct dir_node *dn = ffmem_new(struct dir_node);
	if (dn == NULL)
		return NULL;
	dn->idir = d - (struct dir*)c->dirs.ptr;
	ffrbt_insert4(&c->dirnames, &dn->nod, NULL, ffcrc32_get(d->info.name.ptr, d->info.name.len));
	return d;
}

static struct dir* iso_dir_find(ffiso_cook *c, const ffstr *path)
{
	ffrbtkey key = ffcrc32_get(path->ptr, path->len);
	ffrbt_node *nod = ffrbt_find(&c->dirnames, key, NULL);
	if (nod == NULL)
		return NULL;
	struct dir_node *dn = FF_GETPTR(struct dir_node, nod, nod);
	struct dir *d = ffarr_itemT(&c->dirs, dn->idir, struct dir);
	if (ffstr_eq2(path, &d->info.name))
		return d;
	return NULL;
}

void ffiso_wfile(ffiso_cook *c, const ffiso_file *f)
{
	struct dir *parent, *d;
	ffiso_file *nf;
	ffstr path, name, fn = {};

	if (c->state != W_DIR_WAIT) {
		c->err = FFISO_ENOTREADY;
		goto err;
	}

	if (NULL == ffstr_alloc(&fn, f->name.len)) {
		c->err = FFISO_ESYS;
		goto err;
	}
	fn.len = ffpath_norm(fn.ptr, f->name.len, f->name.ptr, f->name.len, FFPATH_MERGEDOTS | FFPATH_FORCESLASH | FFPATH_TOREL);

	ffpath_split2(fn.ptr, fn.len, &path, &name);
	parent = iso_dir_find(c, &path);
	if (parent == NULL) {
		c->err = FFISO_EDIRORDER; // trying to add "dir/file" with no "dir" added previously
		goto err;
	}

	if (ffiso_file_isdir(f)) {
		uint i = parent - (struct dir*)c->dirs.ptr;
		if (NULL == (d = iso_dir_new(c, &fn))) {
			c->err = FFISO_ESYS;
			goto err;
		}
		d->parent_dir = i;
		parent = ffarr_itemT(&c->dirs, d->parent_dir, struct dir);
		d->ifile = parent->files.len;
		ffstr_null(&fn);
	}

	nf = ffarr_pushgrowT(&parent->files, 64, ffiso_file);
	if (nf == NULL) {
		c->err = FFISO_ESYS;
		goto err;
	}
	ffmem_tzero(nf);
	if (NULL == ffstr_alcopystr(&nf->name, &name)) {
		parent->files.len--;
		c->err = FFISO_ESYS;
		goto err;
	}
	if (!ffiso_file_isdir(f))
		ffstr_free(&fn);
	nf->attr = f->attr;
	nf->mtime = f->mtime;
	nf->size = f->size;
	return;

err:
	ffstr_free(&fn);
	c->state = W_ERR;
}

static struct ffiso_file* iso_file_next(ffiso_cook *c)
{
	struct dir *d = (void*)c->dirs.ptr;
	c->ifile++;

	for (uint i = c->idir;  i != c->dirs.len;  i++) {

		struct ffiso_file *f = (void*)d[i].files.ptr;

		for (uint k = c->ifile;  k != d[i].files.len;  k++) {

			if (!ffiso_file_isdir(&f[k])) {
				c->idir = i;
				c->ifile = k;
				return &f[k];
			}
		}
		c->ifile = 0;
	}
	return NULL;
}

void ffiso_wfilenext(ffiso_cook *c)
{
	if (c->state != W_FILE_NEXT) {
		c->err = FFISO_ENOTREADY;
		c->state = W_ERR;
		return;
	}

	const struct ffiso_file *f = iso_file_next(c);
	if (f == NULL) {
		c->err = FFISO_ENOTREADY;
		c->state = W_ERR;
		return;
	}

	const struct dir *d = ffarr_itemT(&c->dirs, c->idir, struct dir);
	(void)d;
	FFDBG_PRINTLN(10, "writing file data for %S/%S"
		, &d->info.name, &f->name);

	c->state = W_FILE;
	c->curfile_size = 0;
}

void ffiso_wfinish(ffiso_cook *c)
{
	if (c->state != W_FILE_NEXT) {
		c->err = FFISO_ENOTREADY;
		c->state = W_ERR;
		return;
	}
	c->state = W_VOLDESC_SEEK;
}
