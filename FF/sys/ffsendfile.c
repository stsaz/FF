/**
Copyright (c) 2016 Simon Zolin
*/

#include <FF/sys/sendfile.h>


#ifdef FF_UNIX
int64 ffsf_sendasync(ffsf *sf, ffaio_task *t, ffaio_handler handler)
{
	int64 r;

	if (t->wpending) {
		t->wpending = 0;
		return _ffaio_result(t);
	}

	r = ffsf_send(sf, t->sk, 0);
	if (!(r < 0 && fferr_again(fferr_last())))
		return r;

	t->whandler = handler;
	t->wpending = 1;
	return FFAIO_ASYNC;
}
#endif


#ifdef FF_WIN
static ssize_t fmap_send(fffilemap *fm, ffskt sk, int flags)
{
	ffstr dst;
	if (0 != fffile_mapbuf(fm, &dst))
		return -1;
	return ffskt_send(sk, dst.ptr, dst.len, 0);
}

int64 ffsf_send(ffsf *sf, ffskt sk, int flags)
{
	int64 sent = 0;
	int64 r;
	const sf_hdtr *ht = &sf->ht;

	if (ht->hdr_cnt != 0) {
		r = ffskt_sendv(sk, ht->headers, ht->hdr_cnt);
		if (r == -1)
			goto err;

		sent = r;

		if ((sf->fm.fsize != 0 || ht->trl_cnt != 0)
			&& (size_t)r != ffiov_size(ht->headers, ht->hdr_cnt))
			goto done; //headers are not sent yet completely
	}

	if (sf->fm.fsize != 0) {
		r = fmap_send(&sf->fm, sk, flags);
		if (r == -1)
			goto err;

		sent += r;
		if ((size_t)r != sf->fm.fsize)
			goto done; //file is not sent yet completely
	}

	if (ht->trl_cnt != 0) {
		r = ffskt_sendv(sk, ht->trailers, ht->trl_cnt);
		if (r == -1)
			goto err;

		sent += r;
	}

done:
	return sent;

err:
	if (sent != 0)
		return sent;
	return -1;
}

int64 ffsf_sendasync(ffsf *sf, ffaio_task *t, ffaio_handler handler)
{
	const sf_hdtr *ht = &sf->ht;
	int64 r;

	if (t->wpending) {
		t->wpending = 0;
		return _ffaio_result(t);
	}

	r = ffsf_send(sf, t->sk, 0);
	if (!(r < 0 && fferr_again(fferr_last())))
		return r;

	if (ht->hdr_cnt != 0)
		return ffaio_sendv(t, handler, ht->headers, ht->hdr_cnt);

	if (sf->fm.fsize != 0) {
		ffstr dst;
		if (0 != fffile_mapbuf(&sf->fm, &dst))
			return FFAIO_ERROR;

		return ffaio_send(t, handler, dst.ptr, dst.len);
	}

	if (ht->trl_cnt != 0)
		return ffaio_sendv(t, handler, ht->trailers, ht->trl_cnt);

	return FFAIO_ERROR;
}
#endif //FF_WIN

int ffsf_shift(ffsf *sf, uint64 by)
{
	size_t r;
	sf_hdtr *ht = &sf->ht;

	if (sf->ht.hdr_cnt != 0) {
		r = ffiov_shiftv(ht->headers, ht->hdr_cnt, &by);
		ht->headers += r;
		ht->hdr_cnt -= (int)r;
		if (ht->hdr_cnt != 0)
			return 1;
	}

	if (sf->fm.fsize != 0) {
		uint64 fby = ffmin64(sf->fm.fsize, by);
		if (0 != fffile_mapshift(&sf->fm, (int64)fby))
			return 1;
		by -= fby;
	}

	if (ht->trl_cnt != 0) {
		r = ffiov_shiftv(ht->trailers, ht->trl_cnt, &by);
		ht->trailers += r;
		ht->trl_cnt -= (int)r;
		if (ht->trl_cnt != 0)
			return 1;
	}

	return 0;
}

int ffsf_nextchunk(ffsf *sf, ffstr *dst)
{
	if (sf->ht.hdr_cnt != 0) {
		ffstr_setiovec(dst, &sf->ht.headers[0]);
		return sf->fm.fsize != 0 || 0 != ((sf->ht.hdr_cnt - 1) | sf->ht.trl_cnt);

	} else if (sf->fm.fsize != 0) {
		if (0 != fffile_mapbuf(&sf->fm, dst))
			return -1;
		return sf->fm.fsize != dst->len || sf->ht.trl_cnt != 0;

	} else if (sf->ht.trl_cnt != 0) {
		ffstr_setiovec(dst, &sf->ht.trailers[0]);
		return (sf->ht.trl_cnt - 1) != 0;
	}

	ffstr_null(dst);
	return 0;
}
