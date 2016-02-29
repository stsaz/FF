/** ALSA wrapper.
Copyright (c) 2015 Simon Zolin
*/

#pragma once

#include <FF/audio/pcm.h>
#include <FFOS/mem.h>

#include <alsa/asoundlib.h>


#define ffalsa_errstr  snd_strerror


static FFINL int ffalsa_init(void)
{
	return 0;
}

#define ffalsa_uninit()


typedef struct ffalsa_dev {
	int sc;

	char id[32]; //device ID that can be passed to ffalsa_open()
	char *name;
} ffalsa_dev;

enum FFALSA_DEV {
	FFALSA_DEV_PLAYBACK = 1,
	FFALSA_DEV_CAPTURE = 2,
};

static FFINL void ffalsa_devinit(ffalsa_dev *dev)
{
	ffmem_tzero(dev);
	dev->sc = -1;
}

/** Get next device.
@flags: enum FFALSA_DEV
Return 1 if no more devices. */
FF_EXTN int ffalsa_devnext(ffalsa_dev *dev, uint flags);

FF_EXTN void ffalsa_devdestroy(ffalsa_dev *dev);


typedef void (*ffalsa_handler)(void *udata);

typedef struct ffalsa_buf {
	ffalsa_handler handler;
	void *udata;

	snd_pcm_t *pcm;
	snd_async_handler_t *ahandler;
	uint frsize;
	uint bufsize;
	uint width;
	uint channels;

	snd_pcm_uframes_t off
		, frames;

	uint callback :1
		, callback_wait :1
		, capture :1
		, silence :1
		, autostart :1 //start automatically when the buffer is full
		;
} ffalsa_buf;

FF_EXTN int ffalsa_open(ffalsa_buf *snd, const char *dev, ffpcm *fmt, uint bufsize_msec);

FF_EXTN void ffalsa_close(ffalsa_buf *snd);

static FFINL size_t ffalsa_bufsize(ffalsa_buf *snd)
{
	return snd->bufsize;
}

static FFINL size_t ffalsa_filled(ffalsa_buf *snd)
{
	int n = snd_pcm_avail_update(snd->pcm);
	if (n < 0)
		return 0;
	return snd->bufsize - n * snd->frsize;
}

/**
@dataoff: input data offset (in bytes). */
FF_EXTN ssize_t ffalsa_write(ffalsa_buf *snd, const void *data, size_t len, size_t dataoff);

/** Write silence into sound buffer.
Return the number of bytes written. */
FF_EXTN int ffalsa_silence(ffalsa_buf *snd);

/** Enter asynchronous mode - call user handler function.
Return 1 if user handler function has been called. */
FF_EXTN int ffalsa_async(ffalsa_buf *snd, uint enable);

FF_EXTN int ffalsa_start(ffalsa_buf *snd);

FF_EXTN int ffalsa_stop(ffalsa_buf *snd);

FF_EXTN int ffalsa_clear(ffalsa_buf *snd);

/** Return 1 if stopped. */
FF_EXTN int ffalsa_stoplazy(ffalsa_buf *snd);


static FFINL int ffalsa_capt_open(ffalsa_buf *snd, const char *dev, ffpcm *fmt, uint bufsize_msec)
{
	snd->capture = 1;
	return ffalsa_open(snd, dev, fmt, bufsize_msec);
}

#define ffalsa_capt_close  ffalsa_close

FF_EXTN int ffalsa_capt_read(ffalsa_buf *snd, void **data, size_t *len);
