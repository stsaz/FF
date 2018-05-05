/**
Copyright (c) 2017 Simon Zolin
*/

#include <FF/pack/iso.h>
#include <FF/number.h>
#include <FF/time.h>
#include <FF/data/utf8.h>
#include <FF/path.h>


enum { FFISO_SECT = 2048 };

enum ISO_TYPE {
	ISO_T_PRIM = 1,
	ISO_T_JOLIET = 2,
	ISO_T_TERM = 0xff,
};

struct iso_voldesc {
	byte type; //enum ISO_TYPE
	char id[5]; //"CD001"
	byte ver; //=1
	byte data[0];
};

struct iso_date {
	byte year; //+1900
	byte month;
	byte day;
	byte hour;
	byte min;
	byte sec;
	byte gmt15;
};

enum ISO_F {
	ISO_FDIR = 2,
	ISO_FLARGE = 0x80,
};

struct iso_fent {
	byte len;
	byte ext_attr_len;
	byte body_off[8]; //LE,BE
	byte body_len[8]; //LE,BE
	struct iso_date date;
	byte flags; // enum ISO_F
	byte unused[2];
	byte unused1[4];
	byte namelen;
	byte name[0]; //files: "NAME.EXT;NUM"  dirs: 0x00 (self) | 0x01 (parent) | "NAME"
	// byte pad; //exists if namelen is even
};

struct iso_rr {
	char id[2];
	byte len;
	byte data[0];
};

enum ISO_RR_NM_F {
	ISO_RR_NM_FCONTINUE = 1,
};

// [NM(flags:CONTINUE=1)...] NM(flags:CONTINUE=0)
struct iso_rr_nm {
	// "NM"
	byte unused;
	byte flags; //enum ISO_RR_NM_F
	byte data[0];
};

struct iso_rr_px {
	// "PX"
	byte unused1;
	byte mode[8]; //UNIX file mode
	byte unused2[8];
	byte uid[8];
	byte gid[8];
};

struct iso_rr_cl {
	// "CL"
	byte unused;
	byte child_off[8];
};

struct iso_voldesc_prim {
	byte unused;
	byte unused1[32];
	byte name[32];
	byte unused3[8];
	byte vol_size[8]; //LE[4],BE[4]
	byte unused4[32];
	byte unused5[4];
	byte unused6[4];
	byte log_blksize[4]; //LE[2],BE[2]
	byte path_tbl_size[8]; //LE,BE
	byte path_tbl1_off[4]; //LE
	byte path_tbl2_off[4]; //LE
	byte unused7[4];
	byte unused8[4];
	byte root_dir[34]; //struct iso_fent
};


static int iso_voldesc_parse_prim(const void *p);
static void iso_voldesc_prim_write(void *buf, uint type, uint root_dir_off, uint root_dir_size, uint nsectors);
static void iso_date_parse(const struct iso_date *d, ffdtm *dt);
static uint iso_ent_len(const struct iso_fent *ent);
static int iso_ent_parse(const void *p, size_t len, ffiso_file *f, uint64 off);
static ffstr iso_ent_name(const ffstr *name);
static int iso_rr_parse(const void *p, size_t len, ffiso_file *f);
static int iso_ent_write(void *buf, size_t cap, const struct ffiso_file *f, uint64 off, uint flags);
static int iso_dirs_count(ffarr *dirs, uint64 *off, uint flags);
static int iso_dirs_copy(ffarr *dst, ffarr *src);
static int iso_dirs_countall(ffiso_cook *c);
static void iso_files_setoffsets(ffarr *dirs, uint64 off);
static int iso_dir_write(ffiso_cook *c, uint joliet);


/** Parse primary volume descriptor. */
static int iso_voldesc_parse_prim(const void *p)
{
	const struct iso_voldesc *vd = p;
	if (ffs_cmp(vd->id, "CD001", 5))
		return -FFISO_EPRIMID;
	if (vd->ver != 1)
		return -FFISO_EPRIMVER;

	const struct iso_voldesc_prim *prim = (void*)vd->data;

	FFDBG_PRINTLN(5, "Prim Vol Desc:  vol-size:%u  log-blk-size:%u  path-tbl-size:%u  name:%*s"
		, ffint_ltoh32(prim->vol_size),  ffint_ltoh16(prim->log_blksize), ffint_ltoh32(prim->path_tbl_size)
		, (size_t)sizeof(prim->name), prim->name);

	if (ffint_ltoh16(prim->log_blksize) != FFISO_SECT)
		return -FFISO_ELOGBLK;

	return 0;
}

static void* iso_voldesc_write(void *buf, uint type)
{
	struct iso_voldesc *vd = buf;
	vd->type = type;
	ffmemcpy(vd->id, "CD001", 5);
	vd->ver = 1;
	return vd->data;
}

static void iso_write32(byte *dst, uint val)
{
	ffint_htol32(dst, val);
	ffint_hton32(dst + 4, val);
}
static void iso_write16(byte *dst, uint val)
{
	ffint_htol16(dst, val);
	ffint_hton16(dst + 2, val);
}

static void iso_voldesc_prim_write(void *buf, uint type, uint root_dir_off, uint root_dir_size, uint nsectors)
{
	ffmem_zero(buf, FFISO_SECT);
	struct iso_voldesc_prim *prim = iso_voldesc_write(buf, type);
	prim->name[0] = ' ';
	iso_write32(prim->vol_size, nsectors);
	iso_write16(prim->log_blksize, FFISO_SECT);

	struct ffiso_file f = {0};
	ffstr_set(&f.name, "\x00", 1);
	f.off = root_dir_off;
	f.size = root_dir_size;
	f.attr = FFUNIX_FILE_DIR;
	iso_ent_write(prim->root_dir, sizeof(prim->root_dir), &f, root_dir_off, 0);

	FFDBG_PRINTLN(5, "Prim Vol Desc:  vol-size:%u  off:%xu  size:%xu"
		, nsectors, root_dir_off, root_dir_size);
}

/** Parse date from file entry. */
static void iso_date_parse(const struct iso_date *d, ffdtm *dt)
{
	dt->year = 1900 + d->year;
	dt->month = d->month;
	dt->day = d->day;
	dt->hour = d->hour;
	dt->min = d->min;
	dt->sec = d->sec;
	fftime_setmsec(dt, 0);
	dt->weekday = 0;
	dt->yday = 0;
}

/** Write file entry date. */
static void iso_date_write(struct iso_date *d, const ffdtm *dt)
{
	d->year = dt->year - 1900;
	d->month = dt->month;
	d->day = dt->day;
	d->hour = dt->hour;
	d->min = dt->min;
	d->sec = dt->sec;
}

/** Get length of file entry before RR extensions. */
static uint iso_ent_len(const struct iso_fent *ent)
{
	return FFOFF(struct iso_fent, name) + ent->namelen + !(ent->namelen % 2);
}
static uint iso_ent_len2(uint namelen)
{
	return FFOFF(struct iso_fent, name) + namelen + !(namelen % 2);
}

/** Parse file entry.
Return entry length;  0: no more entries;  <0 on error */
static int iso_ent_parse(const void *p, size_t len, ffiso_file *f, uint64 off)
{
	const struct iso_fent *ent = p;

	if (len != 0 && ((byte*)p)[0] == 0)
		return 0;

	if (len < sizeof(struct iso_fent)
		|| len < ent->len
		|| ent->len < iso_ent_len(ent)
		|| ent->namelen == 0)
		return -FFISO_ELARGE;

	FFDBG_PRINTLN(6, "Dir Ent: off:%xU  body-off:%xu  body-len:%xu  flags:%xu  ext_attr_len:%u  name:%*s"
		, off, ffint_ltoh32(ent->body_off) * FFISO_SECT, ffint_ltoh32(ent->body_len)
		, ent->flags, ent->ext_attr_len
		, (size_t)ent->namelen, ent->name);

	if (ent->ext_attr_len != 0)
		return -FFISO_EUNSUPP;

	if (f == NULL)
		goto done;

	f->off = ffint_ltoh32(ent->body_off) * FFISO_SECT;
	f->size = ffint_ltoh32(ent->body_len);
	ffstr_set(&f->name, ent->name, ent->namelen);
	f->attr = 0;

	if (ent->flags & ISO_FDIR) {
		if (ent->namelen == 1 && (ent->name[0] == 0x00 || ent->name[0] == 0x01))
			f->name.len = 0;
		f->attr = FFUNIX_FILE_DIR;
	}

	ffdtm dt;
	iso_date_parse(&ent->date, &dt);
	fftime_join(&f->mtime, &dt, FFTIME_TZLOCAL);

done:
	return ent->len;
}

enum ENT_WRITE_F {
	ENT_WRITE_RR = 1,
	ENT_WRITE_JLT = 2,
	// ENT_WRITE_CUT = 4, //cut large filenames, don't return error
};

/** Write file entry.
@buf: must be filled with zeros.
 if NULL: return output space required.
@flags: enum ENT_WRITE_F
Return bytes written;  <0 on error. */
static int iso_ent_write(void *buf, size_t cap, const struct ffiso_file *f, uint64 off, uint flags)
{
	uint nlen, fnlen, reserved, rrlen;
	size_t n;
	FF_ASSERT(f->name.len != 0);

	reserved = 0;
	if ((f->attr & FFUNIX_FILE_DIR)
		&& f->name.len == 1
		&& (f->name.ptr[0] == 0x00 || f->name.ptr[0] == 0x01))
		reserved = 1;

	// determine filename length
	if (reserved)
		nlen = fnlen = 1;
	else if (flags & ENT_WRITE_JLT) {
		n = f->name.len;
		nlen = ffutf8_to_utf16(NULL, 0, f->name.ptr, &n, FFU_FWHOLE | FFU_UTF16BE);
		if (nlen >= 255) {
			// if (!(flags & ENT_WRITE_CUT))
				return -FFISO_ELARGE;
			nlen = ffmin(nlen, 254);
		}
		fnlen = nlen;

	} else {
		nlen = ffmin(f->name.len, FFSLEN("12345678.123"));
		fnlen = nlen;
		if (!(f->attr & FFUNIX_FILE_DIR))
			fnlen += FFSLEN(";1");
	}

	// get RR extensions length
	rrlen = 0;
	if (!reserved && (flags & ENT_WRITE_RR)) {
		rrlen = sizeof(struct iso_rr) + sizeof(struct iso_rr_nm) + f->name.len;
	}

	if (iso_ent_len2(fnlen) + rrlen > 255)
		return -FFISO_ELARGE;

	if (buf == NULL)
		return iso_ent_len2(fnlen) + rrlen;

	if (cap < iso_ent_len2(fnlen) + rrlen)
		return -FFISO_ELARGE;

	struct iso_fent *ent = buf;
	ent->namelen = fnlen;
	ent->len = iso_ent_len(ent) + rrlen;
	iso_write32(ent->body_off, f->off / FFISO_SECT);
	iso_write32(ent->body_len, f->size);

	ffdtm dt;
	fftime_split(&dt, &f->mtime, FFTIME_TZLOCAL);
	iso_date_write(&ent->date, &dt);

	if (f->attr & FFUNIX_FILE_DIR)
		ent->flags = ISO_FDIR;

	// write filename
	if (reserved)
		ent->name[0] = f->name.ptr[0];
	else if (flags & ENT_WRITE_JLT) {
		n = nlen;
		ffutf8_to_utf16((char*)ent->name, 255, f->name.ptr, &n, FFU_FWHOLE | FFU_UTF16BE);
	} else {
		ffs_upper((char*)ent->name, (char*)ent->name + nlen, f->name.ptr, nlen);
		if (!(f->attr & FFUNIX_FILE_DIR))
			ffs_copyz((char*)ent->name + nlen, (char*)buf + 255, ";1");
	}

	// write RR NM extension
	if (rrlen != 0) {
		struct iso_rr *rr = buf + iso_ent_len(ent);
		ffmemcpy(rr->id, "NM", 2);
		rr->len = rrlen;
		struct iso_rr_nm *nm = (void*)rr->data;
		ffs_copy((char*)nm->data, (char*)buf + 255, f->name.ptr, f->name.len);
	}

	FFDBG_PRINTLN(6, "Dir Ent: off:%xU  body-off:%xu  body-len:%xu  flags:%xu  name:%S  length:%u  RR-len:%u"
		, off, f->off, f->size, ent->flags, &f->name, ent->len, rrlen);
	return ent->len;
}

/** Get real filename. */
static ffstr iso_ent_name(const ffstr *name)
{
	ffstr s, ver;
	ffs_rsplit2by(name->ptr, name->len, ';', &s, &ver);
	if (ffstr_eqcz(&ver, "1")) {
		if (s.len != 0 && ffarr_back(&s) == '.')
			s.len--; // "NAME." -> "NAME"
		return s;
	}
	return *name;
}

/** Parse Rock-Ridge extension entries.
Return # of bytes read;  <0 on error */
static int iso_rr_parse(const void *p, size_t len, ffiso_file *f)
{
	const char *d = p, *end = p + len;
	uint dlen;
	for (;;) {

		if (d == end || d[0] == 0x00)
			break;

		const struct iso_rr *rr = (void*)d;
		if ((size_t)(end - d) < sizeof(struct iso_rr)
			|| (end - d) < rr->len
			|| rr->len <= sizeof(struct iso_rr))
			return -FFISO_ELARGE;
		d += rr->len;
		dlen = rr->len - sizeof(struct iso_rr);

		FFDBG_PRINTLN(10, "RR ext: %2s  size:%u", rr->id, rr->len);

		if (!ffmemcmp(rr->id, "NM", 2)) {
			if (dlen < sizeof(struct iso_rr_nm))
				continue;
			const struct iso_rr_nm *nm = (void*)(rr + 1);
			FF_ASSERT(nm->flags == 0);
			if (nm->flags != 0)
				return -FFISO_EUNSUPP;
			ffstr_set(&f->name, nm->data, (byte*)d - nm->data);

		} else if (!ffmemcmp(rr->id, "PX", 2)) {
			if (dlen < sizeof(struct iso_rr_px))
				continue;
			const struct iso_rr_px *px = (void*)(rr + 1);
			f->attr = ffint_ltoh32(px->mode);

		} else if (!ffmemcmp(rr->id, "CL", 2)) {
			if (dlen < sizeof(struct iso_rr_cl))
				continue;
			const struct iso_rr_cl *cl = (void*)(rr + 1);
			f->off = ffint_ltoh32(cl->child_off) * FFISO_SECT;
			FFDBG_PRINTLN(10, "RR CL: off:%xU", f->off);

		} else if (!ffmemcmp(rr->id, "RE", 2)) {
			if (dlen < 1)
				continue;
			f->name.len = 0;
		}
	}

	return d - (char*)p;
}


static const char* const iso_errs[] = {
	"unsupported logical block size",
	"no primary volume descriptor",
	"empty root directory in primary volume descriptor",
	"primary volume: bad id",
	"primary volume: bad version",
	"unsupported feature",
	"too large value",
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
		r = iso_ent_parse(prim->root_dir, sizeof(prim->root_dir), &f, c->inoff);
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
			r = iso_ent_parse(jlt->root_dir, sizeof(jlt->root_dir), &f, c->inoff);
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

		r = iso_ent_parse(c->d.ptr, c->d.len, &c->curfile, c->inoff);
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
			ffarr_setshift(&rr, ent, r, iso_ent_len((void*)ent));
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
	if (c->fcursor == ffchain_sentl(&c->files))
		return NULL;
	ffiso_file *f = FF_GETPTR(ffiso_file, sib, c->fcursor);
	c->fcursor = c->fcursor->next;
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

int ffiso_wcreate(ffiso_cook *c, uint flags)
{
	if (NULL == ffarr_alloc(&c->buf, 16 * FFISO_SECT))
		return -1;
	c->ifile = -1;
	struct dir *d;
	d = ffarr_pushgrowT(&c->dirs, 64, struct dir);
	if (d == NULL)
		return FFISO_ESYS;
	ffmem_tzero(d);
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

	ffarr_free(&c->buf);
}

enum W {
	W_DIR_WAIT, W_EMPTY, W_EMPTY_VD, W_DIR, W_DIR_JLT, W_FILE_NEXT, W_FILE, W_FILE_DONE,
	W_VOLDESC_SEEK, W_VOLDESC_PRIM, W_VOLDESC_JLT, W_VOLDESC_TERM, W_DONE, W_ERR,
};

/** Set offset and size for each directory. */
static int iso_dirs_count(ffarr *dirs, uint64 *off, uint flags)
{
	int r;
	uint64 size;
	struct dir *d, *parent;
	struct ffiso_file *f;

	FFARR_WALKT(dirs, d, struct dir) {

		size = iso_ent_len2(1) * 2;

		FFARR_WALKT(&d->files, f, struct ffiso_file) {
			r = iso_ent_write(NULL, 0, f, 0, flags);
			if (r < 0)
				return -r;
			size += r;
		}

		if (size > (uint)-1)
			return FFISO_ELARGE;

		d->info.size = size;
		d->info.off = *off;
		if ((void*)d != (void*)dirs->ptr) {
			// set info in the parent's file array
			parent = ffarr_itemT(dirs, d->parent_dir, struct dir);
			f = ffarr_itemT(&parent->files, d->ifile, struct ffiso_file);
			f->size = size;
			f->off = *off;
		}
		*off += ff_align_ceil2(size, FFISO_SECT);
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
			if (!(f->attr & FFUNIX_FILE_DIR)) {
				f->off = off;
				off += ff_align_ceil2(f->size, FFISO_SECT);
			}
		}
	}
}

/** Set offset and size for each directory.
Set offset for each file. */
static int iso_dirs_countall(ffiso_cook *c)
{
	int r;
	uint64 off = (16 + 3) * FFISO_SECT;
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

/** Write directory contents. */
static int iso_dir_write(ffiso_cook *c, uint joliet)
{
	int r;
	ffarr *dirs = (joliet) ? &c->dirs_jlt : &c->dirs;
	const struct dir *d = ffarr_itemT(dirs, c->idir++, struct dir);
	uint size_al = ff_align_ceil2(d->info.size, FFISO_SECT);
	uint flags = !(c->options & FFISO_NORR) ? ENT_WRITE_RR : 0;
	if (joliet)
		flags = ENT_WRITE_JLT;
	c->buf.len = 0;
	if (NULL == ffarr_realloc(&c->buf, size_al))
		return FFISO_ESYS;
	ffmem_zero(c->buf.ptr, size_al);

	struct ffiso_file f = {0};

	f.off = d->info.off;
	f.size = d->info.size;
	f.attr = FFUNIX_FILE_DIR;
	ffstr_set(&f.name, "\x00", 1);
	r = iso_ent_write(ffarr_end(&c->buf), ffarr_unused(&c->buf), &f, c->off, flags);
	c->buf.len += r;

	ffstr_set(&f.name, "\x01", 1);
	const struct dir *parent = ffarr_itemT(dirs, d->parent_dir, struct dir);
	f.off = parent->info.off;
	f.size = ff_align_ceil2(parent->info.size, FFISO_SECT);
	f.attr = FFUNIX_FILE_DIR;
	r = iso_ent_write(ffarr_end(&c->buf), ffarr_unused(&c->buf), &f, c->off, flags);
	c->buf.len += r;

	struct ffiso_file *pf;
	FFARR_WALKT(&d->files, pf, struct ffiso_file) {
		r = iso_ent_write(ffarr_end(&c->buf), ffarr_unused(&c->buf), pf, c->off, flags);
		c->buf.len += r;
	}

	c->buf.len = size_al;
	return 0;
}

/* ISO-9660 write:
. Get the complete and sorted file list from user
 . ffiso_wfile()
. Write empty 16 sectors (FFISO_DATA)
. Write 3 empty sectors for volume descriptors
. For each directory estimate offset and size of its contents
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

/:
  "." -> /
  ".." -> /
  "a" -> /a
    (RR entry)...
  "d" -> /d
  "z" -> /z

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
		if (0 != (r = iso_dirs_countall(c)))
			return r;
		c->idir = 0;
		c->state = W_DIR;
		return FFISO_DATA;

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
		c->nsectors += c->buf.len / FFISO_SECT;
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
		c->in.len = 0;
		c->nsectors += c->buf.len / FFISO_SECT;
		return FFISO_DATA;
	}

	case W_FILE_DONE:
		c->state = W_FILE_NEXT;
		ffmem_zero(c->buf.ptr, FFISO_SECT);
		ffstr_set(&c->out, c->buf.ptr, FFISO_SECT - (c->curfile_size % FFISO_SECT));
		c->nsectors++;
		return FFISO_DATA;

	case W_FILE_NEXT:
		return FFISO_MORE;


	case W_VOLDESC_SEEK:
		c->off = 16 * FFISO_SECT;
		c->state = W_VOLDESC_PRIM;
		return FFISO_SEEK;

	case W_VOLDESC_PRIM: {
		const struct dir *d = (void*)c->dirs.ptr;
		iso_voldesc_prim_write(c->buf.ptr, ISO_T_PRIM
			, d->info.off, d->info.size, 16 + 3 + c->nsectors);

		ffstr_set(&c->out, c->buf.ptr, FFISO_SECT);
		c->off += FFISO_SECT;
		c->state = !(c->options & FFISO_NOJOLIET) ? W_VOLDESC_JLT : W_VOLDESC_TERM;
		return FFISO_DATA;
	}

	case W_VOLDESC_JLT: {
		const struct dir *jd = (void*)c->dirs_jlt.ptr;
		ffmem_zero(c->buf.ptr, FFISO_SECT);
		iso_voldesc_prim_write(c->buf.ptr, ISO_T_JOLIET
			, jd->info.off, jd->info.size, 16 + 3 + c->nsectors);

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

static struct dir* dir_find(ffiso_cook *c, const ffstr *path)
{
	struct dir *d;
	FFARR_WALKT(&c->dirs, d, struct dir) {
		if (ffstr_eq2(path, &d->info.name))
			return d;
	}
	return NULL;
}

void ffiso_wfile(ffiso_cook *c, const ffiso_file *f)
{
	if (c->state != W_DIR_WAIT) {
		c->err = FFISO_ENOTREADY;
		goto err;
	}

	struct dir *parent, *d;
	ffiso_file *nf;
	ffstr path, name;

	ffpath_split2(f->name.ptr, f->name.len, &path, &name);
	parent = dir_find(c, &path);
	if (parent == NULL) {
		c->err = FFISO_EDIRORDER; // trying to add "dir/file" with no "dir" added previously
		goto err;
	}

	if (f->attr & FFUNIX_FILE_DIR) {
		uint i = parent - (struct dir*)c->dirs.ptr;
		d = ffarr_pushgrowT(&c->dirs, 64, struct dir);
		if (d == NULL) {
			c->err = FFISO_ESYS;
			goto err;
		}
		ffmem_tzero(d);
		d->parent_dir = i;
		parent = ffarr_itemT(&c->dirs, d->parent_dir, struct dir);
		d->ifile = parent->files.len;
		if (NULL == ffstr_alcopystr(&d->info.name, &f->name)) {
			c->dirs.len--; {
			c->err = FFISO_ESYS;
			goto err;
		}
		}
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
	nf->attr = f->attr;
	nf->mtime = f->mtime;
	nf->size = f->size;
	return;

err:
	c->state = W_ERR;
}

static struct ffiso_file* file_next(ffiso_cook *c)
{
	struct dir *d = (void*)c->dirs.ptr;
	c->ifile++;

	for (uint i = c->idir;  i != c->dirs.len;  i++) {

		struct ffiso_file *f = (void*)d[i].files.ptr;

		for (uint k = c->ifile;  k != d[i].files.len;  k++) {

			if (!(f[k].attr & FFUNIX_FILE_DIR)) {
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

	const struct ffiso_file *f = file_next(c);
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
