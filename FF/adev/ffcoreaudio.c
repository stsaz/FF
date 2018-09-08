/**
Copyright (c) 2018 Simon Zolin
*/

#include <FF/adev/coreaudio.h>
#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CFString.h>


const char* ffcoraud_errstr(int e)
{
	return "";
}


static const AudioObjectPropertyAddress prop_dev_list = {
	kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMaster
};
static const AudioObjectPropertyAddress prop_dev_outname = {
	kAudioObjectPropertyName, kAudioDevicePropertyScopeOutput, kAudioObjectPropertyElementMaster
};
static const AudioObjectPropertyAddress prop_dev_inname = {
	kAudioObjectPropertyName, kAudioDevicePropertyScopeInput, kAudioObjectPropertyElementMaster
};
static const AudioObjectPropertyAddress prop_dev_outconf = {
	kAudioDevicePropertyStreamConfiguration, kAudioDevicePropertyScopeOutput, kAudioObjectPropertyElementMaster
};
static const AudioObjectPropertyAddress prop_dev_inconf = {
	kAudioDevicePropertyStreamConfiguration, kAudioDevicePropertyScopeInput, kAudioObjectPropertyElementMaster
};

/** Get device list. */
static int dev_list(ffcoraud_dev *d)
{
	int rc = -1;
	OSStatus r;
	uint size;
	r = AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &prop_dev_list, 0, NULL, &size);
	if (r != kAudioHardwareNoError)
		return 0;

	if (NULL == (d->devs = ffmem_alloc(size)))
		return -1;
	r = AudioObjectGetPropertyData(kAudioObjectSystemObject, &prop_dev_list, 0, NULL, &size, d->devs);
	if (r != kAudioHardwareNoError)
		goto end;
	d->ndev = size / sizeof(AudioObjectID);

	rc = 0;

end:
	if (rc != 0)
		ffmem_free0(d->devs);
	return rc;
}

/** Get name of the current device. */
static int dev_name(ffcoraud_dev *d, uint flags)
{
	int rc = -1;
	const AudioObjectPropertyAddress *a;
	AudioObjectID *devs = d->devs;
	AudioBufferList *bufs = NULL;
	CFStringRef cfs = NULL;
	OSStatus r;
	uint size;

	a = (flags & FFCORAUD_DEV_CAPTURE) ? &prop_dev_inconf : &prop_dev_outconf;
	r = AudioObjectGetPropertyDataSize(devs[d->idev], a, 0, NULL, &size);
	if (r != kAudioHardwareNoError)
		goto end;

	if (NULL == (bufs = ffmem_alloc(size)))
		goto end;
	r = AudioObjectGetPropertyData(devs[d->idev], a, 0, NULL, &size, bufs);
	if (r != kAudioHardwareNoError)
		goto end;

	uint ch = 0;
	for (uint i = 0;  i != bufs->mNumberBuffers;  i++) {
		ch |= bufs->mBuffers[i].mNumberChannels;
	}
	if (ch == 0)
		goto end;

	size = sizeof(CFStringRef);
	a = (flags & FFCORAUD_DEV_CAPTURE) ? &prop_dev_inname : &prop_dev_outname;
	r = AudioObjectGetPropertyData(devs[d->idev], a, 0, NULL, &size, &cfs);
	if (r != kAudioHardwareNoError)
		goto end;

	CFIndex len = CFStringGetMaximumSizeForEncoding(CFStringGetLength(cfs), kCFStringEncodingUTF8);
	if (NULL == (d->name = ffmem_alloc(len + 1)))
		goto end;
	if (!CFStringGetCString(cfs, d->name, len + 1, kCFStringEncodingUTF8))
		goto end;

	rc = 0;

end:
	ffmem_free(bufs);
	if (rc != 0)
		ffmem_free0(d->name);
	if (cfs != NULL)
		CFRelease(cfs);
	return rc;
}

int ffcoraud_dev_id(ffcoraud_dev *d)
{
	if (d->idev == 0)
		return 0;
	const AudioObjectID *devs = d->devs;
	return devs[d->idev - 1];
}

int ffcoraud_devnext(ffcoraud_dev *d, uint flags)
{
	if (d->devs == NULL) {
		if (0 != dev_list(d))
			return -1;
	}

	for (;;) {
		ffmem_free0(d->name);

		if (d->idev == d->ndev)
			return 1;

		if (0 != dev_name(d, flags)) {
			d->idev++;
			continue;
		}
		d->idev++;
		return 0;
	}
}

void ffcoraud_devdestroy(ffcoraud_dev *d)
{
	ffmem_free0(d->devs);
	ffmem_free0(d->name);
}


static const AudioObjectPropertyAddress prop_idev_default = {
	kAudioHardwarePropertyDefaultInputDevice, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMaster
};
static const AudioObjectPropertyAddress prop_odev_default = {
	kAudioHardwarePropertyDefaultOutputDevice, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMaster
};
static int coraud_dev_default(uint io)
{
	AudioObjectID dev;
	uint sz = sizeof(AudioObjectID);
	const AudioObjectPropertyAddress *a = (io == FFCORAUD_DEV_CAPTURE) ? &prop_idev_default : &prop_odev_default;
	OSStatus r = AudioObjectGetPropertyData(kAudioObjectSystemObject, a, 0, NULL, &sz, &dev);
	if (r != 0)
		return -1;
	return dev;
}

static OSStatus coraud_ioproc(AudioDeviceID device, const AudioTimeStamp *now
	, const AudioBufferList *indata, const AudioTimeStamp *intime
	, AudioBufferList *outdata, const AudioTimeStamp *outtime
	, void *udata);
static OSStatus coraud_ioproc_capture(AudioDeviceID device, const AudioTimeStamp *now
	, const AudioBufferList *indata, const AudioTimeStamp *intime
	, AudioBufferList *outdata, const AudioTimeStamp *outtime
	, void *udata);

static const AudioObjectPropertyAddress prop_odev_fmt = {
	kAudioDevicePropertyStreamFormat, kAudioDevicePropertyScopeOutput, kAudioObjectPropertyElementMaster
};
static const AudioObjectPropertyAddress prop_idev_fmt = {
	kAudioDevicePropertyStreamFormat, kAudioDevicePropertyScopeInput, kAudioObjectPropertyElementMaster
};
int ffcoraud_open(ffcoraud_buf *snd, int dev, ffpcm *fmt, uint bufsize, uint flags)
{
	int r = 0, rc = -1;

	FF_ASSERT((flags & (FFCORAUD_DEV_PLAYBACK | FFCORAUD_DEV_CAPTURE)) != (FFCORAUD_DEV_PLAYBACK | FFCORAUD_DEV_CAPTURE));

	if (dev < 0) {
		dev = coraud_dev_default(flags & (FFCORAUD_DEV_PLAYBACK | FFCORAUD_DEV_CAPTURE));
		if (dev < 0)
			return -1;
	}

	AudioStreamBasicDescription asbd = {};
	uint sz = sizeof(asbd);
	const AudioObjectPropertyAddress *a = (flags & FFCORAUD_DEV_CAPTURE) ? &prop_idev_fmt : &prop_odev_fmt;
	if (0 != AudioObjectGetPropertyData(dev, a, 0, NULL, &sz, &asbd))
		return -1;

	if (fmt->format != FFPCM_FLOAT) {
		fmt->format = FFPCM_FLOAT;
		r = FFCORAUD_EFMT;
	}

	if (fmt->sample_rate != asbd.mSampleRate) {
		fmt->sample_rate = asbd.mSampleRate;
		r = FFCORAUD_EFMT;
	}

	if (fmt->channels != asbd.mChannelsPerFrame) {
		fmt->channels = asbd.mChannelsPerFrame;
		r = FFCORAUD_EFMT;
	}

	if (r != 0)
		return r;

	void *proc = (flags & FFCORAUD_DEV_CAPTURE) ? &coraud_ioproc_capture : &coraud_ioproc;
	if (0 != AudioDeviceCreateIOProcID(dev, proc, snd, (AudioDeviceIOProcID*)&snd->aprocid)
		|| snd->aprocid == NULL)
		goto end;

	if (bufsize == 0)
		bufsize = 1000;
	bufsize = ffpcm_bytes(fmt, bufsize);
	bufsize = ff_align_power2(bufsize);
	if (NULL == ffstr_alloc(&snd->data, bufsize))
		goto end;
	snd->data.len = bufsize;
	ffringbuf_init(&snd->buf, snd->data.ptr, snd->data.len);

	if (flags & FFCORAUD_DEV_CAPTURE) {
		if (NULL == ffstr_alloc(&snd->data2, bufsize))
			goto end;
		snd->data2.len = bufsize;
	}

	snd->dev = dev;
	rc = 0;

end:
	if (rc != 0)
		ffcoraud_close(snd);
	return rc;
}

void ffcoraud_close(ffcoraud_buf *snd)
{
	AudioDeviceDestroyIOProcID(snd->dev, snd->aprocid);
	snd->dev = 0;
	snd->aprocid = NULL;
	ffstr_free(&snd->data);
	ffstr_free(&snd->data2);
}

int ffcoraud_start(ffcoraud_buf *snd)
{
	return !!AudioDeviceStart(snd->dev, snd->aprocid);
}

int ffcoraud_stop(ffcoraud_buf *snd)
{
	return !!AudioDeviceStop(snd->dev, snd->aprocid);
}

static OSStatus coraud_ioproc(AudioDeviceID device, const AudioTimeStamp *now
	, const AudioBufferList *indata, const AudioTimeStamp *intime
	, AudioBufferList *outdata, const AudioTimeStamp *outtime
	, void *udata)
{
	ffcoraud_buf *snd = udata;
	float *d = outdata->mBuffers[0].mData;
	size_t n = outdata->mBuffers[0].mDataByteSize;

	uint r = ffringbuf_lock_read(&snd->buf, d, n);
	if (r != n) {
		ffmem_zero((char*)d + n, n - r);
		snd->overrun = 1;
	}
	return 0;
}

int ffcoraud_write(ffcoraud_buf *snd, const void *data, size_t len)
{
	uint n = ffringbuf_lock_write(&snd->buf, data, len);

	if (ffringbuf_lock_full(&snd->buf)) {
		if (snd->autostart)
			ffcoraud_start(snd);
	}

	return n;
}

int ffcoraud_filled(ffcoraud_buf *snd)
{
	return ffringbuf_lock_canread(&snd->buf);
}

void ffcoraud_clear(ffcoraud_buf *snd)
{
	ffringbuf_lock_reset(&snd->buf);
}

int ffcoraud_stoplazy(ffcoraud_buf *snd)
{
	if (ffringbuf_lock_empty(&snd->buf)) {
		ffcoraud_stop(snd);
		return 1;
	}

	ffcoraud_start(snd);
	return 0;
}


static OSStatus coraud_ioproc_capture(AudioDeviceID device, const AudioTimeStamp *now
	, const AudioBufferList *indata, const AudioTimeStamp *intime
	, AudioBufferList *outdata, const AudioTimeStamp *outtime
	, void *udata)
{
	ffcoraud_buf *snd = udata;
	const float *d = indata->mBuffers[0].mData;
	size_t n = indata->mBuffers[0].mDataByteSize;

	uint r = ffringbuf_lock_write(&snd->buf, d, n);
	if (r != n)
		snd->overrun = 1;
	return 0;
}

int ffcoraud_read(ffcoraud_buf *snd, ffstr *data)
{
	if (ffringbuf_lock_empty(&snd->buf))
		return 0;
	size_t r = ffringbuf_lock_read(&snd->buf, snd->data2.ptr, snd->data2.len);
	ffstr_set(data, snd->data2.ptr, r);
	return 1;
}
