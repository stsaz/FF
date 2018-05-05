/**
Copyright (c) 2018 Simon Zolin
*/

#include <FF/pic/png.h>


enum PNG_R {
	R_INIT, R_GATHER,
	R_SIGN, R_IHDR_HDR, R_IHDR,
	R_ERR,
};

int ffpngr_open(struct ffpngr *p)
{
	return 0;
}

void ffpngr_close(struct ffpngr *p)
{
	ffarr_free(&p->buf);
}

static const byte png_sign[] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A };

struct png_chunk {
	byte datalen[4];
	byte type[4];
	byte data[0];
	// byte crc[4];
};


enum PNG_CLR {
	PLT = 1,
	CLR = 2,
	ALPHA = 4,
};
struct png_ihdr {
	byte width[4];
	byte height[4];
	byte bit_depth;
	byte color; //bitmask enum PNG_CLR
	byte unused[3];
};

/** Read IHDR data. */
static int png_ihdr(struct ffpngr *p, const ffstr *d)
{
	const struct png_ihdr *ihdr = (void*)d->ptr;
	p->info.width = ffint_ntoh32(ihdr->width);
	p->info.height = ffint_ntoh32(ihdr->height);
	switch (ihdr->color) {
	case CLR:
		p->info.bpp = 24;
		break;
	case CLR | ALPHA:
		p->info.bpp = 32;
		break;
	default:
		p->info.bpp = 8;
	}
	return 0;
}

static void GATHER(struct ffpngr *p, uint nxstate, size_t len)
{
	p->state = R_GATHER,  p->nxstate = nxstate;
	p->gathlen = len;
	p->buf.len = 0;
}

int ffpngr_read(struct ffpngr *p)
{
	int r;

	for (;;) {
	switch ((enum PNG_R)p->state) {

	case R_INIT:
		GATHER(p, R_SIGN, sizeof(png_sign));
		continue;

	case R_GATHER:
		r = ffarr_append_until(&p->buf, p->input.ptr, p->input.len, p->gathlen);
		if (r == 0) {
			p->input.len = 0;
			return FFPNG_MORE;
		} else if (r == -1)
			return FFPNG_ERR;
		ffstr_set2(&p->chunk, &p->buf);
		ffarr_shift(&p->input, r);
		p->state = p->nxstate;
		continue;

	case R_SIGN:
		if (!!memcmp(p->chunk.ptr, png_sign, p->chunk.len))
			return FFPNG_ERR;
		GATHER(p, R_IHDR_HDR, sizeof(struct png_chunk));
		continue;

	case R_IHDR_HDR: {
		const struct png_chunk *d = (void*)p->chunk.ptr;
		uint len = ffint_ntoh32(d->datalen);
		if (!!memcmp(d->type, "IHDR", 4)
			|| len < sizeof(struct png_ihdr))
			return FFPNG_ERR;
		GATHER(p, R_IHDR, len);
		continue;
	}

	case R_IHDR:
		png_ihdr(p, &p->chunk);
		p->state = R_ERR;
		return FFPNG_HDR;

	case R_ERR:
		return FFPNG_ERR;
	}
	}
}
