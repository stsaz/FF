/**
Copyright (c) 2016 Simon Zolin
*/

#include <FF/pic/bmp.h>
#include <FF/pic/pic.h>
#include <FFOS/error.h>


struct bmp_hdr {
	byte bm[2]; //"BM"
	byte filesize[4];
	byte reserved[4];
	byte headersize[4];
	byte infosize[4];
	byte width[4];
	byte height[4];
	byte planes[2];
	byte bpp[2];

	byte compression[4];
	byte sizeimage[4];
	byte xscale[4];
	byte yscale[4];
	byte colors[4];
	byte clrimportant[4];
};


static const char* const errs[] = {
	"unsupported format",
	"incomplete input line",
};

const char* ffbmp_errstr(void *_b)
{
	ffbmp *b = _b;
	switch (b->e) {
	case FFBMP_ESYS:
		return fferr_strp(fferr_last());
	}
	return errs[(uint)b->e - 1];
}

#define ERR(b, r) \
	(b)->e = (r),  FFBMP_ERR


enum { R_GATHER, R_HDR, R_SEEK, R_DATA, R_DONE };

void ffbmp_open(ffbmp *b)
{
	b->state = R_GATHER,  b->nxstate = R_HDR;
	b->gather_size = sizeof(struct bmp_hdr);
}

void ffbmp_close(ffbmp *b)
{
	ffarr_free(&b->inbuf);
}

void ffbmp_region(const ffbmp_pos *pos)
{

}

int ffbmp_read(ffbmp *b)
{
	ssize_t r;

	for (;;) {
	switch (b->state) {

	case R_GATHER:
		r = ffarr_append_until(&b->inbuf, b->data.ptr, b->data.len, b->gather_size);
		switch (r) {
		case 0:
			return FFBMP_MORE;
		case -1:
			return ERR(b, FFBMP_ESYS);
		}
		ffstr_shift(&b->data, r);
		b->inbuf.len = 0;
		b->state = b->nxstate;
		continue;

	case R_HDR: {
		const struct bmp_hdr *h = (void*)b->inbuf.ptr;
		if (!!memcmp(h->bm, "BM", 2))
			return ERR(b, FFBMP_EFMT);
		b->info.width = ffint_ltoh32(h->width);
		b->info.height = ffint_ltoh32(h->height);

		uint bpp = ffint_ltoh16(h->bpp);
		if (bpp != 24 && bpp != 32)
			return ERR(b, FFBMP_EFMT);
		b->info.format = bpp | _FFPIC_BGR;

		uint hdrsize = ffint_ltoh32(h->headersize);
		b->state = R_SEEK;
		b->linesize_o = b->info.width * bpp / 8;
		b->linesize = ff_align_ceil2(b->linesize_o, 4);
		b->dataoff = hdrsize;
		return FFBMP_HDR;
	}

	case R_SEEK:
		b->seekoff = b->dataoff + (b->info.height - b->line - 1) * b->linesize;
		b->state = R_GATHER,  b->nxstate = R_DATA;
		b->gather_size = b->linesize;
		return FFBMP_SEEK;

	case R_DATA:
		ffstr_set(&b->rgb, b->inbuf.ptr, b->linesize_o);

		b->state = R_SEEK;
		if (++b->line == b->info.height)
			b->state = R_DONE;
		return FFBMP_DATA;

	case R_DONE:
		return FFBMP_DONE;
	}
	}
}


enum { W_HDR, W_MORE, W_SEEK, W_DATA, W_PAD };

int ffbmp_create(ffbmp_cook *b, ffpic_info *info)
{
	if (info->format != FFPIC_BGR && info->format != FFPIC_BGRA) {
		info->format = ffpic_bits(info->format) | _FFPIC_BGR;
		return FFBMP_EFMT;
	}

	b->info = *info;
	b->state = W_HDR;
	return 0;
}

void ffbmp_wclose(ffbmp_cook *b)
{
	ffarr_free(&b->buf);
}

/** Write .bmp header. */
static void bmp_hdr_write(ffbmp_cook *b, void *dst)
{
	struct bmp_hdr *h = dst;
	ffmem_tzero(h);
	ffmemcpy(h->bm, "BM", 2);
	ffint_htol32(h->width, b->info.width);
	ffint_htol32(h->height, b->info.height);
	ffint_htol32(h->bpp, ffpic_bits(b->info.format));
	ffint_htol16(h->planes, 1);
	ffint_htol32(h->infosize, 40);
	ffint_htol32(h->headersize, sizeof(struct bmp_hdr));
	ffint_htol32(h->sizeimage, b->info.height * b->linesize);
	ffint_htol32(h->filesize, sizeof(struct bmp_hdr) + b->info.height * b->linesize);
}

int ffbmp_write(ffbmp_cook *b)
{
	for (;;) {
	switch (b->state) {

	case W_HDR: {
		uint bpp = ffpic_bits(b->info.format);
		b->linesize = ff_align_ceil2(b->info.width * bpp / 8, 4);
		b->linesize_o = b->info.width * bpp / 8;

		if (NULL == ffarr_alloc(&b->buf, sizeof(struct bmp_hdr)))
			return ERR(b, FFBMP_ESYS);

		bmp_hdr_write(b, (void*)b->buf.ptr);
		ffstr_set(&b->data, b->buf.ptr, sizeof(struct bmp_hdr));
		b->state = W_SEEK;
		return FFBMP_DATA;
	}

	case W_SEEK:
		b->state = W_DATA;
		b->seekoff = sizeof(struct bmp_hdr) + (b->info.height - b->line - 1) * b->linesize;
		return FFBMP_SEEK;

	case W_DATA:
		if (b->rgb.len < b->linesize_o) {
			if (b->rgb.len != 0)
				return ERR(b, FFBMP_ELINE);
			return FFBMP_MORE;
		}

		ffstr_set(&b->data, b->rgb.ptr, b->linesize_o);
		ffstr_shift(&b->rgb, b->linesize_o);
		b->state = W_PAD;
		return FFBMP_DATA;

	case W_PAD:
		if (b->linesize == b->linesize_o) {
			b->state = W_MORE;
			break;
		}
		ffmem_zero(b->buf.ptr, b->linesize - b->linesize_o);
		ffstr_set(&b->data, b->buf.ptr, b->linesize - b->linesize_o);
		b->state = W_MORE;
		return FFBMP_DATA;

	case W_MORE:
		if (++b->line == b->info.height)
			return FFBMP_DONE;
		b->state = W_SEEK;
		break;
	}
	}
}
