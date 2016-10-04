/**
Copyright (c) 2015 Simon Zolin
*/

#include <FF/audio/ogg.h>
#include <FF/string.h>
#include <FFOS/error.h>
#include <FFOS/mem.h>


enum {
	OGG_MAXPAGE = OGG_MAXHDR + 255 * 255,
	MAX_NOSYNC = 256 * 1024,
};


static int _ffogg_gethdr(ffogg *o, ogg_hdr **h);
static int _ffogg_getbody(ffogg *o);
static int _ffogg_open(ffogg *o);
static int _ffogg_seek(ffogg *o);

static int _ffogg_pkt_vorbtag(ffogg_enc *o, ogg_packet *pkt);
static int _ffogg_enc_hdr(ffogg_enc *o);


enum { I_HDR, I_BODY, I_HDRPKT, I_COMM_PKT, I_COMM, I_BOOK_PKT, I_FIRSTPAGE
	, I_SEEK_EOS, I_SEEK_EOS2, I_SEEK_EOS3
	, I_SEEK, I_SEEK2, I_ASIS_SEEKHDR
	, I_PAGE, I_PKT, I_DECODE, I_DATA };

static const char* const ogg_errstr[] = {
	""
	, "seek error"
	, "bad OGG header",
	"unsupported page version",
	"the serial number of the page did not match the serial number of the bitstream",
	"out of order OGG page",
	"unrecognized data before OGG page",
	"CRC mismatch",
	"invalid tags",
	"couldn't find OGG page",
	"bad packet",
	"too large packet",
	"expected continued packet",
};

const char* ffogg_errstr(int e)
{
	if (e == OGG_ESYS)
		return fferr_strp(fferr_last());

	if (e >= 0)
		return ogg_errstr[e];
	return vorbis_errstr(e);
}

#define ERR(o, n) \
	(o)->err = n, FFOGG_RERR


void ffogg_init(ffogg *o)
{
	ffmem_tzero(o);
	o->nxstate = I_HDRPKT;
}

uint ffogg_bitrate(ffogg *o)
{
	if (o->total_samples == 0 || o->total_size == 0)
		return o->vinfo.bitrate_nominal;
	return ffpcm_brate(o->total_size - o->off_data, o->total_samples, o->vinfo.rate);
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

	if (o->continued && !(hdr->flags & OGG_FCONTINUED)) {
		o->continued = 0,  o->pktdata.len = 0;
		o->err = OGG_ECONTPKT;
		return FFOGG_RWARN;
	}

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

	case I_HDRPKT:
		if (0 != vorb_info((void*)o->opkt.packet, o->opkt.bytes, &o->vinfo.channels, &o->vinfo.rate, &o->vinfo.bitrate_nominal))
			return ERR(o, OGG_EPKT);

		if (0 != (r = vorbis_decode_init(&o->vctx, &o->opkt)))
			return ERR(o, r);

		o->state = I_PKT, o->nxstate = I_COMM_PKT;
		return FFOGG_RHDR;

	case I_COMM_PKT:
		if (NULL == (o->vtag.data = vorb_comm((void*)o->opkt.packet, o->opkt.bytes, &o->vtag.datalen)))
			return ERR(o, OGG_ETAG);
		o->state = I_COMM;
		// break

	case I_COMM:
		r = ffvorbtag_parse(&o->vtag);
		if (r == FFVORBTAG_ERR) {
			o->err = OGG_ETAG;
			return r;
		} else if (r == FFVORBTAG_DONE) {
			if (!(o->vtag.datalen != 0 && o->vtag.data[0] == 1)) {
				o->err = OGG_ETAG;
				return FFOGG_RERR;
			}
			o->state = I_PKT, o->nxstate = I_BOOK_PKT;
			return FFOGG_RDATA;
		}
		return FFOGG_RTAG;

	case I_BOOK_PKT:
		if (0 != (r = vorbis_decode_init(&o->vctx, &o->opkt)))
			return ERR(o, r);

		o->off_data = o->off;
		o->state = I_FIRSTPAGE;
		break;

	case I_FIRSTPAGE:
		r = _ffogg_gethdr(o, &h);
		if (r != FFOGG_RDONE)
			return r;
		uint pagenum = ffint_ltoh32(h->number);
		if (pagenum != 2) {
			o->seektab[0].off = o->pagesize; //remove the first audio page from seek table, because we don't know the audio sample index
			o->first_sample = ffint_ltoh64(h->granulepos);
		}
		o->page_num = pagenum - 1;

		if (o->seekable && o->total_size != 0)
			o->state = I_SEEK_EOS;
		else
			o->state = I_PAGE;
		o->seek_sample = (uint64)-1;
		o->init_done = 1;
		o->nxstate = I_DECODE;
		return FFOGG_RHDRFIN;

	case I_SEEK_EOS:
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

			o->total_samples = ffint_ltoh64(h->granulepos) - o->first_sample;
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
	FF_SAFECLOSE(o->vctx, NULL, vorbis_decode_free);
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

/* Seeking in OGG Vorbis:

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

		switch (o->state) {
		case I_SEEK:
			gpos = ffint_ltoh64(h->granulepos) - o->first_sample;
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
			gpos = ffint_ltoh64(h->granulepos) - o->first_sample;
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

int ffogg_decode(ffogg *o)
{
	int r;
	ogg_hdr *h;

	for (;;) {

	switch (o->state) {
	default:
		if (FFOGG_RDATA != (r = _ffogg_open(o)))
			return r;
		continue;

	case I_SEEK:
	case I_SEEK2:
		if (FFOGG_RDONE != (r = _ffogg_seek(o)))
			return r;
		o->state = I_BODY;
		continue;

	case I_PAGE:
		if (o->page_gpos != (uint64)-1)
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

		if (o->pagenum_err) {
			o->pagenum_err = 0;
			o->err = OGG_EPAGENUM;
			return FFOGG_RWARN;
		}
		// break;

	case I_PKT:
		r = ogg_pkt_next(&o->opkt, o->buf.ptr, &o->segoff, &o->bodyoff);
		if (o->page_continued) {
			o->page_continued = 0;
			if (!o->continued)
				continue; //skip continued packet
		}
		if (r == -1) {
			o->state = I_PAGE;
			continue;
		}

		FFDBG_PRINTLN(10, "packet #%u, size: %u", o->pktno, (int)o->opkt.bytes);

		if (r == -2 || o->continued) {
			if (NULL == ffarr_append(&o->pktdata, o->opkt.packet, o->opkt.bytes))
				return o->err = OGG_ESYS,  FFOGG_RERR;

			if (r == -2) {
				o->continued = 1;
				o->state = I_PAGE;
				continue;
			}

			o->opkt.packet = (void*)o->pktdata.ptr,  o->opkt.bytes = o->pktdata.len;
			o->continued = 0,  o->pktdata.len = 0;
		}

		o->opkt.packetno = o->pktno++;
		o->state = o->nxstate;
		continue;

	case I_DECODE:
		r = vorbis_decode(o->vctx, &o->opkt, &o->pcm);
		if (r < 0) {
			o->state = I_PKT;
			o->err = r;
			return FFOGG_RWARN;
		}

		if (o->seek_sample != (uint64)-1) {
			if (o->seek_sample < o->cursample) {
				//couldn't find the target packet within the page
				o->seek_sample = o->cursample;
			}

			uint skip = ffmin(o->seek_sample - o->cursample, r);
			o->cursample += skip;
			if (o->cursample != o->seek_sample || (uint)r == skip) {
				o->state = I_PKT;
				continue; //not yet reached the target packet
			}

			o->seek_sample = (uint64)-1;
			for (uint i = 0;  i != o->vinfo.channels;  i++) {
				o->pcm_arr[i] = o->pcm[i] + skip;
			}
			o->pcm = o->pcm_arr;
			r -= skip;
		}

		o->nsamples = r;
		o->pcmlen = o->nsamples * sizeof(float) * o->vinfo.channels;
		o->state = I_DATA;
		return FFOGG_RDATA;

	case I_DATA:
		o->cursample += o->nsamples;
		o->state = I_PKT;
		break;
	}
	}
	//unreachable
}

void ffogg_set_asis(ffogg *o, uint64 from_sample)
{
	o->seek_sample = (uint64)-1;
	if (from_sample != (uint64)-1)
		ffogg_seek(o, from_sample);
	o->state = I_ASIS_SEEKHDR;
}

int ffogg_readasis(ffogg *o)
{
	int r;

	for (;;) {
	switch (o->state) {

	case I_ASIS_SEEKHDR:
		o->state = I_HDR;
		o->off = 0;
		return FFOGG_RSEEK;

	case I_PAGE:
		if (o->page_gpos != (uint64)-1)
			o->cursample = o->page_gpos - o->first_sample;
		if (o->page_last)
			return FFOGG_RDONE;
		o->state = I_HDR;
		// break

	case I_HDR: {
		ogg_hdr *h;
		r = _ffogg_gethdr(o, &h);
		if (r != FFOGG_RDONE)
			return r;
		o->state = I_BODY;
		// break
	}

	case I_BODY:
		r = _ffogg_getbody(o);
		if (r == FFOGG_RERR) {
			o->state = I_HDR;
			return FFOGG_RWARN;

		} else if (r != FFOGG_RDONE)
			return r;

		o->state = I_PAGE;
		if (o->off == o->off_data && o->seek_sample != (uint64)-1) {
			o->state = I_SEEK;
		}
		return FFOGG_RPAGE;

	case I_SEEK:
	case I_SEEK2:
		if (FFOGG_RDONE != (r = _ffogg_seek(o)))
			return r;
		o->state = I_BODY;
		continue;
	}
	}
}


void ffogg_enc_init(ffogg_enc *o)
{
	ffmem_tzero(o);
	o->min_tagsize = 1000;
	o->max_pagesize = 8 * 1024;

	if (NULL == ffarr_alloc(&o->vtag.out, 4096))
		return;
	o->vtag.out.len = sizeof(struct vorbis_hdr);
	const char *vendor = vorbis_vendor();
	ffvorbtag_add(&o->vtag, NULL, vendor, ffsz_len(vendor));
}

void ffogg_enc_close(ffogg_enc *o)
{
	ffvorbtag_destroy(&o->vtag);
	ffarr_free(&o->buf);
	FF_SAFECLOSE(o->vctx, NULL, vorbis_encode_free);
}

int ffogg_create(ffogg_enc *o, ffpcm *pcm, int quality, uint serialno)
{
	o->vinfo.channels = pcm->channels;
	o->vinfo.rate = pcm->sample_rate;
	o->vinfo.quality = quality;
	o->page.serial = serialno;
	return 0;
}

static const byte brates[] = {
	45/2, 64/2, 80/2, 96/2, 112/2, 128/2, 160/2, 192/2, 224/2, 256/2, 320/2, 500/2 //q=-1..10 for 44.1kHz mono
};

uint64 ffogg_enc_size(ffogg_enc *o, uint64 total_samples)
{
	uint q = o->vinfo.quality / 10 + 1;
	uint metalen = o->datalen;
	return metalen + (total_samples / o->vinfo.rate + 1) * (brates[q] * 1000 * o->vinfo.channels / 8);
}

/** Get complete packet with Vorbis comments and padding. */
static int _ffogg_pkt_vorbtag(ffogg_enc *o, ogg_packet *pkt)
{
	ffarr *v = &o->vtag.out;
	uint taglen = v->len - sizeof(struct vorbis_hdr);
	uint npadding = (taglen < o->min_tagsize) ? o->min_tagsize - taglen : 0;
	if (NULL == ffarr_grow(v, 1 + npadding, 0)) { //allocate space for "framing bit" and padding
		o->err = OGG_ESYS;
		return OGG_ESYS;
	}

	if (npadding != 0)
		ffmem_zero(v->ptr + v->len + 1, npadding);

	pkt->packet = (void*)v->ptr;
	pkt->bytes = vorb_comm_write(v->ptr, taglen) + npadding;
	return 0;
}

static int _ffogg_enc_hdr(ffogg_enc *o)
{
	int r;
	ogg_packet pkt[3];

	vorbis_encode_params params = {0};
	params.channels = o->vinfo.channels;
	params.rate = o->vinfo.rate;
	params.quality = (float)o->vinfo.quality / 100;
	if (0 != (r = vorbis_encode_create(&o->vctx, &params, &pkt[0], &pkt[2])))
		return r;

	ffvorbtag_fin(&o->vtag);
	if (0 != (r = _ffogg_pkt_vorbtag(o, &pkt[1])))
		return r;

	o->max_pagesize = ffmin(o->max_pagesize, OGG_MAXPAGE - OGG_MAXHDR);
	uint sz = ffmax(OGG_MAXHDR + o->max_pagesize, OGG_MAXHDR + (uint)pkt[0].bytes + OGG_MAXHDR + (uint)pkt[1].bytes + (uint)pkt[2].bytes);
	if (NULL == ffarr_alloc(&o->buf, sz)) {
		o->err = OGG_ESYS;
		goto err;
	}

	ffstr s;
	if (0 != (r = ogg_hdr_write(&o->page, o->buf.ptr, &s, &pkt[0], &pkt[1], &pkt[2]))) {
		o->err = r;
		goto err;
	}
	o->data = s.ptr;
	o->datalen = s.len;

	o->err = 0;

err:
	ffvorbtag_destroy(&o->vtag);
	return o->err;
}

int ffogg_encode(ffogg_enc *o)
{
	enum { I_HDRFLUSH, I_INPUT, I_ENCODE, ENC_PKT, ENC_DONE };
	int r, n = 0;

	for (;;) {

	switch (o->state) {
	case I_HDRFLUSH:
		if (0 != (r = _ffogg_enc_hdr(o)))
			return r;
		o->state = I_INPUT;
		return FFOGG_RDATA;

	case I_INPUT:
		n = (uint)(o->pcmlen / (sizeof(float) * o->vinfo.channels));
		o->pcmlen = 0;
		o->state = I_ENCODE;
		// break

	case I_ENCODE:
		r = vorbis_encode(o->vctx, o->pcm, n, &o->opkt);
		if (r < 0) {
			o->err = r;
			return FFOGG_RERR;
		} else if (r == 0) {
			if (!o->fin) {
				o->state = I_INPUT;
				return FFOGG_RMORE;
			}
			n = -1;
			continue;
		}

		if (o->page.size + o->opkt.bytes > o->max_pagesize || o->page.nsegments == 255) {
			ffstr s;
			ogg_page_write(&o->page, o->buf.ptr, o->granpos, 0, &s);
			o->data = s.ptr;
			o->datalen = s.len;
			o->state = ENC_PKT;
			return FFOGG_RDATA;
		}
		o->state = ENC_PKT;
		// break

	case ENC_PKT:
		if (0 != (r = ogg_pkt_write(&o->page, o->buf.ptr, (void*)o->opkt.packet, o->opkt.bytes))) {
			o->err = r;
			return FFOGG_RERR;
		}
		o->granpos = o->opkt.granulepos;

		if (o->opkt.e_o_s) {
			ffstr s;
			ogg_page_write(&o->page, o->buf.ptr, o->granpos, OGG_FLAST, &s);
			o->data = s.ptr;
			o->datalen = s.len;
			o->state = ENC_DONE;
			return FFOGG_RDATA;
		}
		o->state = I_ENCODE;
		n = 0;
		continue;

	case ENC_DONE:
		return FFOGG_RDONE;
	}
	}

	//unreachable
}
