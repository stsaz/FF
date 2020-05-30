/**
Copyright (c) 2018 Simon Zolin
*/

#include <FF/pic/ico.h>
#include <FF/number.h>
#include <FF/pic/pic.h>


struct ico_hdr {
	byte res[2];
	byte type[2];
	byte num[2];
};

struct ico_ent {
	byte width; //0: 256
	byte height;
	byte colors;
	byte res;
	byte unused[2];
	byte bpp[2];
	byte size[4];
	byte file_offset[4];
};

int ffico_open(ffico *c)
{
	return 0;
}

void ffico_close(ffico *c)
{
	ffslice_free(&c->ents);
	ffarr_free(&c->buf);
}

enum ICO_R {
	R_INIT, R_GATHER,
	R_HDR, R_ENTS, R_SEEK, R_FMT, R_DATA_CACHED, R_DATA,
	R_ERR,
};

static void GATHER(ffico *c, uint nxstate, size_t len)
{
	c->state = R_GATHER,  c->nxstate = nxstate;
	c->gathlen = len;
	c->buf.len = 0;
}

/** Fill in ffico_file object. */
static void f_info(struct ffico_file *f, const struct ico_ent *e)
{
	f->width = (e->width != 0) ? e->width : 256;
	f->height = (e->height != 0) ? e->height : 256;
	f->format = ffint_ltoh16(e->bpp);
	switch (f->format) {
	case 24:
	case 32:
		f->format |= _FFPIC_BGR;
		break;
	}
	f->size = ffint_ltoh32(e->size);
	f->offset = ffint_ltoh32(e->file_offset);
}

static const byte png_sign[] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A };

/** Detect file format. */
static int ico_filefmt(ffico *c, const ffstr *d)
{
	if (ffstr_eq(d, png_sign, sizeof(png_sign)))
		return FFICO_PNG;

	if (ffint_ltoh32(d->ptr + 4) == c->info.width) {
		c->bmphdr_size = ffint_ltoh32(d->ptr);
		return FFICO_BMP;
	}

	return FFICO_UKN;
}

/* .ico reading algorithm:
. Read header (FFICO_FILEINFO)
  User calls ffico_fileinfo() to get file info.
. Finish reading header (FFICO_HDR)

. After ffico_readfile() has been called by user, seek to file data (FFICO_SEEK)
. Read file header and detect format (FFICO_FILEFORMAT)
. Read file data (FFICO_DATA, FFICO_FILEDONE)
*/
int ffico_read(ffico *c)
{
	int r;

	for (;;) {
	switch ((enum ICO_R)c->state) {

	case R_INIT:
		GATHER(c, R_HDR, sizeof(struct ico_hdr));
		continue;

	case R_GATHER:
		r = ffarr_append_until(&c->buf, c->input.ptr, c->input.len, c->gathlen);
		if (r == 0) {
			c->input.len = 0;
			return FFICO_MORE;
		} else if (r == -1)
			return c->err = __LINE__,  FFICO_ERR;
		ffstr_set2(&c->chunk, &c->buf);
		ffarr_shift(&c->input, r);
		c->state = c->nxstate;
		continue;

	case R_HDR: {
		const struct ico_hdr *h = (void*)c->chunk.ptr;
		uint t = ffint_ltoh16(h->type);
		if (t != 1)
			return c->err = __LINE__,  FFICO_ERR;
		c->icons_total = ffint_ltoh16(h->num);
		if (c->icons_total == 0)
			return FFICO_HDR;
		GATHER(c, R_ENTS, c->icons_total * sizeof(struct ico_ent));
		continue;
	}

	case R_ENTS: {
		if (c->chunk.len == 0) {
			c->state = R_ERR;
			return FFICO_HDR;
		}

		if (c->ents.len == 0) {
			if (NULL == ffstr_alcopystr((ffstr*)&c->ents, &c->chunk))
				return c->err = __LINE__,  FFICO_ERR;
			c->ents.len = c->chunk.len / sizeof(struct ico_ent);
		}

		f_info(&c->info, (void*)c->chunk.ptr);
		ffstr_shift(&c->chunk, sizeof(struct ico_ent));
		return FFICO_FILEINFO;
	}

	case R_SEEK:
		if (c->size <= 8)
			return c->err = __LINE__,  FFICO_ERR;
		GATHER(c, R_FMT, 8);
		return FFICO_SEEK;

	case R_FMT:
		c->filefmt = ico_filefmt(c, &c->chunk);
		c->state = R_DATA_CACHED;
		return FFICO_FILEFORMAT;

	case R_DATA_CACHED:
		c->out = c->chunk;
		c->size -= c->chunk.len;
		c->off += c->chunk.len;
		c->state = R_DATA;
		return FFICO_DATA;

	case R_DATA: {
		if (c->size == 0) {
			c->state = R_ERR;
			return FFICO_FILEDONE;
		}
		size_t n = ffmin(c->input.len, c->size);
		c->size -= n;
		c->off += n;
		ffstr_set(&c->out, c->input.ptr, n);
		return FFICO_DATA;
	}

	case R_ERR:
		return c->err = __LINE__,  FFICO_ERR;

	}
	}
}

void ffico_readfile(ffico *c, uint idx)
{
	if (idx >= c->ents.len) {
		c->state = R_ERR;
		return;
	}

	const struct ico_ent *e = ffarr_itemT(&c->ents, idx, struct ico_ent);
	f_info(&c->info, e);

	c->off = ffint_ltoh32(e->file_offset);
	c->size = ffint_ltoh32(e->size);
	c->state = R_SEEK;
}
