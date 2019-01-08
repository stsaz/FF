/**
Copyright (c) 2015 Simon Zolin
*/

/**
In shared mode with AUDCLNT_STREAMFLAGS_EVENTCALLBACK too many events from WASAPI may be triggerred, regardless of buffer size.
This behaviour generates many unnecessary context switches before a call to user function is really needed.
Furthermore, AUDCLNT_STREAMFLAGS_EVENTCALLBACK doesn't work with AUDCLNT_STREAMFLAGS_LOOPBACK.
So the only solution is when user calls WASAPI I/O functions by timer that fires the specified number of times per second.

timer signal -> ffkqu_post()
IOCP posted event -> ffkev_call() -> user handler -> ffwas_write()/read()

In exclusive mode an event from WASAPI is triggerred once per half of the buffer.
User callback function can be called once after ffwas_async(w, 1) and until ffwas_async(w, 0).

WOH event -> _ffwas_onplay_excl() -> ffwasapi.handler() -> ffkqu_post()
IOCP posted event -> ffkev_call() -> real handler
*/


#include <FF/adev/wasapi.h>
#include <FF/aformat/wav.h>
#include <FFOS/error.h>


static int fmt_wfx2ff(const WAVEFORMATEXTENSIBLE *wf);
static int _ffwas_getfmt_mix(IAudioClient *cl, ffpcm *fmt);
static int _ffwas_getfmt_def(IAudioClient *cl, IMMDevice *dev, ffpcm *fmt);
static int _ffwas_getfmt(IAudioClient *cl, IMMDevice *dev, ffpcm *fmt, WAVEFORMATEXTENSIBLE *wf, uint flags);


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


int ffwas_init(void)
{
	CoInitializeEx(NULL, 0);
	return 0;
}

void ffwas_uninit(void)
{
}

static const GUID CLSID_MMDeviceEnumerator_ff = {0xbcde0395, 0xe52f, 0x467c, {0x8e,0x3d, 0xc4,0x57,0x92,0x91,0x69,0x2e}};
static const GUID IID_IMMDeviceEnumerator_ff = {0xa95664d2, 0x9614, 0x4f35, {0xa7,0x46, 0xde,0x8d,0xb6,0x36,0x17,0xe6}};
static const GUID IID_IAudioRenderClient_ff = {0xf294acfc, 0x3146, 0x4483, {0xa7,0xbf, 0xad,0xdc,0xa7,0xc2,0x60,0xe2}};
static const GUID IID_IAudioCaptureClient_ff = {0xc8adbd64, 0xe71e, 0x48a0, {0xa4,0xde, 0x18,0x5c,0x39,0x5c,0xd3,0x17}};
static const GUID IID_IAudioClient_ff = {0x1cb9ad4c, 0xdbfa, 0x4c32, {0xb1,0x78, 0xc2,0xf5,0x68,0xa7,0x03,0xb2}};
static const void* const cli_guids[] = { &IID_IAudioRenderClient_ff, &IID_IAudioCaptureClient_ff };

//#include <functiondiscoverykeys_devpkey.h>
static const PROPERTYKEY PKEY_Device_FriendlyName = {{0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 14}; // DEVPROP_TYPE_STRING

static const PROPERTYKEY PKEY_AudioEngine_DeviceFormat_ff = {{0xf19f064d, 0x082c, 0x4e27, {0xbc,0x73,0x68,0x82,0xa1,0xbb,0x8e,0x4c}}, 0};

static const byte _ffwas_devenum_const[] = { 0xff, eRender, eCapture };

int ffwas_devnext(ffwas_dev *d, uint flags)
{
	HRESULT r;
	IMMDeviceEnumerator *enu = NULL;
	IMMDevice *dev = NULL;
	IPropertyStore *props = NULL;
	size_t n;
	PROPVARIANT name;
	PropVariantInit(&name);

	if (d->dcoll == NULL) {
		if (0 != (r = CoCreateInstance(&CLSID_MMDeviceEnumerator_ff, NULL, CLSCTX_ALL
			, &IID_IMMDeviceEnumerator_ff, (void**)&enu)))
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
	ffmem_free0(d->name);
	FF_SAFECLOSE(d->id, NULL, CoTaskMemFree);
	FF_SAFECLOSE(d->dcoll, NULL, IMMDeviceCollection_Release);
}

static int _ffwas_getfmt_mix(IAudioClient *cl, ffpcm *fmt)
{
	HRESULT r;
	WAVEFORMATEX *wf = NULL;
	if (0 != (r = IAudioClient_GetMixFormat(cl, &wf)))
		return r;
	fmt->sample_rate = wf->nSamplesPerSec;
	fmt->channels = wf->nChannels;
	CoTaskMemFree(wf);
	return 0;
}

/** Get format which is specified in OS as default. */
static int _ffwas_getfmt_def(IAudioClient *cl, IMMDevice *dev, ffpcm *fmt)
{
	int rc = -1;
	HRESULT r;
	IPropertyStore* store = NULL;
	PROPVARIANT prop;
	PropVariantInit(&prop);

	if (0 != (r = IMMDevice_OpenPropertyStore(dev, STGM_READ, &store)))
		goto end;

	if (0 != (r = IPropertyStore_GetValue(store, &PKEY_AudioEngine_DeviceFormat_ff, &prop)))
		goto end;
	const WAVEFORMATEXTENSIBLE *pwf = (void*)prop.blob.pBlobData;

	int rr = fmt_wfx2ff(pwf);
	if (rr < 0)
		goto end;
	fmt->format = rr;
	fmt->sample_rate = pwf->Format.nSamplesPerSec;
	fmt->channels = pwf->Format.nChannels;

	rc = 0;

end:
	PropVariantClear(&prop);
	IPropertyStore_Release(store);
	return rc;
}

static const GUID wfx_guid = { 1, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71} };

static FFINL void makewfxe(WAVEFORMATEXTENSIBLE *wf, const ffpcm *f)
{
	ffwav_makewfx((void*)wf, f);
	wf->Format.wFormatTag = 0xfffe;
	wf->Format.cbSize = 22;
	wf->Samples.wValidBitsPerSample = ffpcm_bits(f->format);
	if (f->format == FFPCM_24_4)
		wf->Samples.wValidBitsPerSample = 24;
	wf->dwChannelMask = 3;
	ffmemcpy(&wf->SubFormat, &wfx_guid, sizeof(GUID));
	if (f->format == FFPCM_FLOAT)
		*(ushort*)&wf->SubFormat = 3;
}

/** WAVEFORMATEX -> FF audio format. */
static FFINL int fmt_wfx2ff(const WAVEFORMATEXTENSIBLE *wf)
{
	uint f, fmt, bps, bps_store;

	f = wf->Format.wFormatTag;
	bps = bps_store = wf->Format.wBitsPerSample;
	if (wf->Format.wFormatTag == 0xfffe) {
		if (wf->Format.cbSize != 22)
			return -1;
		f = *(ushort*)&wf->SubFormat;
		bps = wf->Samples.wValidBitsPerSample;
	}

	switch (f) {
	case 1: {
		switch (bps) {
		case 8:
		case 16:
		case 24:
		case 32:
			if (bps == bps_store)
				fmt = bps;
			else if (bps_store == 32)
				fmt = FFPCM_24_4;
			else
				return -1;
			break;

		default:
			return -1;
		}
		break;
	}

	case 3:
		fmt = FFPCM_FLOAT;
		break;

	default:
		return -1;
	}

	return fmt;
}

/** For shared mode 'closed-match' pointer must be deallocated.
Note: we use WF-extensible format, rather than WFEX format,
 because in exclusive mode "Realtek High Definition Audio" driver
 claims that it supports int24 WFEX format but plays noise because in fact it only supports int24-4.
*/
static int fmt_supported(IAudioClient *cl, uint mode, const ffpcm *fmt, WAVEFORMATEXTENSIBLE *wf, WAVEFORMATEX **owf)
{
	makewfxe(wf, fmt);
	int r = IAudioClient_IsFormatSupported(cl, mode, (void*)wf, owf);
	if (r == S_FALSE)
		CoTaskMemFree(*owf);
	return r;
}

static const ushort fmts[] = {
	FFPCM_FLOAT,
	FFPCM_32,
	FFPCM_24,
	FFPCM_24_4,
	FFPCM_16,
	FFPCM_8,
};

/** Find the supported format in exclusive mode.
. Try input format
. Try other known formats
. Try known formats with channels
Return 0 if input format is supported;
 >0 if new format is set;
 <0 if none of the known formats is supported */
static int _ffwas_getfmt(IAudioClient *cl, IMMDevice *dev, ffpcm *fmt, WAVEFORMATEXTENSIBLE *wf, uint flags)
{
	int rc = 1;
	HRESULT r;
	ffpcm f;
	WAVEFORMATEXTENSIBLE *owfx = NULL;
	WAVEFORMATEX **owf = (flags & FFWAS_EXCL) ? NULL : (void*)&owfx;
	uint mode = (flags & FFWAS_EXCL) ? AUDCLNT_SHAREMODE_EXCLUSIVE : AUDCLNT_SHAREMODE_SHARED;

	if (0 == (r = fmt_supported(cl, mode, fmt, wf, owf)))
		return 0;

	f = *fmt;
	for (uint i = 0;  i != FFCNT(fmts);  i++) {
		f.format = fmts[i];
		if (0 == (r = fmt_supported(cl, mode, &f, wf, owf))) {
			*fmt = f;
			goto end;
		}
	}

	f.channels = (fmt->channels == 2) ? 1 : 2;
	for (uint i = 0;  i != FFCNT(fmts);  i++) {
		f.format = fmts[i];
		if (0 == (r = fmt_supported(cl, mode, &f, wf, owf))) {
			*fmt = f;
			goto end;
		}
	}

	return -1;

end:
	return rc;
}

/* Opening a WASAPI device algorithm:

. Get device enumerator
. Get device (default or specified by ID)
. Get a general client interface
. Find a (probably) supported format
 * input format is supported:
 * new format is set:
  . Go on
 * no format is supported:
  . Get mix format (shared mode)
  . Get OS default format (exclusive mode)
. Open device via a general audio object
 * Success (but new format is set):
  . Fail with AUDCLNT_E_UNSUPPORTED_FORMAT
 * AUDCLNT_E_UNSUPPORTED_FORMAT (shared mode):
  . Get mix format
  . Try again (once)
 * AUDCLNT_E_UNSUPPORTED_FORMAT (exclusive mode):
  . Get OS default format
  . Try again (once)
 * E_POINTER (shared mode):
  . Convert WF-extensible -> WFEX
  . Try again (once)
 * AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED (exclusive mode):
  . Set aligned buffer size
  . Try again (once)
. Get a specific (render/capture) audio object
*/
int ffwas_open(ffwasapi *w, const WCHAR *id, ffpcm *fmt, uint bufsize, uint flags)
{
	HRESULT r;
	int newfmt = 0;
	IMMDeviceEnumerator *enu = NULL;
	IMMDevice *dev = NULL;
	WAVEFORMATEXTENSIBLE wf;
	ffbool balign = 0, e_pointer = 0, find_fmt = 1;
	REFERENCE_TIME dur;

	FF_ASSERT((flags & (FFWAS_DEV_RENDER | FFWAS_DEV_CAPTURE)) != (FFWAS_DEV_RENDER | FFWAS_DEV_CAPTURE));

	uint aflags = (flags & FFWAS_LOOPBACK) ? AUDCLNT_STREAMFLAGS_LOOPBACK : 0;
	aflags |= ((flags & (FFWAS_EXCL | FFWAS_LOOPBACK)) == FFWAS_EXCL) ? AUDCLNT_STREAMFLAGS_EVENTCALLBACK : 0;
	uint mode = (flags & FFWAS_EXCL) ? AUDCLNT_SHAREMODE_EXCLUSIVE : AUDCLNT_SHAREMODE_SHARED;

	if (w->cli != NULL)
		goto phase2;

	w->capture = !!(flags & FFWAS_DEV_CAPTURE) || !!(flags & FFWAS_LOOPBACK);
	w->excl = !!(flags & FFWAS_EXCL);
	w->autostart = !!(flags & FFWAS_AUTOSTART);

	dur = 10 * 1000 * bufsize;
	if (w->excl)
		dur /= 2;

	if (0 != (r = CoCreateInstance(&CLSID_MMDeviceEnumerator_ff, NULL, CLSCTX_ALL
		, &IID_IMMDeviceEnumerator_ff, (void**)&enu)))
		goto fail;

	if (id == NULL)
		r = IMMDeviceEnumerator_GetDefaultAudioEndpoint(enu, (flags & FFWAS_DEV_RENDER) ? eRender : eCapture, eConsole, &dev);
	else
		r = IMMDeviceEnumerator_GetDevice(enu, id, &dev);
	if (r != 0)
		goto fail;

	for (;;) {
		if (0 != (r = IMMDevice_Activate(dev, &IID_IAudioClient_ff, CLSCTX_ALL, NULL, (void**)&w->cli)))
			goto fail;

		if (find_fmt) {
			find_fmt = 0;
			newfmt = _ffwas_getfmt(w->cli, dev, fmt, &wf, flags);
			if (newfmt < 0) {
				if (!w->excl) {
					if (0 != _ffwas_getfmt_mix(w->cli, fmt))
						goto fail;
				} else {
					if (0 != _ffwas_getfmt_def(w->cli, dev, fmt))
						goto fail;
				}
			}
		}

		r = IAudioClient_Initialize(w->cli, mode, aflags, dur, dur, (void*)&wf, NULL);
		if (r == 0) {
			if (newfmt != 0) {
				r = AUDCLNT_E_UNSUPPORTED_FORMAT;
				goto done;
			}
			break;
		}

		switch (r) {
		case E_POINTER:
			if (e_pointer || w->excl)
				goto fail;
			e_pointer = 1;

			if (fmt->format == FFPCM_24_4) {
				newfmt = -1;
				if (0 != _ffwas_getfmt_mix(w->cli, fmt))
					goto fail;
			}

			ffwav_makewfx((void*)&wf, fmt);
			break;

		case AUDCLNT_E_UNSUPPORTED_FORMAT:
			if (newfmt < 0)
				goto fail; // even the default format isn't supported

			// the format approved by IAudioClient_IsFormatSupported() isn't actually supported
			newfmt = -1;
			if (!w->excl) {
				if (0 != _ffwas_getfmt_mix(w->cli, fmt))
					goto fail;
			} else {
				if (0 != _ffwas_getfmt_def(w->cli, dev, fmt))
					goto fail;
			}
			makewfxe(&wf, fmt);
			break;

		case AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED:
			if (balign)
				goto fail;

			if (0 != (r = IAudioClient_GetBufferSize(w->cli, &w->bufsize)))
				goto fail;

			//get an aligned buffer size
			dur = (REFERENCE_TIME)((10000.0 * 1000 / fmt->sample_rate * w->bufsize) + 0.5);
			balign = 1;
			break;

		default:
			goto fail;
		}

		IAudioClient_Release(w->cli);
		w->cli = NULL;
	}

phase2:
	if (aflags & AUDCLNT_STREAMFLAGS_EVENTCALLBACK) {
		if (NULL == (w->evt = CreateEvent(NULL, 0, 0, NULL))) {
			r = fferr_last();
			goto fail;
		}
		if (0 != (r = IAudioClient_SetEventHandle(w->cli, w->evt)))
			goto fail;
		fflk_init(&w->lk);
	}

	if (0 != (r = IAudioClient_GetBufferSize(w->cli, &w->bufsize)))
		goto fail;

	if (0 != (r = IAudioClient_GetService(w->cli, cli_guids[w->capture], (void**)&w->rend)))
		goto fail;

	w->frsize = ffpcm_size(fmt->format, fmt->channels);
	r = 0;

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
	ffwas_async(w, 0);
	if (w->evt != NULL) {
		CloseHandle(w->evt);
		w->evt = NULL;
	}
	if (!w->capture)
		FF_SAFECLOSE(w->rend, NULL, IAudioRenderClient_Release);
	else
		FF_SAFECLOSE(w->capt, NULL, IAudioCaptureClient_Release);
	FF_SAFECLOSE(w->cli, NULL, IAudioClient_Release);
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

void _ffwas_onplay_excl(void *udata)
{
	ffwasapi *w = udata;

	FFDBG_PRINTLN(10, "received event", 0);

	fflk_lock(&w->lk);

	w->evt_recvd++;
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
	if (!w->excl)
		return 0;

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

	if (0 != (r = IAudioRenderClient_ReleaseBuffer(w->rend, n, 0)))
		return r;
	return n * w->frsize;
}

int ffwas_write(ffwasapi *w, const void *d, size_t len)
{
	HRESULT r;
	uint n, nevents;
	const byte *data = d;

	if (!w->excl)
		return _ffwas_write_sh(w, data, len);

	fflk_lock(&w->lk);
	nevents = FF_SWAP(&w->evt_recvd, 0);
	fflk_unlock(&w->lk);
	if (nevents > w->actvbufs)
		ffwas_stop(w); //buffer underrun
	w->actvbufs = ffmax((int)w->actvbufs - (int)nevents, 0);

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
			if (0 != (r = IAudioRenderClient_ReleaseBuffer(w->rend, w->bufsize, 0))) {
				return r;
			}
			w->wptr = NULL;
			w->wpos = 0;
			w->actvbufs++;
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
	if (0 != (r = IAudioRenderClient_ReleaseBuffer(w->rend, nfree, 0)))
		return r;
	return nfree * w->frsize;
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
	w->last = 0;
}

int ffwas_start(ffwasapi *w)
{
	int r;
	if (0 != (r = IAudioClient_Start(w->cli)))
		return r;
	w->started = 1;
	return 0;
}

int ffwas_stop(ffwasapi *w)
{
	ffwas_async(w, 0);
	w->started = 0;
	return IAudioClient_Stop(w->cli);
}

int ffwas_stoplazy(ffwasapi *w)
{
	if (w->excl) {
		uint nevents;
		fflk_lock(&w->lk);
		nevents = FF_SWAP(&w->evt_recvd, 0);
		fflk_unlock(&w->lk);
		w->actvbufs = ffmax((int)w->actvbufs - (int)nevents, 0);
	}

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


static FFINL int ffwas_read_excl(ffwasapi *w, void **data, size_t *len)
{
	int r;
	DWORD flags;
	uint nevents;
	ffstr d;

	fflk_lock(&w->lk);
	nevents = FF_SWAP(&w->evt_recvd, 0);
	fflk_unlock(&w->lk);

	if (nevents)
		w->actvbufs = ffmin(w->actvbufs + nevents, 2);

	if (w->have_buf) {
		w->have_buf = 0;
		if (0 != (r = IAudioCaptureClient_ReleaseBuffer(w->capt, w->wpos)))
			return r;
		w->actvbufs--;
	}

	if (w->actvbufs == 0) {
		*len = 0;
		return 0;
	}

	if (0 != (r = IAudioCaptureClient_GetBuffer(w->capt, (byte**)&d.ptr, &w->wpos, &flags, NULL, NULL))) {
		if (r == AUDCLNT_S_BUFFER_EMPTY) {
			*len = 0;
			return 0;
		}
		return r;
	}
	FFDBG_PRINTLN(10, "IAudioCaptureClient_GetBuffer()  samples:%u  flags:%xu", w->wpos, flags);

	d.len = w->wpos * w->frsize;
	w->have_buf = 1;

	*data = d.ptr,  *len = d.len;
	return 1;
}

int ffwas_capt_read(ffwasapi *w, void **data, size_t *len)
{
	int r;
	DWORD flags;
	ffstr d;

	if (w->excl)
		return ffwas_read_excl(w, data, len);

	if (w->have_buf) {
		w->have_buf = 0;
		if (0 != (r = IAudioCaptureClient_ReleaseBuffer(w->capt, w->wpos)))
			return r;
	}

	if (0 != (r = IAudioCaptureClient_GetBuffer(w->capt, (byte**)&d.ptr, &w->wpos, &flags, NULL, NULL))) {
		if (r == AUDCLNT_S_BUFFER_EMPTY) {
			*len = 0;
			return 0;
		}
		return r;
	}
	FFDBG_PRINTLN(10, "IAudioCaptureClient_GetBuffer()  samples:%u  flags:%xu", w->wpos, flags);

	d.len = w->wpos * w->frsize;
	w->have_buf = 1;

	*data = d.ptr,  *len = d.len;
	return 1;
}
