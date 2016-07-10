/**
Copyright (c) 2016 Simon Zolin
*/

#include <FF/data/xz.h>
#include <FF/number.h>
#include <FF/crc.h>


enum CHK {
	CHK_NONE,
	CHK_CRC32,
	CHK_CRC64 = 4,
	CHK_SHA256 = 0x0A,
};

struct stm_hdr {
	byte magic[6]; //"\xFD" "7zXZ" "\0"
	byte flags1; //=0
	byte flags2; //bit 0..3: enum CHK;  bit 4..7: =0
	byte crc32[4]; //CRC of flags[]
};

struct stm_ftr {
	byte crc32[4]; //CRC of index_size and flags
	byte index_size[4]; //real_size = (size + 1) * 4
	byte flags[2]; //=stm_hdr.flags
	byte magic[2]; //"YZ"
};

struct blk_hdr {
	byte size; //=1..255. real_size = (size + 1) * 4
	union {
	byte flags;
	struct {
	byte nfilt :2,
		res :4, //=0
		have_size :1,
		have_osize :1;
	};
	};
	// varint size
	// varint osize
	// filt_flags filt_flags_list[]
	// byte padding[0..3] //=0
	// byte crc32[4]
};

enum FILT {
	FILT_LZMA2 = 0x21,
};

struct filt_flags {
	byte id[1]; //varint, enum FILT
	// varint props_size
	// byte props[0]
};

struct lzma2_filt_flags {
	byte id[1];
	byte props_size[1]; //=1
	byte props[1]; //bit 0..5: dict size
};

struct idx {
	byte indicator; //=0
	// varint nrec
	// struct {
	// 	varint size; //without padding
	// 	varint osize;
	// } recs[];
	// byte padding[0..3] //=0
	// byte crc32[4]
};


static uint64 xz_varint(const byte **_p, size_t len);
static int xz_stmhdr_parse(const char *buf);
static int64 xz_stmftr_parse(const char *buf);
static int xz_hdr_parse(const char *buf, size_t len, lzma_filter_props *filts);
static int64 xz_idx_parse(const char *buf, size_t len);


/* [(1X*)...]  0X* */
static uint64 xz_varint(const byte **_p, size_t len)
{
	uint i = 0;
	uint64 n = 0;
	const byte *p = *_p;
	len = ffmin(len, 9);
	for (;;) {
		if (i == len) {
			*_p = p + len;
			return 0;
		}
		n |= (uint64)(p[i] & ~0x80) << (i * 7);

		if (!(p[i++] & 0x80))
			break;
	}

	*_p = p + i;
	return n;
}

/** Parse stream header.
Return check method;  <0 on error. */
static int xz_stmhdr_parse(const char *buf)
{
	const struct stm_hdr *h = (void*)buf;

	if (memcmp(h->magic, "\xFD" "7zXZ\0", 6))
		return -FFXZ_EHDR;

	uint crc = ffcrc32_get((void*)&h->flags1, 2);
	if (crc != ffint_ltoh32(h->crc32))
		return -FFXZ_EHDRCRC;

	if (h->flags1 != 0 || (h->flags2 & 0xf0))
		return -FFXZ_EHDRFLAGS;

	return (h->flags2 & 0x0f);
}

/** Parse stream footer.
Return index size;  <0 on error. */
static int64 xz_stmftr_parse(const char *buf)
{
	const struct stm_ftr *f = (void*)buf;

	uint crc = ffcrc32_get((void*)f->index_size, sizeof(f->index_size) + sizeof(f->flags));
	if (crc != ffint_ltoh32(f->crc32))
		return -FFXZ_EFTRCRC;

	if (memcmp(f->magic, "YZ", 2))
		return -FFXZ_EFTR;

	uint64 idx_size = (ffint_ltoh32(f->index_size) + 1) * 4;
	if (idx_size > (uint)-1)
		return -FFXZ_EBIGIDX;
	return idx_size;
}

/** Parse block header.
Return number of filters;  <0 on error. */
static int xz_hdr_parse(const char *buf, size_t len, lzma_filter_props *filts)
{
	const struct blk_hdr *h = (void*)buf;
	const byte *p = (void*)buf, *end = (void*)(buf + len);

	if (h->res != 0)
		return -FFXZ_EBLKHDRFLAGS;

	p += 2; //skip "size", "flags"

	if (h->have_size) {
		/*uint64 size =*/ xz_varint(&p, end - p);
	}

	if (h->have_osize) {
		/*uint64 osize =*/ xz_varint(&p, end - p);
	}

	uint nfilt = h->nfilt + 1;
	for (uint i = 0;  i != nfilt;  ++i) {
		filts[i].id = xz_varint(&p, end - p);
		filts[i].prop_len = xz_varint(&p, end - p);
		filts[i].props = (void*)p;
		p += filts[i].prop_len;
		if (p >= end)
			return -FFXZ_EBLKHDR;
	}

	uint padding = (end - p) % 4;
	if (p + padding > end)
		return -FFXZ_EBLKHDR;
	if (p + padding != (void*)ffs_skip((void*)p, padding, 0x00))
		return -FFXZ_EBLKHDR;
	p += padding;

	uint crc = ffcrc32_get(buf, len - 4);
	if (p + 4 != end || crc != ffint_ltoh32(p))
		return -FFXZ_EBLKHDRCRC;
	return nfilt;
}

/** Parse xz index.
Return the original file size;  <0 on error. */
static int64 xz_idx_parse(const char *buf, size_t len)
{
	uint64 total_osize = 0;
	const byte *p = (void*)buf, *end = (void*)(buf + len);
	if (*p++ != 0)
		return -FFXZ_EIDX;

	uint64 nrec = xz_varint(&p, end - p);
	// if (nrec != )
	// 	return -FFXZ_EIDX;
	for (uint64 i = 0;  i != nrec;  i++) {
		uint64 size = xz_varint(&p, end - p);
		uint64 osize = xz_varint(&p, end - p);
		total_osize += osize;

		FFDBG_PRINTLN(10, "index: block #%U: %U -> %U", i, osize, size);
		(void)size;
	}

	if (p == end)
		return -FFXZ_EIDX;

	uint padding = (end - p) % 4;
	if (p + padding > end)
		return -FFXZ_EIDX;
	if (p + padding != (void*)ffs_skip((void*)p, padding, 0x00))
		return -FFXZ_EIDX;
	p += padding;

	uint crc = ffcrc32_get(buf, len - 4);
	if (p + 4 != end || crc != ffint_ltoh32(p))
		return -FFXZ_EIDXCRC;
	return total_osize;
}


static const char *const xz_errs[] = {
	"bad stream header",
	"bad stream header flags",
	"bad stream header CRC",

	"bad block header",
	"bad block header flags",
	"bad block header CRC",
	"unsupported filter",

	"bad index",
	"bad index CRC",
	"too big index",

	"bad stream footer",
	"bad stream footer flags",
	"bad stream footer CRC",
};

const char* ffxz_errstr(const void *_xz)
{
	const ffxz *xz = _xz;
	switch (xz->err) {
	case FFXZ_ESYS:
		return fferr_strp(fferr_last());

	case FFXZ_ELZMA:
		return lzma_errstr(xz->lzma_err);
	}

	return xz_errs[xz->err - FFXZ_EHDR];
}

#define ERR(xz, n) \
	(xz)->err = n, FFXZ_ERR


void ffxz_close(ffxz *xz)
{
	FF_SAFECLOSE(xz->dec, NULL, lzma_decode_free);
	ffarr_free(&xz->buf);
}

enum {
	R_START, R_GATHER, R_HDRSEEK, R_HDR, R_BLKHDR_SIZE, R_BLKHDR, R_DATA, R_IDX, R_FTR,
	R_CHK
};

/*
. Seek; read stream footer (SEEK)
. Seek; read index (SEEK, INFO)
. Seek; read stream header (SEEK)
. Read data (DATA, DONE) */
int ffxz_read(ffxz *xz, char *dst, size_t cap)
{
	int r;

	for (;;) {
	switch (xz->state) {

	case R_START:
		xz->inoff = xz->inoff - sizeof(struct stm_ftr);
		xz->hsize = sizeof(struct stm_ftr);
		xz->state = R_GATHER, xz->nxstate = R_FTR;
		return FFXZ_SEEK;

	case R_GATHER:
		r = ffarr_append_until(&xz->buf, xz->in.ptr, xz->in.len, xz->hsize);
		switch (r) {
		case 0:
			xz->inoff += xz->in.len;
			return FFXZ_MORE;
		case -1:
			return ERR(xz, FFXZ_ESYS);
		}
		ffstr_shift(&xz->in, r);
		xz->inoff += r;
		xz->state = xz->nxstate;
		continue;

	case R_HDRSEEK:
		xz->inoff = 0;
		xz->hsize = sizeof(struct stm_hdr);
		xz->state = R_GATHER, xz->nxstate = R_HDR;
		return FFXZ_SEEK;

	case R_HDR:
		if (0 > (r = xz_stmhdr_parse(xz->buf.ptr)))
			return ERR(xz, -r);
		xz->check_method = r;
		xz->buf.len = 0;
		xz->hsize = 1;
		xz->state = R_GATHER, xz->nxstate = R_BLKHDR_SIZE;
		continue;

	case R_BLKHDR_SIZE: {
		const byte *b = (void*)xz->buf.ptr;
		if (*b == 0) {
			return FFXZ_DONE;
		}
		xz->hsize = (*b + 1) * 4;
		xz->state = R_GATHER, xz->nxstate = R_BLKHDR;
		continue;
	}

	case R_BLKHDR: {
		lzma_filter_props filts[4];
		if (0 > (r = xz_hdr_parse(xz->buf.ptr, xz->buf.len, filts)))
			return ERR(xz, -r);
		xz->buf.len = 0;

		if (0 != (r = lzma_decode_init(&xz->dec, xz->check_method, filts, r))) {
			xz->lzma_err = r;
			return ERR(xz, FFXZ_ELZMA);
		}

		xz->state = R_DATA;
		//break
	}

	case R_DATA: {
		size_t rd = xz->in.len;
		r = lzma_decode(xz->dec, xz->in.ptr, &rd, dst, cap);

		if (r == LZMA_DONE) {
			xz->hsize = 1;
			xz->state = R_GATHER, xz->nxstate = R_BLKHDR_SIZE;
			continue;

		} else if (r < 0) {
			xz->lzma_err = r;
			return ERR(xz, FFXZ_ELZMA);
		}

		ffstr_shift(&xz->in, rd);
		xz->insize += rd;

		if (r == 0)
			return FFXZ_MORE;

		ffstr_set(&xz->out, dst, r);
		xz->outsize += r;
		return FFXZ_DATA;
	}

	case R_IDX: {
		int64 rr;
		if (0 > (rr = xz_idx_parse(xz->buf.ptr, xz->buf.len)))
			return ERR(xz, -rr);
		xz->osize = rr;
		xz->buf.len = 0;
		xz->state = R_HDRSEEK;
		return FFXZ_INFO;
	}

	case R_FTR: {
		int64 rr;
		if (0 > (rr = xz_stmftr_parse(xz->buf.ptr)))
			return ERR(xz, -rr);

		// if (ftr->flags != hdr->flags)
		// 	return ERR(xz, FFXZ_EFTRFLAGS);

		xz->hsize = rr;
		xz->state = R_GATHER, xz->nxstate = R_IDX;
		xz->inoff = xz->inoff - xz->buf.len - rr;
		xz->buf.len = 0;
		return FFXZ_SEEK;
	}

	}
	}
}
