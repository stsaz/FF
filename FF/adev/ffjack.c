/** JACK wrapper.
Copyright (c) 2020 Simon Zolin
*/

#include <FF/adev/jack.h>
#include <FF/array.h>
#include <jack/jack.h>


static void _ffjack_shut(void *arg);
static int _ffjack_process(jack_nframes_t nframes, void *arg);

enum {
	JACK_EOPEN = _FFJACK_EMAX,
	JACK_EACTIVATE,
	JACK_EPORTREG,
	JACK_EGETPORTS,
	JACK_ECONNECT,
	JACK_ESHUT,

	JACK_ESYS,
};

static const char *const serr[] = {
	"bad audio format",
	"jack_client_open",
	"jack_activate",
	"jack_port_register",
	"jack_get_ports",
	"jack_connect",
	"jack_on_shutdown",
};

const char* ffjack_errstr(int e)
{
	if (e == JACK_ESYS)
		return fferr_strp(fferr_last());
	e--;
	if ((uint)e <= FFCNT(serr))
		return serr[e];
	return "";
}

static jack_client_t *client;

static void _ffjack_log(const char *s)
{
	FFDBG_PRINTLN(10, "%s", s);
}

int ffjack_init(const char *appname)
{
	jack_status_t status;
	FF_ASSERT(client == NULL);
	jack_set_info_function(&_ffjack_log);
	jack_set_error_function(&_ffjack_log);
	if (NULL == (client = jack_client_open(appname, JackNullOption, &status)))
		return JACK_EOPEN;
	return 0;
}

void ffjack_uninit()
{
	if (client == NULL)
		return;
	jack_client_close(client);
	client = NULL;
}


void ffjack_devdestroy(ffjack_dev *d)
{
	jack_free(d->names);
	d->names = NULL;
}

int ffjack_devnext(ffjack_dev *d, uint flags)
{
	if (d->names != NULL) {
		d->idx++;
		if (d->names[d->idx] == NULL)
			return 1;
		return 0;
	}

	const char **portnames = NULL;
	uint f = JackPortIsInput;
	if (flags == FFJACK_DEV_CAPTURE)
		f = JackPortIsOutput;
	portnames = jack_get_ports(client, NULL, NULL, f);
	if (portnames == NULL)
		return 1;
	d->names = portnames;
	return 0;
}


int ffjack_open(ffjack_buf *snd, const char *dev, ffpcm *fmt, uint flags)
{
	int rc = JACK_ESYS;
	const char **portnames = NULL;

	uint rate = jack_get_sample_rate(client);
	if (fmt->format != FFPCM_FLOAT
		|| fmt->sample_rate != rate
		|| fmt->channels != 1) {

		fmt->format = FFPCM_FLOAT;
		fmt->sample_rate = rate;
		fmt->channels = 1;
		return FFJACK_EFMT;
	}

	jack_set_process_callback(client, &_ffjack_process, snd);
	jack_on_shutdown(client, &_ffjack_shut, snd);
	if (0 != jack_activate(client))
		return JACK_EACTIVATE;

	if (NULL == (snd->port = jack_port_register(client, "ff-capture", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0)))
		return JACK_EPORTREG;

	if (dev == NULL) {
		portnames = jack_get_ports(client, NULL, NULL, JackPortIsOutput);
		if (portnames == NULL) {
			rc = JACK_EGETPORTS;
			goto end;
		}
		dev = portnames[0];
	}

	if (0 != jack_connect(client, dev, jack_port_name(snd->port))) {
		rc = JACK_ECONNECT;
		goto end;
	}

	if (flags & FFJACK_DEV_CAPTURE) {
		size_t bufsize = jack_get_buffer_size(client);
		bufsize *= 2 * sizeof(float);
		if (NULL == ffstr_alloc(&snd->data, bufsize))
			goto end;
		snd->data.len = bufsize;

		bufsize = ff_align_power2(bufsize);
		if (NULL == ffstr_alloc(&snd->bufdata, bufsize))
			goto end;
		snd->bufdata.len = bufsize;
		ffringbuf_init(&snd->buf, snd->bufdata.ptr, snd->bufdata.len);
	}

	rc = 0;

end:
	jack_free(portnames);
	if (rc != 0)
		ffjack_close(snd);
	return rc;
}

void ffjack_close(ffjack_buf *snd)
{
	if (snd->port != NULL) {
		jack_port_unregister(client, snd->port);
		snd->port = NULL;
	}
	ffstr_free(&snd->bufdata);
	ffstr_free(&snd->data);
}

int ffjack_start(ffjack_buf *snd)
{
	snd->started = 1;
	return 0;
}

int ffjack_stop(ffjack_buf *snd)
{
	snd->started = 0;
	return 0;
}

int ffjack_async(ffjack_buf *snd, uint flags)
{
	if (!(flags & 1)) {
		snd->callback_wait = snd->signalled = 0;
		return 0;
	}

	if (snd->signalled) {
		snd->signalled = 0;
		snd->handler(snd->udata);
		return 1;
	}

	snd->callback_wait = 1;
	return 0;
}

static void _ffjack_shut(void *arg)
{
	ffjack_buf *snd = arg;
	snd->shut = 1;
}

/** Called by JACK when new audio data is available. */
static int _ffjack_process(jack_nframes_t nframes, void *arg)
{
	ffjack_buf *snd = arg;

	FFDBG_PRINTLN(10, "nframes:%d  callback_wait:%d", nframes, snd->callback_wait);

	if (!snd->started)
		return 0;

	const float *d = jack_port_get_buffer(snd->port, nframes);
	size_t n = nframes * sizeof(float);
	uint r = ffringbuf_lock_write(&snd->buf, d, n);
	if (r != n)
		snd->overrun = 1;

	if (snd->callback_wait) {
		snd->callback_wait = 0;
		snd->handler(snd->udata);
	} else {
		snd->signalled = 1;
	}

	return 0;
}

int ffjack_read(ffjack_buf *snd, ffstr *data)
{
	if (snd->shut)
		return JACK_ESHUT;

	size_t n = ffringbuf_lock_read(&snd->buf, snd->data.ptr, snd->data.len);
	ffstr_set(data, snd->data.ptr, n);
	return 0;
}
