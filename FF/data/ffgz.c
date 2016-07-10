/**
Copyright (c) 2016 Simon Zolin
*/

#include <FF/data/gz.h>
#include <FF/path.h>
#include <FF/crc.h>
#include <FFOS/error.h>


static const char *const gz_errs[] = {
	"not ready",
	"libz init",
	"bad header",
	"too small size",
	"size mismatch",
	"CRC mismatch",
};

const char* ffgz_errstr(const void *_gz)
{
	const ffgz *gz = _gz;
	switch (gz->err) {
	case FFGZ_ESYS:
		return fferr_strp(fferr_last());

	case FFGZ_ELZ:
		return z_errstr(gz->lz);
	}

	return gz_errs[gz->err - FFGZ_ENOTREADY];
}

#define ERR(gz, n) \
	(gz)->err = n, FFGZ_ERR


enum {
	R_TRLSEEK, R_GATHER, R_TRL, R_HDR, R_EXTRA, R_NAME, R_CMT, R_CRC, R_HDROK,
	R_LZINIT, R_DATA, R_DONE,
};

void ffgz_close(ffgz *gz)
{
	ffarr_free(&gz->buf);
	FF_SAFECLOSE(gz->lz, NULL, z_inflate_free);
}

void ffgz_init(ffgz *gz, int64 total_size)
{
	if (total_size < 0) {
		gz->hsize = sizeof(ffgzheader);
		gz->state = R_GATHER, gz->nxstate = R_HDR;
	} else {
		gz->inoff = total_size;
		gz->state = R_TRLSEEK;
	}
}

/*
. (FFGZ_SEEK) Read trailer
. Read header (FFGZ_INFO)
. Decompress data (FFGZ_DATA)
. Check CRC (FFGZ_DONE) */
int ffgz_read(ffgz *gz, char *dst, size_t cap)
{
	const ffgzheader *h;
	ssize_t r;

	for (;;) {
	switch (gz->state) {

	case R_GATHER:
		r = ffarr_append_until(&gz->buf, gz->in.ptr, gz->in.len, gz->hsize);
		switch (r) {
		case 0:
			gz->inoff += gz->in.len;
			return FFGZ_MORE;
		case -1:
			return ERR(gz, FFGZ_ESYS);
		}
		ffstr_shift(&gz->in, r);
		gz->inoff += r;
		gz->state = gz->nxstate;
		continue;

	case R_TRLSEEK:
		gz->state = R_GATHER, gz->nxstate = R_TRL;
		gz->hsize = sizeof(ffgztrailer);
		gz->inoff = gz->inoff - sizeof(ffgztrailer);
		if ((int64)gz->inoff < 0)
			return ERR(gz, FFGZ_ESMALLSIZE);
		return FFGZ_SEEK;

	case R_TRL: {
		const ffgztrailer *trl = (void*)gz->buf.ptr;
		gz->outsize = ffint_ltoh32(trl->isize);

		gz->buf.len = 0;
		gz->inoff = 0;
		gz->hsize = sizeof(ffgzheader);
		gz->state = R_GATHER, gz->nxstate = R_HDR;
		return FFGZ_SEEK;
	}

	case R_HDR:
		h = (void*)gz->buf.ptr;
		if (!(h->id1 == 0x1f && h->id2 == 0x8b && h->comp_meth == 8))
			return ERR(gz, FFGZ_EHDR);
		if (h->fextra) {
			gz->hsize += 2;
			gz->state = R_GATHER, gz->nxstate = R_EXTRA;
			continue;
		}
		gz->state = R_NAME;
		continue;

	case R_EXTRA:
		h = (void*)gz->buf.ptr;
		gz->hsize += ffint_ltoh16((char*)h + sizeof(ffgzheader));
		gz->state = R_GATHER, gz->nxstate = R_NAME;
		continue;

	case R_NAME:
		h = (void*)gz->buf.ptr;
		if (h->fname) {
			size_t len = ffsz_nlen(gz->in.ptr, gz->in.len);
			if (len == gz->in.len) {
				gz->hsize += len;
				gz->state = R_GATHER, gz->nxstate = R_NAME;
				continue;
			}

			gz->nameoff = gz->buf.len;
			if (NULL == ffarr_append(&gz->buf, gz->in.ptr, len + 1))
				return ERR(gz, FFGZ_ESYS);
			ffstr_shift(&gz->in, len + 1);
			gz->inoff += len + 1;
			gz->hsize += len + 1;
		}
		gz->state = R_CMT;
		// break

	case R_CMT:
		h = (void*)gz->buf.ptr;
		if (h->fcomment) {
			size_t len = ffsz_nlen(gz->in.ptr, gz->in.len);
			if (len == gz->in.len) {
				gz->hsize += len;
				gz->state = R_GATHER, gz->nxstate = R_CMT;
				continue;
			}

			if (NULL == ffarr_append(&gz->buf, gz->in.ptr, len + 1))
				return ERR(gz, FFGZ_ESYS);
			ffstr_shift(&gz->in, len + 1);
			gz->inoff += len + 1;
			gz->hsize += len + 1;
		}
		gz->state = R_CRC;
		// break

	case R_CRC:
		h = (void*)gz->buf.ptr;
		if (h->fhcrc) {
			gz->hsize += 2;
			gz->state = R_GATHER, gz->nxstate = R_HDROK;
			continue;
		}
		// break

	case R_HDROK:
		gz->state = R_LZINIT;
		return FFGZ_INFO;

	case R_LZINIT: {
		z_conf conf = {0};
		if (0 != z_inflate_init(&gz->lz, &conf))
			return ERR(gz, FFGZ_ELZINIT);
		gz->buf.len = 0;
		gz->crc = 0;
		gz->outsize = 0;
		gz->state = R_DATA;
		// break
	}

	case R_DATA: {
		size_t rd = gz->in.len;
		r = z_inflate(gz->lz, gz->in.ptr, &rd, dst, cap, 0);

		if (r == Z_DONE) {
			gz->hsize = sizeof(ffgztrailer);
			gz->state = R_GATHER, gz->nxstate = R_DONE;
			continue;

		} else if (r < 0)
			return ERR(gz, FFGZ_ELZ);

		ffstr_shift(&gz->in, rd);
		gz->inoff += rd;

		if (r == 0)
			return FFGZ_MORE;

		ffstr_set(&gz->out, dst, r);
		gz->crc = crc32((void*)dst, r, gz->crc);
		gz->outsize += gz->out.len;
		return FFGZ_DATA;
	}

	case R_DONE: {
		const ffgztrailer *trl = (void*)gz->buf.ptr;
		if (ffint_ltoh32(trl->crc32) != gz->crc)
			return ERR(gz, FFGZ_ECRC);
		// if (ffint_ltoh32(trl->isize) != (uint)gz->outsize)
		// 	return ERR(gz, FFGZ_ESIZE);
		return FFGZ_DONE;
	}

	}
	}
}


enum { W_FIRST, W_HDR, W_DATA, W_TRL, W_DONE };

void ffgz_wclose(ffgz_cook *gz)
{
	ffarr2_free(&gz->buf);
	FF_SAFECLOSE(gz->lz, NULL, z_deflate_free);
}

int ffgz_winit(ffgz_cook *gz, uint level, uint mem)
{
	z_conf conf = {0};
	conf.level = level;
	conf.mem = mem;
	if (0 != z_deflate_init(&gz->lz, &conf))
		return ERR(gz, FFGZ_ELZINIT);
	return 0;
}

static const byte gz_defhdr[] = { 0x1f, 0x8b, 8, 0, 0,0,0,0, 0, 255 };

int ffgz_wfile(ffgz_cook *gz, const char *name, uint mtime)
{
	ffstr nm = {0};

	if (gz->state != W_FIRST)
		return ERR(gz, FFGZ_ENOTREADY);

	if (name != NULL)
		ffpath_split2(name, ffsz_len(name), NULL, &nm);
	if (NULL == ffarr2_alloc(&gz->buf, 1, sizeof(ffgzheader) + nm.len + 1))
		return ERR(gz, FFGZ_ESYS);

	ffarr2_addf(&gz->buf, &gz_defhdr, sizeof(gz_defhdr), sizeof(char));
	ffgzheader *h = (void*)gz->buf.ptr;
	ffint_htol32(h->mtime, mtime);

	if (name != NULL) {
		h->fname = 1;
		ffarr2_addf(&gz->buf, nm.ptr, nm.len + 1, sizeof(char));
	}

	gz->state = W_HDR;
	return 0;
}

int ffgz_write(ffgz_cook *gz, char *dst, size_t cap)
{
	int r;

	for (;;) {
	switch (gz->state) {

	case W_HDR:
		gz->crc = 0;
		ffstr_set(&gz->out, gz->buf.ptr, gz->buf.len);
		gz->outsize += gz->out.len;
		gz->state = W_DATA;
		return FFGZ_DATA;

	case W_DATA: {
		uint f = gz->flush;
		if (gz->flush == Z_SYNC_FLUSH)
			gz->flush = 0;
		size_t rd = gz->in.len;
		r = z_deflate(gz->lz, gz->in.ptr, &rd, dst, cap, f);

		if (r == Z_DONE) {
			gz->state = W_TRL;
			continue;

		} else if (r < 0)
			return ERR(gz, FFGZ_ELZ);

		gz->crc = crc32((void*)gz->in.ptr, rd, gz->crc);
		ffstr_shift(&gz->in, rd);
		gz->insize += rd;

		if (r == 0)
			return FFGZ_MORE;

		ffstr_set(&gz->out, dst, r);
		gz->outsize += r;
		return FFGZ_DATA;
	}

	case W_TRL: {
		ffgztrailer *trl = gz->buf.ptr;
		ffint_htol32(trl->crc32, gz->crc);
		ffint_htol32(trl->isize, (uint)gz->insize);
		ffstr_set(&gz->out, gz->buf.ptr, sizeof(ffgztrailer));
		gz->outsize += gz->out.len;
		gz->state = W_DONE;
		return FFGZ_DATA;
	}

	case W_DONE:
		return FFGZ_DONE;

	case W_FIRST:
		return ERR(gz, FFGZ_ENOTREADY);
	}
	}
	//unreachable
}
