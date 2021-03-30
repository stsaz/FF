/** ff: .mp3 reader
2017,2021, Simon Zolin
*/

/*
ffmpg_fopen
ffmpg_fclose
ffmpg_read
ffmpg_ferrstr
ffmpg_seekoff
ffmpg_hdrok
*/

#include <FF/data/utf8.h>

/** MPEG file reader */
typedef struct ffmpgfile {
	ffuint state;
	ffuint err;
	ffmpgr rdr;

	union {
	ffid31ex id31tag;
	ffid3 id32tag;
	ffapetag apetag;
	};
	int tag;
	ffarr tagval;
	ffuint codepage; //codepage for non-Unicode meta tags
	ffarr buf;
	ffstr gbuf;
	ffuint gsize;

	ffuint options; //enum FFMPG_O
	ffuint is_id32tag :1
		, is_apetag :1
		;
} ffmpgfile;

/** Number of bytes at the end of file to check for ID3v1 and APE tag (initial check) */
#define _MPG_FTRTAGS_CHKSIZE  1000

static inline void ffmpg_fopen(ffmpgfile *m)
{
	ffid3_parseinit(&m->id32tag);
	ffmpg_rinit(&m->rdr);
}

static inline void ffmpg_fclose(ffmpgfile *m)
{
	ffarr_free(&m->tagval);
	if (m->is_apetag)
		ffapetag_parse_fin(&m->apetag);
	if (m->is_id32tag)
		ffid3_parsefin(&m->id32tag);
	ffarr_free(&m->buf);
	ffmpg_rclose(&m->rdr);
}

static inline const char* ffmpg_ferrstr(ffmpgfile *m)
{
	m->rdr.err = m->err;
	return ffmpg_rerrstr(&m->rdr);
}

/** Process ID3v1 at the end of input data */
static int _mp3read_id31(ffmpgfile *m, ffstr *data)
{
	if (data->len < sizeof(ffid31))
		return 0;

	int r = ffid31_parse(&m->id31tag, ffstr_end(data) - sizeof(ffid31), sizeof(ffid31));

	switch (r) {
	case FFID3_RNO:
		break;

	case FFID3_RDONE:
		data->len -= sizeof(ffid31);
		m->rdr.total_size -= sizeof(ffid31);
		m->rdr.off -= sizeof(ffid31);
		ffarr_free(&m->tagval);
		break;

	case FFID3_RDATA:
		m->tag = m->id31tag.field;
		if (m->codepage == 0) {
			ffarr_free(&m->tagval);
			ffarr_set(&m->tagval, m->id31tag.val.ptr, m->id31tag.val.len);

		} else if (0 == ffutf8_strencode(&m->tagval, m->id31tag.val.ptr, m->id31tag.val.len, m->codepage)) {
			m->err = FFMPG_EID31DATA;
			return FFMPG_RWARN;
		}
		return FFMPG_RID31;

	default:
		FF_ASSERT(0);
	}

	ffmem_tzero(&m->id31tag);
	return 0;
}

static int _mp3read_apetag(ffmpgfile *m, ffstr data)
{
	for (;;) {

		ffsize len = data.len;
		int r = ffapetag_parse(&m->apetag, data.ptr, &len);

		switch (r) {
		case FFAPETAG_RDONE:
		case FFAPETAG_RNO:
			m->is_apetag = 0;
			ffapetag_parse_fin(&m->apetag);
			return 0;

		case FFAPETAG_RFOOTER:
			m->is_apetag = 1;
			m->rdr.total_size -= m->apetag.size;
			continue;

		case FFAPETAG_RTAG:
			m->tag = m->apetag.tag;
			ffstr_set2(&m->tagval, &m->apetag.val);
			return FFMPG_RAPETAG;

		case FFAPETAG_RSEEK:
			m->rdr.off -= m->apetag.size;
			return FFMPG_RSEEK;

		case FFAPETAG_RMORE:
			return FFMPG_RMORE;

		case FFAPETAG_RERR:
			m->err = FFMPG_EAPETAG;
			return 0xbad;

		default:
			FF_ASSERT(0);
		}
	}
	//unreachable
}

static int _mp3read_id32(ffmpgfile *m, ffstr *input)
{
	int i;
	ffsize len;

	for (;;) {

		len = input->len;
		i = ffid3_parse(&m->id32tag, input->ptr, &len);
		ffstr_shift(input, len);

		switch (i) {
		case FFID3_RNO:
		case FFID3_RDONE:
			ffarr_free(&m->tagval);
			return 0;

		case FFID3_RMORE:
			return FFMPG_RMORE;

		case FFID3_RHDR:
			m->rdr.dataoff = sizeof(ffid3_hdr) + ffid3_size(&m->id32tag.h);
			continue;

		case FFID3_RFRAME:
			switch (m->id32tag.frame) {
			case FFMMTAG_PICTURE:
				m->id32tag.flags &= ~FFID3_FWHOLE;
				break;

			default:
				m->id32tag.flags |= FFID3_FWHOLE;
			}
			continue;

		case FFID3_RDATA:
			if (!(m->id32tag.flags & FFID3_FWHOLE))
				continue;

			m->tag = (m->id32tag.frame < _FFMMTAG_N) ? m->id32tag.frame : 0;
			if (0 > ffid3_getdata(m->id32tag.frame, m->id32tag.data.ptr, m->id32tag.data.len, m->id32tag.txtenc, m->codepage, &m->tagval)) {
				m->err = FFMPG_EID32DATA;
				return FFMPG_RWARN;
			}

			if (m->id32tag.frame == FFID3_LENGTH && m->id32tag.data.len != 0) {
				uint64 dur;
				if (m->id32tag.data.len == ffs_toint(m->id32tag.data.ptr, m->id32tag.data.len, &dur, FFS_INT64))
					m->rdr.total_len = dur;
			}
			return FFMPG_RID32;

		case FFID3_RERR:
			ffarr_free(&m->tagval);
			m->err = FFMPG_EID32;
			return 0xbad;

		default:
			FF_ASSERT(0);
		}
	}
	//unreachable
}

/* Read MPEG file:
 . parse ID3v2
 . parse ID3v1
 . parse APE tag
 . parse Xing, LAME tags
 . read frames...
*/
static inline int ffmpg_read(ffmpgfile *m, ffstr *input, ffstr *output)
{
	enum {
		I_START, I_FR,
		I_ID3V2, I_FTRTAGS_CHECK, I_FTRTAGS_GATHER, I_ID31, I_APE2, I_APE2_MORE, I_TAG_SKIP
	};
	int r;

	for (;;) {
		switch (m->state) {

		case I_START:
			m->rdr.options = m->options & FFMPG_O_NOXING;

			if (m->options & FFMPG_O_ID3V2) {
				m->is_id32tag = 1;
				m->state = I_ID3V2;
				continue;
			}
			m->state = I_FTRTAGS_CHECK;
			continue;

		case I_ID3V2:
			r = _mp3read_id32(m, input);
			switch (r) {
			case 0:
				break;

			case FFMPG_RMORE:
			case FFMPG_RWARN:
			case FFMPG_RID32:
				return r;

			case 0xbad:
				m->state = I_FTRTAGS_CHECK;
				return FFMPG_RWARN;
			}

			m->state = I_FTRTAGS_CHECK;
			continue;

		case I_FTRTAGS_CHECK:
			if (m->is_id32tag) {
				m->is_id32tag = 0;
				ffid3_parsefin(&m->id32tag);
				ffmem_tzero(&m->id32tag);
			}

			if (m->options & (FFMPG_O_ID3V1 | FFMPG_O_APETAG)) {
				m->state = I_FTRTAGS_GATHER;
				m->gsize = ffmin(_MPG_FTRTAGS_CHKSIZE, m->rdr.total_size);
				m->rdr.off = m->rdr.total_size - m->gsize;
				return FFMPG_RSEEK;
			}
			m->state = I_FR;
			continue;

		case I_FTRTAGS_GATHER:
			r = ffstr_gather((ffstr*)&m->buf, &m->buf.cap, input->ptr, input->len, m->gsize, &m->gbuf);
			if (r < 0) {
				m->err = FFMPG_ESYS;
				return FFMPG_RERR;
			}
			ffstr_shift(input, r);
			m->rdr.off += r;
			if (m->gbuf.len == 0) {
				return FFMPG_RMORE;
			}
			m->buf.len = 0;
			m->state = I_ID31;
			// fallthrough

		case I_ID31:
			if ((m->options & FFMPG_O_ID3V1)
				&& 0 != (r = _mp3read_id31(m, &m->gbuf)))
				return r;
			m->state = I_APE2;
			continue;

		case I_APE2_MORE:
			ffarr_free(&m->buf);
			m->gbuf = *input;
			m->state = I_APE2;
			// fallthrough

		case I_APE2:
			if (!(m->options & FFMPG_O_APETAG)) {
				m->state = I_TAG_SKIP;
				continue;
			}

			r = _mp3read_apetag(m, m->gbuf);
			switch (r) {
			case 0:
				m->state = I_TAG_SKIP;
				continue;

			case FFMPG_RMORE:
			case FFMPG_RSEEK:
				m->state = I_APE2_MORE;
				return r;

			case FFMPG_RAPETAG:
				return FFMPG_RAPETAG;

			case 0xbad:
				m->state = I_TAG_SKIP;
				return FFMPG_RWARN;
			}
			break;

		case I_TAG_SKIP:
			m->rdr.off = m->rdr.dataoff;
			m->state = I_FR;
			return FFMPG_RSEEK;


		case I_FR:
			r = ffmpg_readframe(&m->rdr, input, output);
			m->err = m->rdr.err;
			return r;
		}
	}
}

#undef _MPG_FTRTAGS_CHKSIZE

/** Get an absolute file offset to seek */
#define ffmpg_seekoff(m)  ((m)->rdr.off)

#define ffmpg_hdrok(m)  ((m)->rdr.fmt.channels != 0)
