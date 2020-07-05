/**
Copyright (c) 2016 Simon Zolin
*/

#include <FF/pack/zip.h>
#include <FF/crc.h>
#include <FF/number.h>
#include <FF/path.h>
#include <FF/time.h>
#include <FFOS/dir.h>
#include <FFOS/error.h>


enum ZIP_FLAGS {
	ZIP_FENCRYPTED = 1,
	ZIP_FDATADESC = 8, // zip_trl
};

enum ZIP_COMP {
	ZIP_STORED = 0,
	ZIP_DEFLATED = 8,
};

/** Local file header. */
typedef struct zip_hdr {
	char sig[4]; //"PK\3\4"
	byte minver[2];
	byte flags[2]; //enum ZIP_FLAGS
	byte comp[2]; //enum ZIP_COMP
	byte modtime[2];
	byte moddate[2];
	byte crc32[4];
	byte size[4];
	byte size_orig[4];
	byte filenamelen[2];
	byte extralen[2];
	char filename[0];
	// char extra[0];
} zip_hdr;

/** Data descriptor. */
typedef struct zip_trl {
	// char sig[4]; //"PK\7\8" - optional signature
	byte crc32[4];
	byte size[4];
	byte size_orig[4];
} zip_trl;

enum FFZIP_SYS {
	FFZIP_FAT = 0,
	FFZIP_UNIX = 3,
};

/** Central directory header. */
typedef struct zip_cdir {
	char sig[4]; //"PK\1\2"
	byte ver;
	byte sysver; //enum FFZIP_SYS
	byte minver[2];
	byte flags[2];
	byte comp[2];
	byte modtime[2];
	byte moddate[2];
	byte crc32[4];
	byte size[4];
	byte size_orig[4];
	byte filenamelen[2];
	byte extralen[2];
	byte commentlen[2];
	byte disknum[2];
	byte attrs_int[2]; // bit 0: is text file
	byte attrs[4]; // [0]: enum FFWIN_FILE_ATTR; [2..3]: enum FFUNIX_FILEATTR
	byte offset[4];
	char filename[0];
	// char extra[0]; // struct zip64_extra, struct ntfs_extra
	// char comment[0];
} zip_cdir;

// struct zip64_extra {
	// char id[2]; // "\1\0"
	// byte size[2]; // size of the following data (4 or more)
	// byte size_orig[8]; // appears if zip_cdir.size_orig == -1
	// byte size_comp[8]; // appears if zip_cdir.size == -1
	// byte offset[8]; // appears if zip_cdir.offset == -1
	// byte disk[4]; // appears if zip_cdir.disknum == -1
// };

struct ntfs_extra {
	// char id[2]; // "\x0A\0"
	// byte size[2]; // size of the following data
	byte reserved[4];

	byte tag[2]; // "\1\0"
	byte tag_size[2]; // size of the following data
	byte mod_time[8];
	byte access_time[8];
	byte create_time[8];
};

typedef struct zip_cdir_trl {
	char sig[4]; //"PK\5\6"
	byte disknum[2];
	byte cdir_disk[2];
	byte disk_ents[2];
	byte total_ents[2];
	byte cdir_size[4]; //size of zip_cdir[].  -1: value is in zip64_cdir_trl
	byte cdir_off[4]; // -1: value is in zip64_cdir_trl
	byte commentlen[2];
	byte comment[0];
} zip_cdir_trl;

struct zip64_cdir_trl {
	char sig[4]; //"PK\6\6"
	byte size[8]; // size of the following data
	byte ver[2];
	byte ver2[2];
	byte disk[4];
	byte cdir_disk[4];
	byte disk_ents[8];
	byte total_ents[8];
	byte cdir_size[8];
	byte cdir_offset[8];
	// char data[]
};

struct zip64_cdir_trl_locator {
	char sig[4]; //"PK\6\7"
	byte cdir_trl_disk[4];
	byte offset[8];
	byte disks_number[4];
};

enum {
	ZIP_MINVER = 20,
	ZIP_CDIR_TRL_MAXSIZE = 0xffff + sizeof(zip_cdir_trl),
};

static int zip_cdir_parse(ffzip_file *f, const char *buf);
static int zip_fhdr_parse(ffzip_file *f, const char *buf, ffzip_file *cdir);
static int zip_fhdr_write(char *buf, char *cdir_buf, ffstr *name, const fftime *mtime, const ffzip_fattr *attrs, uint64 total_out);


static FFINL void time_fromdos(ffdtm *dt, uint date, uint time)
{
	dt->year = (date >> 9) + 1980;
	dt->month = (date >> 5) & 0x0f;
	dt->day = (date & 0x1f);
	dt->hour = (time >> 11);
	dt->min = (time >> 5) & 0x3f;
	dt->sec = (time & 0x1f) * 2;
}

static FFINL void time_todos(const ffdtm *dt, uint *date, uint *time)
{
	*date = ((dt->year - 1980) << 9) | (dt->month << 5) | (dt->day);
	*time = (dt->hour << 11) | (dt->min << 5) | (dt->sec / 2);
}

static const byte zip_defhdr[] = {
	'P','K','\x03','\x04',  ZIP_MINVER,0,  ZIP_FDATADESC,0,  ZIP_DEFLATED,0,  0,0,  0,0,
	0,0,0,0,  0,0,0,0,  0,0,0,0,
	0,0,  0,0,
};

static const byte zip_defcdir[] = {
	'P','K','\x01','\x02',  ZIP_MINVER,0,  ZIP_MINVER,0,  ZIP_FDATADESC,0,  ZIP_DEFLATED,0,
};

static const byte zip_defcdir_trl[] = {
	'P','K','\x05','\x06',
};

/** Parse CDIR entry.
Return CDIR entry size;  <0 on error. */
static int zip_cdir_parse(ffzip_file *f, const char *buf)
{
	const zip_cdir *cdir = (void*)buf;
	if (ffs_cmp(cdir->sig, zip_defcdir, 4))
		return -FFZIP_ECDIR;

	ffdtm dt;
	time_fromdos(&dt, ffint_ltoh16(cdir->moddate), ffint_ltoh16(cdir->modtime));
	fftime_join(&f->mtime, &dt, FFTIME_TZLOCAL);

	f->zsize = ffint_ltoh32(cdir->size);
	f->osize = ffint_ltoh32(cdir->size_orig);
	f->attrs.win = cdir->attrs[0];
	f->attrs.unixmode = ffint_ltoh16(&cdir->attrs[2]);
	f->crc = ffint_ltoh32(cdir->crc32);
	f->offset = ffint_ltoh32(cdir->offset);
	return sizeof(zip_cdir) + ffint_ltoh16(cdir->filenamelen) + ffint_ltoh16(cdir->extralen) + ffint_ltoh16(cdir->commentlen);
}

/** Parse file header.
Return file header size;  <0 on error. */
static int zip_fhdr_parse(ffzip_file *f, const char *buf, ffzip_file *cdir)
{
	const zip_hdr *h = (void*)buf;
	if (ffs_cmp(h->sig, zip_defhdr, 4))
		return -FFZIP_EHDR;

	uint flags = ffint_ltoh16(h->flags);
	if (flags & ZIP_FENCRYPTED)
		return -FFZIP_EFLAGS;

	f->comp = ffint_ltoh16(h->comp);
	if (f->comp != ZIP_STORED && f->comp != ZIP_DEFLATED)
		return -FFZIP_ECOMP;
	cdir->comp = f->comp;

	f->crc = ffint_ltoh32(h->crc32);
	f->zsize = ffint_ltoh32(h->size);
	f->osize = ffint_ltoh32(h->size_orig);
	if ((f->crc != 0 && f->crc != cdir->crc)
		|| (f->zsize != 0 && f->zsize != cdir->zsize)
		|| (f->osize != 0 && f->osize != cdir->osize))
		return -FFZIP_EHDR_CDIR;

	return sizeof(zip_hdr) + ffint_ltoh16(h->filenamelen) + ffint_ltoh16(h->extralen);
}

/** Write file header.
Return file header size;  <0 on error. */
static int zip_fhdr_write(char *buf, char *cdir_buf, ffstr *name, const fftime *mtime, const ffzip_fattr *attrs, uint64 total_out)
{
	uint modtime, moddate;
	ffdtm dt;
	fftime_split(&dt, mtime, FFTIME_TZLOCAL);
	time_todos(&dt, &moddate, &modtime);


	zip_hdr *h = (void*)buf;
	ffmemcpy(h, zip_defhdr, sizeof(zip_defhdr));
	ffint_htol16(h->modtime, modtime);
	ffint_htol16(h->moddate, moddate);
	name->len = ffpath_norm(h->filename, name->len, name->ptr, name->len, FFPATH_MERGEDOTS | FFPATH_FORCESLASH | FFPATH_TOREL);
	if (name->len == 0)
		return -FFZIP_EFNAME;
	name->ptr = h->filename;

	if (ffzip_isdir(attrs) && !ffpath_slash(h->filename[name->len - 1]))
		h->filename[name->len++] = '/'; //add trailing slash

	ffint_htol16(h->filenamelen, name->len);


	zip_cdir *cdir = (void*)cdir_buf;
	ffmem_tzero(cdir);
	ffmemcpy(cdir, zip_defcdir, sizeof(zip_defcdir));

#ifdef FF_WIN
	cdir->sysver = FFZIP_FAT;
#else
	cdir->sysver = FFZIP_UNIX;
#endif
	cdir->attrs[0] = (byte)attrs->win;
	ffint_htol16(&cdir->attrs[2], attrs->unixmode);

	ffint_htol16(cdir->modtime, modtime);
	ffint_htol16(cdir->moddate, moddate);
	ffint_htol16(cdir->filenamelen, name->len);
	ffmemcpy(cdir->filename, h->filename, name->len);
	ffint_htol32(cdir->offset, total_out);
	return sizeof(zip_hdr) + name->len;
}

/** Get next CDIR extra record
Return record ID */
static inline int zip_cdirextra_next(ffstr *data, ffstr *chunk)
{
	if (4 > data->len)
		return -1;
	uint id = ffint_ltoh16(data->ptr);
	uint size = ffint_ltoh16(data->ptr + 2);
	if (4 + size > data->len)
		return -1;
	ffstr_set(chunk, data->ptr + 4, size);
	ffstr_shift(data, 4 + size);
	return id;
}

static inline int zip_zip64extra_parse(const zip_cdir *cdir, const ffstr *data, ffzip_file *f)
{
	uint i = 0;

	uint size_orig = ffint_ltoh32(cdir->size_orig);
	if (size_orig == (uint)-1) {
		if (i + 8 > data->len)
			return -1;
		f->osize = ffint_ltoh64(data->ptr + i);
		i += 8;
	}

	uint size_comp = ffint_ltoh32(cdir->size);
	if (size_comp == (uint)-1) {
		if (i + 8 > data->len)
			return -1;
		f->zsize = ffint_ltoh64(data->ptr + i);
		i += 8;
	}

	uint offset = ffint_ltoh32(cdir->offset);
	if (offset == (uint)-1) {
		if (i + 8 > data->len)
			return -1;
		f->offset = ffint_ltoh64(data->ptr + i);
		i += 8;
	}

	// disk

	return 0;
}

static inline int zip_ntfsextra_parse(const ffstr *data, ffzip_file *f)
{
	if (sizeof(struct ntfs_extra) > data->len)
		return -1;
	const struct ntfs_extra *ntfs = (void*)data->ptr;

	uint tag = ffint_ltoh16(ntfs->tag);
	if (tag != 1)
		return -1;
	uint tag_size = ffint_ltoh16(ntfs->tag_size);
	if (tag_size < 8 * 3)
		return -1;

	uint64 t = ffint_ltoh64(ntfs->mod_time);
	fftime_winftime wt;
	wt.lo = (uint)t;
	wt.hi = (uint)(t >> 32);
	f->mtime = fftime_from_winftime(&wt);
	return 0;
}


static const char *const zip_errs[] = {
	"not ready", // FFZIP_ENOTREADY
	"libz init", // FFZIP_ELZINIT
	"reached maximum number of items", // FFZIP_EMAXITEMS
	"invalid filename", // FFZIP_EFNAME
	"multi-disk archives are not supported", // FFZIP_EDISK
	"bad CDIR record", // FFZIP_ECDIR
	"bad local file header", // FFZIP_EHDR
	"unsupported flags", // FFZIP_EFLAGS
	"unsupported compression method", // FFZIP_ECOMP
	"info from CDIR and local file header don't match", // FFZIP_EHDR_CDIR
	"CRC mismatch", // FFZIP_ECRC
	"invalid CDIR size", // FFZIP_ECDIR_SIZE
	"size mismatch for STORED file", // FFZIP_EFSIZE
};

const char* _ffzip_errstr(int err, z_ctx *lz)
{
	switch (err) {
	case FFZIP_ESYS:
		return fferr_strp(fferr_last());

	case FFZIP_ELZ:
		return z_errstr(lz);
	}
	return zip_errs[err - FFZIP_ENOTREADY];
}

#define ERR(z, n) \
	(z)->err = n, FFZIP_ERR

#define ERRSTR(z, msg) \
	(z)->err = FFZIP_EMSG, (z)->errmsg = msg, FFZIP_ERR


void ffzip_close(ffzip *z)
{
	ffarr_free(&z->buf);
	ffchain_item *it;
	for (it = ffchain_first(&z->cdir);  it != ffchain_sentl(&z->cdir); ) {
		ffzip_file *f = FF_GETPTR(ffzip_file, sib, it);
		it = it->next;
		ffmem_safefree(f->fn);
		ffmem_free(f);
	}
	ffchain_init(&z->cdir);
	FF_SAFECLOSE(z->lz, NULL, z_inflate_free);
}

enum E_READ {
	R_GATHER,
	R_CDIR_TRL_SEEK, R_CDIR_TRL,
	R_CDIR_NEXT, R_CDIR, R_CDIR_FIN,
	R_CDIR64_LOC, R_CDIR64,
	R_FHDR_SEEK, R_FHDR, R_FHDR_FIN, R_DATA, R_FDONE,
};

void ffzip_init(ffzip *z, uint64 total_size)
{
	z->inoff = total_size;
	z->codepage = FFUNICODE_WIN1252;
	z->state = R_CDIR_TRL_SEEK;
	ffchain_init(&z->cdir);
}

void ffzip_readfile(ffzip *z, uint64 off)
{
	ffzip_file *f = FF_GETPTR(ffzip_file, sib, z->curfile);
	if (f->offset != off)
		return;
	z->inoff = off;
	z->state = R_FHDR_SEEK;
}

ffzip_file* ffzip_nextfile(ffzip *z)
{
	z->curfile = z->curfile->next;
	if (z->curfile == ffchain_sentl(&z->cdir))
		return NULL;
	ffzip_file *f = FF_GETPTR(ffzip_file, sib, z->curfile);
	return f;
}

static void zlog(ffzip *z, uint level, const char *fmt, ...)
{
	if (z->log == NULL)
		return;

	ffstr s = {};
	ffsize cap = 0;
	va_list va;
	va_start(va, fmt);
	ffstr_growfmtv(&s, &cap, fmt, va);
	va_end(va);

	z->log(z->udata, 0, s);
	ffstr_free(&s);
}

/*
. Find CDIR trailer at file end, get CDIR offset (FFZIP_SEEK)
 . If CDIR offset is -1:
  . Read zip64 CDIR locator, get CDIR trailer offset (FFZIP_SEEK)
  . Read zip64 CDIR trailer, get CDIR offset (FFZIP_SEEK)
. Read entries from CDIR (FFZIP_FILEINFO, FFZIP_DONE)

. After ffzip_readfile() has been called by user, seek to local file header (FFZIP_SEEK)
. Read file header (FFZIP_FILEHDR)
. Decompress file (FFZIP_DATA, FFZIP_FILEDONE) */
int ffzip_read(ffzip *z, char *dst, size_t cap)
{
	int r;

	for (;;) {
	switch ((enum E_READ)z->state) {

	case R_GATHER:
		r = ffarr_append_until(&z->buf, z->in.ptr, z->in.len, z->hsize);
		switch (r) {
		case 0:
			z->inoff += z->in.len;
			return FFZIP_MORE;
		case -1:
			return ERR(z, FFZIP_ESYS);
		}
		ffstr_shift(&z->in, r);
		z->inoff += r;
		z->state = z->nxstate;

		zlog(z, 0, "gathered data chunk: [%L] %*Xb @%xU"
			, z->buf.len, ffmin(z->buf.len, 80), z->buf.ptr
			, z->inoff - z->buf.len);
		continue;

	case R_CDIR_TRL_SEEK: {
		uint64 total_size = z->inoff;
		if (total_size < sizeof(struct zip_cdir_trl))
			return ERR(z, FFZIP_ECDIR);

		z->inoff = ffmax(0, (int64)total_size - ZIP_CDIR_TRL_MAXSIZE);
		z->hsize = total_size - z->inoff;
		z->state = R_GATHER, z->nxstate = R_CDIR_TRL;
		return FFZIP_SEEK;
	}

	case R_CDIR_TRL: {
		ffssize pos = ffs_rfindstr(z->buf.ptr, z->buf.len - sizeof(struct zip_cdir_trl) + 4, "PK\5\6", 4);
		if (pos < 0)
			return ERRSTR(z, "no CDIR trailer");
		z->inoff = z->inoff - z->buf.len + pos;

		const zip_cdir_trl *trl = (void*)((char*)z->buf.ptr + pos);
		zlog(z, 0, "CDIR trailer: %*Xb", sizeof(struct zip_cdir_trl), trl);
		z->buf.len = 0;

		uint disknum, cdir_disk, cdir_size, cdir_off;
		disknum = ffint_ltoh16(trl->disknum);
		cdir_disk = ffint_ltoh16(trl->cdir_disk);
		cdir_size = ffint_ltoh32(trl->cdir_size);
		cdir_off = ffint_ltoh32(trl->cdir_off);
		if (disknum != 0 || cdir_disk != 0)
			return ERR(z, FFZIP_EDISK);

		if (cdir_off == (uint)-1) {
			z->inoff -= sizeof(struct zip64_cdir_trl_locator);
			z->hsize = sizeof(struct zip64_cdir_trl_locator);
			z->state = R_GATHER, z->nxstate = R_CDIR64_LOC;
			z->buf.len = 0;
			return FFZIP_SEEK;
		}

		z->cdir_off = cdir_off;
		z->cdir_end = cdir_off + cdir_size;
		z->inoff = z->cdir_off;
		z->state = R_CDIR_NEXT;
		return FFZIP_SEEK;
	}

	case R_CDIR64_LOC: {
		const struct zip64_cdir_trl_locator *loc = (void*)z->buf.ptr;
		if (0 != ffs_cmp(loc->sig, "PK\x06\x07", 4))
			return ERRSTR(z, "bad ZIP64 CDIR trailer locator");

		uint cdir_trl_disk = ffint_ltoh32(loc->cdir_trl_disk);
		uint disks_number = ffint_ltoh32(loc->disks_number);
		uint64 off = ffint_ltoh64(loc->offset);
		if (cdir_trl_disk != 0 || disks_number != 1)
			return ERR(z, FFZIP_EDISK);

		z->inoff = off;
		z->hsize = sizeof(struct zip64_cdir_trl);
		z->state = R_GATHER, z->nxstate = R_CDIR64;
		z->buf.len = 0;
		return FFZIP_SEEK;
	}

	case R_CDIR64: {
		const struct zip64_cdir_trl *trl = (void*)z->buf.ptr;
		if (ffs_cmp(trl->sig, "PK\x06\x06", 4))
			return ERRSTR(z, "bad ZIP64 CDIR trailer");

		uint size = ffint_ltoh32(trl->size);
		uint disk = ffint_ltoh32(trl->disk);
		uint cdir_disk = ffint_ltoh32(trl->cdir_disk);
		uint64 cdir_size = ffint_ltoh64(trl->cdir_size);
		uint64 cdir_offset = ffint_ltoh64(trl->cdir_offset);
		if (size < sizeof(struct zip64_cdir_trl) - 12)
			return ERRSTR(z, "bad size of ZIP64 CDIR trailer");
		if (disk != 0 || cdir_disk != 0)
			return ERR(z, FFZIP_EDISK);

		z->cdir_off = cdir_offset;
		z->cdir_end = cdir_offset + cdir_size;
		z->inoff = z->cdir_off;
		z->state = R_CDIR_NEXT;
		return FFZIP_SEEK;
	}

	case R_CDIR_NEXT:
		if (z->cdir_off == z->cdir_end) {
			z->curfile = ffchain_sentl(&z->cdir);
			return FFZIP_DONE;
		}

		z->hsize = sizeof(zip_cdir);
		z->state = R_GATHER, z->nxstate = R_CDIR;
		z->buf.len = 0;
		continue;

	case R_CDIR: {
		ffzip_file *f;
		if (NULL == (f = ffmem_tcalloc1(ffzip_file)))
			return ERR(z, FFZIP_ESYS);
		if (0 > (r = zip_cdir_parse(f, z->buf.ptr))) {
			ffmem_free(f);
			return ERR(z, -r);
		}
		z->hsize = r;
		ffchain_add(&z->cdir, &f->sib);
		z->state = R_GATHER, z->nxstate = R_CDIR_FIN;
		z->cdir_off += z->hsize;

		if (z->cdir_off > z->cdir_end)
			return ERR(z, FFZIP_ECDIR_SIZE);
		continue;
	}

	case R_CDIR_FIN: {
		const zip_cdir *cdir = (void*)z->buf.ptr;
		uint filenamelen = ffint_ltoh16(cdir->filenamelen);
		ffzip_file *f = FF_GETPTR(ffzip_file, sib, ffchain_last(&z->cdir));

		if (ffutf8_valid(cdir->filename, filenamelen)) {
			if (NULL == (f->fn = ffsz_dupn(cdir->filename, filenamelen)))
				return ERR(z, FFZIP_ESYS);
		} else {
			ffstr s = {};
			ffsize cap = 0;
			if (0 == ffstr_growadd_codepage(&s, &cap, cdir->filename, filenamelen, z->codepage)
				|| 0 == ffstr_growaddchar(&s, &cap, '\0')) {
				ffstr_free(&s);
				return ERR(z, FFZIP_ESYS);
			}
			f->fn = s.ptr;
		}

		ffstr extra;
		ffstr_set2(&extra, &z->buf);
		z->buf.len = 0;
		ffstr_shift(&extra, sizeof(struct zip_cdir) + filenamelen);
		while (extra.len != 0) {
			ffstr s;
			r = zip_cdirextra_next(&extra, &s);
			if (r < 0)
				break;

			zlog(z, 0, "CDIR extra: %xu [%L] %*Xb"
				, r, s.len, s.len, s.ptr);

			switch (r) {
			case 0x0001:
				(void) zip_zip64extra_parse(cdir, &s, f);
				break;
			case 0x000A:
				(void) zip_ntfsextra_parse(&s, f);
				break;
			}
		}

		z->state = R_CDIR_NEXT;
		return FFZIP_FILEINFO;
	}

	case R_FHDR_SEEK:
		z->hsize = sizeof(zip_hdr);
		z->state = R_GATHER, z->nxstate = R_FHDR;
		return FFZIP_SEEK;

	case R_FHDR: {
		ffzip_file f;
		ffzip_file *curf = FF_GETPTR(ffzip_file, sib, z->curfile);
		if (0 > (r = zip_fhdr_parse(&f, z->buf.ptr, curf)))
			return ERR(z, -r);
		if (curf->comp == ZIP_STORED && curf->osize != curf->zsize)
			return ERR(z, FFZIP_EFSIZE);
		z->hsize = r;
		z->state = R_GATHER, z->nxstate = R_FHDR_FIN;
		continue;
	}

	case R_FHDR_FIN: {
		z->buf.len = 0;
		z->outsize = 0;
		z->crc = 0;
		ffzip_file *f = FF_GETPTR(ffzip_file, sib, z->curfile);
		if (f->comp == ZIP_STORED)
		{}
		else if (f->comp == ZIP_DEFLATED) {
			if (!z->lzinit) {
				z_conf conf = {0};
				if (0 != z_inflate_init(&z->lz, &conf))
					return ERR(z, FFZIP_ELZINIT);
				z->lzinit = 1;
			}
		}
		z->state = R_DATA;
		return FFZIP_FILEHDR;
	}

	case R_DATA: {
		size_t rd;
		ffzip_file *f = FF_GETPTR(ffzip_file, sib, z->curfile);
		if (f->comp == ZIP_DEFLATED) {
			rd = z->in.len;
			r = z_inflate(z->lz, z->in.ptr, &rd, dst, cap, 0);

			if (r == Z_DONE) {
				z_inflate_reset(z->lz);
				z->state = R_FDONE;
				continue;

			} else if (r < 0)
				return ERR(z, FFZIP_ELZ);
		} else {
			rd = f->zsize - z->outsize;
			if (rd == 0) {
				z->state = R_FDONE;
				continue;
			}
			rd = ffmin(rd, cap);
			rd = ffmin(rd, z->in.len);
			ffmemcpy(dst, z->in.ptr, rd);
			r = rd;
		}

		ffstr_shift(&z->in, rd);

		if (r == 0)
			return FFZIP_MORE;

		z->crc = crc32((void*)dst, r, z->crc);
		z->outsize += r;
		ffstr_set(&z->out, dst, r);
		return FFZIP_DATA;
	}

	case R_FDONE: {
		ffzip_file *f = FF_GETPTR(ffzip_file, sib, z->curfile);
		if (z->crc != f->crc)
			return ERR(z, FFZIP_ECRC);

		z->state = R_FHDR;
		return FFZIP_FILEDONE;
	}

	}
	}
}


enum { W_NEWFILE, W_FHDR, W_FDATA, W_FTRL, W_FILEDONE, W_CDIR, W_DONE };

void ffzip_wclose(ffzip_cook *z)
{
	ffarr_free(&z->buf);
	ffarr_free(&z->cdir);
	FF_SAFECLOSE(z->lz, NULL, z_deflate_free);
}

int ffzip_winit(ffzip_cook *z, uint level, uint mem)
{
	z_conf conf = {0};
	conf.level = level;
	conf.mem = mem;
	if (0 != z_deflate_init(&z->lz, &conf)) {
		z->err = FFZIP_ELZINIT;
		return -1;
	}
	return 0;
}

int ffzip_wfile(ffzip_cook *z, const char *name, const fftime *mtime, const ffzip_fattr *attrs)
{
	int r;
	if (z->state != W_NEWFILE)
		return ERR(z, FFZIP_ENOTREADY);

	ffstr nm;
	ffstr_setz(&nm, name);
	if (NULL == ffarr_grow(&z->buf, sizeof(zip_hdr) + nm.len + 1, 256))
		return ERR(z, FFZIP_ESYS);

	if (NULL == ffarr_grow(&z->cdir, sizeof(zip_cdir) + nm.len, 256 | FFARR_GROWQUARTER))
		return ERR(z, FFZIP_ESYS);

	if (0 > (r = zip_fhdr_write(z->buf.ptr, ffarr_end(&z->cdir), &nm, mtime, attrs, z->total_out)))
		return ERR(z, -r);
	z->buf.len = r;
	z->cdir_hdrlen = sizeof(zip_cdir) + nm.len;
	z->filedone = 0;
	z->state = W_FHDR;
	return 0;
}

void ffzip_wfinish(ffzip_cook *z)
{
	z->state = W_CDIR;
}

int ffzip_write(ffzip_cook *z, char *dst, size_t cap)
{
	for (;;) {
	switch (z->state) {

	case W_FHDR:
		if (z->in.len == 0) {
			if (!z->filedone)
				return FFZIP_MORE;
			zip_hdr *h = (void*)z->buf.ptr;
			ffint_htol16(h->comp, ZIP_STORED);
			z->state = W_FTRL;
		} else
			z->state = W_FDATA;

		if (z->items == (ushort)-1)
			return ERR(z, FFZIP_EMAXITEMS);

		z->crc = 0;
		z->file_insize = 0;
		z->file_outsize = 0;
		ffstr_set2(&z->out, &z->buf);
		z->buf.len = 0;
		z->total_out += z->out.len;
		return FFZIP_DATA;

	case W_FDATA: {
		size_t rd = z->in.len;
		int r = z_deflate(z->lz, z->in.ptr, &rd, dst, cap, (z->filedone) ? Z_FINISH : 0);

		if (r == Z_DONE) {
			z_deflate_reset(z->lz);
			z->state = W_FTRL;
			continue;

		} else if (r < 0)
			return ERR(z, FFZIP_ELZ);

		z->crc = crc32((void*)z->in.ptr, rd, z->crc);
		ffstr_shift(&z->in, rd);
		z->file_insize += rd;
		z->total_in += rd;

		if (r == 0)
			return FFZIP_MORE;

		ffstr_set(&z->out, dst, r);
		z->file_outsize += r;
		z->total_out += z->out.len;
		return FFZIP_DATA;
	}

	case W_FTRL: {
		ffmemcpy(z->buf.ptr, "PK\x07\x08", 4);
		zip_trl *trl = (void*)(z->buf.ptr + 4);
		ffint_htol32(trl->crc32, z->crc);
		ffint_htol32(trl->size, z->file_outsize);
		ffint_htol32(trl->size_orig, z->file_insize);
		z->buf.len = 4 + sizeof(zip_trl);

		zip_cdir *cdir = (void*)ffarr_end(&z->cdir);
		ffint_htol32(cdir->crc32, z->crc);
		ffint_htol32(cdir->size, z->file_outsize);
		ffint_htol32(cdir->size_orig, z->file_insize);
		z->cdir.len += z->cdir_hdrlen;

		z->items++;
		ffstr_set2(&z->out, &z->buf);
		z->buf.len = 0;
		z->total_out += z->out.len;
		z->state = W_FILEDONE;
		return FFZIP_DATA;
	}

	case W_FILEDONE:
		z->state = W_NEWFILE;
		return FFZIP_FILEDONE;

	case W_CDIR: {
		if (NULL == ffarr_grow(&z->cdir, sizeof(zip_cdir_trl), 0))
			return ERR(z, FFZIP_ESYS);
		zip_cdir_trl *trl = (void*)ffarr_end(&z->cdir);
		ffmem_tzero(trl);
		ffmemcpy(trl, zip_defcdir_trl, sizeof(zip_defcdir_trl));
		ffint_htol16(trl->disk_ents, z->items);
		ffint_htol16(trl->total_ents, z->items);
		ffint_htol32(trl->cdir_size, z->cdir.len);
		ffint_htol32(trl->cdir_off, z->total_out);
		z->cdir.len += sizeof(zip_cdir_trl);
		ffstr_set2(&z->out, &z->cdir);
		z->total_out += z->out.len;
		z->state = W_DONE;
		return FFZIP_DATA;
	}

	case W_DONE:
		return FFZIP_DONE;

	case W_NEWFILE:
		return ERR(z, FFZIP_ENOTREADY);
	}
	}

	return 0;
}
