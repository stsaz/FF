/**
Copyright (c) 2013 Simon Zolin
*/

#include <FF/audio/id3.h>
#include <FF/data/utf8.h>


ffbool ffid3_valid(const ffid3_hdr *h)
{
	const uint *hsize = (void*)h->size;
	return h->id3[0] == 'I' && h->id3[1] == 'D' && h->id3[2] == '3'
		&& h->ver[0] != 0xff && h->ver[1] != 0xff
		&& (*hsize & 0x80808080) == 0;
}

#pragma pack(push, 4)
const char ffid3_frames[][4] = {
	{'A','P','I','C'}
	, {'C','O','M','M'}
	, {'T','A','L','B'}
	, {'T','C','O','N'}
	, {'T','I','T','2'}
	, {'T','L','E','N'}
	, {'T','P','E','1'}
	, {'T','P','E','2'}
	, {'T','R','C','K'}
	, {'T','Y','E','R'}
};
#pragma pack(pop)

static FFINL uint i28_i32(const void *src)
{
	uint i = ffint_ntoh32(src);
	return ((i & 0x7f000000) >> 3)
		| ((i & 0x007f0000) >> 2)
		| ((i & 0x00007f00) >> 1)
		| (i & 0x0000007f);
}

static FFINL void i32_i28(void *dst, uint i32)
{
	uint i = (i32 & 0x0000007f)
		| ((i32 & (0x0000007f << 7*1)) << 1)
		| ((i32 & (0x0000007f << 7*2)) << 2)
		| ((i32 & (0x0000007f << 7*3)) << 3);
	ffint_hton32(dst, i);
}

uint ffid3_size(const ffid3_hdr *h)
{
	return i28_i32(h->size);
}

uint ffid3_frame(const ffid3_frhdr *fr)
{
	uint i, *p = (void*)fr->id, id = *p;
	for (i = 0;  i < FFCNT(ffid3_frames);  i++) {
		p = (void*)ffid3_frames[i];
		if (id == *p)
			return i;
	}
	return (uint)-1;
}

uint ffid3_frsize(const ffid3_frhdr *fr, uint majver)
{
	if (majver == 4)
		return i28_i32(fr->size);
	return ffint_ntoh32(fr->size);
}


enum { I_HDR, I_FR, I_TXTENC, I_DATA, I_UNSYNC_00, I_FRDONE, I_PADDING };

/*replace: FF 00 -> FF*/
static FFINL int _ffid3_unsync(ffid3 *p, const char *data, size_t len)
{
	size_t i;
	char *out;

	if (NULL == ffarr_grow(&p->data, len, 0))
		return -1; //no mem
	out = ffarr_end(&p->data);

	for (i = 0;  i != len;  i++) {
		switch (p->state) {

		case I_DATA:
			if ((byte)data[i] == 0xff)
				p->state = I_UNSYNC_00;
			break;

		case I_UNSYNC_00:
			p->state = I_DATA;
			if ((byte)data[i] == 0x00)
				continue;
			break;
		}

		*out++ = data[i];
	}

	p->data.len += out - ffarr_end(&p->data);
	return 0;
}

int ffid3_parse(ffid3 *p, const char *data, size_t *len)
{
	const char *dstart = data, *end = data + *len;
	uint n;
	uint r = FFID3_RERR;

	*len = 0;
	if (p->size != 0)
		end = data + ffmin(p->size, end - data);

	while (data != end) {

		switch (p->state) {
		case I_TXTENC:
		case I_DATA:
		case I_UNSYNC_00:
			if (p->frsize == 0)
				p->state = I_FRDONE;
			break;
		}

		switch (p->state) {
		case I_HDR:
			if (sizeof(ffid3_hdr) > end - data)
				return FFID3_RDONE;

			ffmemcpy(&p->h, data, sizeof(ffid3_hdr));
			if (!ffid3_valid(&p->h))
				return FFID3_RDONE; //not a valid ID3v2

			data += sizeof(ffid3_hdr);
			p->size = ffid3_size(&p->h);
			end = data + p->size;

			if (p->h.ver[0] > 4 //v2.4 is max supported version
				|| (p->h.flags & ~0x80) != 0) //not supported
				goto done;

			p->state = I_FR;
			r = FFID3_RHDR;
			goto done;

		case I_FR:
			if (*data == 0x00) {
				p->state = I_PADDING;
				break;
			}

			n = (uint)ffmin(sizeof(ffid3_frhdr) - p->frsize, end - data);
			if (n > p->size)
				goto done; //no space for frame header within the tag
			ffmemcpy((char*)&p->fr + p->frsize, data, n);
			p->frsize += n;
			data += n;
			p->size -= n;

			if (p->frsize != sizeof(ffid3_frhdr)) {
				r = FFID3_RMORE; //frame header is split between the 2 data chunks
				goto done;
			}

			p->frsize = ffid3_frsize(&p->fr, p->h.ver[0]);
			if (p->frsize > p->size)
				goto done; //frame size is too large

			if ((p->fr.flags[1] & ~0x02) != 0)
				goto done; //not supported

			if (p->fr.id[0] == 'T')
				p->state = I_TXTENC;
			else
				p->state = I_DATA;
			r = FFID3_RFRAME;
			goto done;

		case I_TXTENC:
			p->txtenc = (byte)*data;
			data += 1;
			p->size -= 1;
			p->frsize -= 1;
			p->state = I_DATA;
			break;

		case I_DATA:
		case I_UNSYNC_00:
			n = (uint)ffmin(p->frsize, end - data);
			if (p->fr.unsync || p->h.unsync) {
				if (0 != _ffid3_unsync(p, data, n))
					goto done;

			} else if ((p->flags & FFID3_FWHOLE) && (n != p->frsize || p->data.len != 0)) {
				if (NULL == ffarr_append(&p->data, (char*)data, n))
					goto done; //no mem

			} else {
				ffarr_free(&p->data);
				ffarr_set(&p->data, (char*)data, ffmin(p->frsize, end - data));
			}
			data += n;
			p->size -= n;
			p->frsize -= n;

			if ((p->flags & FFID3_FWHOLE) && p->frsize != 0)
				break; //wait until we copy data completely

			r = FFID3_RDATA;
			goto done;

		case I_FRDONE:
			p->data.len = 0;
			p->state = I_FR;
			break;

		case I_PADDING:
			p->size -= (uint)(end - data);
			data = end;
			break;
		}

		if (p->size == 0) {
			r = FFID3_RDONE;
			goto done;
		}
	}

	r = FFID3_RMORE;

done:
	if (r == FFID3_RERR)
		p->state = I_PADDING; //skip the rest of the tag
	*len = data - dstart;
	return r;
}

int ffid3_getdata(const char *data, size_t len, uint txtenc, uint codepage, ffstr3 *dst)
{
	const char *end = data + len;
	uint f;

	switch (txtenc) {
	case FFID3_UTF8:
		ffarr_free(dst);
		ffarr_set(dst, (char*)data, len);
		return (int)len;

	case FFID3_UTF16BOM:
		f = ffutf_bom(data, &len);
		if (f == (uint)-1)
			return -1;
		data += len;
		break;

	case FFID3_UTF16BE:
		f = FFU_UTF16BE;
		break;

	case FFID3_ANSI:
		f = (codepage != 0) ? codepage : FFU_WIN1252;
		break;

	default:
		return -1;
	}

	return (int)ffutf8_strencode(dst, data, end - data, f);
}
