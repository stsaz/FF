/** Windows Direct Sound.
Copyright (c) 2015 Simon Zolin
*/

#pragma once

#include <FF/audio/pcm.h>
#include <FFOS/error.h>

#include <dsound.h>


#define ffdsnd_errstr(e)  fferr_strp(e)

FF_EXTN int ffdsnd_init(void);
FF_EXTN void ffdsnd_uninit(void);


struct ffdsnd_devenum;
struct ffdsnd_devenum {
	struct ffdsnd_devenum *next;
	GUID *id
		, guid;
	char name[0];
};

enum FFDSND_DEV {
	FFDSND_DEV_RENDER = 1
	, FFDSND_DEV_CAPTURE = 2
};

/** List all available devices.
@flags: enum FFDSND_DEV
Return 0 on success. */
FF_EXTN int ffdsnd_devenum(struct ffdsnd_devenum **head, uint flags);
FF_EXTN void ffdsnd_devenumfree(struct ffdsnd_devenum *head);


typedef void (*ffdsnd_handler)(void *udata);

typedef struct ffdsnd_buf {
	ffdsnd_handler handler;
	void *udata;

	IDirectSound8 *dev;
	IDirectSoundBuffer *buf;
	HANDLE evtnotify;
	uint bufsize;
	uint wpos
		, rpos;
	unsigned isfull :1
		, underflow :1
		, last :1
		;
} ffdsnd_buf;

/** Open device for playback.
@dev_id: NULL for a default device. */
FF_EXTN int ffdsnd_open(ffdsnd_buf *ds, const GUID *dev_id, ffpcm *fmt, uint bufsize_msec);

FF_EXTN void ffdsnd_close(ffdsnd_buf *ds);

/** Return the number of bytes left in sound buffer. */
FF_EXTN uint ffdsnd_filled(ffdsnd_buf *ds);

/** Write data into sound buffer.
Return the number of bytes written. */
FF_EXTN int ffdsnd_write(ffdsnd_buf *ds, const void *data, size_t len);

FF_EXTN void ffdsnd_clear(ffdsnd_buf *ds);

/** Write silence into sound buffer. */
FF_EXTN int ffdsnd_silence(ffdsnd_buf *ds);

static FFINL int ffdsnd_start(ffdsnd_buf *ds)
{
	return IDirectSoundBuffer_Play(ds->buf, 0, 0, 0 /*DSBPLAY_LOOPING*/);
}

static FFINL int ffdsnd_pause(ffdsnd_buf *ds)
{
	return IDirectSoundBuffer_Stop(ds->buf);
}

/** Return 1 if stopped. */
FF_EXTN int ffdsnd_stoplazy(ffdsnd_buf *ds);


typedef struct ffdsnd_capt {
	ffdsnd_handler handler;
	void *udata;

	IDirectSoundCapture *dev;
	IDirectSoundCaptureBuffer *buf;
	HANDLE evtnotify;
	uint bufsize;
	uint pos;

	struct {
		void *ptr;
		DWORD len;
	} bufs[2];
	uint ibuf;
} ffdsnd_capt;

/** Open device for capture.
@dev_id: NULL for a default device. */
FF_EXTN int ffdsnd_capt_open(ffdsnd_capt *dsc, const GUID *dev_id, ffpcm *fmt, uint bufsize_msec);

FF_EXTN void ffdsnd_capt_close(ffdsnd_capt *dsc);

/** Read from capture buffer. */
FF_EXTN int ffdsnd_capt_read(ffdsnd_capt *dsc, void **data, size_t *len);

static FFINL int ffdsnd_capt_start(ffdsnd_capt *dsc)
{
	return IDirectSoundCaptureBuffer_Start(dsc->buf, DSCBSTART_LOOPING);
}

static FFINL int ffdsnd_capt_stop(ffdsnd_capt *dsc)
{
	return IDirectSoundCaptureBuffer_Stop(dsc->buf);
}
