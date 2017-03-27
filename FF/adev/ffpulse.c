/**
Copyright (c) 2017 Simon Zolin
*/

/* Pulse Audio playback:

User calls ffpulse_write() until it returns 0 (audio buffer is full).
User calls ffpulse_async(enable=1) when he's ready to receive events from Pulse.
User handler function is called by timer handler:
  timer handler -> pa_mainloop_iterate()... -> stm_on_write() -> ffpulse_buf.handler()
*/

#include <FF/adev/pulse.h>
#include <FF/string.h>


enum {
	PULSE_DEFNFY = 4,
};

typedef struct _ffpulse {
	pa_mainloop *ml;
	pa_context *ctx;
	fffd kq;
} _ffpulse;

static _ffpulse *_p;

enum {
	ESYS = 0x10000,
	E_pa_mainloop_new,
	E_pa_mainloop_iterate,
	E_pa_context_new_with_proplist,
};

static const char* const errs[] = {
	"pa_mainloop_new()",
	"pa_mainloop_iterate()",
	"pa_context_new_with_proplist()",
};

const char* ffpulse_errstr(int e)
{
	e = -e;

	if (e == ESYS)
		return fferr_strp(fferr_last());

	else if (e > ESYS) {
		e -= ESYS + 1;
		if ((uint)e > FFCNT(errs))
			return "";
		return errs[e];
	}

	return pa_strerror(e);
}

int ffpulse_init(fffd kq)
{
	int r, n, st;
	pa_mainloop_api *mlapi;
	_ffpulse *p;

	if (_p != NULL) {
		_p->kq = kq;
		return 0;
	}

	if (NULL == (p = ffmem_new(_ffpulse)))
		return -ESYS;
	if (NULL == (p->ml = pa_mainloop_new())) {
		r = -E_pa_mainloop_new;
		goto end;
	}
	mlapi = pa_mainloop_get_api(p->ml);
	if (NULL == (p->ctx = pa_context_new_with_proplist(mlapi, "ff", NULL))) {
		r = -E_pa_context_new_with_proplist;
		goto end;
	}

	pa_context_connect(p->ctx, NULL, 0, NULL);

	for (;;) {
		st = pa_context_get_state(p->ctx);
		if (st == PA_CONTEXT_READY)
			break;
		switch (st) {
		case PA_CONTEXT_FAILED:
		case PA_CONTEXT_TERMINATED:
			r = -pa_context_errno(p->ctx);
			goto end;
		}

		n = pa_mainloop_iterate(p->ml, 1, NULL);
		if (n < 0) {
			r = -E_pa_mainloop_iterate;
			goto end;
		}
	}

	p->kq = kq;
	r = 0;
	_p = p;

end:
	if (r != 0)
		ffpulse_uninit();
	return r;
}

void ffpulse_uninit()
{
	_ffpulse *p = _p;

	if (p == NULL)
		return;

	if (p->ctx != NULL) {
		pa_context_disconnect(p->ctx);
		pa_context_unref(p->ctx);
		p->ctx = NULL;
	}

	FF_SAFECLOSE(p->ml, NULL, pa_mainloop_free);
	ffmem_free0(_p);
}


static void dev_on_next_sink(pa_context *c, const pa_sink_info *l, int eol, void *udata)
{
	if (eol > 0)
		return;

	ffpulse_dev *d = udata;
	d->id = ffsz_alcopyz(l->name);
	d->name = ffsz_alcopyz(l->description);
	if (d->id == NULL || d->name == NULL)
		d->err = 1;
}

static void dev_on_next_source(pa_context *c, const pa_source_info *l, int eol, void *udata)
{
	if (eol > 0)
		return;

	ffpulse_dev *d = udata;
	d->id = ffsz_alcopyz(l->name);
	d->name = ffsz_alcopyz(l->description);
	if (d->id == NULL || d->name == NULL)
		d->err = 1;
}

void ffpulse_devdestroy(ffpulse_dev *d)
{
	FF_SAFECLOSE(d->op, NULL, pa_operation_unref);
	ffmem_safefree0(d->id);
	ffmem_safefree0(d->name);
}

int ffpulse_devnext(ffpulse_dev *d, uint flags)
{
	int r, st;

	if (d->op == NULL) {
		if (flags == FFPULSE_DEV_PLAYBACK)
			d->op = pa_context_get_sink_info_list(_p->ctx, &dev_on_next_sink, d);
		else
			d->op = pa_context_get_source_info_list(_p->ctx, &dev_on_next_source, d);
	}

	ffmem_safefree0(d->id);
	ffmem_safefree0(d->name);
	d->err = 0;

	for (;;) {
		st = pa_operation_get_state(d->op);
		if (st == PA_OPERATION_DONE || st == PA_OPERATION_CANCELLED)
			return 1;

		r = pa_mainloop_iterate(_p->ml, 1, NULL);
		if (r < 0)
			return -E_pa_mainloop_iterate;

		if (d->err)
			return -ESYS;
		if (d->id != NULL)
			break;
	}

	return 0;
}


static void stm_on_write(pa_stream *s, size_t nbytes, void *udata)
{
	ffpulse_buf *snd = udata;
	FFDBG_PRINTLN(10, "n:%L", nbytes);
	if (snd->callback_wait) {
		snd->callback_wait = 0;
		snd->handler(snd->udata);
	}
}

static void on_tmr(void *udata)
{
	ffpulse_buf *snd = udata;
	fftmr_read(snd->tmr);

	FFDBG_PRINTLN(10, "", 0);

	int r;
	for (;;) {
		r = pa_mainloop_iterate(_p->ml, 0, NULL);
		if (r <= 0)
			break;
	}
}

static FFINL int tmr_create(ffpulse_buf *snd, const ffpcm *fmt)
{
	if (FF_BADTMR == (snd->tmr = fftmr_create(0)))
		return -ESYS;

	snd->evtmr.oneshot = 0;
	snd->evtmr.handler = &on_tmr;
	snd->evtmr.udata = snd;
	uint n = snd->nfy_interval;
	if (n == 0)
		n = ffpcm_bytes2time(fmt, snd->bufsize / PULSE_DEFNFY);

	if (0 != fftmr_start(snd->tmr, _p->kq, ffkev_ptr(&snd->evtmr), n))
		return -ESYS;

	return 0;
}

static const ushort afmt_ff[] = {
	FFPCM_16, FFPCM_24, FFPCM_32, FFPCM_FLOAT,
};

static const uint afmt_pa[] = {
	PA_SAMPLE_S16LE, PA_SAMPLE_S24LE, PA_SAMPLE_S32LE, PA_SAMPLE_FLOAT32LE,
};

static FFINL int findfmt(uint f)
{
	int r;
	if (-1 == (r = ffint_find2(afmt_ff, FFCNT(afmt_ff), f))) {
		fferr_set(EINVAL);
		return -ESYS;
	}
	return afmt_pa[r];
}

int ffpulse_open(ffpulse_buf *snd, const char *dev, ffpcm *fmt, uint bufsize_msec)
{
	uint f;
	int r;

	pa_sample_spec spec;
	if (0 > (r = findfmt(fmt->format)))
		return r;
	spec.format = r;
	spec.rate = fmt->sample_rate;
	spec.channels = fmt->channels;
	snd->stm = pa_stream_new(_p->ctx, "ff", &spec, NULL);

	pa_buffer_attr attr;
	memset(&attr, 0xff, sizeof(pa_buffer_attr));
	attr.tlength = ffpcm_bytes(fmt, bufsize_msec);

	f = PA_STREAM_START_CORKED;
	pa_stream_set_write_callback(snd->stm, &stm_on_write, snd);
	pa_stream_connect_playback(snd->stm, dev, &attr, f, NULL, NULL);

	for (;;) {
		r = pa_stream_get_state(snd->stm);
		if (r == PA_STREAM_READY)
			break;
		else if (r == PA_STREAM_FAILED) {
			r = -pa_context_errno(_p->ctx);
			goto end;
		}

		r = pa_mainloop_iterate(_p->ml, 1, NULL);
		if (r < 0) {
			r = -E_pa_mainloop_iterate;
			goto end;
		}
	}

	snd->bufsize = pa_stream_writable_size(snd->stm);

	if (0 != (r = tmr_create(snd, fmt)))
		goto end;

	r = 0;

end:
	if (r != 0)
		ffpulse_close(snd);
	return r;
}

void ffpulse_close(ffpulse_buf *snd)
{
	if (snd->stm != NULL) {
		pa_stream_disconnect(snd->stm);
		pa_stream_unref(snd->stm);
		snd->stm = NULL;
	}
	if (snd->tmr != FF_BADTMR) {
		fftmr_close(snd->tmr, _p->kq);
		snd->tmr = FF_BADTMR;
	}
}

size_t ffpulse_filled(ffpulse_buf *snd)
{
	return snd->bufsize - pa_stream_writable_size(snd->stm);
}

ssize_t ffpulse_write(ffpulse_buf *snd, const void *data, size_t len, size_t dataoff)
{
	int r;
	size_t n, all = 0;
	void *buf;

	while (len != 0) {
		n = pa_stream_writable_size(snd->stm);
		pa_stream_begin_write(snd->stm, &buf, &n);

		if (n == 0) {
			if (snd->autostart && 0 != (r = ffpulse_start(snd)))
				goto err;
			break;
		}

		n = ffmin(len, n);
		ffmemcpy(buf, data + dataoff, n);
		pa_stream_write(snd->stm, buf, n, NULL, 0, PA_SEEK_RELATIVE);
		FFDBG_PRINTLN(10, "pa_stream_write():%L", n);
		dataoff += n;
		len -= n;
		all += n;
	}

	return all;

err:
	return r;
}

int ffpulse_async(ffpulse_buf *snd, uint enable)
{
	snd->callback_wait = enable;
	return 0;
}

static void stm_on_op(pa_stream *s, int success, void *udata)
{
}

static void op_wait(pa_operation *op)
{
	int st;
	for (;;) {
		st = pa_operation_get_state(op);
		if (st == PA_OPERATION_DONE || st == PA_OPERATION_CANCELLED)
			break;
		if (0 > pa_mainloop_iterate(_p->ml, 1, NULL))
			break;
	}
}

int ffpulse_start(ffpulse_buf *snd)
{
	if (!pa_stream_is_corked(snd->stm))
		return 0;

	pa_operation *op = pa_stream_cork(snd->stm, 0, &stm_on_op, snd);
	op_wait(op);
	pa_operation_unref(op);
	return 0;
}

int ffpulse_stop(ffpulse_buf *snd)
{
	if (pa_stream_is_corked(snd->stm))
		return 0;

	pa_operation *op = pa_stream_cork(snd->stm, 1, &stm_on_op, snd);
	op_wait(op);
	pa_operation_unref(op);
	return 0;
}

int ffpulse_clear(ffpulse_buf *snd)
{
	pa_operation *op = pa_stream_flush(snd->stm, &stm_on_op, snd);
	op_wait(op);
	pa_operation_unref(op);
	return 0;
}

int ffpulse_drain(ffpulse_buf *snd)
{
	if (0 == ffpulse_filled(snd))
		return 1;

	if (snd->draining)
		return 0;
	pa_operation *op = pa_stream_drain(snd->stm, &stm_on_op, snd);
	pa_operation_unref(op);
	snd->draining = 1;
	return 0;
}
