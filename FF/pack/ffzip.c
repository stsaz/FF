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
	// char sig[4]; //"PK\7\8"
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
	// char extra[0];
	// char comment[0];
} zip_cdir;

typedef struct zip_cdir_trl {
	char sig[4]; //"PK\5\6"
	byte disknum[2];
	byte cdir_disk[2];
	byte disk_ents[2];
	byte total_ents[2];
	byte cdir_size[4]; //size of zip_cdir[]
	byte cdir_off[4];
	byte commentlen[2];
	byte comment[0];
} zip_cdir_trl;

enum {
	ZIP_MINVER = 20,
	ZIP_CDIR_TRL_MAXSIZE = 4 * 1024,
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

/** Find CDIR trailer and store it in 'buf'.
Return 1 if found;  0 if more data is needed;  -1 on error. */
static int zip_cdir_trl_find(ffarr *buf, ffstr *in)
{
	int r;
	struct ffbuf_gather bg = {0};
	ffstr_set2(&bg.data, in);
	bg.ctglen = sizeof(zip_cdir_trl);
	while (FFBUF_DONE != (r = ffbuf_gather(buf, &bg))) {
		switch (r) {
		case 0:
		case -1:
			return r;

		case FFBUF_READY: {
			char *p;
			if (ffarr_end(buf) != (p = ffs_finds(buf->ptr, buf->len, (char*)zip_defcdir_trl, 4)))
				bg.off = p - buf->ptr + 1;
			break;
		}
		}
	}

	return 1;
}

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


static const char *const zip_errs[] = {
	"not ready",
	"libz init",
	"reached maximum number of items",
	"invalid filename",
	"multi-disk archives are not supported",
	"bad CDIR record",
	"bad local file header",
	"unsupported flags",
	"unsupported compression method",
	"info from CDIR and local file header don't match",
	"CRC mismatch",
	"invalid CDIR size",
	"size mismatch for STORED file",
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
	R_GATHER, R_CDIR_TRL_SEEK, R_CDIR_TRL, R_CDIR_SEEK, R_CDIR_NEXT, R_CDIR, R_CDIR_FIN,
	R_FHDR_SEEK, R_FHDR, R_FHDR_FIN, R_DATA, R_FDONE,
};

void ffzip_init(ffzip *z, uint64 total_size)
{
	z->inoff = total_size;
	z->state = R_CDIR_TRL_SEEK;
	ffchain_init(&z->cdir);
}

void ffzip_readfile(ffzip *z, uint off)
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

/*
. Find CDIR trailer, get CDIR offset (FFZIP_SEEK)
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
			return FFZIP_MORE;
		case -1:
			return ERR(z, FFZIP_ESYS);
		}
		ffstr_shift(&z->in, r);
		z->inoff += r;
		z->state = z->nxstate;
		continue;

	case R_CDIR_TRL_SEEK:
		z->inoff = ffmax(0, (int64)z->inoff - ZIP_CDIR_TRL_MAXSIZE);
		z->hsize = sizeof(zip_cdir_trl);
		z->state = R_CDIR_TRL;
		return FFZIP_SEEK;

	case R_CDIR_TRL: {
		r = zip_cdir_trl_find(&z->buf, &z->in);
		if (r == 0)
			return FFZIP_MORE;
		else if (r < 0)
			return ERR(z, FFZIP_ESYS);

		const zip_cdir_trl *trl = (void*)z->buf.ptr;
		z->buf.len = 0;

		uint disknum, cdir_disk, cdir_size, cdir_off;
		disknum = ffint_ltoh16(trl->disknum);
		cdir_disk = ffint_ltoh16(trl->cdir_disk);
		cdir_size = ffint_ltoh32(trl->cdir_size);
		cdir_off = ffint_ltoh32(trl->cdir_off);
		if (disknum != 0 || cdir_disk != 0)
			return ERR(z, FFZIP_EDISK);

		z->cdir_off = cdir_off;
		z->cdir_end = cdir_off + cdir_size;
		z->state = R_CDIR_SEEK;
		// break
	}

	case R_CDIR_SEEK:
		z->inoff = z->cdir_off;
		z->state = R_CDIR_NEXT;
		return FFZIP_SEEK;

	case R_CDIR_NEXT:
		if (z->cdir_off == z->cdir_end) {
			z->curfile = ffchain_sentl(&z->cdir);
			return FFZIP_DONE;
		}

		z->hsize = sizeof(zip_cdir);
		z->state = R_GATHER, z->nxstate = R_CDIR;
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
		z->buf.len = 0;
		const zip_cdir *cdir = (void*)z->buf.ptr;
		uint filenamelen = ffint_ltoh16(cdir->filenamelen);
		ffzip_file *f = FF_GETPTR(ffzip_file, sib, ffchain_last(&z->cdir));
		if (NULL == (f->fn = ffsz_alcopy(cdir->filename, filenamelen)))
			return ERR(z, FFZIP_ESYS);

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
