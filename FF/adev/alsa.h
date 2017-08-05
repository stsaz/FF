/** ALSA wrapper.
Copyright (c) 2015 Simon Zolin
*/

#pragma once

#include <FF/audio/pcm.h>
#include <FFOS/mem.h>
#include <FFOS/file.h>

#include <alsa/asoundlib.h>


FF_EXTN const char* ffalsa_errstr(int e);


/**
@kq: (optional) kqueue for receiving SIGIO events. */
FF_EXTN int ffalsa_init(fffd kq);
FF_EXTN void ffalsa_uninit(fffd kq);


typedef struct ffalsa_dev {
	uint st;
	int sc;
	int idev;
	snd_ctl_t *sctl;
	snd_ctl_card_info_t *scinfo;

	char id[32]; //device ID that can be passed to ffalsa_open()
	char *name;
} ffalsa_dev;

/** "plughw:0,0" -> "hw:0,0" */
#define FFALSA_DEVID_HW(id)  ((id) + FFSLEN("plug"))

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
	uint nfy_interval; //device notify interval (in samples)

	snd_pcm_uframes_t off
		, frames;

	const char *errfunc;

	uint callback :1 //set when audio event has been received but user isn't expecting it
		, callback_wait :1 //set while user is expecting an event from kernel
		, capture :1
		, silence :1
		, autostart :1 //start automatically when the buffer is full
		, ileaved :1
		;
} ffalsa_buf;

enum {
	FFALSA_EFMT = 0x10000,
};

/**
Return <0 on error.
 -FFALSA_EFMT: 'fmt' contains supported settings.
  For "hw" device this may indicate that "plughw" should be used. */
FF_EXTN int ffalsa_open(ffalsa_buf *snd, const char *dev, ffpcmex *fmt, uint bufsize_msec);

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


static FFINL int ffalsa_capt_open(ffalsa_buf *snd, const char *dev, ffpcmex *fmt, uint bufsize_msec)
{
	snd->capture = 1;
	return ffalsa_open(snd, dev, fmt, bufsize_msec);
}

#define ffalsa_capt_close  ffalsa_close

/**
Interleaved: only data[0] is set. */
FF_EXTN int ffalsa_capt_read(ffalsa_buf *snd, void **data, size_t *len);
