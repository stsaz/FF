/**
Copyright (c) 2015 Simon Zolin
*/

#include <FF/adev/wasapi.h>
#include <FF/audio/wav.h>
#include <FF/sys/wohandler.h>
#include <FFOS/error.h>


enum {
	WAS_DEFNFYRATE = 4,
};


static void _ffwas_onplay_excl(void *udata);
static void _ffwas_onplay_sh(void *udata);
static void _ffwas_oncapt_sh(void *udata);
static void _ffwas_getfmt(ffwasapi *w, ffpcm *fmt);


static const char *const _ffwas_serr[] = {
	""
	, "AUDCLNT_E_NOT_INITIALIZED"
	, "AUDCLNT_E_ALREADY_INITIALIZED"
	, "AUDCLNT_E_WRONG_ENDPOINT_TYPE"
	, "AUDCLNT_E_DEVICE_INVALIDATED"
	, "AUDCLNT_E_NOT_STOPPED"
	, "AUDCLNT_E_BUFFER_TOO_LARGE"
	, "AUDCLNT_E_OUT_OF_ORDER"
	, "AUDCLNT_E_UNSUPPORTED_FORMAT"
	, "AUDCLNT_E_INVALID_SIZE"
	, "AUDCLNT_E_DEVICE_IN_USE"
	, "AUDCLNT_E_BUFFER_OPERATION_PENDING"
	, "AUDCLNT_E_THREAD_NOT_REGISTERED"
	, ""
	, "AUDCLNT_E_EXCLUSIVE_MODE_NOT_ALLOWED"
	, "AUDCLNT_E_ENDPOINT_CREATE_FAILED"
	, "AUDCLNT_E_SERVICE_NOT_RUNNING"
	, "AUDCLNT_E_EVENTHANDLE_NOT_EXPECTED"
	, "AUDCLNT_E_EXCLUSIVE_MODE_ONLY"
	, "AUDCLNT_E_BUFDURATION_PERIOD_NOT_EQUAL"
	, "AUDCLNT_E_EVENTHANDLE_NOT_SET"
	, "AUDCLNT_E_INCORRECT_BUFFER_SIZE"
	, "AUDCLNT_E_BUFFER_SIZE_ERROR"
	, "AUDCLNT_E_CPUUSAGE_EXCEEDED"
	, "AUDCLNT_E_BUFFER_ERROR"
	, "AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED"
	, ""
	, ""
	, ""
	, ""
	, ""
	, ""
	, "AUDCLNT_E_INVALID_DEVICE_PERIOD"
	, "AUDCLNT_E_INVALID_STREAM_FLAG"
	, "AUDCLNT_E_ENDPOINT_OFFLOAD_NOT_CAPABLE"
	, "AUDCLNT_E_OUT_OF_OFFLOAD_RESOURCES"
	, "AUDCLNT_E_OFFLOAD_MODE_ONLY"
	, "AUDCLNT_E_NONOFFLOAD_MODE_ONLY"
	, "AUDCLNT_E_RESOURCES_INVALIDATED"
};

const char* ffwas_errstr(int e)
{
	uint code;
	if ((e & 0xffff0000) != (uint)MAKE_HRESULT(SEVERITY_ERROR, FACILITY_AUDCLNT, 0))
		return fferr_strp(e);

	code = e & 0xffff;
	if (code < FFCNT(_ffwas_serr))
		return _ffwas_serr[code];
	return "";
}


static ffwoh *_ffwas_woh;

int ffwas_init(void)
{
	if (NULL == (_ffwas_woh = ffwoh_create()))
		return 1;
	CoInitializeEx(NULL, 0);
	fflk_setup();
	return 0;
}

void ffwas_uninit(void)
{
	if (_ffwas_woh != NULL) {
		ffwoh_free(_ffwas_woh);
		_ffwas_woh = NULL;
	}
}

const GUID CLSID_MMDeviceEnumerator = {0xbcde0395, 0xe52f, 0x467c, {0x8e,0x3d, 0xc4,0x57,0x92,0x91,0x69,0x2e}};
const GUID IID_IMMDeviceEnumerator = {0xa95664d2, 0x9614, 0x4f35, {0xa7,0x46, 0xde,0x8d,0xb6,0x36,0x17,0xe6}};
const GUID IID_IAudioRenderClient = {0xf294acfc, 0x3146, 0x4483, {0xa7,0xbf, 0xad,0xdc,0xa7,0xc2,0x60,0xe2}};
const GUID IID_IAudioCaptureClient = {0xc8adbd64, 0xe71e, 0x48a0, {0xa4,0xde, 0x18,0x5c,0x39,0x5c,0xd3,0x17}};
const GUID IID_IAudioClient = {0x1cb9ad4c, 0xdbfa, 0x4c32, {0xb1,0x78, 0xc2,0xf5,0x68,0xa7,0x03,0xb2}};

//#include <functiondiscoverykeys_devpkey.h>
static const PROPERTYKEY PKEY_Device_FriendlyName = {{0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 14}; // DEVPROP_TYPE_STRING

static const byte _ffwas_devenum_const[] = { 0xff, eRender, eCapture };

int ffwas_devnext(ffwas_dev *d, uint flags)
{
	HRESULT r;
	IMMDeviceEnumerator *enu = NULL;
	IMMDevice *dev = NULL;
	IPropertyStore *props = NULL;
	size_t n;
	PROPVARIANT name;

	if (d->dcoll == NULL) {
		PropVariantInit(&name);
		if (0 != (r = CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL
			, &IID_IMMDeviceEnumerator, (void**)&enu)))
			goto fail;

		if (0 != (r = IMMDeviceEnumerator_EnumAudioEndpoints(enu, _ffwas_devenum_const[flags], DEVICE_STATE_ACTIVE, &d->dcoll)))
			goto fail;
	}

	if (0 != (r = IMMDeviceCollection_Item(d->dcoll, d->idx++, &dev))) {
		r = 1;
		goto fail;
	}

	if (0 != (r = IMMDevice_OpenPropertyStore(dev, STGM_READ, &props)))
		goto fail;

	if (0 != (r = IPropertyStore_GetValue(props, &PKEY_Device_FriendlyName, &name)))
		goto fail;

	if (0 != (r = IMMDevice_GetId(dev, &d->id)))
		goto fail;

	n = ff_wtou(NULL, 0, name.pwszVal, (size_t)-1, 0);
	d->name = ffmem_alloc(n + 1);
	if (d->name == NULL) {
		r = fferr_last();
		goto fail;
	}

	ff_wtou(d->name, n + 1, name.pwszVal, (size_t)-1, 0);
	r = 0;
	goto done;

fail:
	ffwas_devdestroy(d);
	ffwas_devinit(d);

done:
	PropVariantClear(&name);
	if (dev != NULL)
		IMMDevice_Release(dev);
	if (props != NULL)
		IPropertyStore_Release(props);
	if (enu != NULL)
		IMMDeviceEnumerator_Release(enu);
	return r;
}

void ffwas_devdestroy(ffwas_dev *d)
{
	ffmem_safefree(d->name);
	FF_SAFECLOSE(d->id, NULL, CoTaskMemFree);
	FF_SAFECLOSE(d->dcoll, NULL, IMMDeviceCollection_Release);
}

static FFINL void _ffwas_getfmt(ffwasapi *w, ffpcm *fmt)
{
	HRESULT r;
	WAVEFORMATEX *wf = NULL;
	if (0 != (r = IAudioClient_GetMixFormat(w->cli, &wf)))
		return;
	fmt->sample_rate = wf->nSamplesPerSec;
	fmt->channels = wf->nChannels;
	CoTaskMemFree(wf);
}

int ffwas_open(ffwasapi *w, const WCHAR *id, ffpcm *fmt, uint bufsize)
{
	HRESULT r;
	IMMDeviceEnumerator *enu = NULL;
	IMMDevice *dev = NULL;
	WAVEFORMATEX wf;
	unsigned balign = 0;
	REFERENCE_TIME dur;

	w->frsize = ffpcm_size(fmt->format, fmt->channels);
	ffwav_makewfx(&wf, fmt);

	dur = 10 * 1000 * bufsize;
	if (w->excl)
		dur /= 2;

	if (0 != (r = CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL
		, &IID_IMMDeviceEnumerator, (void**)&enu)))
		goto fail;

	if (id == NULL)
		r = IMMDeviceEnumerator_GetDefaultAudioEndpoint(enu, (!w->capture) ? eRender : eCapture, eConsole, &dev);
	else
		r = IMMDeviceEnumerator_GetDevice(enu, id, &dev);
	if (r != 0)
		goto fail;

	for (;;) {
		if (0 != (r = IMMDevice_Activate(dev, &IID_IAudioClient, CLSCTX_ALL, NULL, (void**)&w->cli)))
			goto fail;

		r = IAudioClient_Initialize(w->cli
			, (w->excl) ? AUDCLNT_SHAREMODE_EXCLUSIVE : AUDCLNT_SHAREMODE_SHARED
			, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, dur, dur, &wf, NULL);

		if (r == AUDCLNT_E_UNSUPPORTED_FORMAT && !w->excl) {
			_ffwas_getfmt(w, fmt);
			goto fail;
		}

		if (r != AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED || balign)
			break;

		if (0 != (r = IAudioClient_GetBufferSize(w->cli, &w->bufsize)))
			goto fail;
		IAudioClient_Release(w->cli);
		w->cli = NULL;

		//get an aligned buffer size
		dur = (REFERENCE_TIME)((10000.0 * 1000 / fmt->sample_rate * w->bufsize) + 0.5);
		balign = 1;
	}

	if (r != 0)
		goto fail;

	if (NULL == (w->evt = CreateEvent(NULL, 0, 0, NULL))) {
		r = fferr_last();
		goto fail;
	}
	if (0 != (r = IAudioClient_SetEventHandle(w->cli, w->evt)))
		goto fail;

	if (0 != (r = IAudioClient_GetBufferSize(w->cli, &w->bufsize)))
		goto fail;
	if (!w->capture
		&& 0 != (r = IAudioClient_GetService(w->cli, &IID_IAudioRenderClient, (void**)&w->rend)))
		goto fail;
	else if (w->capture
		&& 0 != (r = IAudioClient_GetService(w->cli, &IID_IAudioCaptureClient, (void**)&w->capt)))
		goto fail;

	fflk_init(&w->lk);
	r = 0;

	if (w->nfy_interval == 0)
		w->nfy_interval = w->bufsize / WAS_DEFNFYRATE;
	w->nfy_next = w->capture ? (int)w->nfy_interval : -(int)w->nfy_interval;

	if (w->capture)
		if (NULL == ffarr_alloc(&w->buf, ffwas_bufsize(w))) {
			r = fferr_last();
			goto fail;
		}

	goto done;

fail:
	ffwas_close(w);

done:
	if (dev != NULL)
		IMMDevice_Release(dev);
	if (enu != NULL)
		IMMDeviceEnumerator_Release(enu);
	return r;
}

void ffwas_close(ffwasapi *w)
{
	if (w->evt != NULL) {
		ffwoh_rm(_ffwas_woh, w->evt);
		CloseHandle(w->evt);
		w->evt = NULL;
	}
	if (!w->capture)
		FF_SAFECLOSE(w->rend, NULL, IAudioRenderClient_Release);
	else
		FF_SAFECLOSE(w->capt, NULL, IAudioCaptureClient_Release);
	FF_SAFECLOSE(w->cli, NULL, IAudioClient_Release);
	ffarr_free(&w->buf);
}

int ffwas_filled(ffwasapi *w)
{
	HRESULT hr;
	uint filled;

	if (w->excl)
		return (w->actvbufs * w->bufsize + w->wpos) * w->frsize;

	if (0 != (hr = IAudioClient_GetCurrentPadding(w->cli, &filled)))
		return hr;
	return filled * w->frsize;
}

static void _ffwas_onplay_sh(void *udata)
{
	ffwasapi *w = udata;
	int fld;

	fflk_lock(&w->lk);
	fld = ffwas_filled(w) / w->frsize;
	if (fld != 0 && fld > w->nfy_next)
		goto done; //don't trigger too many events

	if (fld == 0) {
		if (!w->underflow) {
			w->underflow = 1;
			ffwas_stop(w);
		}
	}

	w->nfy_next -= w->nfy_interval;

	if (!w->callback_wait) {
		w->callback = 1;
		goto done;
	}
	w->callback_wait = 0;
	fflk_unlock(&w->lk);
	w->handler(w->udata);
	return;

done:
	fflk_unlock(&w->lk);
}

static void _ffwas_oncapt_sh(void *udata)
{
	ffwasapi *w = udata;
	int fld = ffwas_filled(w) / w->frsize;
	if (fld < w->nfy_next)
		return; //don't trigger too many events
	w->nfy_next += w->nfy_interval;
	w->handler(w->udata);
}

static void _ffwas_onplay_excl(void *udata)
{
	ffwasapi *w = udata;

	fflk_lock(&w->lk);

	if (w->capture) {
		fflk_unlock(&w->lk);
		w->handler(w->udata);
		return;
	}

	if (w->actvbufs == 0) {
		if (!w->underflow) {
			w->underflow = 1;
			ffwas_stop(w);
		}
		goto done;
	}

	w->actvbufs--;

	if (!w->callback_wait) {
		w->callback = 1;
		goto done;
	}
	w->callback_wait = 0;

	fflk_unlock(&w->lk);
	w->handler(w->udata);
	return;

done:
	fflk_unlock(&w->lk);
}

int ffwas_async(ffwasapi *w, uint enable)
{
	fflk_lock(&w->lk);

	if (!enable) {
		w->callback_wait = w->callback = 0;
		goto done;
	}

	if (w->callback) {
		w->callback = 0;
		fflk_unlock(&w->lk);
		w->handler(w->udata);
		return 1;
	}

	w->callback_wait = 1;
done:
	fflk_unlock(&w->lk);
	return 0;
}

static int _ffwas_write_sh(ffwasapi *w, const void *data, size_t len)
{
	HRESULT r;
	byte *d;
	uint n, nfree;

	nfree = w->bufsize - ffwas_filled(w) / w->frsize;
	if (nfree == 0) {
		if (!w->started && w->autostart) {
			if (0 != (r = ffwas_start(w)))
				return r;
		}
		return 0;
	}

	n = (uint)ffmin(len / w->frsize, nfree);
	if (0 != (r = IAudioRenderClient_GetBuffer(w->rend, n, &d)))
		return r;

	ffmemcpy(d, data, n * w->frsize);

	fflk_lock(&w->lk);
	if (0 != (r = IAudioRenderClient_ReleaseBuffer(w->rend, n, 0)))
		goto done;
	w->nfy_next += n;
	fflk_unlock(&w->lk);
	return n * w->frsize;

done:
	fflk_unlock(&w->lk);
	return r;
}

int ffwas_write(ffwasapi *w, const void *d, size_t len)
{
	HRESULT r;
	uint n;
	const byte *data = d;

	if (!w->excl)
		return _ffwas_write_sh(w, data, len);

	fflk_lock(&w->lk);
	while (len != 0) {

		if (w->actvbufs == 2) {
			if (!w->started && w->autostart) {
				if (0 != (r = ffwas_start(w)))
					goto done;
			}
			break;
		}

		if (w->wptr == NULL) {
			if (0 != (r = IAudioRenderClient_GetBuffer(w->rend, w->bufsize, &w->wptr)))
				goto done;
		}

		n = (uint)ffmin(len, (w->bufsize - w->wpos) * w->frsize);
		ffmemcpy(w->wptr + w->wpos * w->frsize, data, n);
		w->wpos += n / w->frsize;
		data += n;
		len -= n;

		if (w->wpos == w->bufsize) {
			if (0 != (r = IAudioRenderClient_ReleaseBuffer(w->rend, w->bufsize, 0)))
				goto done;
			w->wptr = NULL;
			w->wpos = 0;
			w->actvbufs++;
		}
	}

	r = (int)(data - (byte*)d);

done:
	fflk_unlock(&w->lk);
	return r;
}

int ffwas_silence(ffwasapi *w)
{
	HRESULT r;
	byte *d;
	uint nfree;

	if (w->excl) {
		if (w->wpos != 0) {
			nfree = w->bufsize - w->wpos;
			ffmem_zero(w->wptr + w->wpos * w->frsize, nfree * w->frsize);
			fflk_lock(&w->lk);
			if (0 != (r = IAudioRenderClient_ReleaseBuffer(w->rend, w->bufsize, 0)))
				goto done;
			w->wptr = NULL;
			w->wpos = 0;
			w->actvbufs++;
			fflk_unlock(&w->lk);
			return nfree * w->frsize;
		}
		return 0;
	}

	nfree = w->bufsize - ffwas_filled(w) / w->frsize;
	if (nfree == 0)
		return 0;
	if (0 != (r = IAudioRenderClient_GetBuffer(w->rend, nfree, &d)))
		return r;
	ffmem_zero(d, nfree * w->frsize);
	fflk_lock(&w->lk);
	if (0 != (r = IAudioRenderClient_ReleaseBuffer(w->rend, nfree, 0)))
		goto done;
	w->nfy_next += nfree;
	fflk_unlock(&w->lk);
	return nfree * w->frsize;

done:
	fflk_unlock(&w->lk);
	return r;
}

void ffwas_clear(ffwasapi *w)
{
	if (w->excl) {
		if (w->wptr != NULL) {
			IAudioRenderClient_ReleaseBuffer(w->rend, w->bufsize, 0);
			w->wptr = NULL;
		}
		w->wpos = 0;
		w->actvbufs = 0;
	}

	IAudioClient_Reset(w->cli);
	w->nfy_next = w->capture ? (int)w->nfy_interval : -(int)w->nfy_interval;
	w->last = 0;
}

static FFINL int woh_add(ffwasapi *w)
{
	int r;
	handler_t woh_func;

	if (w->excl)
		woh_func = &_ffwas_onplay_excl;
	else if (w->capture)
		woh_func = &_ffwas_oncapt_sh;
	else
		woh_func = &_ffwas_onplay_sh;

	if (0 != (r = ffwoh_add(_ffwas_woh, w->evt, woh_func, w))) {
		return fferr_last();
	}

	return 0;
}

int ffwas_start(ffwasapi *w)
{
	int r;
	if (0 != (r = woh_add(w)))
		return r;

	if (w->underflow)
		w->underflow = 0;

	if (0 != (r = IAudioClient_Start(w->cli)))
		return r;
	w->started = 1;
	return 0;
}

int ffwas_stop(ffwasapi *w)
{
	ffwoh_rm(_ffwas_woh, w->evt);
	w->started = 0;
	return IAudioClient_Stop(w->cli);
}

int ffwas_stoplazy(ffwasapi *w)
{
	HRESULT r;
	if (0 == ffwas_filled(w))
		return 1;

	if (!w->last) {
		w->last = 1;
		if (0 > (r = ffwas_silence(w)))
			return r;
	}

	if (!w->started) {
		if (0 != (r = ffwas_start(w)))
			return r;
	}

	return 0;
}


enum {
	AUDCNT_S_BUFFEREMPTY = 0x8890001,
};

int ffwas_capt_read(ffwasapi *w, void **data, size_t *len)
{
	int r;
	DWORD flags;
	ffstr d;

	for (;;) {
		if (w->actvbufs != 0) {
			if (0 != (r = IAudioCaptureClient_ReleaseBuffer(w->capt, w->wpos)))
				return r;
			w->actvbufs = 0;
		}

		if (0 != (r = IAudioCaptureClient_GetBuffer(w->capt, (byte**)&d.ptr, &w->wpos, &flags, NULL, NULL))) {
			if (r == AUDCNT_S_BUFFEREMPTY) {
				*len = 0;
				return 0;
			}
			return r;
		}
		d.len = w->wpos * w->frsize;
		w->actvbufs = 1;
		w->nfy_next -= w->wpos;

		if (!w->excl) {
			ffarr_append(&w->buf, d.ptr, d.len);
			if (w->buf.len >= w->nfy_interval * w->frsize) {
				*data = w->buf.ptr,  *len = w->buf.len;
				w->buf.len = 0;
				return 1;
			}
		} else {
			*data = d.ptr,  *len = d.len;
			return 1;
		}
	}
}
