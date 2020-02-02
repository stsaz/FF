/** JACK wrapper.
Copyright (c) 2020 Simon Zolin
*/

#pragma once

#include <FF/audio/pcm.h>
#include <FF/ring.h>


FF_EXTN const char* ffjack_errstr(int e);

/**
User should call fflk_setup(). */
FF_EXTN int ffjack_init(const char *appname);
FF_EXTN void ffjack_uninit();


typedef struct ffjack_dev {
	const char **names;
	uint idx;
} ffjack_dev;

enum FFJACK_DEV {
	FFJACK_DEV_PLAYBACK = 1,
	FFJACK_DEV_CAPTURE = 2,
};

static FFINL void ffjack_devinit(ffjack_dev *d)
{
	ffmem_tzero(d);
}

FF_EXTN void ffjack_devdestroy(ffjack_dev *d);

/** Get next device.
@flags: enum FFJACK_DEV
Return 1 if no more devices. */
FF_EXTN int ffjack_devnext(ffjack_dev *d, uint flags);

#define ffjack_devname(d)  ((d)->names[(d)->idx])


typedef void (*ffjack_handler)(void *udata);

typedef struct ffjack_buf {
    void *port; // jack_port_t*
	ffringbuf buf;
	ffstr bufdata;
	ffstr data; // holds data read from capture buffer
	uint started :1;
	uint signalled :1; //set when audio event has been received but user isn't expecting it
	uint callback_wait :1; //set while user is expecting an event
	uint overrun :1;
	uint shut :1;

	ffjack_handler handler;
	void *udata;
} ffjack_buf;

// public error codes
enum {
	FFJACK_EFMT = 1,
	_FFJACK_EMAX,
};

/**
@flags: enum FFJACK_DEV
Return error code
 FFJACK_EFMT: 'fmt' contains supported settings. */
FF_EXTN int ffjack_open(ffjack_buf *snd, const char *dev, ffpcm *fmt, uint flags);
FF_EXTN void ffjack_close(ffjack_buf *snd);

/** Return total buffer size (in bytes). */
static inline uint ffjack_bufsize(ffjack_buf *snd)
{
	return snd->data.len;
}

/** Get overrun flag value and reset it. */
static inline int ffjack_overrun(ffjack_buf *snd)
{
	int r = snd->overrun;
	snd->overrun = 0;
	return r;
}

/** Allow user handler function to be called.
flags: 1:enable notification;  0:disable notification
Return 1 if user handler function is called. */
FF_EXTN int ffjack_async(ffjack_buf *snd, uint flags);

FF_EXTN int ffjack_start(ffjack_buf *snd);
FF_EXTN int ffjack_stop(ffjack_buf *snd);

/** Read from capture buffer.
data: reference to internal audio buffer (interleaved) (valid until the next call to ffjack_read());
 if data->len==0: no more data is available at the moment
Return error code. */
FF_EXTN int ffjack_read(ffjack_buf *snd, ffstr *data);
