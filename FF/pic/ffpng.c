/**
Copyright (c) 2016 Simon Zolin
*/

#include <FF/pic/png.h>
#include <FFOS/error.h>


static const char* const errs[] = {
	"unsupported format",
	"incomplete input line",
};

static const char* errmsg(int e, void *p)
{
	switch (e) {
	case FFPNG_ESYS:
		return fferr_strp(fferr_last());

	case 0:
		return png_errstr(p);
	}
	return errs[(uint)e - 1];
}


enum { R_HDR, R_DATA };

const char* ffpng_errstr(ffpng *p)
{
	return errmsg(p->e, p->png);
}

void ffpng_open(ffpng *p)
{
	p->state = R_HDR;
}

void ffpng_close(ffpng *p)
{
	FF_SAFECLOSE(p->png, NULL, png_rfree);
	ffarr_free(&p->buf);
}

int ffpng_read(ffpng *p)
{
	int r;
	size_t len;

	switch (p->state) {
	case R_HDR: {
		struct png_conf conf = {0};
		len = p->data.len;
		conf.total_size = p->info.total_size;
		r = png_open(&p->png, p->data.ptr, &len, &conf);
		if (r == PNG_RMORE)
			return FFPNG_MORE;
		else if (r < 0)
			return FFPNG_ERR;

		ffstr_shift(&p->data, len);
		p->info.width = conf.width;
		p->info.height = conf.height;
		p->info.format = conf.bpp;

		p->linesize = p->info.width * ffpic_bits(p->info.format) / 8;
		if (NULL == ffarr_alloc(&p->buf, p->linesize))
			return p->e = FFPNG_ESYS,  FFPNG_ERR;
		p->state = R_DATA;
		return FFPNG_HDR;
	}

	case R_DATA:
		len = p->data.len;
		r = png_read(p->png, p->data.ptr, &len, p->buf.ptr);
		ffstr_shift(&p->data, len);
		if (r == PNG_RMORE)
			return FFPNG_MORE;
		else if (r == PNG_RDONE)
			return FFPNG_DONE;
		else if (r < 0)
			return FFPNG_ERR;

		ffstr_set(&p->rgb, p->buf.ptr, p->linesize);
		return FFPNG_DATA;
	}
	return 0;
}


const char* ffpng_werrstr(ffpng_cook *p)
{
	return errmsg(p->e, p->png);
}

enum { W_INIT, W_DATA };

int ffpng_create(ffpng_cook *p, ffpic_info *info)
{
	if ((info->format & _FFPIC_BGR)
		|| !(ffpic_bits(info->format) == 24 || ffpic_bits(info->format) == 32)) {
		info->format = ffpic_bits(info->format);
		return FFPNG_EFMT;
	}

	p->info.width = info->width;
	p->info.height = info->height;
	p->info.format = info->format;
	p->state = W_INIT;
	p->info.complevel = 9;
	return 0;
}

void ffpng_wclose(ffpng_cook *p)
{
	FF_SAFECLOSE(p->png, NULL, png_wfree);
}

int ffpng_write(ffpng_cook *p)
{
	int r;

	switch (p->state) {
	case W_INIT: {
		struct png_conf conf = {0};
		conf.width = p->info.width;
		conf.height = p->info.height;
		conf.bpp = ffpic_bits(p->info.format);
		conf.complevel = p->info.complevel;
		conf.comp_bufsize = p->info.comp_bufsize;
		if (0 != png_create(&p->png, &conf))
			return FFPNG_ERR;

		p->linesize = p->info.width * ffpic_bits(p->info.format) / 8;
		p->state = W_DATA;
		// break
	}

	case W_DATA: {
		if (p->rgb.len != p->linesize) {
			if (p->rgb.len != 0)
				return p->e = FFPNG_ELINE,  FFPNG_ERR;
			return FFPNG_MORE;
		}

		const void *ptr;
		r = png_write(p->png, p->rgb.ptr, &ptr);
		if (r == PNG_RMORE)
			return FFPNG_MORE;
		else if (r == PNG_RDONE)
			return FFPNG_DONE;
		else if (r < 0)
			return FFPNG_ERR;

		ffstr_set(&p->data, ptr, r);
		return FFPNG_DATA;
	}
	}

	return FFPNG_ERR;
}
