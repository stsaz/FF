/**
Copyright (c) 2015 Simon Zolin
*/

#include <FF/audio/ogg.h>
#include <FF/string.h>
#include <FFOS/error.h>
#include <FFOS/mem.h>

#include <vorbis/vorbisenc.h>


enum OGG_F {
	OGG_FCONTINUED = 1,
	OGG_FFIRST = 2,
	OGG_FLAST = 4,
};

typedef struct ogg_hdr {
	char sync[4]; //"OggS"
	byte version; //0
	byte flags; //enum OGG_F
	byte granulepos[8];
	byte serial[4];
	byte number[4];
	byte crc[4];
	byte nsegments;
	byte segments[0];
} ogg_hdr;

enum VORBIS_HDR_T {
	T_INFO = 1,
	T_COMMENT = 3,
};

#define OGG_STR  "OggS"
#define VORB_STR  "vorbis"

struct vorbis_hdr {
	byte type; //enum VORBIS_HDR_T
	char vorbis[6]; //"vorbis"
};

struct _vorbis_info {
	byte ver[4]; //0
	byte channels;
	byte rate[4];
	byte br_max[4];
	byte br_nominal[4];
	byte br_min[4];
	byte blocksize;
	byte framing_bit; //1
};

enum {
	OGG_MAXHDR = sizeof(ogg_hdr) + 255,
	OGG_MAXPAGE = OGG_MAXHDR + 255 * 255,
	MAX_NOSYNC = 256 * 1024 * 1024,
};

static int ogg_parse(ogg_hdr *h, const char *data, size_t len);
static uint ogg_pagesize(const ogg_hdr *h);
static int ogg_findpage(const char *data, size_t len, ogg_hdr *h);
static int ogg_pkt_write(ffogg_page *p, char *buf, const char *pkt, uint len);
static int ogg_page_write(ffogg_page *p, char *buf, uint64 granulepos, uint flags, ffstr *page);
static int ogg_hdr_write(ffogg_page *p, char *buf, ffstr *data,
	const ogg_packet *info, const ogg_packet *tag, const ogg_packet *codbk);

static void* ogg_tag(const char *d, size_t len, size_t *vorbtag_len);
static uint ogg_tag_write(char *d, size_t vorbtag_len);

static int _ffogg_gethdr(ffogg *o, ogg_hdr **h);
static int _ffogg_open(ffogg *o);
static int _ffogg_seek(ffogg *o);

static int _ffogg_enc_hdr(ffogg_enc *o);


enum { I_HDR, I_BODY, I_HDRPKT, I_COMM, I_HDRDONE, I_HDRPAGE
	, I_SEEK_EOS, I_SEEK_EOS2, I_SEEK_EOS3
	, I_SEEK, I_SEEKDATA, I_SEEK2
	, I_PAGE, I_PKT, I_SYNTH, I_DATA };

enum OGG_E {
	OGG_EOK
	, OGG_EBADPAGE
	, OGG_EGAP
	, OGG_ESEEK
	, OGG_EHDR,
	OGG_EJUNKDATA,
	OGG_ECRC,
	OGG_ETAG,
	OGG_ENOSYNC,
	OGG_EBIGPKT,

	OGG_ESYS,
};

static const char* const ogg_errstr[] = {
	""
	, "the serial number of the page did not match the serial number of the bitstream, the page version was incorrect, or an internal error occurred"
	, "out of sync and there is a gap in the data"
	, "seek error"
	, "bad OGG header",
	"unrecognized data before OGG page",
	"CRC mismatch",
	"invalid tags",
	"couldn't find OGG page",
	"too large packet",
};

static const char* const vorb_errstr[] = {
	"" /*OV_EREAD*/
	, "internal error; indicates a bug or memory corruption" /*OV_EFAULT*/
	, "unimplemented; not supported by this version of the library" /*OV_EIMPL*/
	, "invalid parameter" /*OV_EINVAL*/
	, "the packet is not a Vorbis header packet" /*OV_ENOTVORBIS*/
	, "error interpreting the packet" /*OV_EBADHEADER*/
	, "" /*OV_EVERSION*/
	, "the packet is not an audio packet" /*OV_ENOTAUDIO*/
	, "there was an error in the packet" /*OV_EBADPACKET*/
	, "" /*OV_EBADLINK*/
	, "" /*OV_ENOSEEK*/
};

const char* ffogg_errstr(int e)
{
	if (e == OGG_ESYS)
		return fferr_strp(fferr_last());

	if (e >= 0)
		return ogg_errstr[e];
	e = -(e - (OV_EREAD));
	if ((uint)e < FFCNT(vorb_errstr))
		return vorb_errstr[e];
	return "";
}


/** Parse header.
Return header length;  0 if more data is needed;  -OGG_E* on error. */
static int ogg_parse(ogg_hdr *h, const char *data, size_t len)
{
	ogg_hdr *hdr = (void*)data;
	if (len < sizeof(ogg_hdr))
		return 0;
	if (!!ffs_cmp(data, OGG_STR, FFSLEN(OGG_STR)) || hdr->version != 0)
		return -OGG_EHDR;
	if (len < sizeof(ogg_hdr) + hdr->nsegments)
		return 0;
	return sizeof(ogg_hdr) + hdr->nsegments;
}

static uint ogg_pagesize(const ogg_hdr *h)
{
	uint i, r = 0;
	for (i = 0;  i != h->nsegments;  i++) {
		r += h->segments[i];
	}
	return sizeof(ogg_hdr) + h->nsegments + r;
}

/**
Return offset of the page;  -1 on error. */
static int ogg_findpage(const char *data, size_t len, ogg_hdr *h)
{
	const char *d = data, *end = data + len;

	while (d != end) {

		if (d[0] != 'O' && NULL == (d = ffs_findc(d, end - d, 'O')))
			break;

		if (ogg_parse(h, d, end - d) > 0)
			return d - data;

		d++;
	}

	return -1;
}

/** Add packet into page. */
static int ogg_pkt_write(ffogg_page *p, char *buf, const char *pkt, uint len)
{
	uint i, newsegs = p->nsegments + len / 255 + 1;

	if (newsegs > 255)
		return OGG_EBIGPKT; // partial packets aren't supported

	for (i = p->nsegments;  i != newsegs - 1;  i++) {
		p->segs[i] = 255;
	}
	p->segs[i] = len % 255;
	p->nsegments = newsegs;

	ffmemcpy(buf + p->size + OGG_MAXHDR, pkt, len);
	p->size += len;
	return 0;
}

/** Write page header into the position in buffer before page body.
Buffer: [... OGG_HDR PKT1 PKT2 ...]
@page: is set to page data within buffer.
@flags: enum OGG_F. */
static int ogg_page_write(ffogg_page *p, char *buf, uint64 granulepos, uint flags, ffstr *page)
{
	ogg_hdr *h = (void*)(buf + OGG_MAXHDR - (sizeof(ogg_hdr) + p->nsegments));
	ffmemcpy(h->sync, OGG_STR, FFSLEN(OGG_STR));
	h->version = 0;
	h->flags = flags;
	ffint_htol64(h->granulepos, granulepos);
	ffint_htol32(h->serial, p->serial);
	ffint_htol32(h->number, p->number++);
	h->nsegments = p->nsegments;
	ffmemcpy(h->segments, p->segs, h->nsegments);

	p->size += sizeof(ogg_hdr) + p->nsegments;
	ffint_htol32(h->crc, ogg_checksum((void*)h, p->size));

	ffstr_set(page, (void*)h, p->size);

	p->nsegments = 0;
	p->size = 0;
	return 0;
}

/** Get 2 pages at once containting all 3 vorbis headers. */
static int ogg_hdr_write(ffogg_page *p, char *buf, ffstr *data,
	const ogg_packet *info, const ogg_packet *tag, const ogg_packet *codbk)
{
	ffstr s;
	char *d = buf + OGG_MAXHDR + info->bytes;
	if (0 != ogg_pkt_write(p, d, (void*)tag->packet, tag->bytes)
		|| 0 != ogg_pkt_write(p, d, (void*)codbk->packet, codbk->bytes))
		return OGG_EBIGPKT;
	p->number = 1;
	ogg_page_write(p, d, 0, 0, &s);

	if (info->bytes != sizeof(struct vorbis_hdr) + sizeof(struct _vorbis_info))
		return OGG_EHDR;
	d = s.ptr - (OGG_MAXHDR + info->bytes);
	ogg_pkt_write(p, d, (void*)info->packet, info->bytes);
	p->number = 0;
	ogg_page_write(p, d, 0, OGG_FFIRST, data);
	data->len += s.len;
	p->number = 2;

	return 0;
}

/**
Return pointer to the beginning of Vorbis comments data;  NULL if not Vorbis comments header. */
static void* ogg_tag(const char *d, size_t len, size_t *vorbtag_len)
{
	const struct vorbis_hdr *h = (void*)d;

	if (len < (uint)sizeof(struct vorbis_hdr)
		|| !(h->type == T_COMMENT && !ffs_cmp(h->vorbis, VORB_STR, FFSLEN(VORB_STR))))
		return NULL;

	*vorbtag_len = len - sizeof(struct vorbis_hdr);
	return (char*)d + sizeof(struct vorbis_hdr);
}

/**
Return packet length. */
static uint ogg_tag_write(char *d, size_t vorbtag_len)
{
	struct vorbis_hdr *h = (void*)d;
	h->type = T_COMMENT;
	ffmemcpy(h->vorbis, VORB_STR, FFSLEN(VORB_STR));
	d[sizeof(struct vorbis_hdr) + vorbtag_len] = 1; //set framing bit
	return sizeof(struct vorbis_hdr) + vorbtag_len + 1;
}


void ffogg_init(ffogg *o)
{
	ffmem_tzero(o);
	vorbis_info_init(&o->vinfo);
}

uint ffogg_bitrate(ffogg *o)
{
	if (o->total_samples == 0 || o->total_size == 0)
		return o->vinfo.bitrate_nominal;
	return ffpcm_brate(o->total_size - o->off_data, o->total_samples, o->vinfo.rate);
}

/** Get the whole OGG page header.
Return 0 on success. */
static int _ffogg_gethdr(ffogg *o, ogg_hdr **h)
{
	uint lostsync = 0;
	int r;

	if (o->buf.len != 0) {
		uint buflen = o->buf.len;
		r = ffmin(o->datalen, OGG_MAXHDR - buflen);
		if (NULL == ffarr_append(&o->buf, o->data, r)) {
			o->err = OGG_ESYS;
			return FFOGG_RERR;
		}

		r = ogg_findpage(o->buf.ptr, o->buf.len, NULL);
		if (r == -1) {
			if (o->datalen > OGG_MAXHDR - buflen) {
				o->buf.len = 0;
				goto dfind;
			}
			goto more;

		} else if (r != 0) {
			lostsync = 1;
			_ffarr_rmleft(&o->buf, r, sizeof(char));
		}

		o->hdrsize = sizeof(ogg_hdr) + ((ogg_hdr*)o->buf.ptr)->nsegments;
		o->pagesize = ogg_pagesize((void*)o->buf.ptr);
		o->buf.len = o->hdrsize;

		FFARR_SHIFT(o->data, o->datalen, o->hdrsize - buflen);
		o->off += o->hdrsize - buflen;

		*h = (void*)o->buf.ptr;
		goto done;
	}

dfind:
	r = ogg_findpage(o->data, o->datalen, NULL);
	if (r == -1) {
		uint n = ffmin(o->datalen, OGG_MAXHDR - 1);
		if (NULL == ffarr_append(&o->buf, o->data + o->datalen - n, n)) {
			o->err = OGG_ESYS;
			return FFOGG_RERR;
		}

		goto more;

	} else if (r != 0) {
		lostsync = 1;
		FFARR_SHIFT(o->data, o->datalen, r);
		o->off += r;
	}
	*h = (void*)o->data;
	o->hdrsize = sizeof(ogg_hdr) + ((ogg_hdr*)o->data)->nsegments;
	o->pagesize = ogg_pagesize((void*)o->data);

done:
	if (o->bytes_skipped != 0)
		o->bytes_skipped = 0;

	if (lostsync) {
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

/*
Return codes sequence when 'nodecode' is active:
RHDR -> RPAGE -> RTAG... -> RHDRFIN -> RPAGE -> RINFO
*/
static int _ffogg_open(ffogg *o)
{
	int r;
	ogg_packet opkt;
	ogg_hdr *h;

	for (;;) {

	switch (o->state) {

	case I_HDRPAGE:
		if (o->seekable && o->total_size != 0)
			o->state = I_SEEK_EOS;
		else
			o->state = I_PAGE;
		return FFOGG_RPAGE;

	case I_HDRPKT:
		r = ogg_stream_packetout(&o->ostm, &opkt);
		if (r == 0) {
			o->state = I_HDR;
			if (o->nodecode)
				return FFOGG_RPAGE;
			return FFOGG_RDATA;

		} else if (r < 0) {
			o->err = OGG_EGAP;
			return FFOGG_RERR;
		}

		if (o->nhdr == 1) {
			o->nhdr++;
			if (NULL == (o->vtag.data = ogg_tag((void*)opkt.packet, opkt.bytes, &o->vtag.datalen))) {
				o->err = OGG_ETAG;
				return FFOGG_RERR;
			}
			o->state = I_COMM;
			break;
		}

		r = vorbis_synthesis_headerin(&o->vinfo, NULL, &opkt);
		if (r != 0) {
			o->err = r;
			return FFOGG_RERR;
		}

		switch (++o->nhdr) {
		case 1:
			return FFOGG_RHDR;

		case 3:
			o->state = I_HDRDONE;
			break;
		}
		break;

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
			o->state = I_HDRPKT;
			break;
		}
		return FFOGG_RTAG;

	case I_HDRDONE:
		if (0 == vorbis_synthesis_init(&o->vds, &o->vinfo)) {
			vorbis_block_init(&o->vds, &o->vblk);
			o->vblk_valid = 1;
		}
		o->off_data = o->off;

		if (o->nodecode)
			o->state = I_HDRPAGE;
		else if (o->seekable && o->total_size != 0)
			o->state = I_SEEK_EOS;
		else
			o->state = I_PAGE;
		o->init_done = 1;
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
		o->state = I_PAGE;
		return FFOGG_RSEEK;
	}
	}
	//unreachable
}

void ffogg_close(ffogg *o)
{
	if (o->vblk_valid) {
		vorbis_block_clear(&o->vblk);
		vorbis_dsp_clear(&o->vds);
	}
	if (o->ostm_valid)
		ogg_stream_clear(&o->ostm);
	vorbis_info_clear(&o->vinfo);
	ffarr_free(&o->buf);
}

void ffogg_seek(ffogg *o, uint64 sample)
{
	if (sample >= o->total_samples || o->total_size == 0)
		return;
	o->seek_sample = sample;
	if (o->state == I_DATA)
		o->state = I_SEEKDATA;
	else {
		if (o->state == I_SEEK_EOS3)
			o->firstseek = 1;
		o->state = I_SEEK;
	}
	ffmemcpy(o->seekpt, o->seektab, sizeof(o->seekpt));
	o->skoff = (uint64)-1;
	o->buf.len = 0;
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
	uint64 gpos;

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

		switch (o->state) {
		case I_SEEK:
			gpos = ffint_ltoh64(h->granulepos);
			o->buf.len = 0;
			if (gpos > o->seek_sample && o->skoff == o->seekpt[0].off) {
				sk.fr_index = o->seekpt[0].sample;
				sk.fr_samples = gpos - o->seekpt[0].sample;
				sk.fr_size = o->pagesize;
				break;
			}
			if (h == (void*)o->data) {
				FFARR_SHIFT(o->data, o->datalen, o->hdrsize);
				o->off += o->hdrsize;
			}
			o->cursample = gpos;
			o->state = I_SEEK2;
			continue;

		case I_SEEK2:
			o->state = I_SEEK;
			if (o->off - o->off_data >= o->seekpt[1].off) {
				uint64 newoff = ffmax((int64)o->skoff - OGG_MAXPAGE, (int64)o->seekpt[0].off);
				if (newoff == o->skoff) {
					o->err = OGG_ESEEK;
					return FFOGG_RERR;
				}
				o->skoff = newoff;
				o->off = o->off_data + o->skoff;
				return FFOGG_RSEEK;
			}
			gpos = ffint_ltoh64(h->granulepos);
			sk.fr_index = o->cursample;
			sk.fr_samples = gpos - o->cursample;
			sk.fr_size = o->pagesize;
			break;
		}

		break;
	}

sk:
	sk.target = o->seek_sample;
	sk.off = o->off - o->off_data;
	sk.lastoff = o->skoff;
	sk.pt = o->seekpt;
	sk.avg_fr_samples = 0;
	sk.flags = FFPCM_SEEK_ALLOW_BINSCH;

	r = ffpcm_seek(&sk);
	if (r == 1) {
		o->skoff = sk.off;
		o->off = o->off_data + sk.off;
		return FFOGG_RSEEK;

	} else if (r == -1) {
		o->err = OGG_ESEEK;
		return FFOGG_RERR;
	}

	return FFOGG_RDONE;
}

/*
ogg_sync_state -> ogg_page -> ogg_stream_state -> ogg_packet -> vorbis_block -> vorbis_dsp_state
*/
int ffogg_decode(ffogg *o)
{
	int r;
	ogg_packet opkt;
	ogg_hdr *h;
	ogg_page opg;

	for (;;) {

	switch (o->state) {
	default:
		if (FFOGG_RDATA != (r = _ffogg_open(o)))
			return r;
		continue;

	case I_SEEKDATA:
		vorbis_synthesis_read(&o->vds, o->nsamples);
		o->state = I_SEEK;
		//break;

	case I_SEEK:
	case I_SEEK2:
		if (FFOGG_RDONE != (r = _ffogg_seek(o)))
			return r;
		ogg_stream_reset(&o->ostm);
		o->state = I_BODY;
		continue;


	case I_PAGE:
		if (o->page_gpos != (uint64)-1)
			o->cursample = o->page_gpos - o->first_sample;
		if (o->page_last)
			return FFOGG_RDONE;
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
		r = ffarr_append_until(&o->buf, o->data, o->datalen, o->pagesize);
		if (r == 0)
			return FFOGG_RMORE;
		else if (r == -1) {
			o->err = OGG_ESYS;
			return FFOGG_RERR;
		}
		FFARR_SHIFT(o->data, o->datalen, r);
		o->off += o->pagesize;
		o->buf.len = 0;
		h = (void*)o->buf.ptr;

		{
		uint crc = ogg_checksum(o->buf.ptr, o->pagesize);
		uint hcrc = ffint_ltoh32(h->crc);
		if (crc != hcrc) {
			FFDBG_PRINTLN(1, "Bad page CRC:%xu, CRC in header:%xu", crc, hcrc);
			o->err = OGG_ECRC;
			o->state = I_HDR;
			return (o->init_done) ? FFOGG_RWARN : FFOGG_RERR;
		}
		}
		o->page_last = (h->flags & OGG_FLAST) != 0;
		o->page_num = ffint_ltoh32(h->number);
		o->page_gpos = ffint_ltoh64(h->granulepos);

		opg.header = (void*)o->buf.ptr;
		opg.header_len = o->hdrsize;
		opg.body = (void*)(o->buf.ptr + o->hdrsize);
		opg.body_len = o->pagesize - o->hdrsize;

		FFDBG_PRINTLN(10, "page #%u  end-pos:%xU  packets:%u  continued:%u  size:%u"
			, o->page_num, o->page_gpos
			, ogg_page_packets(&opg), (h->flags & OGG_FCONTINUED) != 0
			, o->pagesize);

		if (!o->ostm_valid) {
			ogg_stream_init(&o->ostm, ffint_ltoh32(h->serial));
			o->ostm_valid = 1;
		}

		if (0 > ogg_stream_pagein(&o->ostm, &opg)) {
			o->err = OGG_EBADPAGE;
			o->state = I_HDR;
			return (o->init_done) ? FFOGG_RWARN : FFOGG_RERR;
		}

		if (!o->init_done) {
			o->state = I_HDRPKT;
			continue;
		}

		if (o->nodecode) {
			o->state = I_PAGE;
			return FFOGG_RPAGE;
		}

		o->state = I_PKT;
		// break;

	case I_PKT:
		r = ogg_stream_packetout(&o->ostm, &opkt);
		if (r == 0) {
			o->state = I_PAGE;
			break;
		} else if (r < 0) {
			o->err = OGG_EGAP;
			return FFOGG_RWARN;
		}

		if (0 != (r = vorbis_synthesis(&o->vblk, &opkt))) {
			o->err = r;
			return FFOGG_RWARN;
		}

		vorbis_synthesis_blockin(&o->vds, &o->vblk);
		// o->state = I_SYNTH;
		// break;

	case I_SYNTH:
		if (0 != (o->nsamples = vorbis_synthesis_pcmout(&o->vds, (float***)&o->pcm))) {
			o->pcmlen = o->nsamples * sizeof(float) * o->vinfo.channels;
			o->state = I_DATA;
			return FFOGG_RDATA;
		}
		o->state = I_PKT;
		break;

	case I_DATA:
		vorbis_synthesis_read(&o->vds, o->nsamples);
		o->cursample += o->nsamples;
		o->state = I_SYNTH;
		break;
	}
	}
	//unreachable
}


void ffogg_enc_init(ffogg_enc *o)
{
	ffmem_tzero(o);
	o->min_tagsize = 1000;
	o->max_pagesize = 8 * 1024;
	vorbis_info_init(&o->vinfo);

	if (NULL == ffarr_alloc(&o->buf, 4096))
		return;
	o->vtag.out = o->buf.ptr + sizeof(struct vorbis_hdr);
	o->vtag.outcap = o->buf.cap - sizeof(struct vorbis_hdr) - 1;
	const char *vendor = vorbis_vendor();
	ffvorbtag_add(&o->vtag, NULL, vendor, ffsz_len(vendor));
}

void ffogg_enc_close(ffogg_enc *o)
{
	if (o->vblk_valid) {
		vorbis_block_clear(&o->vblk);
		vorbis_dsp_clear(&o->vds);
	}
	vorbis_info_clear(&o->vinfo);
	ffarr_free(&o->buf);
}

int ffogg_create(ffogg_enc *o, ffpcm *pcm, int quality, uint serialno)
{
	int r;

	if (0 != (r = vorbis_encode_init_vbr(&o->vinfo, pcm->channels, pcm->sample_rate, (float)quality / 100)))
		return r;

	vorbis_analysis_init(&o->vds, &o->vinfo);
	vorbis_block_init(&o->vds, &o->vblk);
	o->vblk_valid = 1;
	o->page.serial = serialno;
	return 0;
}

static int _ffogg_enc_hdr(ffogg_enc *o)
{
	int r;
	ogg_packet pkt[3];

	if (0 != (r = vorbis_analysis_headerout(&o->vds, NULL, &pkt[0], NULL, &pkt[2])))
		return r;

	if (0 != ffvorbtag_fin(&o->vtag))
		return OGG_ETAG;

	pkt[1].packet = (void*)o->buf.ptr;
	pkt[1].bytes = ogg_tag_write(o->buf.ptr, o->vtag.outlen);

	if (o->vtag.outlen < o->min_tagsize) {
		uint npadding = ffmin(o->min_tagsize, o->vtag.outcap) - o->vtag.outlen;
		ffmem_zero(pkt[1].packet + pkt[1].bytes, npadding);
		pkt[1].bytes += npadding;
	}

	ffarr tags = o->buf;

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
	ffarr_free(&tags);
	return o->err;
}

int ffogg_encode(ffogg_enc *o)
{
	enum { I_HDRFLUSH, I_INPUT, I_ENCODE, ENC_PKT, ENC_DONE };
	int r;
	uint i, n;
	float **fpcm;

	for (;;) {

	switch (o->state) {
	case I_HDRFLUSH:
		if (0 != (r = _ffogg_enc_hdr(o)))
			return r;
		o->state = I_INPUT;
		return FFOGG_RDATA;

	case I_INPUT:
		if (o->pcmlen == 0 && !o->fin)
			return FFOGG_RMORE;

		n = (uint)(o->pcmlen / (sizeof(float) * o->vinfo.channels));
		fpcm = vorbis_analysis_buffer(&o->vds, n);
		if (o->pcmlen != 0) {
			for (i = 0;  i != (uint)o->vinfo.channels;  i++) {
				ffmemcpy(fpcm[i], o->pcm[i], n * sizeof(float));
			}
		}
		vorbis_analysis_wrote(&o->vds, n);
		o->pcmlen = 0;
		o->state = I_ENCODE;
		//break;

	case I_ENCODE:
		r = vorbis_analysis_blockout(&o->vds, &o->vblk);
		if (r < 0) {
			o->err = r;
			return FFOGG_RERR;
		} else if (r != 1) {
			o->state = I_INPUT;
			break;
		}

		if (0 != (r = vorbis_analysis(&o->vblk, &o->opkt))) {
			o->err = r;
			return FFOGG_RERR;
		}

		if (o->page.size + o->opkt.bytes > o->max_pagesize) {
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
		continue;

	case ENC_DONE:
		return FFOGG_RDONE;
	}
	}

	//unreachable
}
