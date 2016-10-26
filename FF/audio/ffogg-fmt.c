/**
Copyright (c) 2016 Simon Zolin
*/

#include <FF/audio/ogg-fmt.h>
#include <FF/number.h>

#include <ogg/ogg-crc.h>


#define OGG_STR  "OggS"


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

int ogg_pkt_next(ffstr *pkt, const char *buf, uint *segoff, uint *bodyoff)
{
	const ogg_hdr *h = (void*)buf;
	uint seglen, pktlen = 0, nsegs = h->nsegments, i = *segoff;

	if (i == nsegs)
		return -1;

	do {
		seglen = h->segments[i++];
		pktlen += seglen;
	} while (seglen == 255 && i != nsegs);

	ffstr_set(pkt, (byte*)buf + sizeof(ogg_hdr) + nsegs + *bodyoff, pktlen);
	*segoff = i;
	*bodyoff += pktlen;
	return (seglen == 255) ? -2 : (int)pktlen;
}

uint ogg_pkt_write(ffogg_page *p, char *buf, const char *pkt, size_t len)
{
	FF_ASSERT(len != 0);

	uint pktsegs_all = len / 255 + 1;
	uint pktsegs = ffmin(pktsegs_all, 255 - p->nsegments);
	uint newsegs = p->nsegments + pktsegs;
	uint complete = (pktsegs == pktsegs_all);

	if (!complete)
		len = pktsegs * 255;

	if (buf == NULL)
		return len;

	if (len >= 255)
		memset(p->segs + p->nsegments, 255, pktsegs - complete);

	if (complete)
		p->segs[newsegs - 1] = len % 255;

	p->nsegments = newsegs;

	ffmemcpy(buf + p->size + OGG_MAXHDR, pkt, len);
	p->size += len;
	return len;
}

uint ogg_page_write(ffogg_page *p, char *buf, uint64 granulepos, uint flags, ffstr *page)
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

	uint nhdr = sizeof(ogg_hdr) + p->nsegments;
	p->nsegments = 0;
	p->size = 0;
	return nhdr;
}
