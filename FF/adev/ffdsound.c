/**
Copyright (c) 2015 Simon Zolin
*/

#include <FF/adev/dsound.h>
#include <FF/audio/wav.h>
#include <FF/sys/wohandler.h>
#include <FFOS/error.h>
#include <FFOS/mem.h>


static ffwoh *_ffdsnd_nfy;

int ffdsnd_init(void)
{
	if (NULL == (_ffdsnd_nfy = ffwoh_create()))
		return 1;
	return 0;
}

void ffdsnd_uninit(void)
{
	ffwoh_free(_ffdsnd_nfy);
	_ffdsnd_nfy = NULL;
}

static int _ffdsnd_nfy_start(void *ds, void *buf, HANDLE *evt, uint bufsize)
{
	HRESULT r;
	IDirectSoundNotify *nfy;

	if (0 != (r = IDirectSoundBuffer_QueryInterface((IDirectSoundBuffer*)buf, &IID_IDirectSoundNotify8, (void**)&nfy)))
		return r;

	*evt = CreateEvent(NULL, 0, 0, NULL);
	if (*evt == NULL)
		goto fail;

	{
	DSBPOSITIONNOTIFY nfypos[] = {
		{ bufsize * 1 / 4 - 1, *evt }
		, { bufsize * 2 / 4 - 1, *evt }
		, { bufsize * 3 / 4 - 1, *evt }
		, { bufsize * 4 / 4 - 1, *evt }
	};

	if (0 != (r = IDirectSoundNotify_SetNotificationPositions(nfy, FFCNT(nfypos), nfypos)))
		goto fail;
	}

	r = 0;

fail:
	IDirectSoundNotify_Release(nfy);
	return r;
}


struct _ffdsnd_enumctx {
	struct ffdsnd_devenum *head
		, *cur;
};

static BOOL CALLBACK _ffdsnd_devenumproc(GUID *guid, const WCHAR *desc, const WCHAR *sguid, void *udata)
{
	struct _ffdsnd_enumctx *dx = (struct _ffdsnd_enumctx*)udata;
	struct ffdsnd_devenum *d;
	size_t r = ff_wtou(NULL, 0, desc, (size_t)-1, 0);

	d = (struct ffdsnd_devenum*)ffmem_alloc(sizeof(struct ffdsnd_devenum) + r + 1);
	if (d == NULL)
		return 0;
	ff_wtou(d->name, r + 1, desc, (size_t)-1, 0);

	if (guid != NULL) {
		d->guid = *guid;
		d->id = &d->guid;
	} else
		d->id = NULL;

	d->next = NULL;
	if (dx->head == NULL)
		dx->head = d;
	else
		dx->cur->next = d;
	dx->cur = d;
	return 1;
}

int ffdsnd_devenum(struct ffdsnd_devenum **dhead, uint flags)
{
	HRESULT r;
	struct _ffdsnd_enumctx dx = {0};

	if (flags == FFDSND_DEV_RENDER)
		r = DirectSoundEnumerate(&_ffdsnd_devenumproc, &dx);
	else if (flags == FFDSND_DEV_CAPTURE)
		r = DirectSoundCaptureEnumerate(&_ffdsnd_devenumproc, &dx);
	else
		return 1;

	if (r != 0) {
		ffdsnd_devenumfree(dx.head);
		return r;
	}

	*dhead = dx.head;
	return 0;
}

void ffdsnd_devenumfree(struct ffdsnd_devenum *head)
{
	struct ffdsnd_devenum *d, *next;
	for (d = head;  d != NULL;  d = next) {
		next = d->next;
		ffmem_free(d);
	}
}


static void _ffdsnd_onplay(void *udata)
{
	ffdsnd_buf *ds = udata;
	DWORD rpos;
	if (0 == IDirectSoundBuffer8_GetCurrentPosition(ds->buf, &rpos, NULL)) {
		uint filled = ffdsnd_filled(ds);
		ds->rpos = rpos;
		if (ffdsnd_filled(ds) > filled) {
			ds->underflow = 1;
			ffdsnd_pause(ds);
			IDirectSoundBuffer_SetCurrentPosition(ds->buf, 0);
			ds->wpos = ds->rpos = 0;
		}
	}
	ds->isfull = 0;
	ds->handler(ds->udata);
}

int ffdsnd_open(ffdsnd_buf *ds, const GUID *dev_id, ffpcm *fmt, uint bufsize)
{
	WAVEFORMATEX wf;
	HRESULT r;
	uint sample_size = ffpcm_size(fmt->format, fmt->channels);
	HWND h;

	if (0 != (r = DirectSoundCreate8(dev_id, &ds->dev, 0))) //dsound.dll
		return r;

	h = GetDesktopWindow();
	if (0 != (r = IDirectSound8_SetCooperativeLevel(ds->dev, h, DSSCL_NORMAL)))
		goto fail;

	ds->bufsize = ffpcm_samples(bufsize, fmt->sample_rate) * sample_size;
	ffwav_makewfx(&wf, fmt);

	{
	DSBUFFERDESC bufdesc = {
		sizeof(DSBUFFERDESC)
		, DSBCAPS_GLOBALFOCUS | DSBCAPS_CTRLPOSITIONNOTIFY
		, ds->bufsize, 0, &wf, {0}
	};

	if (0 != (r = IDirectSound8_CreateSoundBuffer(ds->dev, &bufdesc, &ds->buf, NULL)))
		goto fail;
	}

	if (0 != (r = _ffdsnd_nfy_start(ds, ds->buf, &ds->evtnotify, ds->bufsize)))
		goto fail;

	if (0 != ffwoh_add(_ffdsnd_nfy, ds->evtnotify, &_ffdsnd_onplay, ds)) {
		r = fferr_last();
		goto fail;
	}

	return 0;

fail:
	ffdsnd_close(ds);
	ffmem_tzero(ds);
	return r;
}

void ffdsnd_close(ffdsnd_buf *ds)
{
	if (ds->evtnotify != NULL) {
		ffwoh_rm(_ffdsnd_nfy, ds->evtnotify);
		CloseHandle(ds->evtnotify);
	}
	if (ds->buf != NULL)
		IDirectSoundBuffer_Release(ds->buf);
	if (ds->dev != NULL)
		IDirectSound8_Release(ds->dev);
}

uint ffdsnd_filled(ffdsnd_buf *ds)
{
	if (ds->wpos == ds->rpos && ds->isfull)
		return ds->bufsize;

	return (ds->wpos >= ds->rpos) ? (ds->wpos - ds->rpos) : ds->bufsize - (ds->rpos - ds->wpos);
}

int ffdsnd_write(ffdsnd_buf *ds, const void *data, size_t len)
{
	void *buf1 = NULL, *buf2 = NULL;
	DWORD nbuf1 = 0, nbuf2 = 0;
	uint nfree, n;

	nfree = ds->bufsize - ffdsnd_filled(ds);
	if (nfree == 0)
		return 0;
	n = (uint)ffmin(len, nfree);

	if (0 != IDirectSoundBuffer_Lock(ds->buf, ds->wpos, n, &buf1, &nbuf1, &buf2, &nbuf2, 0 /*DSBLOCK_FROMWRITECURSOR*/))
		return -1;

	ffmemcpy(buf1, data, nbuf1);
	if (nbuf2 != 0)
		ffmemcpy(buf2, (char*)data + nbuf1, nbuf2);

	ds->wpos = (ds->wpos + nbuf1 + nbuf2) % ds->bufsize;
	ds->isfull = 1;

	IDirectSoundBuffer_Unlock(ds->buf, buf1, nbuf1, buf2, nbuf2);
	return n;
}

void ffdsnd_clear(ffdsnd_buf *ds)
{
	IDirectSoundBuffer_SetCurrentPosition(ds->buf, 0);
	ds->wpos = ds->rpos = 0;
	ds->isfull = 0;
}

int ffdsnd_silence(ffdsnd_buf *ds)
{
	void *buf1 = NULL, *buf2 = NULL;
	DWORD nbuf1 = 0, nbuf2 = 0;
	uint nfree;
	HRESULT r;

	nfree = ds->bufsize - ffdsnd_filled(ds);
	if (nfree == 0)
		return 0;

	if (0 != (r = IDirectSoundBuffer_Lock(ds->buf, ds->wpos, nfree
		, &buf1, &nbuf1, &buf2, &nbuf2, 0 /*DSBLOCK_FROMWRITECURSOR*/)))
		return r;

	ffmem_zero(buf1, nbuf1);
	if (nbuf2 != 0)
		ffmem_zero(buf2, nbuf2);

	ds->wpos = (ds->wpos + nbuf1 + nbuf2) % ds->bufsize;

	IDirectSoundBuffer_Unlock(ds->buf, buf1, nbuf1, buf2, nbuf2);
	return nfree;
}

int ffdsnd_stoplazy(ffdsnd_buf *ds)
{
	HRESULT r;
	if (0 == ffdsnd_filled(ds))
		return 1;

	if (!ds->last) {
		ds->last = 1;
		if (0 > (r = ffdsnd_silence(ds)))
			return r;
	}

	if (0 != (r = ffdsnd_start(ds)))
		return r;

	return 0;
}


int ffdsnd_capt_open(ffdsnd_capt *dsc, const GUID *dev_id, ffpcm *fmt, uint bufsize)
{
	WAVEFORMATEX wf;
	HRESULT r;
	uint sample_size = ffpcm_size(fmt->format, fmt->channels);

	if (0 != (r = DirectSoundCaptureCreate(dev_id, &dsc->dev, NULL)))
		return r;

	dsc->bufsize = ffpcm_samples(bufsize, fmt->sample_rate) * sample_size;
	ffwav_makewfx(&wf, fmt);

	{
	DSCBUFFERDESC desc = {
		sizeof(DSCBUFFERDESC), 0, dsc->bufsize, 0, &wf, 0, NULL
	};

	if (0 != (r = IDirectSoundCapture_CreateCaptureBuffer(dsc->dev, &desc, &dsc->buf, NULL)))
		goto fail;
	}

	if (0 != (r = _ffdsnd_nfy_start(dsc, dsc->buf, &dsc->evtnotify, dsc->bufsize)))
		goto fail;

	if (0 != ffwoh_add(_ffdsnd_nfy, dsc->evtnotify, dsc->handler, dsc->udata)) {
		r = fferr_last();
		goto fail;
	}

	return 0;

fail:
	ffdsnd_capt_close(dsc);
	ffmem_tzero(dsc);
	return r;
}

void ffdsnd_capt_close(ffdsnd_capt *dsc)
{
	if (dsc->evtnotify != NULL) {
		ffwoh_rm(_ffdsnd_nfy, dsc->evtnotify);
		CloseHandle(dsc->evtnotify);
	}
	if (dsc->buf != NULL)
		IDirectSoundCaptureBuffer_Release(dsc->buf);
	if (dsc->dev != NULL)
		IDirectSoundCapture_Release(dsc->dev);
}

int ffdsnd_capt_read(ffdsnd_capt *dsc, void **data, size_t *len)
{
	DWORD capturepos;
	uint navail;
	HRESULT r;

	if (dsc->ibuf == 2) {
		if (0 != (r = IDirectSoundCaptureBuffer_Unlock(dsc->buf
			, dsc->bufs[0].ptr, dsc->bufs[0].len, dsc->bufs[1].ptr, dsc->bufs[1].len)))
			return r;
		dsc->bufs[0].ptr = dsc->bufs[1].ptr = NULL;
		dsc->bufs[0].len = dsc->bufs[1].len = 0;
		dsc->ibuf = 0;
	}

	if (dsc->ibuf == 0) {
		if (0 != (r = IDirectSoundCaptureBuffer_GetCurrentPosition(dsc->buf, &capturepos, NULL)))
			return r;
		navail = (capturepos >= dsc->pos) ? capturepos - dsc->pos : dsc->bufsize - (dsc->pos - capturepos);
		if (navail == 0)
			goto nodata;

		if (0 != (r = IDirectSoundCaptureBuffer_Lock(dsc->buf, dsc->pos, navail
			, &dsc->bufs[0].ptr, &dsc->bufs[0].len, &dsc->bufs[1].ptr, &dsc->bufs[1].len
			, 0 /*DSCBLOCK_ENTIREBUFFER*/)))
			return r;
	}

	while (dsc->ibuf != 2) {
		uint i = dsc->ibuf++;

		if (dsc->bufs[i].len != 0) {
			dsc->pos = (dsc->pos + dsc->bufs[i].len) % dsc->bufsize;
			*len = dsc->bufs[i].len;
			*data = dsc->bufs[i].ptr;
			return 1;
		}
	}

nodata:
	*len = 0;
	return 0;
}
