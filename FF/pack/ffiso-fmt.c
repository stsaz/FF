/**
Copyright (c) 2018 Simon Zolin
*/

#include <FF/pack/iso-fmt.h>
#include <FF/path.h>
#include <FF/number.h>
#include <FF/data/utf8.h>


struct iso_rr {
	char id[2];
	byte len;
	byte ver; //=1
	byte data[0];
};

enum RR_FLAGS {
	RR_HAVE_PX = 1,
	RR_HAVE_NM = 8,
};

// "SP"
struct iso_rr_sp {
	byte data[3];
};

// "RR"
struct iso_rr_rr {
	byte flags; //enum RR_FLAGS
};

enum ISO_RR_NM_F {
	ISO_RR_NM_FCONTINUE = 1,
};

// [NM(flags:CONTINUE=1)...] NM(flags:CONTINUE=0)
struct iso_rr_nm {
	// "NM"
	byte flags; //enum ISO_RR_NM_F
	byte data[0];
};

struct iso_rr_px {
	// "PX"
	byte mode[8]; //UNIX file mode
	byte unused2[8];
	byte uid[8];
	byte gid[8];
};

struct iso_rr_cl {
	// "CL"
	byte child_off[8];
};


static uint iso_ent_name_write(char *dst, const ffstr *filename, uint attr);
static int iso_rr_write(void *dst, const char *name, uint datalen);


#define ISO_SYSNAME  "LINUX"
#define ISO_UCS2L3_ESCSEQ  "%/E"

int iso_voldesc_parse_prim(const void *p)
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

void* iso_voldesc_write(void *buf, uint type)
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

static void iso_writename(char *dst, size_t cap, const char *s)
{
	const char *end = dst + cap;
	dst = ffs_copyz(dst, end, s);
	ffs_fill(dst, end, ' ', end - dst);
}

static void iso_writename16(char *dst, size_t cap, const char *s)
{
	uint i = ffutf8_to_utf16(dst, cap, s, ffsz_len(s), FFUNICODE_UTF16BE);
	for (;  i != cap;  i += 2) {
		dst[i] = '\0';
		dst[i + 1] = ' ';
	}
}

void iso_voldesc_prim_write(void *buf, const struct iso_voldesc_prim_host *info)
{
	ffmem_zero(buf, FFISO_SECT);
	struct iso_voldesc_prim *prim = iso_voldesc_write(buf, info->type);
	if (info->type == ISO_T_JOLIET) {
		iso_writename16((void*)prim->system, sizeof(prim->system), ISO_SYSNAME);
		iso_writename16((void*)prim->name, sizeof(prim->name), info->name);
		ffmemcpy(prim->esc_seq, ISO_UCS2L3_ESCSEQ, 3);
	} else {
		iso_writename((void*)prim->system, sizeof(prim->system), ISO_SYSNAME);
		iso_writename((void*)prim->name, sizeof(prim->name), info->name);
	}
	iso_write32(prim->vol_size, info->vol_size);
	iso_write16(prim->vol_set_size, 1);
	iso_write16(prim->vol_set_seq, 1);
	iso_write16(prim->log_blksize, FFISO_SECT);

	iso_write32(prim->path_tbl_size, info->path_tbl_size);
	ffint_htol32(prim->path_tbl1_off, info->path_tbl_off);
	ffint_hton32(prim->path_tbl1_off_be, info->path_tbl_off_be);

	struct ffiso_file f = {0};
	ffstr_set(&f.name, "\x00", 1);
	f.off = info->root_dir_off;
	f.size = info->root_dir_size;
	f.attr = FFUNIX_FILE_DIR;
	iso_ent_write(prim->root_dir, sizeof(prim->root_dir), &f, info->root_dir_off, 0);

	FFDBG_PRINTLN(5, "Prim Vol Desc:  vol-size:%u  off:%xu  size:%xu  path-table:%xu %xu,%xu"
		, info->vol_size, info->root_dir_off, info->root_dir_size
		, info->path_tbl_size, info->path_tbl_off, info->path_tbl_off_be);
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

int iso_ent_parse(const void *p, size_t len, ffiso_file *f, uint64 off)
{
	const struct iso_fent *ent = p;

	if (len != 0 && ((byte*)p)[0] == 0)
		return 0;

	if (len < sizeof(struct iso_fent)
		|| len < ent->len
		|| ent->len < iso_ent_len(ent)
		|| ent->namelen == 0)
		return -FFISO_ELARGE;

	FFDBG_PRINTLN(6, "Dir Ent: off:%xU  body-off:%xu  body-len:%xu  flags:%xu  ext_attr_len:%u  length:%u  RR-len:%u  name:%*s"
		, off, ffint_ltoh32(ent->body_off) * FFISO_SECT, ffint_ltoh32(ent->body_len)
		, ent->flags, ent->ext_attr_len
		, ent->len, ent->len - iso_ent_len(ent)
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

int iso_ent_write(void *buf, size_t cap, const struct ffiso_file *f, uint64 off, uint flags)
{
	uint fnlen, reserved, rrlen;
	FF_ASSERT(f->name.len != 0);

	reserved = 0;
	if (ffiso_file_isdir(f)
		&& f->name.len == 1
		&& (f->name.ptr[0] == 0x00 || f->name.ptr[0] == 0x01))
		reserved = 1;

	// determine filename length
	if (reserved)
		fnlen = 1;
	else if (flags & ENT_WRITE_JLT) {
		// Note: by spec these chars are not supported: 0x00..0x1f, * / \\ : ; ?
		ffssize ss = ffutf8_to_utf16(NULL, 0, f->name.ptr, f->name.len, FFUNICODE_UTF16BE);
		if (ss < 0)
			return -FFISO_ELARGE; // can't encode into UTF-16
		fnlen = ss;

	} else {
		fnlen = iso_ent_name_write(NULL, &f->name, f->attr);
	}

	// get RR extensions length
	rrlen = 0;
	if (flags & ENT_WRITE_RR) {
		rrlen = sizeof(struct iso_rr) + sizeof(struct iso_rr_rr);
		if (!reserved)
			rrlen += sizeof(struct iso_rr) + sizeof(struct iso_rr_nm) + f->name.len;
		if (flags & ENT_WRITE_RR_SP)
			rrlen += sizeof(struct iso_rr) + sizeof(struct iso_rr_sp);
	}

	if (iso_ent_len2(fnlen) + rrlen > 255)
		return -FFISO_ELARGE;

	if (buf == NULL)
		return iso_ent_len2(fnlen) + rrlen;

	if (cap < iso_ent_len2(fnlen) + rrlen)
		return -FFISO_ELARGE;

	struct iso_fent *ent = buf;
	iso_write16(ent->vol_seqnum, 1);
	ent->namelen = fnlen;
	ent->len = iso_ent_len(ent) + rrlen;
	iso_write32(ent->body_off, f->off / FFISO_SECT);
	iso_write32(ent->body_len, f->size);

	ffdtm dt;
	fftime_split(&dt, &f->mtime, FFTIME_TZLOCAL);
	iso_date_write(&ent->date, &dt);

	if (ffiso_file_isdir(f))
		ent->flags = ISO_FDIR;

	// write filename
	if (reserved)
		ent->name[0] = f->name.ptr[0];
	else if (flags & ENT_WRITE_JLT) {
		ffutf8_to_utf16((char*)ent->name, 255, f->name.ptr, f->name.len, FFUNICODE_UTF16BE);
	} else {
		iso_ent_name_write((char*)ent->name, &f->name, f->attr);
	}

	// write RR extensions
	if (rrlen != 0) {
		char *p = (char*)buf + iso_ent_len(ent);

		if (flags & ENT_WRITE_RR_SP) {
			p += iso_rr_write(p, "SP", sizeof(struct iso_rr_sp));
			struct iso_rr_sp *sp = (void*)p;
			sp->data[0] = 0xbe;
			sp->data[1] = 0xef;
			p += sizeof(struct iso_rr_sp);
		}

		p += iso_rr_write(p, "RR", sizeof(struct iso_rr_rr));
		struct iso_rr_rr *r = (void*)p;
		p += sizeof(struct iso_rr_rr);

		if (!reserved) {
			p += iso_rr_write(p, "NM", sizeof(struct iso_rr_nm) + f->name.len);
			struct iso_rr_nm *nm = (void*)p;
			ffs_copy((char*)nm->data, (char*)buf + 255, f->name.ptr, f->name.len);
			p += sizeof(struct iso_rr_nm) + f->name.len;
			r->flags |= RR_HAVE_NM;
		}
	}

	FFDBG_PRINTLN(6, "Dir Ent: off:%xU  body-off:%xu  body-len:%xu  flags:%xu  name:%S  length:%u  RR-len:%u"
		, off, f->off, f->size, ent->flags, &f->name, ent->len, rrlen);
	return ent->len;
}

ffstr iso_ent_name(const ffstr *name)
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

static size_t iso_copyname(char *dst, const char *end, const char *src, size_t len)
{
	size_t i;
	len = ffmin(len, end - dst);

	for (i = 0;  i != len;  i++) {
		uint ch = (byte)src[i];

		if (ffchar_isletter(ch))
			dst[i] = ffchar_upper(ch);
		else if (ffchar_isdigit(ch))
			dst[i] = ch;
		else
			dst[i] = '_';
	}

	return i;
}

/*
Filename with extension: "NAME.EXT;1"
Filename without extension: "NAME."
Directory name: "NAME"
*/
static uint iso_ent_name_write(char *dst, const ffstr *filename, uint attr)
{
	ffstr name, ext;
	ffpath_splitname(filename->ptr, filename->len, &name, &ext);
	name.len = ffmin(name.len, 8);
	ext.len = ffmin(ext.len, 3);
	uint fnlen = name.len + ext.len;
	if (!(attr & FFUNIX_FILE_DIR) || ext.len != 0)
		fnlen += FFSLEN(".");
	if (!(attr & FFUNIX_FILE_DIR))
		fnlen += FFSLEN(";1");

	if (dst == NULL)
		return fnlen;

	char *p = dst;
	const char *end = dst + fnlen;
	p += iso_copyname(p, end, name.ptr, name.len);
	if (!(attr & FFUNIX_FILE_DIR) || ext.len != 0)
		*p++ = '.';
	p += iso_copyname(p, end, ext.ptr, ext.len);
	if (!(attr & FFUNIX_FILE_DIR))
		ffs_copyz(p, end, ";1");
	return fnlen;
}


int iso_rr_parse(const void *p, size_t len, ffiso_file *f)
{
	const char *d = p, *end = (char*)p + len;
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

static int iso_rr_write(void *dst, const char *name, uint datalen)
{
	FF_ASSERT(datalen + sizeof(struct iso_rr) < 255);

	struct iso_rr *rr = dst;
	ffmemcpy(rr->id, name, 2);
	rr->len = datalen + sizeof(struct iso_rr);
	rr->ver = 1;
	return sizeof(struct iso_rr);
}


struct iso_pathent {
	byte len;
	byte unused;
	byte extent[4];
	byte parent_index[2];
	byte name[0];
	// byte pad;
};

int iso_pathent_write(void *dst, size_t cap, const ffstr *name, uint extent, uint parent, uint flags)
{
	int reserved;
	size_t fnlen;

	reserved = (name->len == 1 && name->ptr[0] == 0x00);

	if (reserved)
		fnlen = 1;
	else if (flags & PATHENT_WRITE_JLT)
		fnlen = name->len * 2;
	else
		fnlen = iso_ent_name_write(NULL, name, FFUNIX_FILE_DIR);
	uint n = sizeof(struct iso_pathent) + fnlen + !!(fnlen % 2);
	if (n > 255)
		return -FFISO_ELARGE;
	if (dst == NULL)
		return n;
	if (n > cap)
		return -FFISO_ELARGE;

	struct iso_pathent *p = dst;
	p->len = fnlen;

	if (flags & PATHENT_WRITE_BE) {
		ffint_hton32(p->extent, extent);
		ffint_hton16(p->parent_index, parent);
	} else {
		ffint_htol32(p->extent, extent);
		ffint_htol16(p->parent_index, parent);
	}

	if (reserved)
		p->name[0] = '\0';
	else if (flags & PATHENT_WRITE_JLT) {
		ffutf8_to_utf16((char*)p->name, 255, name->ptr, name->len, FFUNICODE_UTF16BE);
	} else
		iso_ent_name_write((char*)p->name, name, FFUNIX_FILE_DIR);

	FFDBG_PRINTLN(10, "name:%S  body-off:%xu  parent:%u"
		, name, extent * FFISO_SECT, parent);
	return n;
}
