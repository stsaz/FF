/** Send file.
Copyright (c) 2014 Simon Zolin
*/

#pragma once

#include <FF/filemap.h>
#include <FFOS/socket.h>
#include <FFOS/asyncio.h>


typedef struct ffsf {
	fffilemap fm;
	sf_hdtr ht;
} ffsf;

static FFINL void ffsf_init(ffsf *sf) {
	fffile_mapinit(&sf->fm);
	ffmem_tzero(&sf->ht);
}

/** Close file mapping in sf. */
#define ffsf_close(sf)  fffile_mapclose(&(sf)->fm)

/** Get the overall number of bytes. */
static FFINL uint64 ffsf_len(const ffsf *sf) {
	return sf->fm.fsize
		+ ffiov_size(sf->ht.headers, sf->ht.hdr_cnt)
		+ ffiov_size(sf->ht.trailers, sf->ht.trl_cnt);
}

/** Return TRUE if empty. */
static FFINL ffbool ffsf_empty(const ffsf *sf) {
	return sf->fm.fsize == 0
		&& (sf->ht.hdr_cnt | sf->ht.trl_cnt) == 0;
}

#ifdef FF_UNIX

/** Send file synchronously.
Return the number of bytes sent. */
static FFINL int64 ffsf_send(ffsf *sf, ffskt sk, int flags) {
	uint64 sent = 0;
	int r = ffskt_sendfile(sk, sf->fm.fd, sf->fm.foff, sf->fm.fsize, &sf->ht, &sent, flags);
	if (r != 0 && sent == 0)
		return -1;
	return sent;
}

#else

FF_EXTN int64 ffsf_send(ffsf *sf, ffskt sk, int flags);
#endif

/** Send file asynchronously.
Return bytes sent or enum FFAIO_RET. */
FF_EXTN int64 ffsf_sendasync(ffsf *sf, ffaio_task *t, ffaio_handler handler);

/** Shift file mapping and sf_hdtr.
Return 0 if there is no more data. */
FF_EXTN int ffsf_shift(ffsf *sf, uint64 by);

/** Get next buffer of header, file (mapping) or trailer.
Return 0 if there's no more data.  Return -1 on error. */
FF_EXTN int ffsf_nextchunk(ffsf *sf, ffstr *dst);
