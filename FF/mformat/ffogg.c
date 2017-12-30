/**
Copyright (c) 2015 Simon Zolin
*/

#include <FF/mformat/ogg.h>
#include <FF/string.h>
#include <FFOS/error.h>
#include <FFOS/mem.h>


enum {
	OGG_MAXPAGE = OGG_MAXHDR + 255 * 255,
	MAX_NOSYNC = 256 * 1024,
	OGG_GPOS_UNDEF = ~0ULL,
};


static int _ffogg_gethdr(ffogg *o, ogg_hdr **h);
static int _ffogg_getbody(ffogg *o);
static int _ffogg_open(ffogg *o);
static int _ffogg_seek(ffogg *o);


enum { I_HDR, I_BODY, I_INFO
	, I_SEEK_EOS, I_SEEK_EOS2, I_SEEK_EOS3
	, I_SEEK, I_SEEK2
	, I_PAGE, I_PKT };

static const char* const ogg_errstr[] = {
	"",
	"seek error",
	"bad OGG header",
	"unsupported page version",
	"the serial number of the page did not match the serial number of the bitstream",
	"out of order OGG page",
	"unrecognized data before OGG page",
	"CRC mismatch",
	"couldn't find OGG page",
	"expected continued packet",
};

const char* ffogg_errstr(int e)
{
	if (e == OGG_ESYS)
		return fferr_strp(fferr_last());

	if (e >= 0)
		return ogg_errstr[e];
	return "";
}

#define ERR(o, n) \
	(o)->err = n, FFOGG_RERR


void ffogg_init(ffogg *o)
{
	ffmem_tzero(o);
	o->state = I_HDR;
}

uint ffogg_bitrate(ffogg *o, uint sample_rate)
{
	if (o->total_samples == 0 || o->total_size == 0)
		return 0;
	return ffpcm_brate(o->total_size - o->off_data, o->total_samples, sample_rate);
}

/** Get the whole OGG page header.
Return 0 on success. o->data points to page body. */
static int _ffogg_gethdr(ffogg *o, ogg_hdr **h)
{
	int r, n = 0;
	const ogg_hdr *hdr;

	struct ffbuf_gather d = {0};
	ffstr_set(&d.data, o->data, o->datalen);
	d.ctglen = OGG_MAXHDR;

	while (FFBUF_DONE != (r = ffbuf_gather(&o->buf, &d))) {

		if (r == FFBUF_ERR) {
			o->err = OGG_ESYS;
			return FFOGG_RERR;

		} else if (r == FFBUF_MORE) {
			goto more;
		}

		if (-1 != (n = ogg_findpage(o->buf.ptr, o->buf.len, NULL))) {
			hdr = (void*)(o->buf.ptr + n);
			o->hdrsize = sizeof(ogg_hdr) + hdr->nsegments;
			o->pagesize = ogg_pagesize(hdr);
			d.ctglen = o->hdrsize;
			d.off = n + 1;
		}
	}
	o->off += d.data.ptr - (char*)o->data;
	o->data = d.data.ptr;
	o->datalen = d.data.len;

	hdr = (void*)o->buf.ptr;
	*h = (void*)o->buf.ptr;

	if (o->bytes_skipped != 0)
		o->bytes_skipped = 0;

	FFDBG_PRINTLN(10, "page #%u  end-pos:%xU  packets:%u  continued:%u  size:%u  offset:%xU"
		, ffint_ltoh32(hdr->number), ffint_ltoh64(hdr->granulepos)
		, ogg_packets(hdr), (hdr->flags & OGG_FCONTINUED) != 0
		, o->pagesize, o->off - o->hdrsize);

	if (n != 0) {
		o->err = OGG_EJUNKDATA;
		return FFOGG_RWARN;
	}
	return FFOGG_RDONE;

more:
	o->bytes_skipped += o->datalen;
	if (o->bytes_skipped > MAX_NOSYNC) {
		o->err = OGG_ENOSYNC;
		return FFOGG_RERR;
	}

	o->off += o->datalen;
	return FFOGG_RMORE;
}

static int _ffogg_getbody(ffogg *o)
{
	int r = ffarr_append_until(&o->buf, o->data, o->datalen, o->pagesize);
	if (r == 0) {
		o->off += o->datalen;
		return FFOGG_RMORE;
	} else if (r == -1) {
		o->err = OGG_ESYS;
		return FFOGG_RERR;
	}
	FFARR_SHIFT(o->data, o->datalen, r);
	o->off += r;
	o->buf.len = 0;
	const ogg_hdr *h = (void*)o->buf.ptr;

	uint crc = ogg_checksum(o->buf.ptr, o->pagesize);
	uint hcrc = ffint_ltoh32(h->crc);
	if (crc != hcrc) {
		FFDBG_PRINTLN(1, "Bad page CRC:%xu, CRC in header:%xu", crc, hcrc);
		o->err = OGG_ECRC;
		return FFOGG_RERR;
	}

	if (h->version != 0)
		return ERR(o, OGG_EVERSION);

	uint serial = ffint_ltoh32(h->serial);
	if (!o->init_done)
		o->serial = serial;
	else if (serial != o->serial)
		return ERR(o, OGG_ESERIAL);

	o->page_continued = (h->flags & OGG_FCONTINUED) != 0;
	o->page_last = (h->flags & OGG_FLAST) != 0;

	uint pagenum = ffint_ltoh32(h->number);
	if (o->page_num != 0 && pagenum != o->page_num + 1)
		o->pagenum_err = 1;
	o->page_num = pagenum;

	o->page_gpos = ffint_ltoh64(h->granulepos);
	o->segoff = 0;
	o->bodyoff = 0;
	return FFOGG_RDONE;
}

static int _ffogg_open(ffogg *o)
{
	int r;
	ogg_hdr *h;

	for (;;) {
	switch (o->state) {

	case I_SEEK_EOS:
		o->buf.len = 0;
		o->off = o->total_size - ffmin(OGG_MAXPAGE, o->total_size);
		o->state = I_SEEK_EOS2;
		return FFOGG_RSEEK;

	case I_SEEK_EOS2:
		for (;;) {
			r = _ffogg_gethdr(o, &h);
			if (r != FFOGG_RDONE && !(r == FFOGG_RWARN && o->err == OGG_EJUNKDATA)) {
				if (o->off == o->total_size)
					break; // no eos page
				return FFOGG_RMORE;
			}
			uint buflen = o->buf.len;
			o->buf.len = 0;

			uint64 gpos = ffint_ltoh64(h->granulepos);
			if (gpos != OGG_GPOS_UNDEF)
				o->total_samples = gpos - o->first_sample;
			if (h->flags & OGG_FLAST)
				break;

			o->off += o->pagesize - buflen;
			if (o->off == o->total_size)
				break; // no eos page
			return FFOGG_RSEEK;
		}

		if (o->total_samples != 0) {
			o->seektab[1].sample = o->total_samples;
			o->seektab[1].off = o->total_size - o->off_data;
		}

		o->state = I_SEEK_EOS3;
		return FFOGG_RINFO;

	case I_SEEK_EOS3:
		o->off = o->off_data;
		o->state = I_HDR;
		return FFOGG_RSEEK;
	}
	}
	//unreachable
}

void ffogg_close(ffogg *o)
{
	ffarr_free(&o->buf);
	ffarr_free(&o->pktdata);
}

void ffogg_seek(ffogg *o, uint64 sample)
{
	if (sample >= o->total_samples || o->total_size == 0)
		return;
	o->seek_sample = sample;
	if (o->state == I_SEEK_EOS3)
		o->firstseek = 1;
	o->state = I_SEEK;
	ffmemcpy(o->seekpt, o->seektab, sizeof(o->seekpt));
	o->skoff = (uint64)-1;
	o->buf.len = 0;

	o->continued = 0,  o->pktdata.len = 0;
}

/* Seeking in OGG:

... [P1 ^P2 P3] P4 ...
where:
 P{N} are OGG pages
 ^ is the target page
 [...] are the search boundaries

An OGG page is not enough to determine whether this page is our target,
 e.g. audio position of P2 becomes known only from P1.granule_pos.
Therefore, we need additional processing:

 0. Parse 2 consecutive pages and pass info from the 2nd page to ffpcm_seek(),
    e.g. parse P2 and P3, then pass P3 to ffpcm_seek().

 1. If the 2nd page is out of search boundaries (P4), we must not pass it to ffpcm_seek().
    We seek backward on a file (somewhere to [P1..P2]) and try again (cases 0 or 2).

 2. If the currently processed page is the first within search boundaries (P2 in [^P2 P3])
    and it's proven to be the target page (its granulepos > target sample),
    use audio position value from the lower search boundary, so we don't need the previous page.

 3. If the page's granulepos is undefined, skip it.
*/
static int _ffogg_seek(ffogg *o)
{
	int r;
	struct ffpcm_seek sk;
	ogg_hdr *h;
	uint64 gpos, foff = o->off;

	if (o->firstseek) {
		// we don't have any page right now
		o->firstseek = 0;
		sk.fr_index = 0;
		sk.fr_samples = 0;
		sk.fr_size = sizeof(struct ogg_hdr);
		goto sk;
	}

	for (;;) {
		r = _ffogg_gethdr(o, &h);
		if (r != FFOGG_RDONE && !(r == FFOGG_RWARN && o->err == OGG_EJUNKDATA))
			return r;
		foff = o->off - o->hdrsize;
		gpos = ffint_ltoh64(h->granulepos);

		switch (o->state) {
		case I_SEEK:
			if (gpos == OGG_GPOS_UNDEF) {
				o->buf.len = 0;
				continue;
			}

			gpos -= o->first_sample;
			if (gpos > o->seek_sample && o->skoff == o->seekpt[0].off) {
				sk.fr_index = o->seekpt[0].sample;
				o->cursample = o->seekpt[0].sample;
				sk.fr_samples = gpos - o->seekpt[0].sample;
				sk.fr_size = o->pagesize;
				break;
			}
			o->buf.len = 0;
			o->cursample = gpos;
			o->state = I_SEEK2;
			continue;

		case I_SEEK2:
			if (gpos == OGG_GPOS_UNDEF) {
				o->buf.len = 0;
				continue;
			}

			o->state = I_SEEK;
			if (foff - o->off_data >= o->seekpt[1].off) {
				uint64 newoff = ffmax((int64)o->skoff - OGG_MAXPAGE, (int64)o->seekpt[0].off);
				if (newoff == o->skoff) {
					o->err = OGG_ESEEK;
					return FFOGG_RERR;
				}
				o->buf.len = 0;
				o->skoff = newoff;
				o->off = o->off_data + o->skoff;
				return FFOGG_RSEEK;
			}
			gpos -= o->first_sample;
			sk.fr_index = o->cursample;
			sk.fr_samples = gpos - o->cursample;
			sk.fr_size = o->pagesize;
			break;
		}

		break;
	}

sk:
	sk.target = o->seek_sample;
	sk.off = foff - o->off_data;
	sk.lastoff = o->skoff;
	sk.pt = o->seekpt;
	sk.avg_fr_samples = 0;
	sk.flags = FFPCM_SEEK_ALLOW_BINSCH;

	r = ffpcm_seek(&sk);
	if (r == 1) {
		o->buf.len = 0;
		o->skoff = sk.off;
		o->off = o->off_data + sk.off;
		return FFOGG_RSEEK;

	} else if (r == -1) {
		o->err = OGG_ESEEK;
		return FFOGG_RERR;
	}

	o->page_num = 0;
	return FFOGG_RDONE;
}

/*
. Read and return packets from the first 2 pages (header, tags)
. Determine stream length:
  . Get ending position of the first audio page
  . Seek to EOF and get ending position of the last page
  . Seek back to the first audio page
. Read and return audio packets...
*/
int ffogg_read(ffogg *o)
{
	int r;
	ogg_hdr *h;

	for (;;) {

	switch (o->state) {
	default:
		if (FFOGG_RDATA != (r = _ffogg_open(o)))
			return r;
		continue;

	case I_INFO:
		o->state = I_BODY;
		return FFOGG_RINFO;

	case I_SEEK:
	case I_SEEK2:
		if (FFOGG_RDONE != (r = _ffogg_seek(o)))
			return r;
		o->state = I_BODY;
		continue;

	case I_PAGE:
		if (o->page_gpos != OGG_GPOS_UNDEF)
			o->cursample = o->page_gpos - o->first_sample;
		if (o->page_last)
			return FFOGG_RDONE;
		o->state = I_HDR;
		// break

	case I_HDR:
		r = _ffogg_gethdr(o, &h);
		if (r == FFOGG_RWARN)
			return (o->init_done) ? FFOGG_RWARN : FFOGG_RERR;
		else if (r != FFOGG_RDONE)
			return r;

		if (o->continued && !(h->flags & OGG_FCONTINUED)) {
			o->continued = 0,  o->pktdata.len = 0;
			o->err = OGG_ECONTPKT;
			return FFOGG_RWARN;
		}

		if (!o->init_done && !(h->flags & OGG_FCONTINUED) && ++o->pgno == 3) {
			o->off_data = o->off - o->hdrsize;
			uint pagenum = ffint_ltoh32(h->number);
			if (pagenum != o->page_num + 1) {
				o->seektab[0].off = o->pagesize; //remove the first audio page from seek table, because we don't know the audio sample index
				o->first_sample = ffint_ltoh64(h->granulepos);
			}

			if (o->seekable && o->total_size != 0) {
				if (o->total_samples != 0) {
					// total_samples is set by user: no need to find EOS page
					o->seektab[1].sample = o->total_samples;
					o->seektab[1].off = o->total_size - o->off_data;
					o->state = I_INFO;
				} else
					o->state = I_SEEK_EOS;
			} else
				o->state = I_BODY;
			o->init_done = 1;
			return FFOGG_RHDRFIN;
		}

		o->state = I_BODY;
		// break

	case I_BODY:
		r = _ffogg_getbody(o);
		if (r == FFOGG_RERR) {
			if (!o->init_done)
				return FFOGG_RERR;

			o->state = I_HDR;
			return FFOGG_RWARN;

		} else if (r != FFOGG_RDONE)
			return r;

		o->state = I_PKT;
		o->pktno = 0;

		if (o->pagenum_err) {
			o->pagenum_err = 0;
			o->err = OGG_EPAGENUM;
			return FFOGG_RWARN;
		}
		// break;

	case I_PKT: {
		ffstr opkt;
		r = ogg_pkt_next(&opkt, o->buf.ptr, &o->segoff, &o->bodyoff);
		if (o->page_continued) {
			o->page_continued = 0;
			if (!o->continued)
				continue; //skip continued packet
		}
		if (r == -1) {
			o->state = I_PAGE;
			continue;
		}

		FFDBG_PRINTLN(10, "packet #%u, size: %u", o->pktno, (int)opkt.len);

		if (r == -2 || o->continued) {
			if (NULL == ffarr_append(&o->pktdata, opkt.ptr, opkt.len))
				return o->err = OGG_ESYS,  FFOGG_RERR;

			if (r == -2) {
				o->continued = 1;
				o->state = I_PAGE;
				continue;
			}

			ffstr_set2(&opkt, &o->pktdata);
			o->continued = 0,  o->pktdata.len = 0;
		}

		o->pktno++;
		ffstr_set2(&o->out, &opkt);
		return (o->init_done) ? FFOGG_RDATA : FFOGG_RHDR;
	}
	}
	}
	//unreachable
}


int ffogg_create(ffogg_cook *o, uint serialno)
{
	o->page.serial = serialno;
	if (NULL == ffarr_alloc(&o->buf, OGG_MAXPAGE))
		return OGG_ESYS;
	o->page_endpos = (uint64)-1;
	o->max_pagedelta = (uint)-1;
	return 0;
}

void ffogg_wclose(ffogg_cook *o)
{
	ffarr_free(&o->buf);
}

/* OGG write algorithm:
A page (containing >=1 packets) is returned BEFORE a new packet is added when:
. Page size is about to become larger than page size limit.
  This helps to avoid partial packets.
. Page time is about to exceed page time limit.
  This helps to achieve faster seeking to a position divisible by page granularity value.

A page is returned AFTER a new packet is added when:
. User ordered to flush the page.
. The last packet is written.

The first page has BOS flag set;  the last page has EOS flag set.
The page that starts with a continued partial packet has CONTINUED flag set.

The returned page has its granule position equal to ending position of the last finished packet.
If a page contains no finished packets, its granule position is -1.
*/
int ffogg_write(ffogg_cook *o)
{
	int r;
	uint f = 0, partial = 0;

	if (o->pkt.len == 0) {
		if (o->fin)
			goto fin;
		return FFOGG_RMORE;
	}

	if (o->page.nsegments != 0) {
		r = ogg_pkt_write(&o->page, NULL, NULL, o->pkt.len);
		if (r == 0
			|| ((uint)r != o->pkt.len && !o->allow_partial))
			goto flush;

		if (o->max_pagedelta != (uint)-1
			&& o->pkt_endpos - o->page_startpos > o->max_pagedelta)
			goto flush;
	}

	r = ogg_pkt_write(&o->page, o->buf.ptr, o->pkt.ptr, o->pkt.len);
	ffstr_shift(&o->pkt, r);
	if (o->pkt.len != 0) {
		partial = 1;
		goto flush;
	}

	o->page_endpos = o->pkt_endpos;
	o->stat.npkts++;

	if (o->fin)
		goto fin;

	if (o->flush) {
		o->flush = 0;
		goto flush;
	}

	return FFOGG_RMORE;

fin:
	f |= OGG_FLAST;

flush:
	f |= (o->page.number == 0) ? OGG_FFIRST : 0;
	f |= (o->continued) ? OGG_FCONTINUED : 0;
	o->continued = partial;
	o->stat.total_payload += o->page.size;
	r = ogg_page_write(&o->page, o->buf.ptr, o->page_endpos, f, &o->out);
	o->stat.total_ogg += r;
	o->stat.npages++;
	o->page_startpos = o->page_endpos,  o->page_endpos = (uint64)-1;
	return (f & OGG_FLAST) ? FFOGG_RDONE : FFOGG_RDATA;
}
