/** Pulse Audio wrapper.
Copyright (c) 2017 Simon Zolin
*/

#pragma once

#include <FF/audio/pcm.h>
#include <FFOS/mem.h>
#include <FFOS/timer.h>
#include <pulse/pulseaudio.h>


/**
Note: libpulse's code calls _exit() when it fails to allocate a memory buffer (/src/pulse/xmalloc.c). */
FF_EXTN const char* ffpulse_errstr(int e);

FF_EXTN int ffpulse_init(fffd kq);
FF_EXTN void ffpulse_uninit();


typedef struct ffpulse_dev {
	pa_operation *op;
	char *id;
	char *name;
	uint err :1;
} ffpulse_dev;

enum FFPULSE_DEV {
	FFPULSE_DEV_PLAYBACK = 1,
	FFPULSE_DEV_CAPTURE = 2,
};

static FFINL void ffpulse_devinit(ffpulse_dev *d)
{
	ffmem_tzero(d);
}

FF_EXTN void ffpulse_devdestroy(ffpulse_dev *d);

/** Get next device.
@flags: enum FFPULSE_DEV
Return 1 if no more devices. */
FF_EXTN int ffpulse_devnext(ffpulse_dev *d, uint flags);


typedef void (*ffpulse_handler)(void *udata);

typedef struct ffpulse_buf {
	pa_stream *stm;
	uint bufsize;
	uint nfy_interval; //minimum interval between calls to user handler function (msec)
	uint autostart :1; //auto-start processing when audio buffer is full
	uint callback :1; //set when audio event has been received but user isn't expecting it
	uint callback_wait :1; //set while user is expecting an event
	uint draining :1;

	ffkevent evtmr;
	fftmr tmr;

	ffpulse_handler handler;
	void *udata;
} ffpulse_buf;

FF_EXTN int ffpulse_open(ffpulse_buf *snd, const char *dev, ffpcm *fmt, uint bufsize_msec);
FF_EXTN void ffpulse_close(ffpulse_buf *snd);

#define ffpulse_bufsize(snd)  ((snd)->bufsize)

FF_EXTN size_t ffpulse_filled(ffpulse_buf *snd);

/**
@dataoff: input data offset (in bytes). */
FF_EXTN ssize_t ffpulse_write(ffpulse_buf *snd, const void *data, size_t len, size_t dataoff);

/** Allow user handler function to be called. */
FF_EXTN int ffpulse_async(ffpulse_buf *snd, uint enable);

FF_EXTN int ffpulse_start(ffpulse_buf *snd);
FF_EXTN int ffpulse_stop(ffpulse_buf *snd);
FF_EXTN int ffpulse_clear(ffpulse_buf *snd);

/** Return 1 if stopped. */
FF_EXTN int ffpulse_drain(ffpulse_buf *snd);
