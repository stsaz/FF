/**
Copyright (c) 2016 Simon Zolin
*/

#include <FF/audio/vorbis.h>
#include <FFOS/error.h>


enum VORBIS_HDR_T {
	T_INFO = 1,
	T_COMMENT = 3,
};

struct vorbis_hdr {
	byte type; //enum VORBIS_HDR_T
	char vorbis[6]; //"vorbis"
};

#define VORB_STR  "vorbis"

struct vorbis_info {
	byte ver[4]; //0
	byte channels;
	byte rate[4];
	byte br_max[4];
	byte br_nominal[4];
	byte br_min[4];
	byte blocksize;
	byte framing_bit; //1
};

/** Parse Vorbis-info packet. */
static int vorb_info(const char *d, size_t len, uint *channels, uint *rate, uint *br_nominal);

/**
Return pointer to the beginning of Vorbis comments data;  NULL if not Vorbis comments header. */
static void* vorb_comm(const char *d, size_t len, size_t *vorbtag_len);

/** Prepare OGG packet for Vorbis comments.
@d: buffer for the whole packet, must have 1 byte of free space at the end
Return packet length. */
static uint vorb_comm_write(char *d, size_t vorbtag_len);


int vorb_info(const char *d, size_t len, uint *channels, uint *rate, uint *br_nominal)
{
	const struct vorbis_hdr *h = (void*)d;
	if (len < sizeof(struct vorbis_hdr) + sizeof(struct vorbis_info)
		|| !(h->type == T_INFO && !ffs_cmp(h->vorbis, VORB_STR, FFSLEN(VORB_STR))))
		return -1;

	const struct vorbis_info *vi = (void*)(d + sizeof(struct vorbis_hdr));
	if (0 != ffint_ltoh32(vi->ver)
		|| 0 == (*channels = vi->channels)
		|| 0 == (*rate = ffint_ltoh32(vi->rate))
		|| vi->framing_bit != 1)
		return -1;

	*br_nominal = ffint_ltoh32(vi->br_nominal);
	return 0;
}

void* vorb_comm(const char *d, size_t len, size_t *vorbtag_len)
{
	const struct vorbis_hdr *h = (void*)d;

	if (len < (uint)sizeof(struct vorbis_hdr)
		|| !(h->type == T_COMMENT && !ffs_cmp(h->vorbis, VORB_STR, FFSLEN(VORB_STR))))
		return NULL;

	*vorbtag_len = len - sizeof(struct vorbis_hdr);
	return (char*)d + sizeof(struct vorbis_hdr);
}

uint vorb_comm_write(char *d, size_t vorbtag_len)
{
	struct vorbis_hdr *h = (void*)d;
	h->type = T_COMMENT;
	ffmemcpy(h->vorbis, VORB_STR, FFSLEN(VORB_STR));
	d[sizeof(struct vorbis_hdr) + vorbtag_len] = 1; //set framing bit
	return sizeof(struct vorbis_hdr) + vorbtag_len + 1;
}


#define ERR(v, n) \
	(v)->err = n,  FFVORBIS_RERR

static const char* const _ffvorbis_errs[] = {
	"",
	"unsupported input audio format",
	"bad packet",
	"invalid tags",
};

const char* _ffvorbis_errstr(int e)
{
	if (e == FFVORBIS_ESYS)
		return fferr_strp(fferr_last());

	if (e >= 0)
		return _ffvorbis_errs[e];
	return vorbis_errstr(e);
}


int ffvorbis_open(ffvorbis *v)
{
	v->seek_sample = (uint64)-1;
	v->total_samples = (uint64)-1;
	return 0;
}

void ffvorbis_close(ffvorbis *v)
{
	FF_SAFECLOSE(v->vctx, NULL, vorbis_decode_free);
}

void ffvorbis_seek(ffvorbis *v, uint64 sample)
{
	v->seek_sample = sample;
}

int ffvorbis_decode(ffvorbis *v, const char *pkt, size_t len)
{
	enum { R_HDR, R_TAGS, R_TAG, R_BOOK, R_DATA };
	int r;

	if (len == 0)
		return FFVORBIS_RMORE;

	ogg_packet opkt;
	ffmem_tzero(&opkt);
	opkt.packet = (void*)pkt,  opkt.bytes = len;
	opkt.packetno = v->pktno++;
	opkt.granulepos = -1;
	opkt.e_o_s = v->fin;

	switch (v->state) {
	case R_HDR:
		if (0 != vorb_info(pkt, len, &v->info.channels, &v->info.rate, &v->info.bitrate_nominal))
			return ERR(v, FFVORBIS_EPKT);

		if (0 != (r = vorbis_decode_init(&v->vctx, &opkt)))
			return ERR(v, r);
		v->state = R_TAGS;
		return FFVORBIS_RHDR;

	case R_TAGS:
		if (NULL == (v->vtag.data = vorb_comm(pkt, len, &v->vtag.datalen)))
			return ERR(v, FFVORBIS_ETAG);
		v->state = R_TAG;
		// break

	case R_TAG:
		r = ffvorbtag_parse(&v->vtag);
		if (r == FFVORBTAG_ERR)
			return ERR(v, FFVORBIS_ETAG);
		else if (r == FFVORBTAG_DONE) {
			if (!(v->vtag.datalen != 0 && v->vtag.data[0] == 1))
				return ERR(v, FFVORBIS_ETAG);
			v->state = R_BOOK;
			return FFVORBIS_RHDRFIN;
		}
		return FFVORBIS_RTAG;

	case R_BOOK:
		if (0 != (r = vorbis_decode_init(&v->vctx, &opkt)))
			return ERR(v, r);
		v->state = R_DATA;
		return FFVORBIS_RMORE;
	}

	r = vorbis_decode(v->vctx, &opkt, &v->pcm);
	if (r < 0)
		return v->err = r,  FFVORBIS_RWARN;
	else if (r == 0)
		return FFVORBIS_RMORE;

	if (v->seek_sample != (uint64)-1) {
		if (v->seek_sample < v->cursample) {
			//couldn't find the target packet within the page
			v->seek_sample = v->cursample;
		}

		uint skip = ffmin(v->seek_sample - v->cursample, r);
		v->cursample += skip;
		if (v->cursample != v->seek_sample || (uint)r == skip)
			return FFVORBIS_RMORE; //not yet reached the target packet

		v->seek_sample = (uint64)-1;
		for (uint i = 0;  i != v->info.channels;  i++) {
			v->pcm_arr[i] = v->pcm[i] + skip;
		}
		v->pcm = v->pcm_arr;
		r -= skip;
	}

	if (v->cursample + r >= v->total_samples)
		r = v->total_samples - v->cursample;

	v->pkt_samples = r;
	v->pcmlen = r * sizeof(float) * v->info.channels;
	v->cursample += r;
	return FFVORBIS_RDATA;
}


int ffvorbis_create(ffvorbis_enc *v, const ffpcm *fmt, int quality)
{
	int r;

	if (fmt->format != FFPCM_FLOAT)
		return ERR(v, FFVORBIS_EFMT);

	vorbis_encode_params params = {0};
	params.channels = fmt->channels;
	params.rate = fmt->sample_rate;
	params.quality = (float)quality / 100;
	ogg_packet pkt[2];
	if (0 != (r = vorbis_encode_create(&v->vctx, &params, &pkt[0], &pkt[1])))
		return ERR(v, r);

	if (NULL == ffarr_alloc(&v->vtag.out, 4096))
		return ERR(v, FFVORBIS_ESYS);
	v->vtag.out.len = sizeof(struct vorbis_hdr);
	const char *vendor = vorbis_vendor();
	ffvorbtag_add(&v->vtag, NULL, vendor, ffsz_len(vendor));

	v->channels = fmt->channels;
	ffstr_set(&v->pkt_hdr, pkt[0].packet, pkt[0].bytes);
	ffstr_set(&v->pkt_book, pkt[1].packet, pkt[1].bytes);
	v->min_tagsize = 1000;
	return 0;
}

void ffvorbis_enc_close(ffvorbis_enc *v)
{
	ffvorbtag_destroy(&v->vtag);
	FF_SAFECLOSE(v->vctx, NULL, vorbis_encode_free);
}

static const byte _ffvorbis_brates[] = {
	45/2, 64/2, 80/2, 96/2, 112/2, 128/2, 160/2, 192/2, 224/2, 256/2, 320/2, 500/2 //q=-1..10 for 44.1kHz mono
};

uint ffvorbis_enc_bitrate(ffvorbis_enc *v, int quality)
{
	uint q = quality / 10 + 1;
	return _ffvorbis_brates[q] * 1000 * v->channels;
}

/** Get complete packet with Vorbis comments and padding. */
static int _ffvorbis_tags(ffvorbis_enc *v, ffstr *pkt)
{
	ffarr *vt = &v->vtag.out;
	uint taglen = vt->len - sizeof(struct vorbis_hdr);
	uint npadding = (taglen < v->min_tagsize) ? v->min_tagsize - taglen : 0;
	if (NULL == ffarr_grow(vt, 1 + npadding, 0)) //allocate space for "framing bit" and padding
		return FFVORBIS_ESYS;

	if (npadding != 0)
		ffmem_zero(vt->ptr + vt->len + 1, npadding);

	ffstr_set(pkt, (void*)vt->ptr, vorb_comm_write(vt->ptr, taglen) + npadding);
	return 0;
}

int ffvorbis_encode(ffvorbis_enc *v)
{
	ogg_packet pkt;
	int r;
	enum { W_HDR, W_COMM, W_BOOK, W_DATA };

	switch (v->state) {
	case W_HDR:
		ffstr_set2(&v->data, &v->pkt_hdr);
		v->state = W_COMM;
		return FFVORBIS_RDATA;

	case W_COMM:
		ffvorbtag_fin(&v->vtag);
		if (0 != (r = _ffvorbis_tags(v, &v->data)))
			return ERR(v, r);

		v->state = W_BOOK;
		return FFVORBIS_RDATA;

	case W_BOOK:
		ffstr_set2(&v->data, &v->pkt_book);
		v->state = W_DATA;
		return FFVORBIS_RDATA;
	}

	int n = (uint)(v->pcmlen / (sizeof(float) * v->channels));
	v->pcmlen = 0;

	for (;;) {
		r = vorbis_encode(v->vctx, v->pcm, n, &pkt);
		if (r < 0) {
			v->err = r;
			return FFVORBIS_RERR;
		} else if (r == 0) {
			if (v->fin) {
				n = -1;
				continue;
			}
			return FFVORBIS_RMORE;
		}
		break;
	}

	v->granulepos = pkt.granulepos;
	ffstr_set(&v->data, pkt.packet, pkt.bytes);
	return (pkt.e_o_s) ? FFVORBIS_RDONE : FFVORBIS_RDATA;
}
