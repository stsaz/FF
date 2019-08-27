/**
Copyright (c) 2019 Simon Zolin
*/

#include <FF/aformat/flac.h>


const char* ffflac_ogg_errstr(ffflac_ogg *f)
{
	return "";
}


struct flacogg_hdr {
	byte type; // =0x7f
	char head[4]; // ="FLAC"
	byte ver[2]; // =1.0
	byte hdr_packets[2]; // 0:unknown
	char sync[4]; // ="fLaC"
	struct flac_hdr metahdr;
	struct flac_streaminfo info;
};


enum {
	R_HDR,
	R_META,
	R_DATA,
};

int ffflac_ogg_open(ffflac_ogg *f)
{
	return 0;
}

void ffflac_ogg_close(ffflac_ogg *f)
{
}

#define ERR(f, e) \
	(f)->err = e,  FFFLAC_RERR

/*
. parse header (FFFLAC_RHDR)
. parse meta blocks (FFFLAC_RTAG, FFFLAC_RHDRFIN)
. return audio frames (FFFLAC_RDATA...)
No data gathering is required, because of OGG.
*/
int ffflac_ogg_read(ffflac_ogg *f)
{
	int r;

	for (;;) {
	switch (f->st) {

	case R_HDR: {
		if (f->in.len == 0)
			return FFFLAC_RMORE;
		if (f->in.len != sizeof(struct flacogg_hdr))
			return ERR(f, FLAC_EHDR);

		const struct flacogg_hdr *h = (void*)f->in.ptr;

		if (!!memcmp(h, "\x7f""FLAC""\x01\x00", 7))
			return ERR(f, FLAC_EHDR);

		if (!!memcmp(h->sync, FLAC_SYNC, 4))
			return ERR(f, FLAC_ESYNC);

		uint islast;
		r = flac_info(h->sync, f->in.len - FFOFF(struct flacogg_hdr, sync), &f->info, &f->fmt, &islast);
		if (r <= 0)
			return ERR(f, FLAC_EHDR);

		f->in.len = 0;
		f->st = R_META;
		return FFFLAC_RHDR;
	}

	case R_META:
		if (f->in.len == 0)
			return FFFLAC_RMORE;

		if ((byte)f->in.ptr[0] == 0xff) {
			f->st = R_DATA;
			return FFFLAC_RHDRFIN;
		}

		FFDBG_PRINTLN(10, "meta block type:%xu", (uint)(byte)f->in.ptr[0]);
		f->meta_type = (byte)f->in.ptr[0];
		f->in.len = 0;
		return FFFLAC_RTAG;

	case R_DATA:
		if (f->in.len == 0)
			return FFFLAC_RMORE;
		f->out = f->in;
		f->in.len = 0;
		return FFFLAC_RDATA;
	}
	}
}
