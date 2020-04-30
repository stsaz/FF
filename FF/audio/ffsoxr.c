/**
Copyright (c) 2015 Simon Zolin
*/

#include <FF/audio/soxr.h>


static const byte soxr_fmts[2][3] = {
	{ SOXR_INT16_S, SOXR_INT32_S, SOXR_FLOAT32_S },
	{ SOXR_INT16_I, SOXR_INT32_I, SOXR_FLOAT32_I },
};

static int _ffsoxr_getfmt(int fmt, int ileaved)
{
	switch (fmt) {
	case FFPCM_16:
		return soxr_fmts[ileaved][0];
	case FFPCM_32:
		return soxr_fmts[ileaved][1];
	case FFPCM_FLOAT:
		return soxr_fmts[ileaved][2];
	}

	return -1;
}

int ffsoxr_create(ffsoxr *soxr, const ffpcmex *inpcm, const ffpcmex *outpcm)
{
	soxr_io_spec_t io;
	soxr_quality_spec_t qual;

	if (inpcm->channels != outpcm->channels)
		return -1;


	int itype = _ffsoxr_getfmt(inpcm->format, inpcm->ileaved);
	int otype = _ffsoxr_getfmt(outpcm->format, outpcm->ileaved);
	if (itype == -1 || otype == -1)
		return -1;
	io.itype = itype;
	io.otype = otype;
	io.scale = 1;
	io.e = NULL;
	io.flags = soxr->dither ? SOXR_TPDF : SOXR_NO_DITHER;

	qual = soxr_quality_spec(soxr->quality, SOXR_ROLLOFF_SMALL);

	soxr->soxr = soxr_create(inpcm->sample_rate, outpcm->sample_rate, inpcm->channels, &soxr->err
		, &io, &qual, NULL);
	if (soxr->err != NULL)
		return -1;

	soxr->isampsize = ffpcm_size1(inpcm);
	soxr->osampsize = ffpcm_size1(outpcm);
	soxr->outcap = outpcm->sample_rate;
	if (NULL == (soxr->out = ffmem_alloc(sizeof(void*) * outpcm->channels + soxr->outcap * soxr->osampsize)))
		return -1;
	if (!outpcm->ileaved) {
		ffstr s;
		ffstr_set(&s, (char*)soxr->out + sizeof(void*) * outpcm->channels, soxr->outcap * ffpcm_bits(outpcm->format) / 8);
		// soxr->outni = soxr->out;
		ffarrp_setbuf(soxr->outni, outpcm->channels, s.ptr, s.len);
	}
	soxr->in_ileaved = inpcm->ileaved;
	soxr->nchannels = inpcm->channels;
	return 0;
}

void ffsoxr_destroy(ffsoxr *soxr)
{
	ffmem_safefree(soxr->out);
	soxr_delete(soxr->soxr);
}

int ffsoxr_convert(ffsoxr *soxr)
{
	size_t idone, odone;
	uint i, fin = 0;
	void *in = soxr->in;
	const void *inarr[8];

	for (;;) {

		if (soxr->inlen == 0) {
			if (!soxr->fin) {
				soxr->outlen = 0;
				return 0;
			}
			in = NULL;
			fin = 1;

		} else if (soxr->in_ileaved)
			in = (char*)soxr->in_i + soxr->inoff * soxr->isampsize;

		else {
			for (i = 0;  i != soxr->nchannels;  i++) {
				inarr[i] = (char*)soxr->in[i] + soxr->inoff * soxr->isampsize / soxr->nchannels;
			}
			in = inarr;
		}

		soxr->err = soxr_process(soxr->soxr, in, soxr->inlen / soxr->isampsize, &idone
			, soxr->out, soxr->outcap, &odone);
		if (soxr->err != NULL)
			return -1;

		soxr->inoff += idone;
		soxr->inlen -= idone * soxr->isampsize;
		if (soxr->inlen == 0)
			soxr->inoff = 0;
		soxr->outlen = odone * soxr->osampsize;

		if (odone != 0 || fin)
			break;
	}
	return 0;
}
