/** AAC ADTS reader (.aac).
Copyright (c) 2017 Simon Zolin
*/

#include <FF/audio/aac-adts.h>
#include <FF/number.h>

enum {
	FR_SAMPLES = 1024,
};


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
	if (h->frlen < ((h->have_crc) ? 7 : 9))
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
	return (ffint_ntoh32(a) & H_CONSTMASK) != (ffint_ntoh32(b) & H_CONSTMASK);
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

	ffint_hton32(dst, v);
	return ffbit_nbytes(off);
}

#define GATHER(a, st, len) \
	(a)->state = R_GATHER,  (a)->nxstate = (st),  (a)->gathlen = (len)

enum { R_INIT, R_GATHER, R_HDR_SYNC, R_HDR, R_CRC, R_DATA };

void ffaac_adts_open(ffaac_adts *a)
{
}

void ffaac_adts_close(ffaac_adts *a)
{
	ffarr_free(&a->buf);
}


/* AAC ADTS (.aac) stream reading:
. gather ADTS header and parse it
. gather and skip CRC if present
. gather frame body and return it to user (RDATA)
*/
int ffaac_adts_read(ffaac_adts *a)
{
	int r;
	struct adts_hdr h;

	for (;;) {
	switch (a->state) {

	case R_INIT:
		GATHER(a, R_HDR_SYNC, sizeof(struct adts_hdr_packed));
		// fall through

	case R_GATHER:
		r = ffarr_append_until(&a->buf, a->in.ptr, a->in.len, a->gathlen);
		if (r == 0) {
			if (a->fin && a->in.len == 0)
				return FFAAC_ADTS_RDONE;
			return FFAAC_ADTS_RMORE;
		}
		else if (r < 0)
			return a->err = ADTS_ESYS,  FFAAC_ADTS_RERR;
		ffstr_shift(&a->in, r);
		a->off += r;
		ffstr_set(&a->out, a->buf.ptr, a->gathlen);
		a->buf.len = 0;
		a->gathlen = 0;
		a->state = a->nxstate;
		continue;

	case R_HDR_SYNC:
	case R_HDR:
		if (0 != (r = adts_hdr_parse(&h, (void*)a->out.ptr)))
			return a->err = r,  FFAAC_ADTS_RERR;

		FFDBG_PRINTLN(10, "frame #%U: size:%u  raw-blocks:%u"
			, a->frno++, h.frlen, h.raw_blocks);

		if (a->firsthdr[0] != 0) {
			if (0 != adts_hdr_cmp((void*)a->firsthdr, (void*)a->out.ptr))
				return a->err = ADTS_ELOSTSYNC,  FFAAC_ADTS_RERR;
		}

		if (!h.have_crc)
			GATHER(a, R_DATA, h.frlen - sizeof(struct adts_hdr_packed));
		else {
			a->frlen = h.frlen;
			GATHER(a, R_CRC, 2);
		}

		if (a->info.sample_rate == 0) {
			ffmemcpy(a->firsthdr, a->out.ptr, sizeof(struct adts_hdr_packed));
			a->info.codec = h.aot;
			a->info.channels = h.chan_conf;
			a->info.sample_rate = mp4_asc_samp_freq[h.samp_freq_idx] * 5;
			r = adts_mk_mp4asc(a->asc, &h);
			ffstr_set(&a->out, a->asc, r);
			return FFAAC_ADTS_RHDR;
		}
		continue;

	case R_CRC:
		GATHER(a, R_DATA, a->frlen - sizeof(struct adts_hdr_packed) - 2);
		continue;

	case R_DATA:
		a->nsamples += FR_SAMPLES;
		GATHER(a, R_HDR, sizeof(struct adts_hdr_packed));
		return FFAAC_ADTS_RDATA;
	}
	}
}
