/**
Copyright (c) 2018 Simon Zolin
*/

#include <FF/pic/jpeg.h>


enum JPEG_R {
	R_INIT, R_GATHER,
	R_SOI, R_MARKER_NEXT, R_MARKER, R_MARKER_DATA, R_MARKER_SKIP,
	R_ERR,
};

int ffjpegr_open(struct ffjpegr *j)
{
	return 0;
}

void ffjpegr_close(struct ffjpegr *j)
{
	ffarr_free(&j->buf);
}

#define M_START 0xff

enum JPEG_M {
	M_SOF0 = 0xc0,
	M_SOF1,
	M_SOF2,

	M_SOI = 0xd8,
	M_EOI,

	M_SOS = 0xda,
};
struct jpeg_marker {
	byte start; //=0xff
	byte type; // enum JPEG_M
	byte len[2]; //= 2 + datalen
	byte data[0];
};


struct jpeg_sof {
	byte unused;
	byte height[2];
	byte width[2];
	byte unused2;
};

/** Parse SOF marker. */
static int jpeg_sof(struct ffjpegr *j, const struct jpeg_marker *m)
{
	uint len = ffint_ntoh16(m->len);
	if (len < sizeof(struct jpeg_sof))
		return FFJPEG_ERR;
	const struct jpeg_sof *sof = (void*)m->data;
	j->info.width = ffint_ntoh16(sof->width);
	j->info.height = ffint_ntoh16(sof->height);
	j->info.bpp = 0;
	FFDBG_PRINTLN(10, "width:%u  height:%u"
		, j->info.width, j->info.height);
	return 0;
}


static void GATHER(struct ffjpegr *j, uint nxstate, size_t len)
{
	j->state = R_GATHER,  j->nxstate = nxstate;
	j->gathlen = len;
	j->buf.len = 0;
}

static void GATHER_MORE(struct ffjpegr *j, uint nxstate, size_t len)
{
	j->state = R_GATHER,  j->nxstate = nxstate;
	FF_ASSERT(j->gathlen == j->buf.len);
	j->gathlen = j->buf.len + len;
}

int ffjpegr_read(struct ffjpegr *j)
{
	int r;

	for (;;) {
	switch ((enum JPEG_R)j->state) {

	case R_INIT:
		GATHER(j, R_SOI, 2);
		continue;

	case R_GATHER:
		r = ffarr_append_until(&j->buf, j->input.ptr, j->input.len, j->gathlen);
		if (r == 0) {
			j->input.len = 0;
			return FFJPEG_MORE;
		} else if (r == -1)
			return FFJPEG_ERR;
		ffstr_set2(&j->chunk, &j->buf);
		ffarr_shift(&j->input, r);
		j->state = j->nxstate;
		continue;

	case R_SOI: {
		const byte *b = (void*)j->chunk.ptr;
		if (!(b[0] == M_START && b[1] == M_SOI))
			return FFJPEG_ERR;
		j->state = R_MARKER_NEXT;
		continue;
	}

	case R_MARKER_NEXT:
		GATHER(j, R_MARKER, sizeof(struct jpeg_marker));
		continue;

	case R_MARKER: {
		struct jpeg_marker *m = (void*)j->chunk.ptr;
		if (m->start != M_START)
			return FFJPEG_ERR;
		uint len = ffint_ntoh16(m->len);
		FFDBG_PRINTLN(10, "marker type:%xu  len:%u", m->type, len);
		if (len < 2)
			return FFJPEG_ERR;
		len -= 2;

		switch (m->type) {

		case M_SOF0:
		case M_SOF1:
		case M_SOF2:
			GATHER_MORE(j, R_MARKER_DATA, len);
			continue;

		case M_SOS:
			return FFJPEG_HDR;
		case M_EOI:
			return FFJPEG_DONE;
		}

		j->state = R_MARKER_SKIP;
		j->gathlen = len;
		continue;
	}

	case R_MARKER_DATA: {
		const struct jpeg_marker *m = (void*)j->chunk.ptr;
		switch (m->type) {
		case M_SOF0:
		case M_SOF1:
		case M_SOF2:
			if (0 != (r = jpeg_sof(j, m)))
				return r;
			break;
		}
		j->state = R_MARKER_NEXT;
		continue;
	}

	case R_MARKER_SKIP:
		if (j->input.len < j->gathlen)
			return FFJPEG_ERR;
		ffstr_shift(&j->input, j->gathlen);
		j->state = R_MARKER_NEXT;
		continue;

	case R_ERR:
		return FFJPEG_ERR;
	}
	}
}
