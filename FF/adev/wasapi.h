/** WASAPI wrapper.
Copyright (c) 2015 Simon Zolin
*/

#include <FF/audio/pcm.h>
#include <FF/sys/wohandler.h>
#include <FFOS/mem.h>
#include <FFOS/atomic.h>

#define COBJMACROS
#include <mmdeviceapi.h>
#include <audioclient.h>


FF_EXTN const char* ffwas_errstr(int e);

FF_EXTN int ffwas_init(void);
FF_EXTN void ffwas_uninit(void);


typedef struct ffwas_dev {
	IMMDeviceCollection *dcoll;
	uint idx;

	WCHAR *id;
	char *name;
} ffwas_dev;

enum FFWAS_DEV {
	FFWAS_DEV_RENDER = 1
	, FFWAS_DEV_CAPTURE = 2
};

static FFINL void ffwas_devinit(ffwas_dev *dev)
{
	ffmem_tzero(dev);
}

/** Get next device.
@flags: enum FFWAS_DEV
Return 1 if no more devices. */
FF_EXTN int ffwas_devnext(ffwas_dev *dev, uint flags);
FF_EXTN void ffwas_devdestroy(ffwas_dev *dev);


typedef void (*ffsnd_handler)(void *udata);

typedef struct ffwasapi {
	IAudioClient *cli;
	union {
	IAudioRenderClient *rend;
	IAudioCaptureClient *capt;
	};
	uint bufsize;
	uint frsize;

	fflock lk;
	byte *wptr;
	uint wpos;
	uint actvbufs;
	uint evt_recvd;
	HANDLE evt;
	ffsnd_handler handler;
	void *udata;

	unsigned excl :1 //exclusive mode
		, capture :1
		, autostart :1

		, started :1
		, callback :1
		, callback_wait :1
		, have_buf :1
		, last :1
		;
} ffwasapi;

enum FFWAS_F {
	/* enum FFWAS_DEV{} */

	/** Use exclusive mode rather than shared. */
	FFWAS_EXCL = 4,

	/** Render: open audio device in a loopback mode.
	Note: async notifications don't work, so user must keep calling ffwas_capt_read() periodically. */
	FFWAS_LOOPBACK = 8,

	/** Render: automatically start playing when the whole buffer is filled. */
	FFWAS_AUTOSTART = 0x10,
};

/** Set parameters required for event notifications in exclusive mode.
After ffwas_open() is done, user registers _ffwas_onplay_excl() as a handler for 'w->evt' event object. */
#define ffwas_excl(w, _handler, _udata) \
do { \
	(w)->handler = (_handler); \
	(w)->udata = (_udata); \
} while (0)

FF_EXTN void _ffwas_onplay_excl(void *udata);

/** Open device.
In shared mode there's only one supported sample rate.  In exclusive mode a device may support different sample rates.
@dev_id: NULL for a default device.
@flags: enum FFWAS_F.
Return AUDCLNT_E* on error.
 AUDCLNT_E_UNSUPPORTED_FORMAT:
  shared mode: 'fmt' is set to the format which is configured for the device. */
FF_EXTN int ffwas_open(ffwasapi *w, const WCHAR *dev_id, ffpcm *fmt, uint bufsize_msec, uint flags);

/**
In exclusive mode user unregisters 'w->evt' event object from WOH before calling this function. */
FF_EXTN void ffwas_close(ffwasapi *w);

/** Return the number of bytes left in sound buffer. */
FF_EXTN int ffwas_filled(ffwasapi *w);

/** Return total buffer size (in bytes). */
static FFINL uint ffwas_bufsize(ffwasapi *w)
{
	return ((w->excl) ? w->bufsize * 2 : w->bufsize) * w->frsize;
}

#define ffwas_buf_samples(w) \
	(((w)->excl) ? (w)->bufsize * 2 : (w)->bufsize)

/** Write data into sound buffer.
Return the number of bytes written. */
FF_EXTN int ffwas_write(ffwasapi *w, const void *data, size_t len);

/** Write silence into sound buffer.
Return the number of bytes written. */
FF_EXTN int ffwas_silence(ffwasapi *w);

/** Enter asynchronous mode - call user handler function.
Return 1 if user handler function has been called. */
FF_EXTN int ffwas_async(ffwasapi *w, uint enable);

FF_EXTN int ffwas_start(ffwasapi *w);

FF_EXTN int ffwas_stop(ffwasapi *w);

/** Clear the buffers. */
FF_EXTN void ffwas_clear(ffwasapi *w);

/** Return 1 if stopped. */
FF_EXTN int ffwas_stoplazy(ffwasapi *w);

#define ffwas_capt_close  ffwas_close

FF_EXTN int ffwas_capt_read(ffwasapi *w, void **data, size_t *len);
