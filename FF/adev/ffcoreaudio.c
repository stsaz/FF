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
	CFStringRef cfs = NULL;
	uint size = sizeof(CFStringRef);
	const AudioObjectPropertyAddress *a = (flags & FFCORAUD_DEV_CAPTURE) ? &prop_dev_inname : &prop_dev_outname;
	AudioObjectID *devs = d->devs;
	OSStatus r = AudioObjectGetPropertyData(devs[d->idev], a, 0, NULL, &size, &cfs);
	if (r != kAudioHardwareNoError)
		goto end;

	CFIndex len = CFStringGetMaximumSizeForEncoding(CFStringGetLength(cfs), kCFStringEncodingUTF8);
	if (NULL == (d->name = ffmem_alloc(len + 1)))
		goto end;
	if (!CFStringGetCString(cfs, d->name, len + 1, kCFStringEncodingUTF8))
		goto end;

	rc = 0;

end:
	if (rc != 0)
		ffmem_free0(d->name);
	CFRelease(cfs);
	return rc;
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

		if (0 != dev_name(d, flags))
			continue;
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

static const AudioObjectPropertyAddress prop_odev_fmt = {
	kAudioDevicePropertyStreamFormat, kAudioDevicePropertyScopeOutput, kAudioObjectPropertyElementMaster
};

int ffcoraud_open(ffcoraud_buf *snd, int dev, ffpcm *fmt, uint bufsize)
{
	int r = 0;

	if (dev < 0) {
		dev = coraud_dev_default(FFCORAUD_DEV_PLAYBACK);
		if (dev < 0)
			return -1;
	}

	AudioStreamBasicDescription asbd = {};
	uint sz = sizeof(asbd);
	if (0 != AudioObjectGetPropertyData(dev, &prop_odev_fmt, 0, NULL, &sz, &asbd))
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

	if (0 != AudioDeviceCreateIOProcID(dev, &coraud_ioproc, snd, (AudioDeviceIOProcID*)&snd->aprocid)
		|| snd->aprocid == NULL)
		return -1;

	bufsize = ffpcm_bytes(fmt, bufsize);
	if (NULL == ffarr_alloc(&snd->data, bufsize))
		return -1;

	snd->dev = dev;
	return 0;
}

void ffcoraud_close(ffcoraud_buf *snd)
{
	AudioDeviceDestroyIOProcID(snd->dev, snd->aprocid);
	snd->dev = 0;
	snd->aprocid = NULL;
	ffarr_free(&snd->data);
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
	size_t i = ffmin(n, snd->data.len);
	ffmemcpy(d, snd->data.ptr, i);
	_ffarr_rmleft(&snd->data, i, sizeof(char));
	ffmem_zero((char*)d + n, n - i);
	return 0;
}

ssize_t ffcoraud_write(ffcoraud_buf *snd, const void *data, size_t len)
{
	if (NULL == ffarr_append(&snd->data, data, len))
		return -1;
	return len;
}
