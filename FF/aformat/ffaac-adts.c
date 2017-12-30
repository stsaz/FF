/** AAC ADTS reader (.aac).
Copyright (c) 2017 Simon Zolin
*/

#include <FF/aformat/aac-adts.h>
#include <FF/number.h>

enum {
	FR_SAMPLES = 1024,
	ADTS_HDRLEN = 7,
};


struct adts_hdr;
struct adts_hdr_packed;
static int adts_hdr_parse(struct adts_hdr *h, const struct adts_hdr_packed *d);
static struct adts_hdr_packed* adts_hdr_find(struct adts_hdr *h, const char *data, size_t len, const struct adts_hdr_packed *hp);


enum ADTS_E {
	ADTS_ESYS = 1,
	ADTS_ESYNC,
	ADTS_ELAYER,
	ADTS_ESAMPFREQ,
	ADTS_ECHANCONF,
	ADRS_EFRAMELEN,
	ADRS_ERAWBLOCKS,
	ADTS_ELOSTSYNC,
};

static const char* const errs[] = {
	"bad sync",
	"bad layer",
	"bad sample frequency index",
	"bad channel configuration",
	"too small frame length",
	"unsupported raw blocks number",
	"lost synchronization",
};

const char* ffaac_adts_errstr(void *_a)
{
	struct ffaac_adts *a = _a;
	if (a->err == ADTS_ESYS)
		return fferr_strp(fferr_last());
	FF_ASSERT(a->err - 2 <= FFCNT(errs));
	return errs[a->err - 2];
}


static uint64 bit_read64(uint64 val, uint *off, uint n)
{
	uint64 r = ffbit_read64(val, *off, n);
	*off += n;
	return r;
}

static uint bit_write32(uint val, uint *off, uint n)
{
	uint r = ffbit_write32(val, *off, n);
	*off += n;
	return r;
}


enum ADTS_H {
	H_SYNC = 12, //=0x0fff
	H_MPEG_ID = 1,
	H_LAYER = 2, //=0
	H_PROTECTION_ABSENT = 1,
	H_PROFILE = 2, //AOT-1
	H_SAMPLE_FREQ_INDEX = 4, //0..12
	H_PRIVATE_BIT = 1,
	H_CHANNEL_CONFIG = 3,
	H_ORIGINAL = 1,
	H_HOME = 1,
	H_COPYRIGHT_ID = 1,
	H_COPYRIGHT_START = 1,
	H_FRAME_LENGTH = 13,
	H_FULLNESS = 11,
	H_NUM_RAW_BLOCKS = 2, //AAC frames in ADTS frame (aac-frames = raw-blocks - 1)

	H_CONSTMASK = 0xfffefdc0 /*15 . 6 . 3*/,
};

struct adts_hdr_packed {
	byte hdr[7]; //enum ADTS_H
	//byte crc[2]; //if PROTECTION_ABSENT==0
};

struct adts_hdr {
	uint aot;
	uint samp_freq_idx;
	uint chan_conf;
	uint frlen;
	uint datalen;
	uint raw_blocks;
	uint have_crc;
};

/** Parse 7 bytes of ADTS frame header. */
static int adts_hdr_parse(struct adts_hdr *h, const struct adts_hdr_packed *d)
{
	uint off = 0;
	uint64 v = ffint_ntoh64(d);
	if (0x0fff != bit_read64(v, &off, H_SYNC))
		return ADTS_ESYNC;
	off += H_MPEG_ID;
	if (0 != bit_read64(v, &off, H_LAYER))
		return ADTS_ELAYER;
	h->have_crc = !bit_read64(v, &off, H_PROTECTION_ABSENT);
	h->aot = bit_read64(v, &off, H_PROFILE) + 1;

	h->samp_freq_idx = bit_read64(v, &off, H_SAMPLE_FREQ_INDEX);
	if (h->samp_freq_idx >= 13)
		return ADTS_ESAMPFREQ;

	off += H_PRIVATE_BIT;

	h->chan_conf = bit_read64(v, &off, H_CHANNEL_CONFIG);
	if (h->chan_conf == 0)
		return ADTS_ECHANCONF;

	off += H_ORIGINAL;
	off += H_HOME;
	off += H_COPYRIGHT_ID;
	off += H_COPYRIGHT_START;

	h->frlen = bit_read64(v, &off, H_FRAME_LENGTH);
	h->datalen = h->frlen - ((h->have_crc) ? 9 : 7);
	if ((int)h->datalen < 0)
		return ADRS_EFRAMELEN;

	off += H_FULLNESS;

	h->raw_blocks = bit_read64(v, &off, H_NUM_RAW_BLOCKS) + 1;
	if (h->raw_blocks != 1)
		return ADRS_ERAWBLOCKS;

	return 0;
}

/** Compare 2 frame headers. */
static int adts_hdr_cmp(const struct adts_hdr_packed *a, const struct adts_hdr_packed *b)
{
	uint mask = ffhton32(H_CONSTMASK);
	return (*(uint*)a & mask) != (*(uint*)b & mask);
}

/** Find header.
@hp: (optional) a new header must match with this one
Return NULL on error. */
static struct adts_hdr_packed* adts_hdr_find(struct adts_hdr *h, const char *data, size_t len, const struct adts_hdr_packed *hp)
{
	struct adts_hdr_packed *p;
	const char *e = data + len;
	for (;;) {
		if ((byte)data[0] != 0xff
			&& NULL == (data = ffs_findc(data, e - data, 0xff)))
			break;
		if ((int)sizeof(struct adts_hdr_packed) > e - data)
			break;
		p = (void*)data;
		if ((hp == NULL || !adts_hdr_cmp(p, hp))
			&& 0 == adts_hdr_parse(h, p))
			return p;
		data++;
	}
	return NULL;
}

static const ushort mp4_asc_samp_freq[] = {
	96000/5, 88200/5, 64000/5, 48000/5, 44100/5, 32000/5, 24000/5, 22050/5, 16000/5, 12000/5, 11025/5, 8000/5, 7350/5, 0, 0,
	-1,
};

/** MPEG-4 Audio Specific Config, a bit-array. */
enum ASC {
	ASC_AOT = 5,
	ASC_FREQ_IDX = 4, //mp4_asc_samp_freq[]
	ASC_FREQ_VAL = 24, //if ASC_FREQ_IDX == 15
	ASC_CHAN_CONF = 4,

	//explicit non-backward compatible SBR signaling
	/* ASC_AOT = 5 */
	/* ASC_FREQ_IDX = 4 */
};

/** "ASC  ASC_AAC  [EXT=SBR  [EXT=PS]]" */
enum ASC_AAC {
	AAC_FRAME_LEN = 1, //0: each packet contains 1024 samples;  1: 960 samples
	AAC_DEPENDSONCORECODER = 1,
	AAC_EXT = 1,
	/* PCE (if ASC_CHAN_CONF == 0) */
};

/** Explicit backward compatible SBR signaling */
enum ASC_EXT {
	ASC_EXT_ID = 11, //=0x2b7
	/* ASC_AOT = 5 */
	ASC_EXT_SBR = 1,
	/* ASC_FREQ_IDX = 4 (if ASC_EXT_SBR == 1) */

	//PS:
	/* ASC_EXT_ID = 11 */ //=0x548
	ASC_EXT_PS = 1,
};

/** Create MPEG-4 ASC data: "ASC  ASC_AAC" */
static int adts_mk_mp4asc(char *dst, const struct adts_hdr *h)
{
	uint off = 0, v = 0;
	v |= bit_write32(h->aot, &off, ASC_AOT);
	v |= bit_write32(h->samp_freq_idx, &off, ASC_FREQ_IDX);
	v |= bit_write32(h->chan_conf, &off, ASC_CHAN_CONF);
	off += AAC_FRAME_LEN;
	off += AAC_DEPENDSONCORECODER;
	off += AAC_EXT;

	ffint_hton32(&v, v);
	ffmemcpy(dst, &v, 2);
	return ffbit_nbytes(off);
}

#define GATHER(a, st, len) \
	(a)->state = R_GATHER_SHIFT,  (a)->nxstate = (st),  (a)->gathlen = (len)

enum R {
	R_INIT, R_GATHER_SHIFT, R_GATHER,
	R_HDR_SYNC, R_HDR_SYNC2,
	R_HDR, R_CRC, R_DATA, R_FR,
};

void ffaac_adts_open(ffaac_adts *a)
{
}

void ffaac_adts_close(ffaac_adts *a)
{
	ffarr_free(&a->buf);
}

static void shift(ffaac_adts *a, ssize_t n)
{
	if (a->buf.len != 0) {
		if (n > 0)
			_ffarr_rmleft(&a->buf, n, 1);
	} else
		ffarr_shift(&a->in, n);
}

/* AAC ADTS (.aac) stream reading:
. synchronize:
 . search for header, parse
 . gather data for the whole first frame and the next header
 . parse and compare the next header
  . if error, continue searching
. gather ADTS header and parse it
 . if it's the very first header, return to user (RHDR)
. gather full frame
. if needed, return the whole frame to user (RFRAME)
. skip CRC if present
. return frame body to user (RDATA)
*/
int ffaac_adts_read(ffaac_adts *a)
{
	int r;
	struct adts_hdr h;
	ffstr s;

	for (;;) {
	switch (a->state) {

	case R_INIT:
		if (NULL == ffarr_alloc(&a->buf, ADTS_HDRLEN * 2))
			return a->err = ADTS_ESYS,  FFAAC_ADTS_RERR;
		a->state = R_HDR_SYNC;
		// fall through

	case R_HDR_SYNC: {
		const void *hp = (*(uint*)a->firsthdr == 0) ? NULL : a->firsthdr;
		for (;;) {
			r = ffbuf_contig(&a->buf, &a->in, ADTS_HDRLEN, &s);
			ffarr_shift(&a->in, r);
			a->off += r;

			if (s.len >= ADTS_HDRLEN) {
				hp = adts_hdr_find(&h, s.ptr, s.len, hp);
				if (hp != NULL)
					break;
			}

			r = ffbuf_contig_store(&a->buf, &a->in, ADTS_HDRLEN);
			ffarr_shift(&a->in, r);
			a->off += r;

			if (a->in.len == 0)
				return FFAAC_ADTS_RMORE;
		}

		shift(a, (char*)hp - s.ptr);
		a->frlen = h.frlen;
		a->shift = 0;
		GATHER(a, R_HDR_SYNC2, h.frlen + ADTS_HDRLEN);
		continue;
	}

	case R_HDR_SYNC2: {
		const void *hp = s.ptr;
		const void *hp2 = (char*)hp + a->frlen;
		if (0 != (r = adts_hdr_parse(&h, hp2))
			|| 0 != adts_hdr_cmp(hp, hp2)) {
			shift(a, 1);
			a->state = R_HDR_SYNC;
			continue;
		}

		a->shift = -(int)a->gathlen;
		GATHER(a, R_HDR, ADTS_HDRLEN);
		continue;
	}

	case R_GATHER_SHIFT:
		shift(a, a->shift);
		a->state = R_GATHER;
		// fall through

	case R_GATHER:
		r = ffarr_gather2(&a->buf, a->in.ptr, a->in.len, a->gathlen, &s);
		if (r < 0)
			return a->err = ADTS_ESYS,  FFAAC_ADTS_RERR;
		ffarr_shift(&a->in, r);
		a->off += r;
		if (s.len < a->gathlen) {
			if (a->fin && a->in.len == 0)
				return FFAAC_ADTS_RDONE;
			return FFAAC_ADTS_RMORE;
		}
		a->shift = (a->buf.len != 0) ? a->gathlen : 0;
		a->state = a->nxstate;
		continue;

	case R_HDR:
		if (0 != (r = adts_hdr_parse(&h, (void*)s.ptr))) {
			a->state = R_HDR_SYNC;
			return a->err = ADTS_ELOSTSYNC,  FFAAC_ADTS_RWARN;
		}

		FFDBG_PRINTLN(10, "frame #%U: size:%u  raw-blocks:%u"
			, a->frno++, h.frlen, h.raw_blocks);

		if (a->options & FFAAC_ADTS_OPT_WHOLEFRAME) {
			a->shift = -ADTS_HDRLEN;
			GATHER(a, R_FR, h.frlen);
		} else if (h.have_crc)
			GATHER(a, R_CRC, 2 + h.datalen);
		else
			GATHER(a, R_DATA, h.datalen);

		if (a->info.sample_rate == 0) {
			ffmemcpy(a->firsthdr, s.ptr, ADTS_HDRLEN);
			a->info.codec = h.aot;
			a->info.channels = h.chan_conf;
			a->info.sample_rate = mp4_asc_samp_freq[h.samp_freq_idx] * 5;
			r = adts_mk_mp4asc(a->asc, &h);
			ffstr_set(&a->out, a->asc, r);
			return FFAAC_ADTS_RHDR;
		}

		if (0 != adts_hdr_cmp((void*)a->firsthdr, (void*)s.ptr)) {
			a->state = R_HDR_SYNC;
			return a->err = ADTS_ELOSTSYNC,  FFAAC_ADTS_RWARN;
		}
		continue;

	case R_CRC:
		ffstr_shift(&s, 2);
		//fall through

	case R_DATA:
		a->nsamples += FR_SAMPLES;
		GATHER(a, R_HDR, ADTS_HDRLEN);
		a->out = s;
		return FFAAC_ADTS_RDATA;

	case R_FR:
		a->nsamples += FR_SAMPLES;
		GATHER(a, R_HDR, ADTS_HDRLEN);
		a->out = s;
		return FFAAC_ADTS_RFRAME;
	}
	}
}
