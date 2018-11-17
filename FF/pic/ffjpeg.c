/**
Copyright (c) 2016 Simon Zolin
*/

#include <FF/pic/jpeg.h>
#include <FFOS/error.h>


static const char* const errs[] = {
	"unsupported format",
	"incomplete input line",
};

static const char* errmsg(int e, void *p)
{
	switch (e) {
	case FFJPEG_ESYS:
		return fferr_strp(fferr_last());

	case 0:
		return jpeg_errstr(p);
	}
	return errs[(uint)e - 1];
}


const char* ffjpeg_errstr(ffjpeg *j)
{
	return errmsg(j->e, j->jpeg);
}

enum { R_INIT, R_DATA };

void ffjpeg_open(ffjpeg *j)
{
	j->state = R_INIT;
}

void ffjpeg_close(ffjpeg *j)
{
	FF_SAFECLOSE(j->jpeg, NULL, jpeg_free);
	ffarr_free(&j->buf);
}

int ffjpeg_read(ffjpeg *j)
{
	int r;
	size_t len;

	for (;;) {
	switch (j->state) {

	case R_INIT: {
		struct jpeg_conf conf = {0};
		len = j->data.len;
		r = jpeg_open(&j->jpeg, j->data.ptr, &len, &conf);
		if (r == JPEG_RMORE)
			return FFJPEG_MORE;
		else if (r < 0)
			return FFJPEG_ERR;

		ffstr_shift(&j->data, len);
		j->info.width = conf.width;
		j->info.height = conf.height;
		j->info.format = 24;

		j->linesize = j->info.width * j->info.format / 8;
		if (NULL == ffarr_realloc(&j->buf, j->linesize))
			return j->e = FFJPEG_ESYS,  FFJPEG_ERR;

		j->state = R_DATA;
		return FFJPEG_HDR;
	}

	case R_DATA:
		len = j->data.len;
		r = jpeg_read(j->jpeg, j->data.ptr, &len, j->buf.ptr);
		ffstr_shift(&j->data, len);
		if (r == JPEG_RMORE)
			return FFJPEG_MORE;
		else if (r == JPEG_RDONE)
			return FFJPEG_DONE;
		else if (r < 0)
			return FFJPEG_ERR;

		ffstr_set(&j->rgb, j->buf.ptr, j->linesize);
		j->line++;
		return FFJPEG_DATA;
	}
	}
}


const char* ffjpeg_werrstr(ffjpeg_cook *j)
{
	return errmsg(j->e, j->jpeg);
}

enum { W_INIT, W_DATA };

int ffjpeg_create(ffjpeg_cook *j, ffpic_info *info)
{
	if (info->format != 24) {
		info->format = 24;
		return FFJPEG_EFMT;
	}
	j->state = W_INIT;
	j->info.width = info->width;
	j->info.height = info->height;
	j->info.format = info->format;
	j->info.quality = 85;
	j->info.bufcap = 16 * 1024; //must be larger than 8k (or libjpeg may return JERR_CANT_SUSPEND)
	return 0;
}

void ffjpeg_wclose(ffjpeg_cook *j)
{
	FF_SAFECLOSE(j->jpeg, NULL, jpeg_wfree);
	ffarr_free(&j->buf);
}

int ffjpeg_write(ffjpeg_cook *j)
{
	int r;

	switch (j->state) {
	case W_INIT:
		if (NULL == ffarr_alloc(&j->buf, j->info.bufcap))
			return j->e = FFJPEG_ESYS,  FFJPEG_ERR;

		struct jpeg_conf conf = {0};
		conf.width = j->info.width;
		conf.height = j->info.height;
		conf.quality = j->info.quality;
		conf.buf_size = j->buf.cap;
		if (0 != jpeg_create(&j->jpeg, &conf))
			return FFJPEG_ERR;

		j->linesize = j->info.width * j->info.format / 8;
		j->state = W_DATA;
		// break

	case W_DATA:
		if (j->rgb.len != j->linesize) {
			if (j->rgb.len != 0)
				return j->e = FFJPEG_ELINE,  FFJPEG_ERR;
			return FFJPEG_MORE;
		}

		r = jpeg_write(j->jpeg, j->rgb.ptr, j->buf.ptr);
		if (r == JPEG_RMORE)
			return FFJPEG_MORE;
		else if (r == JPEG_RDONE)
			return FFJPEG_DONE;
		else if (r < 0)
			return FFJPEG_ERR;

		ffstr_set(&j->data, j->buf.ptr, r);
		return FFJPEG_DATA;
	}

	return FFJPEG_ERR;
}
