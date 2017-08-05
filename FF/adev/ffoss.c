/**
Copyright (c) 2017 Simon Zolin
*/

#include <FF/adev/oss.h>
#include <FFOS/error.h>

#include <fcntl.h>


const char* ffoss_errstr(int e)
{
	return fferr_strp(-e);
}


void ffoss_devdestroy(ffoss_dev *d)
{
	FF_SAFECLOSE(d->mixer, FF_BADFD, close);
}

int ffoss_devnext(ffoss_dev *d, uint flags)
{
	if (d->mixer == FF_BADFD) {
		if (FF_BADFD == (d->mixer = open("/dev/mixer", O_RDONLY, 0)))
			return -1;

		oss_sysinfo si;
		ffmem_tzero(&si);
		ioctl(d->mixer, SNDCTL_SYSINFO, &si);
		d->ndevs = si.numaudios;
	}

	for (;;) {
		if (d->idx == d->ndevs)
			return 1;

		ffmem_tzero(&d->ainfo);
		d->ainfo.dev = d->idx++;
		if (0 > ioctl(d->mixer, SNDCTL_AUDIOINFO_EX, &d->ainfo))
			return -1;

		if (((flags & FFOSS_DEV_PLAYBACK) && (d->ainfo.caps & PCM_CAP_OUTPUT))
			|| ((flags & FFOSS_DEV_CAPTURE) && (d->ainfo.caps & PCM_CAP_INPUT)))
			break;
	}

	d->id = d->ainfo.devnode;
	d->name = d->ainfo.name;
	return 0;
}


static int _ff2oss(uint fmt)
{
	switch (fmt) {
	case FFPCM_16:
		return AFMT_S16_LE;
	case FFPCM_32:
		return AFMT_S32_LE;
	}
	return -1;
}

static int _oss2ff(uint ossfmt)
{
	switch (ossfmt) {
	case AFMT_S16_LE:
		return FFPCM_16;
	case AFMT_S32_LE:
		return FFPCM_32;
	}
	return -1;
}

int ffoss_open(ffoss_buf *snd, const char *dev, ffpcm *fmt, uint bufsize_msec, uint flags)
{
	int r = 0;

	FF_ASSERT((flags & (FFOSS_DEV_PLAYBACK | FFOSS_DEV_CAPTURE)) != 0);
	FF_ASSERT((flags & (FFOSS_DEV_PLAYBACK | FFOSS_DEV_CAPTURE)) != (FFOSS_DEV_PLAYBACK | FFOSS_DEV_CAPTURE));

	uint f;
	f = (flags & FFOSS_DEV_PLAYBACK) ? O_WRONLY : O_RDONLY;
	f |= O_EXCL;
	if (dev == NULL)
		dev = "/dev/dsp";
	if (FF_BADFD == (snd->fd = open(dev, f, 0)))
		goto syserr;

	int fmt1, fmt2;
	fmt1 = _ff2oss(fmt->format);
	fmt2 = (fmt1 == -1) ? AFMT_S16_LE : fmt1;
	if (0 > ioctl(snd->fd, SNDCTL_DSP_SETFMT, &fmt2))
		goto syserr;
	if (fmt2 != fmt1) {
		if (0 > (r = _oss2ff(fmt2))) {
			r = FFOSS_EFMT;
			goto end;
		}
		fmt->format = r;
		r = FFOSS_EFMT;
	}

	uint ch = fmt->channels;
	if (0 > ioctl(snd->fd, SNDCTL_DSP_CHANNELS, &ch))
		goto syserr;
	if (ch != fmt->channels) {
		fmt->channels = ch;
		r = FFOSS_EFMT;
	}

	uint rate = fmt->sample_rate;
	if (0 > ioctl(snd->fd, SNDCTL_DSP_SPEED, &rate))
		goto syserr;
	if (rate != fmt->sample_rate) {
		fmt->sample_rate = rate;
		r = FFOSS_EFMT;
	}

	if (r == FFOSS_EFMT)
		goto end;

	audio_buf_info info;

	if (bufsize_msec != 0) {
		ffmem_tzero(&info);
		ioctl(snd->fd, SNDCTL_DSP_GETOSPACE, &info);

		uint frag_num = ffpcm_bytes(fmt, bufsize_msec) / info.fragsize;
		uint fr = (frag_num << 16) | (uint)log2(info.fragsize); //buf_size = frag_num * 2^n
		ioctl(snd->fd, SNDCTL_DSP_SETFRAGMENT, &fr);
	}

	ffmem_tzero(&info);
	ioctl(snd->fd, SNDCTL_DSP_GETOSPACE, &info);
	snd->bufsize = info.fragstotal * info.fragsize;

	return 0;

syserr:
	r = fferr_last();
end:
	ffoss_close(snd);
	return -r;
}
void ffoss_close(ffoss_buf *snd)
{
	FF_SAFECLOSE(snd->fd, FF_BADFD, close);
}

size_t ffoss_filled(ffoss_buf *snd)
{
	audio_buf_info info;
	ffmem_tzero(&info);
	ioctl(snd->fd, SNDCTL_DSP_GETOSPACE, &info);
	return snd->bufsize - info.fragments * info.fragsize;
}

ssize_t ffoss_write(ffoss_buf *snd, const void *data, size_t len, size_t dataoff)
{
	int r;
	if (0 > (r = write(snd->fd, data, len)))
		goto end;
	return r;

end:
	return -fferr_last();
}

int ffoss_start(ffoss_buf *snd)
{
	return 0;
}

int ffoss_stop(ffoss_buf *snd)
{
	ioctl(snd->fd, SNDCTL_DSP_HALT, 0);
	return 0;
}

int ffoss_clear(ffoss_buf *snd)
{
	return 0;
}

int ffoss_drain(ffoss_buf *snd)
{
	ioctl(snd->fd, SNDCTL_DSP_SYNC, 0);
	return 1;
}
