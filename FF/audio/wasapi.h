/** WASAPI wrapper.
Copyright (c) 2015 Simon Zolin
*/

#include <FF/audio/pcm.h>
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
	ffsnd_handler handler;
	void *udata;

	IAudioClient *cli;
	IAudioRenderClient *rend;
	uint bufsize;
	uint frsize;
	HANDLE evt;

	fflock lk;
	byte *wptr;
	uint wpos;
	uint actvbufs;

	unsigned excl :1 //exclusive mode
		, autostart :1

		, started :1
		, underflow :1
		, starting :2
		;
} ffwasapi;

/** Open device for playback.
@dev_id: NULL for a default device.
Return AUDCLNT_E* on error. */
FF_EXTN int ffwas_open(ffwasapi *w, const WCHAR *dev_id, ffpcm *fmt, uint bufsize_msec);

FF_EXTN void ffwas_close(ffwasapi *w);

/** Return the number of bytes left in sound buffer. */
FF_EXTN int ffwas_filled(ffwasapi *w);

static FFINL int ffwas_bufsize(ffwasapi *w)
{
	return ((w->excl) ? w->bufsize * 2 : w->bufsize) * w->frsize;
}

/** Write data into sound buffer.
Return the number of bytes written. */
FF_EXTN int ffwas_write(ffwasapi *w, const void *data, size_t len);

/** Write silence into sound buffer.*/
FF_EXTN int ffwas_silence(ffwasapi *w);

static FFINL int ffwas_start(ffwasapi *w)
{
	int r;
	w->starting = 2;
	if (0 != (r = IAudioClient_Start(w->cli)))
		return r;
	w->started = 1;
	return 0;
}

static FFINL int ffwas_stop(ffwasapi *w)
{
	w->started = 0;
	return IAudioClient_Stop(w->cli);
}
