/** CoreAudio wrapper.
Copyright (c) 2018 Simon Zolin
*/

#pragma once

#include <FF/array.h>
#include <FF/audio/pcm.h>


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

/** Get next device.
@flags: enum FFCORAUD_DEV
Return 1 if no more devices. */
FF_EXTN int ffcoraud_devnext(ffcoraud_dev *d, uint flags);
FF_EXTN void ffcoraud_devdestroy(ffcoraud_dev *d);


typedef struct ffcoraud_buf {
	uint dev;
	void *aprocid;
	ffarr data;
} ffcoraud_buf;

enum FFCORAUD_E {
	FFCORAUD_EFMT = 0x10000,
};

/**
Return 0 on success.
 FFCORAUD_EFMT: 'fmt' contains supported settings.
*/
FF_EXTN int ffcoraud_open(ffcoraud_buf *snd, int dev, ffpcm *fmt, uint bufsize_msec);

FF_EXTN void ffcoraud_close(ffcoraud_buf *snd);

/** Add data to a playback buffer.
Return the number of bytes written;  <0 on error. */
FF_EXTN ssize_t ffcoraud_write(ffcoraud_buf *snd, const void *data, size_t len);

FF_EXTN int ffcoraud_start(ffcoraud_buf *snd);

FF_EXTN int ffcoraud_stop(ffcoraud_buf *snd);
