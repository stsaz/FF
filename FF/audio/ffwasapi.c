/**
Copyright (c) 2015 Simon Zolin
*/

#include <FF/audio/wasapi.h>
#include <FF/audio/wav.h>
#include <FF/wohandler.h>
#include <FFOS/error.h>


static void _ffwas_onplay_excl(void *udata);
static void _ffwas_onplay_sh(void *udata);
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
	if ((e & 0xffff0000) != MAKE_HRESULT(SEVERITY_ERROR, FACILITY_AUDCLNT, 0))
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
	if (d->id != NULL)
		CoTaskMemFree(d->id);
	if (d->name != NULL)
		ffmem_free(d->name);
	if (d->dcoll != NULL)
		IMMDeviceCollection_Release(d->dcoll);
}

static FFINL void _ffwas_getfmt(ffwasapi *w, ffpcm *fmt)
{
	HRESULT r;
	WAVEFORMATEX *wf = NULL;
	if (0 != (r = IAudioClient_GetMixFormat(w->cli, &wf)))
		return;
	fmt->sample_rate = wf->nSamplesPerSec;
	CoTaskMemFree(wf);
}

int ffwas_open(ffwasapi *w, const WCHAR *id, ffpcm *fmt, uint bufsize)
{
	HRESULT r;
	IMMDeviceEnumerator *enu = NULL;
	IMMDevice *dev = NULL;
	WAVEFORMATEX wf;
	ffwav_fmt ww;
	unsigned balign = 0;
	REFERENCE_TIME dur;

	w->frsize = ffpcm_size(fmt->format, fmt->channels);
	ffwav_pcmfmtset(&ww, fmt->format);
	wf.wFormatTag = ww.format;
	wf.wBitsPerSample = ww.bit_depth;

	wf.nChannels = fmt->channels;
	wf.nSamplesPerSec = fmt->sample_rate;
	wf.nBlockAlign = w->frsize;
	wf.nAvgBytesPerSec = fmt->sample_rate * w->frsize;
	wf.cbSize = 0;

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

	if (0 != (r = ffwoh_add(_ffwas_woh, w->evt, (w->excl) ? &_ffwas_onplay_excl : &_ffwas_onplay_sh, w))) {
		r = fferr_last();
		goto fail;
	}

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
	goto done;

fail:
	ffwas_close(w);
	ffmem_tzero(w);

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
	}
	if (!w->capture && w->rend != NULL)
		IAudioRenderClient_Release(w->rend);
	else if (w->capture && w->capt != NULL)
		IAudioCaptureClient_Release(w->capt);
	if (w->cli != NULL)
		IAudioClient_Release(w->cli);
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
	if ((!w->capture && ffwas_filled(w) / w->frsize > w->bufsize * 3 / 4)
		|| (w->capture && ffwas_filled(w) / w->frsize < w->bufsize / 4))
		return; //don't trigger too many events
	w->handler(w->udata);
}

static void _ffwas_onplay_excl(void *udata)
{
	ffwasapi *w = udata;

	fflk_lock(&w->lk);
	if (w->starting != 0 && --w->starting != 0)
		goto done; //skip events signaled by ffwas_start()

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
	fflk_unlock(&w->lk);
	w->handler(w->udata);
	return;

done:
	fflk_unlock(&w->lk);
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

	if (0 != (r = IAudioRenderClient_ReleaseBuffer(w->rend, n, 0)))
		return r;
	return n * w->frsize;
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
				if (w->underflow)
					w->underflow = 0;
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
			ffmem_zero(w->wptr + w->wpos * w->frsize, (w->bufsize - w->wpos) * w->frsize);
			if (0 != (r = IAudioRenderClient_ReleaseBuffer(w->rend, w->bufsize, 0)))
				return r;
			w->wptr = NULL;
			w->wpos = 0;
			w->actvbufs++;
		}
		return 0;
	}

	nfree = w->bufsize - ffwas_filled(w) / w->frsize;
	if (nfree == 0)
		return 0;
	if (0 != (r = IAudioRenderClient_GetBuffer(w->rend, nfree, &d)))
		return r;
	ffmem_zero(d, nfree * w->frsize);
	if (0 != (r = IAudioRenderClient_ReleaseBuffer(w->rend, nfree, 0)))
		return r;
	return nfree * w->frsize;
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


int ffwas_capt_read(ffwasapi *w, void **data, size_t *len)
{
	int r;
	DWORD flags;

	if (w->actvbufs != 0) {
		if (0 != (r = IAudioCaptureClient_ReleaseBuffer(w->capt, w->wpos)))
			return r;
		w->actvbufs = 0;
	}

	if (0 != (r = IAudioCaptureClient_GetBuffer(w->capt, (byte**)data, &w->wpos, &flags, NULL, NULL))) {
		if (r == 0x8890001 /*AUDCNT_S_BUFFEREMPTY*/) {
			*len = 0;
			return 0;
		}
		return r;
	}

	w->actvbufs = 1;
	*len = w->wpos * w->frsize;
	return 1;
}
