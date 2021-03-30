/**
Copyright (c) 2013 Simon Zolin
*/

#include <FF/aformat/mp3.h>
#include <FF/string.h>
#include <FF/number.h>
#include <FFOS/error.h>


enum {
	MPG_FTRTAGS_CHKSIZE = 1000, //number of bytes at the end of file to check for ID3v1 and APE tag (initial check)
};

enum {
	CPY_START, CPY_GATHER,
	CPY_ID32_HDR, CPY_ID32_DATA,
	CPY_DATA, CPY_FR1, CPY_FR, CPY_FR_OUT,
	CPY_FTRTAGS_SEEK, CPY_FTRTAGS, CPY_FTRTAGS_OUT,
};

void ffmpg_copy_close(ffmpgcopy *m)
{
	ffarr_free(&m->buf);
	ffmpg_rclose(&m->rdr);
	ffmpg_wclose(&m->writer);
}

/* MPEG copy:
. Read and return ID3v2 (FFMPG_RID32)
. Read the first frame (FFMPG_RHDR)
  User may call ffmpg_copy_seek() now.
. If input is seekable:
  . Seek input to the end (FFMPG_RSEEK), read ID3v1
  . Seek input to the needed audio position (FFMPG_RSEEK)
. Return empty Xing frame (FFMPG_RFRAME)
. Read and return MPEG frames (FFMPG_RFRAME) until:
  . User calls ffmpg_copy_fin()
  . Or the end of audio data is reached
. Return ID3v1 (FFMPG_RID31)
. Seek output to Xing tag offset (FFMPG_ROUTSEEK)
. Write the complete Xing tag (FFMPG_RFRAME)
*/
int ffmpg_copy(ffmpgcopy *m, ffstr *input, ffstr *output)
{
	int r;
	ffstr fr;

	for (;;) {
	switch (m->state) {

	case CPY_START:
		ffmpg_rinit(&m->rdr);

		ffmpg_winit(&m->writer);
		m->writer.options = m->options & FFMPG_WRITE_XING;

		if (m->options & FFMPG_WRITE_ID3V2) {
			m->gsize = sizeof(ffid3_hdr);
			m->state = CPY_GATHER,  m->gstate = CPY_ID32_HDR;
			continue;
		}
		m->state = CPY_DATA,  m->gstate = CPY_FR1;
		continue;

	case CPY_GATHER:
		r = ffarr_append_until(&m->buf, input->ptr, input->len, m->gsize);
		switch (r) {
		case 0:
			return FFMPG_RMORE;
		case -1:
			return m->rdr.err = FFMPG_ESYS,  FFMPG_RERR;
		}
		ffstr_shift(input, r);
		m->state = m->gstate;
		continue;


	case CPY_ID32_HDR:
		if (ffid3_valid((void*)m->buf.ptr)) {
			m->gsize = ffid3_size((void*)m->buf.ptr);
			m->rdr.dataoff = sizeof(ffid3_hdr) + m->gsize;
			m->rdr.off += m->rdr.dataoff;
			m->wdataoff = m->rdr.dataoff;
			m->state = CPY_ID32_DATA;
			ffstr_set2(output, &m->buf);
			m->buf.len = 0;
			return FFMPG_RID32;
		}

		m->rdr.dataoff = 0;
		m->wdataoff = 0;
		ffstr_set(&m->rinput, m->buf.ptr, m->buf.len);
		m->buf.len = 0;
		m->state = CPY_FR1;
		continue;

	case CPY_ID32_DATA:
		if (m->gsize == 0) {
			m->state = CPY_DATA,  m->gstate = CPY_FR1;
			continue;
		}

		if (input->len == 0)
			return FFMPG_RMORE;

		r = ffmin(input->len, m->gsize);
		m->gsize -= r;
		ffstr_set(output, input->ptr, r);
		ffstr_shift(input, r);
		return FFMPG_RID32;


	case CPY_FTRTAGS_SEEK:
		if (m->rdr.total_size == 0) {
			m->state = CPY_DATA,  m->gstate = CPY_FR;
			continue;
		}

		m->gsize = ffmin64(MPG_FTRTAGS_CHKSIZE, m->rdr.total_size);
		m->state = CPY_GATHER,  m->gstate = CPY_FTRTAGS;
		m->off = ffmin64(m->rdr.total_size - MPG_FTRTAGS_CHKSIZE, m->rdr.total_size);
		return FFMPG_RSEEK;

	case CPY_FTRTAGS: {
		const void *h = m->buf.ptr + m->buf.len - sizeof(ffid31);
		if (m->buf.len >= sizeof(ffid31) && ffid31_valid(h)) {
			if (m->options & FFMPG_WRITE_ID3V1)
				ffmemcpy(&m->id31, h, sizeof(ffid31));
			m->rdr.total_size -= sizeof(ffid31);
		}

		m->state = CPY_DATA,  m->gstate = CPY_FR;
		m->buf.len = 0;
		m->off = _mpgr_getseekoff(&m->rdr);
		return FFMPG_RSEEK;
	}

	case CPY_FTRTAGS_OUT:
		m->writer.fin = 1;
		m->state = CPY_FR_OUT;
		if (m->id31.tag[0] != '\0') {
			ffstr_set(output, &m->id31, sizeof(ffid31));
			return FFMPG_RID31;
		}
		continue;


	case CPY_DATA:
		if (input->len == 0)
			return FFMPG_RMORE;
		ffstr_set(&m->rinput, input->ptr, input->len);
		input->len = 0;
		m->state = m->gstate;
		continue;

	case CPY_FR:
	case CPY_FR1:
		r = ffmpg_readframe(&m->rdr, &m->rinput, &fr);
		switch (r) {

		case FFMPG_RMORE:
			m->gstate = m->state;
			m->state = CPY_DATA;
			continue;

		case FFMPG_RSEEK:
			m->off = m->rdr.off;
			m->gstate = m->state;
			m->state = CPY_DATA;
			return FFMPG_RSEEK;

		case FFMPG_RDONE:
			m->state = CPY_FTRTAGS_OUT;
			continue;

		case FFMPG_RXING:
			m->writer.xing.vbr = m->rdr.xing.vbr;
			return FFMPG_RXING;

		case FFMPG_RHDR:
			m->state = CPY_FTRTAGS_SEEK;
			return FFMPG_RHDR;

		case FFMPG_RFRAME:
			m->state = CPY_FR_OUT;
			continue;
		}
		return r;

	case CPY_FR_OUT:
		r = ffmpg_writeframe(&m->writer, fr.ptr, fr.len, output);
		switch (r) {

		case FFMPG_RDONE:
			return FFMPG_RDONE;

		case FFMPG_RDATA:
			if (!m->writer.fin)
				m->state = CPY_FR;
			return FFMPG_RFRAME;

		case FFMPG_RERR:
			m->rdr.err = FFMPG_ESYS;
			return FFMPG_RERR;

		case FFMPG_RSEEK:
			m->off = m->wdataoff + ffmpg_wseekoff(&m->writer);
			return FFMPG_ROUTSEEK;
		}
		FF_ASSERT(0);
		return FFMPG_RERR;

	}
	}
}

void ffmpg_copy_seek(ffmpgcopy *m, uint64 sample)
{
	ffmpg_rseek(&m->rdr, sample);
}

void ffmpg_copy_fin(ffmpgcopy *m)
{
	m->state = CPY_FTRTAGS_OUT;
}
