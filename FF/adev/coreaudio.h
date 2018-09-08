/** CoreAudio wrapper.
Copyright (c) 2018 Simon Zolin
*/

#pragma once

#include <FF/string.h>
#include <FF/audio/pcm.h>
#include <FF/ring.h>


FF_EXTN const char* ffcoraud_errstr(int e);


typedef struct ffcoraud_dev {
	uint idev;
	uint ndev;
	void *devs;
	char *name;
} ffcoraud_dev;

enum FFCORAUD_DEV {
	FFCORAUD_DEV_PLAYBACK = 1,
	FFCORAUD_DEV_CAPTURE = 2,
};

static FFINL void ffcoraud_devinit(ffcoraud_dev *d)
{
	ffmem_tzero(d);
}

/** Get device name. */
#define ffcoraud_dev_name(d)  ((d)->name)

/** Get the current device's ID. */
FF_EXTN int ffcoraud_dev_id(ffcoraud_dev *d);

/** Get next device.
@flags: enum FFCORAUD_DEV
Return 1 if no more devices. */
FF_EXTN int ffcoraud_devnext(ffcoraud_dev *d, uint flags);
FF_EXTN void ffcoraud_devdestroy(ffcoraud_dev *d);


/** Audio buffer. */
typedef struct ffcoraud_buf {
	uint dev;
	void *aprocid;
	ffstr data;
	ffstr data2; // holds data read from capture buffer
	ffringbuf buf;
	uint autostart :1; //start I/O automatically when the buffer is full
	uint overrun :1;
} ffcoraud_buf;

enum FFCORAUD_E {
	FFCORAUD_EFMT = 0x10000, // the requested format isn't supported
};

/** Open audio buffer.
dev: ffcoraud_dev_id() or -1 (default device)
fmt: audio format (interleaved)
flags: enum FFCORAUD_DEV
Return 0 on success.
 FFCORAUD_EFMT: 'fmt' contains supported settings.
*/
FF_EXTN int ffcoraud_open(ffcoraud_buf *snd, int dev, ffpcm *fmt, uint bufsize_msec, uint flags);

FF_EXTN void ffcoraud_close(ffcoraud_buf *snd);

/** Add data to a playback buffer.
Return the number of bytes written;  <0 on error. */
FF_EXTN int ffcoraud_write(ffcoraud_buf *snd, const void *data, size_t len);

/** Read data from a capture buffer.
Return 0 if no more data;  <0 on error. */
FF_EXTN int ffcoraud_read(ffcoraud_buf *snd, ffstr *data);

/** Start buffer I/O. */
FF_EXTN int ffcoraud_start(ffcoraud_buf *snd);

/** Stop buffer I/O. */
FF_EXTN int ffcoraud_stop(ffcoraud_buf *snd);

/** Stop when the buffer becomes empty.
Return 1 if stopped. */
FF_EXTN int ffcoraud_stoplazy(ffcoraud_buf *snd);

/** Return the number of bytes left in sound buffer. */
FF_EXTN int ffcoraud_filled(ffcoraud_buf *snd);

/** Return total buffer size (in bytes). */
static inline uint ffcoraud_bufsize(ffcoraud_buf *snd)
{
	return snd->data.len;
}

/** Clear buffer. */
FF_EXTN void ffcoraud_clear(ffcoraud_buf *snd);
