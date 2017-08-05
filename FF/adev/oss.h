/** OSS wrapper.
Copyright (c) 2017 Simon Zolin
*/

#pragma once

#include <FF/audio/pcm.h>
#include <FFOS/mem.h>

#include <sys/soundcard.h>


FF_EXTN const char* ffoss_errstr(int e);

#define ffoss_init()  (0)
#define ffoss_uninit()


typedef struct ffoss_dev {
	char *id;
	char *name;
	oss_audioinfo ainfo;
	fffd mixer;
	uint ndevs;
	uint idx;
} ffoss_dev;

enum FFOSS_DEV {
	FFOSS_DEV_PLAYBACK = 1,
	FFOSS_DEV_CAPTURE = 2,
};

static FFINL void ffoss_devinit(ffoss_dev *d)
{
	ffmem_tzero(d);
	d->mixer = FF_BADFD;
}

FF_EXTN void ffoss_devdestroy(ffoss_dev *d);

/** Get next device.
@flags: enum FFOSS_DEV
Return 1 if no more devices. */
FF_EXTN int ffoss_devnext(ffoss_dev *d, uint flags);


typedef struct ffoss_buf {
	fffd fd;
	uint bufsize;
} ffoss_buf;

enum {
	FFOSS_EFMT = 0x10000,
};

/**
Return <0 on error.
 -FFOSS_EFMT: 'fmt' contains supported settings. */
FF_EXTN int ffoss_open(ffoss_buf *snd, const char *dev, ffpcm *fmt, uint bufsize_msec, uint flags);
FF_EXTN void ffoss_close(ffoss_buf *snd);

#define ffoss_bufsize(snd)  ((snd)->bufsize)

FF_EXTN size_t ffoss_filled(ffoss_buf *snd);

/**
@dataoff: input data offset (in bytes). */
FF_EXTN ssize_t ffoss_write(ffoss_buf *snd, const void *data, size_t len, size_t dataoff);

FF_EXTN int ffoss_start(ffoss_buf *snd);
FF_EXTN int ffoss_stop(ffoss_buf *snd);
FF_EXTN int ffoss_clear(ffoss_buf *snd);

/** Return 1 if stopped. */
FF_EXTN int ffoss_drain(ffoss_buf *snd);
