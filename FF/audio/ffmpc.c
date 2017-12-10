/**
Copyright (c) 2017 Simon Zolin
*/

/*
block:
	byte id[2]
	varint size // id & size included
*/

#include <FF/audio/mpc.h>
#include <FFOS/error.h>


enum {
	BLKHDR_MINSIZE = 2 + 1,
	MAX_BLOCK = 1 * 1024 * 1024,
	FTRTAGS_CHKSIZE = 1000,
};

static int mpc_int(const char *d, size_t len, uint64 *n);
static int mpc_sh_parse(ffmpcr *m, const ffstr *body);
static int mpc_ei_parse(ffmpcr *m, const ffstr *body);
static int mpc_rg_parse(ffmpcr *m, const ffstr *body);
static int mpc_apetag(ffmpcr *m, ffarr *buf);
static int64 mpc_getseekoff(ffmpcr *m);
static int mpc_findAP(ffmpcr *m);


/* (0x80 | x)... x */
static int mpc_int(const char *d, size_t len, uint64 *n)
{
	uint i;
	uint64 r = 0;
	len = ffmin(len, 8);

	for (i = 0;  ;  i++) {
		if (i == len)
			return -1;

		r = (r << 7) | ((byte)d[i] & ~0x80);

		if (!(d[i] & 0x80)) {
			i++;
			break;
		}
	}

	*n = r;
	return i;
}


enum {
	FFMPC_EHDR = 1,
	FFMPC_ENOHDR,
	FFMPC_EVER,
	FFMPC_EBLOCKHDR,
	FFMPC_ELARGEBLOCK,
	FFMPC_ESMALLBLOCK,
	FFMPC_EBADSO,
	FFMPC_EAPETAG,

	FFMPC_ESYS,
};

#define ERR(m, r) \
	(m)->err = (r),  FFMPC_RERR

#define WARN(m, r) \
	(m)->err = (r),  FFMPC_RWARN

static const char* const mpc_errs[] = {
	"",
	"bad header",
	"no header",
	"unsupported version",
	"bad block header",
	"too large block",
	"too small block",
	"bad APE tag",
};

const char* _ffmpc_errstr(uint e)
{
	switch (e) {
	case FFMPC_ESYS:
		return fferr_strp(fferr_last());
	}
	FF_ASSERT(e < FFCNT(mpc_errs));
	return mpc_errs[e];
}

const char* ffmpc_rerrstr(ffmpcr *m)
{
	return _ffmpc_errstr(m->err);
}

enum {
	R_START, R_GATHER, R_HDR, R_NXTBLOCK, R_BLOCK_HDR_MIN, R_BLOCK_HDR_MAX, R_BLOCK_HDR, R_BLOCK_SKIP, R_AFRAME_SEEK,
	R_ST_SEEK, R_SEEK, R_FIND_AP,
	R_TAGS_SEEK, R_APETAG, R_APETAG_MORE,

	R_AP = 0x100,
	R_EI,
	R_RG,
	R_SE,
	R_SH,
	R_SO,
	R_ST,
};

void ffmpc_ropen(ffmpcr *m)
{
}

void ffmpc_rclose(ffmpcr *m)
{
	if (m->seekctx != NULL)
		mpc_seekfree(m->seekctx);
	ffarr_free(&m->gbuf);
}

static int mpc_apetag(ffmpcr *m, ffarr *buf)
{
	for (;;) {

	size_t len = buf->len;
	int r = ffapetag_parse(&m->apetag, buf->ptr, &len);

	switch (r) {
	case FFAPETAG_RDONE:
	case FFAPETAG_RNO:
		ffapetag_parse_fin(&m->apetag);
		return 0;

	case FFAPETAG_RFOOTER:
		m->total_size -= m->apetag.size;
		continue;

	case FFAPETAG_RTAG:
		m->tag = m->apetag.tag;
		ffstr_set2(&m->tagval, &m->apetag.val);
		return FFMPC_RTAG;

	case FFAPETAG_RSEEK:
		m->off -= m->apetag.size;
		m->state = R_APETAG_MORE;
		return FFMPC_RSEEK;

	case FFAPETAG_RMORE:
		m->state = R_APETAG_MORE;
		return FFMPC_RMORE;

	case FFAPETAG_RERR:
		m->state = R_AFRAME_SEEK;
		m->err = FFMPC_EAPETAG;
		return FFMPC_RWARN;

	default:
		FF_ASSERT(0);
	}
	}
	//unreachable
}

static const char block_ids[][2] = {
	"AP",
	"EI",
	"RG",
	"SE",
	"SH",
	"SO",
	"ST",
};

static int mpc_block(const char *name)
{
	ssize_t r = ffcharr_findsorted(block_ids, FFCNT(block_ids), 2, name, 2);
	if (r < 0)
		return R_BLOCK_SKIP;
	return 0x100 | r;
}

static const uint mpc_rates[] = { 44100, 48000, 37800, 32000 };

/* SH block:
byte crc[4]
byte ver //8
varint samples
varint delay
byte rate :3
byte max_band :5 // +1
byte channels :4 // +1
byte midside_stereo :1
byte block_frames_pwr :3 // block_frames = 2^(2*x)
*/
static int mpc_sh_parse(ffmpcr *m, const ffstr *body)
{
	int n;
	const char *d = body->ptr, *end = ffarr_end(body);

	if (end - d < 4 + 1)
		return FFMPC_EHDR;
	d += 4;

	if (d[0] != 8)
		return FFMPC_EVER;
	d++;

	if (0 > (n = mpc_int(d, end - d, &m->total_samples)))
		return FFMPC_EHDR;
	d += n;

	if (0 > (n = mpc_int(d, end - d, &m->delay)))
		return FFMPC_EHDR;
	d += n;

	if (end - d != 2)
		return FFMPC_EHDR;

	m->sample_rate = mpc_rates[((byte)d[0] & 0xe0) >> 5];
	m->channels = (((byte)d[1] & 0xf0) >> 4) + 1;
	m->blk_samples = MPC_FRAME_SAMPLES << (2 * ((byte)d[1] & 0x07));
	return 0;
}

/* EI block:
byte profile :7
byte pns :1
byte ver[3]
*/
static int mpc_ei_parse(ffmpcr *m, const ffstr *body)
{
	const byte *d = (void*)body->ptr;
	if (body->len != 4)
		return FFMPC_EHDR;
	m->enc_profile = (d[0] >> 1) & 0x7f;
	ffmemcpy(m->enc_ver, d + 1, 3);
	return 0;
}

struct rg_block {
	byte ver; //1
	byte gain_title[2];
	byte peak_title[2];
	byte gain_album[2];
	byte peak_album[2];
};

static int mpc_rg_parse(ffmpcr *m, const ffstr *body)
{
	return 0;
}


/* Seeking in .mpc:
. Seek to the needed audio position (FFMPC_RSEEK)
. Find the first AP block
  If seek table is used, AP block should be at the beginning of input buffer
. If decoding was unsuccessful (user reports this), find the next AP block
*/
void ffmpc_blockseek(ffmpcr *m, uint64 sample)
{
	sample += m->delay;
	if (sample >= m->total_samples)
		return;
	m->seek_sample = sample;
	if (m->hdrok)
		m->state = R_SEEK;
}

void ffmpc_streamerr(ffmpcr *m)
{
	m->gbuf.len = 0;
	m->state = R_FIND_AP;
}

static FFINL int64 mpc_getseekoff(ffmpcr *m)
{
	uint64 off = 0;

	if (m->seekctx != NULL) {
		uint blk;
		blk = m->seek_sample / m->blk_samples;
		off = mpc_seek(m->seekctx, &blk);
		FFDBG_PRINTLN(10, "block:%u offset:%xU"
			, blk, off);
		m->blk_apos = blk * m->blk_samples;
	}

	if (off == 0) {
		struct ffpcm_seek sk = {0};
		struct ffpcm_seekpt pt[2];
		pt[0].sample = 0;
		pt[0].off = m->dataoff;
		pt[1].sample = m->total_samples;
		pt[1].off = m->total_size;
		sk.target = m->seek_sample;
		sk.pt = pt;
		ffpcm_seek(&sk);
		m->blk_apos = m->seek_sample - (m->seek_sample % m->blk_samples);
		off = sk.off;
	}

	return off;
}

static FFINL int mpc_findAP(ffmpcr *m)
{
	int r;
	ffstr s;

	for (;;) {
		r = ffbuf_contig(&m->gbuf, &m->input, FFSLEN("AP"), &s);
		if (r < 0)
			return ERR(m, FFMPC_ESYS);
		ffarr_shift(&m->input, r);
		m->off += r;
		if (s.len >= FFSLEN("AP")
			&& 0 <= (r = ffstr_find(&s, "AP", 2))) {
			ffarr_shift(&m->input, r);
			m->off += r;
			break;
		}

		r = ffbuf_contig_store(&m->gbuf, &m->input, FFSLEN("AP"));
		if (r < 0)
			return ERR(m, FFMPC_ESYS);
		ffarr_shift(&m->input, r);
		m->off += r;

		if (m->input.len == 0)
			return FFMPC_RMORE;
	}
	return 0;
}

/* .mpc reader:
. Check Musepack ID
. Gather block header (id & size)
. Gather and process or skip block body until the first AP block is met
. Store ST block offset from SO block
. Return SH block body (FFMPC_RHDR)
. Seek to ST block (FFMPC_RSEEK); parse it
. Seek to the end and parse APE tag (FFMPC_RSEEK, FFMPC_RTAG)
. Seek to audio data (FFMPC_RSEEK)
. Gather and return AP blocks until SE block is met (FFMPC_RBLOCK)
*/
int ffmpc_read(ffmpcr *m)
{
	int r;
	ffstr s;

	for (;;) {
	switch (m->state) {

	case R_START:
		m->gsize = 4;
		m->state = R_GATHER,  m->gstate = R_HDR;
		continue;

	case R_GATHER:
		r = ffarr_gather(&m->gbuf, m->input.ptr, m->input.len, m->gsize);
		if (r < 0)
			return ERR(m, FFMPC_ESYS);
		ffstr_shift(&m->input, r);
		m->off += r;
		if (m->gbuf.len < m->gsize)
			return FFMPC_RMORE;
		m->state = m->gstate;
		continue;

	case R_HDR:
		if (!ffstr_eqcz(&m->gbuf, "MPCK"))
			return ERR(m, FFMPC_EHDR);
		m->gbuf.len = 0;
		m->state = R_NXTBLOCK;
		continue;

	case R_NXTBLOCK:
		_ffarr_rmleft(&m->gbuf, m->blk_size, sizeof(char));
		m->gsize = BLKHDR_MINSIZE;
		m->state = R_GATHER,  m->gstate = R_BLOCK_HDR_MIN;
		continue;

	case R_BLOCK_HDR_MIN:
		if (1 == mpc_int(m->gbuf.ptr + 2, 1, &m->blk_size)) {
			m->blk_off = 2 + 1;
			m->state = R_BLOCK_HDR;
			continue;
		}
		m->gsize = FFMPC_BLKHDR_MAXSIZE;
		m->state = R_GATHER,  m->gstate = R_BLOCK_HDR_MAX;
		continue;

	case R_BLOCK_HDR_MAX:
		if (-1 == (r = mpc_int(m->gbuf.ptr + 2, m->gbuf.len - 2, &m->blk_size))) {
			if (m->hdrok) {
				ffmpc_streamerr(m);
				return WARN(m, FFMPC_EBLOCKHDR);
			}
			return ERR(m, FFMPC_EBLOCKHDR);
		}
		m->blk_off = 2 + r;
		// break

	case R_BLOCK_HDR:
		FFDBG_PRINTLN(10, "block:%2s size:%U offset:%xU", m->gbuf.ptr, m->blk_size, m->off);

		if (m->blk_size < m->blk_off) {
			if (m->hdrok) {
				ffmpc_streamerr(m);
				return WARN(m, FFMPC_ESMALLBLOCK);
			}
			return ERR(m, FFMPC_ESMALLBLOCK);
		}

		if (m->blk_size > MAX_BLOCK) {
			if (m->hdrok) {
				ffmpc_streamerr(m);
				return WARN(m, FFMPC_ELARGEBLOCK);
			}
			return ERR(m, FFMPC_ELARGEBLOCK);
		}

		m->gsize = m->blk_size;
		r = mpc_block(m->gbuf.ptr);

		if (m->hdrok) {
			switch (r) {
			case R_AP:
			case R_SE:
				m->state = R_GATHER,  m->gstate = r;
				break;
			default:
				m->state = R_BLOCK_SKIP;
			}
			continue;
		}

		switch (r) {

		case R_AP:
			if (m->sample_rate == 0)
				return ERR(m, FFMPC_ENOHDR);
			m->dataoff = m->off - m->gbuf.len;
			if ((m->options & FFMPC_O_SEEKTABLE) && (m->ST_off != 0))
				m->state = R_ST_SEEK;
			else if (m->options & FFMPC_O_APETAG)
				m->state = R_TAGS_SEEK;
			else {
				m->hdrok = 1;
				m->state = R_AP;
			}
			return FFMPC_RHDR;

		case R_BLOCK_SKIP:
			m->state = R_BLOCK_SKIP;
			break;

		default:
			m->state = R_GATHER,  m->gstate = r;
		}
		continue;

	case R_AFRAME_SEEK:
		m->gbuf.len = 0;
		m->hdrok = 1;
		if (m->seek_sample != 0) {
			m->state = R_SEEK;
			continue;
		}
		m->off = m->dataoff;
		m->gsize = BLKHDR_MINSIZE;
		m->state = R_GATHER,  m->gstate = R_BLOCK_HDR_MIN;
		return FFMPC_RSEEK;

	case R_BLOCK_SKIP:
		if (m->gbuf.len != 0) {
			r = ffmin(m->blk_size, m->gbuf.len);
			_ffarr_rmleft(&m->gbuf, r, sizeof(char));
			m->blk_size -= r;
		}

		if (m->input.len != 0) {
			r = ffmin(m->blk_size, m->input.len);
			ffstr_shift(&m->input, r);
			m->off += r;
			m->blk_size -= r;
		}

		if (m->blk_size != 0)
			return FFMPC_RMORE;

		m->state = R_NXTBLOCK;
		continue;

	case R_SH:
		ffarr_setshift(&s, m->gbuf.ptr, m->blk_size, m->blk_off);
		if (0 != (r = mpc_sh_parse(m, &s)))
			return ERR(m, r);

		m->seek_sample = m->delay;
		ffmemcpy(m->sh_block, s.ptr, s.len);
		m->sh_block_len = s.len;
		m->state = R_NXTBLOCK;
		continue;

	case R_EI:
		ffarr_setshift(&s, m->gbuf.ptr, m->blk_size, m->blk_off);
		if (0 != (r = mpc_ei_parse(m, &s)))
			return ERR(m, r);
		m->state = R_NXTBLOCK;
		continue;

	case R_RG:
		ffarr_setshift(&s, m->gbuf.ptr, m->blk_size, m->blk_off);
		if (0 != (r = mpc_rg_parse(m, &s)))
			return ERR(m, r);
		m->state = R_NXTBLOCK;
		continue;

	case R_AP:
		m->blk_apos += m->blk_samples;
		if (m->seek_sample != 0) {
			if (m->seek_sample >= m->blk_apos) {
				m->state = R_NXTBLOCK;
				continue;
			}
			m->seek_sample = 0;
		}
		ffarr_setshift(&m->block, m->gbuf.ptr, m->blk_size, m->blk_off);
		m->state = R_NXTBLOCK;
		return FFMPC_RBLOCK;

	case R_SO:
		/* SO block:
		varint ST_block_offset
		*/
		ffarr_setshift(&s, m->gbuf.ptr, m->blk_size, m->blk_off);
		if (-1 == (r = mpc_int(s.ptr, s.len, &m->ST_off)))
			return ERR(m, FFMPC_EBADSO);
		m->ST_off += m->off - m->blk_size;
		m->state = R_NXTBLOCK;
		continue;

	case R_ST_SEEK:
		m->gbuf.len = 0;
		m->off = m->ST_off;
		m->gsize = BLKHDR_MINSIZE;
		m->state = R_GATHER,  m->gstate = R_BLOCK_HDR_MIN;
		return FFMPC_RSEEK;

	case R_ST:
		if (!(m->options & FFMPC_O_SEEKTABLE)) {
			m->state = R_NXTBLOCK;
			continue;
		}

		ffarr_setshift(&s, m->gbuf.ptr, m->blk_size, m->blk_off);
		// use libmpc to parse ST block
		if (0 != (r = mpc_seekinit(&m->seekctx, m->sh_block, m->sh_block_len, s.ptr, s.len)))
			return ERR(m, r);

		if (m->options & FFMPC_O_APETAG) {
			m->state = R_TAGS_SEEK;
			continue;
		}

		m->state = R_AFRAME_SEEK;
		continue;

	case R_SE:
		return FFMPC_RDONE;


	case R_SEEK:
		m->off = mpc_getseekoff(m);
		m->gbuf.len = 0;
		m->state = R_FIND_AP;
		return FFMPC_RSEEK;

	case R_FIND_AP:
		if (0 != (r = mpc_findAP(m)))
			return r;

		m->gsize = BLKHDR_MINSIZE;
		m->state = R_GATHER,  m->gstate = R_BLOCK_HDR_MIN;
		continue;


	case R_TAGS_SEEK:
		m->state = R_GATHER,  m->gstate = R_APETAG;
		m->gsize = ffmin64(m->total_size, FTRTAGS_CHKSIZE);
		m->off = ffmin64(m->total_size, m->total_size - FTRTAGS_CHKSIZE);
		m->gbuf.len = 0;
		return FFMPC_RSEEK;

	case R_APETAG_MORE:
		ffarr_free(&m->gbuf);
		ffstr_set2(&m->gbuf, &m->input);
		m->state = R_APETAG;
		// break

	case R_APETAG:
		if (0 != (r = mpc_apetag(m, &m->gbuf)))
			return r;
		m->state = R_AFRAME_SEEK;
		continue;
	}
	}
}


const char* ffmpc_errstr(ffmpc *m)
{
	if (m->err < 0)
		return mpc_errstr(m->err);
	return _ffmpc_errstr(m->err);
}

int ffmpc_open(ffmpc *m, ffpcmex *fmt, const char *conf, size_t len)
{
	if (0 != (m->err = mpc_decode_open(&m->mpc, conf, len)))
		return -1;
	if (NULL == (m->pcm = ffmem_alloc(MPC_ABUF_CAP)))
		return m->err = FFMPC_ESYS,  -1;
	fmt->format = FFPCM_FLOAT;
	fmt->ileaved = 1;
	m->channels = fmt->channels;
	m->need_data = 1;
	return 0;
}

void ffmpc_close(ffmpc *m)
{
	ffmem_safefree(m->pcm);
	mpc_decode_free(m->mpc);
}

int ffmpc_decode(ffmpc *m)
{
	int r;

	if (m->need_data) {
		if (m->input.len == 0)
			return FFMPC_RMORE;
		m->need_data = 0;
		mpc_decode_input(m->mpc, m->input.ptr, m->input.len);
		m->input.len = 0;
	}

	m->pcmoff = 0;

	for (;;) {

		r = mpc_decode(m->mpc, m->pcm);
		if (r == 0) {
			m->need_data = 1;
			return FFMPC_RMORE;
		} else if (r < 0) {
			m->need_data = 1;
			return ERR(m, r);
		}

		m->cursample += r;
		if (m->seek_sample != 0) {
			if (m->seek_sample >= m->cursample)
				continue;
			uint64 oldpos = m->cursample - r;
			uint skip = ffmax((int64)(m->seek_sample - oldpos), 0);
			m->pcmoff = skip * m->channels * sizeof(float);
			r -= skip;
			m->seek_sample = 0;
		}
		break;
	}

	m->pcmlen = r * m->channels * sizeof(float);
	m->frsamples = r;
	return FFMPC_RDATA;
}
