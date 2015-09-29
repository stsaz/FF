/**
Copyright (c) 2015 Simon Zolin
*/

#include <FF/audio/soxr.h>
#include <FF/string.h>


static const byte soxr_fmts[2][3] = {
	{ SOXR_INT16_S, SOXR_INT32_S, SOXR_FLOAT32_S },
	{ SOXR_INT16_I, SOXR_INT32_I, SOXR_FLOAT32_I },
};

int ffsoxr_create(ffsoxr *soxr, const ffpcmex *inpcm, const ffpcmex *outpcm)
{
	soxr_io_spec_t io;
	soxr_quality_spec_t qual;

	if (inpcm->channels != outpcm->channels
		|| !outpcm->ileaved)
		return -1;

	io.itype = soxr_fmts[inpcm->ileaved][inpcm->format];
	io.otype = soxr_fmts[outpcm->ileaved][outpcm->format];
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
	if (NULL == (soxr->out = ffmem_alloc(soxr->outcap * soxr->osampsize)))
		return -1;
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
	uint i;
	void *in = soxr->in;

	if (soxr->inlen == 0) {
		soxr->outlen = 0;
		return 0;
	}

	for (i = 0;  i != 2;  i++) {
		soxr->err = soxr_process(soxr->soxr, in, soxr->inlen / soxr->isampsize, &idone
			, soxr->out, soxr->outcap, &odone);
		if (soxr->err != NULL)
			return -1;

		if (soxr->in_ileaved)
			soxr->in_i = (char*)soxr->in_i + idone * soxr->isampsize;
		else if (idone != 0)
			ffarrp_shift((void**)soxr->in, soxr->nchannels, idone * soxr->isampsize / soxr->nchannels);
		soxr->inlen -= idone * soxr->isampsize;
		soxr->outlen = odone * soxr->osampsize;

		if (odone != 0 || !soxr->fin)
			break;

		in = NULL;
	}
	return 0;
}