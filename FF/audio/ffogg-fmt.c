/**
Copyright (c) 2016 Simon Zolin
*/

#include <FF/audio/ogg-fmt.h>
#include <FF/number.h>

#include <ogg/ogg-crc.h>


struct vorbis_info {
	byte ver[4]; //0
	byte channels;
	byte rate[4];
	byte br_max[4];
	byte br_nominal[4];
	byte br_min[4];
	byte blocksize;
	byte framing_bit; //1
};

#define OGG_STR  "OggS"
#define VORB_STR  "vorbis"


uint ogg_checksum(const char *d, size_t len)
{
	uint crc = 0, i;

	for (i = 0;  i != 22;  i++)
		crc = (crc << 8) ^ ogg_crc_table[((crc >> 24) & 0xff) ^ (byte)d[i]];
	for (;  i != 26;  i++)
		crc = (crc << 8) ^ ogg_crc_table[((crc >> 24) & 0xff) ^ 0x00];
	for (;  i != len;  i++)
		crc = (crc << 8) ^ ogg_crc_table[((crc >> 24) & 0xff) ^ (byte)d[i]];

	return crc;
}

int ogg_parse(ogg_hdr *h, const char *data, size_t len)
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

uint ogg_pagesize(const ogg_hdr *h)
{
	uint i, r = 0;
	for (i = 0;  i != h->nsegments;  i++) {
		r += h->segments[i];
	}
	return sizeof(ogg_hdr) + h->nsegments + r;
}

uint ogg_packets(const ogg_hdr *h)
{
	uint i, n = 0;
	for (i = 0;  i != h->nsegments;  i++) {
		if (h->segments[i] < 255)
			n++;
	}
	return n;
}

int ogg_findpage(const char *data, size_t len, ogg_hdr *h)
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

int ogg_pkt_next(ogg_packet *pkt, const char *buf, uint *segoff, uint *bodyoff)
{
	const ogg_hdr *h = (void*)buf;
	uint seglen, pktlen = 0, nsegs = h->nsegments, i = *segoff;

	if (i == nsegs)
		return -1;

	do {
		seglen = h->segments[i++];
		pktlen += seglen;
	} while (seglen == 255 && i != nsegs);

	pkt->packet = (byte*)buf + sizeof(ogg_hdr) + nsegs + *bodyoff;
	pkt->bytes = pktlen;
	*segoff = i;
	*bodyoff += pktlen;

	pkt->b_o_s = 0;
	pkt->e_o_s = 0;
	pkt->granulepos = -1;
	return (seglen == 255) ? -2 : (int)pktlen;
}

int ogg_pkt_write(ffogg_page *p, char *buf, const char *pkt, uint len)
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

int ogg_page_write(ffogg_page *p, char *buf, uint64 granulepos, uint flags, ffstr *page)
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

int ogg_hdr_write(ffogg_page *p, char *buf, ffstr *data,
	const ogg_packet *info, const ogg_packet *tag, const ogg_packet *codbk)
{
	ffstr s;
	char *d = buf + OGG_MAXHDR + info->bytes;
	if (0 != ogg_pkt_write(p, d, (void*)tag->packet, tag->bytes)
		|| 0 != ogg_pkt_write(p, d, (void*)codbk->packet, codbk->bytes))
		return OGG_EBIGPKT;
	p->number = 1;
	ogg_page_write(p, d, 0, 0, &s);

	if (info->bytes != sizeof(struct vorbis_hdr) + sizeof(struct vorbis_info))
		return OGG_EHDR;
	d = s.ptr - (OGG_MAXHDR + info->bytes);
	ogg_pkt_write(p, d, (void*)info->packet, info->bytes);
	p->number = 0;
	ogg_page_write(p, d, 0, OGG_FFIRST, data);
	data->len += s.len;
	p->number = 2;

	return 0;
}

int vorb_info(const char *d, size_t len, uint *channels, uint *rate, uint *br_nominal)
{
	const struct vorbis_hdr *h = (void*)d;
	if (len < sizeof(struct vorbis_hdr) + sizeof(struct vorbis_info)
		|| !(h->type == T_INFO && !ffs_cmp(h->vorbis, VORB_STR, FFSLEN(VORB_STR))))
		return -1;

	const struct vorbis_info *vi = (void*)(d + sizeof(struct vorbis_hdr));
	if (0 != ffint_ltoh32(vi->ver)
		|| 0 == (*channels = vi->channels)
		|| 0 == (*rate = ffint_ltoh32(vi->rate))
		|| vi->framing_bit != 1)
		return -1;

	*br_nominal = ffint_ltoh32(vi->br_nominal);
	return 0;
}

void* vorb_comm(const char *d, size_t len, size_t *vorbtag_len)
{
	const struct vorbis_hdr *h = (void*)d;

	if (len < (uint)sizeof(struct vorbis_hdr)
		|| !(h->type == T_COMMENT && !ffs_cmp(h->vorbis, VORB_STR, FFSLEN(VORB_STR))))
		return NULL;

	*vorbtag_len = len - sizeof(struct vorbis_hdr);
	return (char*)d + sizeof(struct vorbis_hdr);
}

uint vorb_comm_write(char *d, size_t vorbtag_len)
{
	struct vorbis_hdr *h = (void*)d;
	h->type = T_COMMENT;
	ffmemcpy(h->vorbis, VORB_STR, FFSLEN(VORB_STR));
	d[sizeof(struct vorbis_hdr) + vorbtag_len] = 1; //set framing bit
	return sizeof(struct vorbis_hdr) + vorbtag_len + 1;
}
