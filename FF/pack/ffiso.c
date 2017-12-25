/**
Copyright (c) 2017 Simon Zolin
*/

#include <FF/pack/iso.h>
#include <FF/number.h>
#include <FF/time.h>
#include <FF/data/utf8.h>


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
static void iso_date_parse(const struct iso_date *d, ffdtm *dt);
static uint iso_ent_len(const struct iso_fent *ent);
static int iso_ent_parse(const void *p, size_t len, ffiso_file *f, uint64 off);
static ffstr iso_ent_name(const ffstr *name);
static int iso_rr_parse(const void *p, size_t len, ffiso_file *f);


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

/** Get length of file entry before RR extensions. */
static uint iso_ent_len(const struct iso_fent *ent)
{
	return FFOFF(struct iso_fent, name) + ent->namelen + !(ent->namelen % 2);
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
			ffarr a = {0};
			ffstr_catfmt(&a, "%S/%S", &c->curdir->name, &c->curfile.name);
			ffstr_set2(&c->curfile.name, &a);
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
