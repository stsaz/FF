/**
Copyright (c) 2015 Simon Zolin
*/

#include <FF/audio/alsa.h>
#include <FF/string.h>
#include <FFOS/mem.h>

enum {
	ALSA_NFYRATE = 4,
};

static void _ffalsa_callback(snd_async_handler_t *ahandler);


int ffalsa_devnext(ffalsa_dev *d, uint flags)
{
	int e;
	snd_ctl_card_info_t *info;
	snd_ctl_t *sctl;
	char scard[32];

	snd_ctl_card_info_alloca(&info);

	for (;;) {

		if (0 != (e = snd_card_next(&d->sc)))
			return e;
		if (d->sc == -1)
			return 1;

		ffs_fmt(scard, scard + sizeof(scard), "hw:%u%Z", d->sc);
		if (0 != snd_ctl_open(&sctl, scard, 0))
			continue;

		if (0 != snd_ctl_card_info(sctl, info)) {
			snd_ctl_close(sctl);
			continue;
		}

		ffmem_safefree(d->name);
		d->name = ffsz_alcopyz(snd_ctl_card_info_get_name(info));

		ffs_fmt(d->id, d->id + sizeof(d->id), "plughw:%u,0%Z", snd_ctl_card_info_get_card(info));

		snd_ctl_close(sctl);
		break;
	}

	return 0;
}

void ffalsa_devdestroy(ffalsa_dev *d)
{
	ffmem_safefree(d->name);
}


int ffalsa_open(ffalsa_buf *snd, const char *dev, ffpcm *fmt, uint bufsize)
{
	snd_pcm_hw_params_t *params;
	uint rate, period;
	int e, format;

	switch (fmt->format) {
	case FFPCM_16LE:
		format = SND_PCM_FORMAT_S16_LE;
		break;
	case FFPCM_32LE:
		format = SND_PCM_FORMAT_S32_LE;
		break;
	case FFPCM_FLOAT:
		format = SND_PCM_FORMAT_FLOAT_LE;
		break;
	default:
		FF_ASSERT(0);
		return -EINVAL;
	}

	snd_pcm_hw_params_alloca(&params);

	if (dev == NULL || *dev == '\0')
		dev = "plughw:0,0";
	if (0 != (e = snd_pcm_open(&snd->pcm, dev, SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK)))
		return e;

	if (0 != (e = snd_pcm_hw_params_any(snd->pcm, params)))
		goto fail;
	if (0 != (e = snd_pcm_hw_params_set_access(snd->pcm, params, SND_PCM_ACCESS_MMAP_NONINTERLEAVED)))
		goto fail;
	if (0 != (e = snd_pcm_hw_params_set_format(snd->pcm, params, format)))
		goto fail;
	if (0 != (e = snd_pcm_hw_params_set_channels(snd->pcm, params, fmt->channels)))
		goto fail;

	rate = fmt->sample_rate;
	if (0 != (e = snd_pcm_hw_params_set_rate_near(snd->pcm, params, &rate, 0)))
		goto fail;

	bufsize *= 1000; //nsec
	if (0 != (e = snd_pcm_hw_params_set_buffer_time_near(snd->pcm, params, &bufsize, NULL)))
		goto fail;

	period = bufsize / ALSA_NFYRATE;
	if (0 != (e = snd_pcm_hw_params_set_period_time_near(snd->pcm, params, &period, NULL)))
		goto fail;

	if (0 != (e = snd_pcm_hw_params(snd->pcm, params)))
		goto fail;

	if (0 != (e = snd_async_add_pcm_handler(&snd->ahandler, snd->pcm, &_ffalsa_callback, snd)))
		goto fail;

	snd->frsize = ffpcm_size(fmt->format, fmt->channels);
	snd->bufsize = ffpcm_bytes(fmt, bufsize / 1000);
	snd->channels = fmt->channels;
	snd->width = snd->frsize / fmt->channels;
	return 0;

fail:
	ffalsa_close(snd);
	return e;
}

void ffalsa_close(ffalsa_buf *snd)
{
	FF_SAFECLOSE(snd->pcm, NULL, snd_pcm_close);
}

int ffalsa_start(ffalsa_buf *snd)
{
	int r;

	r = snd_pcm_state(snd->pcm);
	if (r == SND_PCM_STATE_RUNNING)
		return 0;

	if (r == SND_PCM_STATE_PAUSED) {
		if (0 != (r = snd_pcm_pause(snd->pcm, 0)))
			return r;

	} else {
		if (0 != (r = snd_pcm_start(snd->pcm)))
			return r;
	}
	return 0;
}

int ffalsa_stop(ffalsa_buf *snd)
{
	int r, st;

	st = snd_pcm_state(snd->pcm);
	if (st != SND_PCM_STATE_RUNNING)
		return 0;

	if (0 != (r = snd_pcm_pause(snd->pcm, 1)))
		return r;
	return 0;
}

int ffalsa_clear(ffalsa_buf *snd)
{
	snd_pcm_reset(snd->pcm);
	snd->silence = 0;
	return 0;
}

int ffalsa_stoplazy(ffalsa_buf *snd)
{
	int r;
	if (0 == ffalsa_filled(snd))
		return 1;

	if (!snd->silence) {
		if (0 > (r = ffalsa_silence(snd)))
			return r;
		snd->silence = 1;
	}
	return 0;
}

/* SIGIO handler */
static void _ffalsa_callback(snd_async_handler_t *ahandler)
{
	ffalsa_buf *snd = snd_async_handler_get_callback_private(ahandler);
	if (!snd->callback_wait) {
		snd->callback = 1;
		return;
	}
	snd->callback_wait = 0;
	snd->handler(snd->udata);
}

int ffalsa_async(ffalsa_buf *snd, uint enable)
{
	if (!enable) {
		snd->callback_wait = snd->callback = 0;
		return 0;
	}

	if (snd->callback) {
		snd->callback = 0;
		snd->handler(snd->udata);
		return 1;
	}

	snd->callback_wait = 1;
	return 0;
}

ssize_t ffalsa_write(ffalsa_buf *snd, const void *data, size_t len, size_t dataoff)
{
	const snd_pcm_channel_area_t *areas;
	snd_pcm_uframes_t off, frames;
	snd_pcm_sframes_t wr;
	size_t inframes, inoff = dataoff / snd->channels;
	int e;
	uint i;
	void **datani = (void**)data;

	FF_ASSERT(len % snd->frsize == 0);

	inframes = len / snd->frsize;

	while (inframes != 0) {

		wr = snd_pcm_avail_update(snd->pcm); //needed for snd_pcm_mmap_begin()
		if (wr < 0)
			goto err;

		frames = inframes;
		if (0 != (e = snd_pcm_mmap_begin(snd->pcm, &areas, &off, &frames)))
			return e;

		if (frames == 0) {
			if (snd->autostart && 0 != (e = ffalsa_start(snd)))
				return e;
			break;
		}

		for (i = 0;  i != snd->channels;  i++) {
			ffmemcpy((char*)areas[i].addr + off * areas[i].step/8, (char*)datani[i] + inoff, frames * snd->width);
		}
		inoff += frames * snd->width;

		wr = snd_pcm_mmap_commit(snd->pcm, off, frames);
		if (wr >= 0 && (snd_pcm_uframes_t)wr != frames)
			wr = -EPIPE;

		if (wr < 0) {
err:
			wr = snd_pcm_recover(snd->pcm, wr, 1 /*silent*/);
			if (wr < 0)
				return wr;
			continue;
		}

		inframes -= frames;
	}

	return len - inframes * snd->frsize;
}

int ffalsa_silence(ffalsa_buf *snd)
{
	const snd_pcm_channel_area_t *areas;
	snd_pcm_uframes_t off, frames;
	snd_pcm_sframes_t wr;
	int r, all = 0;
	uint i;

	for (;;) {

		wr = snd_pcm_avail_update(snd->pcm); //needed for snd_pcm_mmap_begin()
		if (wr < 0)
			goto err;

		frames = wr;
		if (0 != (r = snd_pcm_mmap_begin(snd->pcm, &areas, &off, &frames)))
			return r;

		if (frames == 0) {
			if (snd->autostart && 0 != (r = ffalsa_start(snd)))
				return r;
			break;
		}

		for (i = 0;  i != snd->channels;  i++) {
			ffmem_zero((char*)areas[i].addr + off * areas[i].step/8, frames * snd->width);
		}

		wr = snd_pcm_mmap_commit(snd->pcm, off, frames);
		if (wr >= 0 && (snd_pcm_uframes_t)wr != frames)
			wr = -EPIPE;

		if (wr < 0) {
err:
			wr = snd_pcm_recover(snd->pcm, wr, 1 /*silent*/);
			if (wr < 0)
				return wr;
			continue;
		}

		all += frames;
	}

	return all * snd->frsize;
}
