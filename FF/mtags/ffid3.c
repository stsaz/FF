/**
Copyright (c) 2013 Simon Zolin
*/

#include <FF/mtags/id3.h>
#include <FF/data/utf8.h>
#include <FF/number.h>
#include <FFOS/error.h>


static const char *const id3_genres[] = {
	"Blues", "Classic Rock", "Country", "Dance", "Disco",
	"Funk", "Grunge", "Hip-Hop", "Jazz", "Metal",
	"New Age", "Oldies", "Other", "Pop", "R&B",
	"Rap", "Reggae", "Rock", "Techno", "Industrial",
	"Alternative", "Ska", "Death Metal", "Pranks", "Soundtrack",
	"Euro-Techno", "Ambient", "Trip-Hop", "Vocal", "Jazz+Funk",
	"Fusion", "Trance", "Classical", "Instrumental", "Acid",
	"House", "Game", "Sound Clip", "Gospel", "Noise",
	"AlternRock", "Bass", "Soul", "Punk", "Space",
	"Meditative", "Instrumental Pop", "Instrumental Rock", "Ethnic", "Gothic",
	"Darkwave", "Techno-Industrial", "Electronic", "Pop-Folk", "Eurodance",
	"Dream", "Southern Rock", "Comedy", "Cult", "Gangsta",
	"Top 40", "Christian Rap", "Pop/Funk", "Jungle", "Native American",
	"Cabaret", "New Wave", "Psychadelic", "Rave", "Showtunes",
	"Trailer", "Lo-Fi", "Tribal", "Acid Punk", "Acid Jazz",
	"Polka", "Retro", "Musical", "Rock & Roll", "Hard Rock", //75-79
};

const char* ffid31_genre(uint id)
{
	if (id >= FFCNT(id3_genres))
		return "";
	return id3_genres[id];
}

int ffid31_parse(ffid31ex *id31ex, const char *data, size_t len)
{
	enum { I_HDR, I_TITLE, I_ARTIST, I_ALBUM, I_YEAR, I_COMMENT, I_TRK, I_GENRE, I_DONE };
	ffid31 *id31 = (void*)data;
	uint n, *state = &id31ex->state;
	int r = FFID3_RDATA;
	ffstr *val = &id31ex->val;

	switch (*state) {

	case I_HDR:
		if (len != sizeof(ffid31) || !ffid31_valid(id31)) {
			r = FFID3_RNO;
			break;
		}
		*state = I_TITLE;
		// break

	case I_TITLE:
		if (id31->title[0] != '\0') {
			ffstr_setnz(val, id31->title, sizeof(id31->title));
			ffstr_rskip(val, ' ');
			if (val->len != 0) {
				id31ex->field = FFMMTAG_TITLE;
				*state = I_ARTIST;
				break;
			}
		}
		//break

	case I_ARTIST:
		if (id31->artist[0] != '\0') {
			ffstr_setnz(val, id31->artist, sizeof(id31->artist));
			ffstr_rskip(val, ' ');
			if (val->len != 0) {
				id31ex->field = FFMMTAG_ARTIST;
				*state = I_ALBUM;
				break;
			}
		}
		//break

	case I_ALBUM:
		if (id31->album[0] != '\0') {
			ffstr_setnz(val, id31->album, sizeof(id31->album));
			ffstr_rskip(val, ' ');
			if (val->len != 0) {
				id31ex->field = FFMMTAG_ALBUM;
				*state = I_YEAR;
				break;
			}
		}
		//break

	case I_YEAR:
		if (id31->year[0] != '\0') {
			ffstr_setnz(val, id31->year, sizeof(id31->year));
			ffstr_rskip(val, ' ');
			if (val->len != 0) {
				id31ex->field = FFMMTAG_DATE;
				*state = I_COMMENT;
				break;
			}
		}
		//break

	case I_COMMENT:
		if (id31->comment[0] != '\0') {
			n = (id31->comment30[28] != '\0') ? sizeof(id31->comment30) : sizeof(id31->comment);
			ffstr_setnz(val, id31->comment, n);
			ffstr_rskip(val, ' ');
			if (val->len != 0) {
				id31ex->field = FFMMTAG_COMMENT;
				*state = (id31->comment30[28] != '\0') ? I_DONE : I_TRK;
				break;
			}
		}
		//break

	case I_TRK:
		if (id31->track_no != 0) {
			n = ffs_fromint(id31->track_no, id31ex->trkno, sizeof(id31ex->trkno), FFINT_ZEROWIDTH | FFINT_WIDTH(2));
			ffstr_set(val, id31ex->trkno, n);
			id31ex->field = FFMMTAG_TRACKNO;
			*state = I_GENRE;
			break;
		}
		//break

	case I_GENRE:
		if (id31->genre < FFCNT(id3_genres)) {
			ffstr_setz(val, id3_genres[id31->genre]);
			id31ex->field = FFMMTAG_GENRE;
			*state = I_DONE;
			break;
		}
		//break

	case I_DONE:
		r = FFID3_RDONE;
		break;
	}

	return r;
}


void ffid31_init(ffid31 *id31)
{
	ffmem_tzero(id31);
	ffmemcpy(id31->tag, "TAG", 3);
	id31->genre = (byte)-1;
}

int ffid31_add(ffid31 *id31, uint id, const char *data, size_t len)
{
	char *s;

	switch (id) {
	case FFMMTAG_TITLE:
		s = id31->title;
		len = ffmin(len, sizeof(id31->title));
		break;

	case FFMMTAG_ARTIST:
		s = id31->artist;
		len = ffmin(len, sizeof(id31->artist));
		break;

	case FFMMTAG_ALBUM:
		s = id31->album;
		len = ffmin(len, sizeof(id31->album));
		break;

	case FFMMTAG_DATE:
		s = id31->year;
		len = ffmin(len, sizeof(id31->year));
		break;

	case FFMMTAG_COMMENT:
		s = id31->comment;
		len = ffmin(len, sizeof(id31->comment) - 1);
		break;

	case FFMMTAG_TRACKNO:
		if (len != ffs_toint(data, len, &id31->track_no, FFS_INT8))
			return 0;
		return len;

	case FFMMTAG_GENRE:
		{
		ssize_t r;
		if (-1 == (r = ffs_ifindarrz(id3_genres, FFCNT(id3_genres), data, len)))
			return 0;
		id31->genre = r;
		return len;
		}

	default:
		return 0;
	}

	ffmemcpy(s, data, len);
	return len;
}


ffbool ffid3_valid(const ffid3_hdr *h)
{
	const uint *hsize = (void*)h->size;
	return h->id3[0] == 'I' && h->id3[1] == 'D' && h->id3[2] == '3'
		&& h->ver[0] != 0xff && h->ver[1] != 0xff
		&& (*hsize & 0x80808080) == 0;
}

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

static const byte ffid3_framei[] = {
	FFMMTAG_PICTURE,
	FFMMTAG_COMMENT,
	FFMMTAG_ALBUM,
	FFMMTAG_GENRE,
	FFMMTAG_TITLE,
	FFID3_LENGTH,
	FFMMTAG_ARTIST,
	FFMMTAG_ALBUMARTIST,
	FFMMTAG_PUBLISHER,
	FFMMTAG_TRACKNO,
	FFMMTAG_DATE,
};

static const char ffid3_frames[][4] = {
	"APIC",
	"COMM", // "LNG" "SHORT" \0 "TEXT"
	"TALB",
	"TCON", // "Genre" | "(NN)Genre" | "(NN)" where NN is ID3v1 genre index
	// "TDRC", // "yyyy[-MM[-dd[THH[:mm[:ss]]]]]"
	// "TENC",
	"TIT2",
	"TLEN",
	"TPE1",
	"TPE2",
	"TPUB",
	"TRCK", // "N[/TOTAL]"
	"TYER",
};

static const byte ffid3_22_framei[] = {
	FFMMTAG_COMMENT,
	FFMMTAG_PICTURE,
	FFMMTAG_ALBUM,
	FFMMTAG_GENRE,
	FFID3_LENGTH,
	FFMMTAG_ARTIST,
	FFMMTAG_TRACKNO,
	FFMMTAG_TITLE,
	FFMMTAG_DATE,
};

static const char ffid3_22_frames[][3] = {
	"COM",
	"PIC",
	"TAL",
	"TCO",
	// "TEN",
	"TLE",
	"TP1",
	"TRK",
	"TT2",
	"TYE",
};

/** Return enum FFID3_FRAME. */
static FFINL int _ffid3_frame(const ffid3_frhdr *fr)
{
	int i = ffcharr_findsorted(ffid3_frames, FFCNT(ffid3_frames), sizeof(*ffid3_frames), fr->id, 4);
	if (i < 0)
		return 0;
	return ffid3_framei[i];
}

static FFINL int _ffid3_frame22(const ffid3_frhdr22 *fr)
{
	int id = ffcharr_findsorted(ffid3_22_frames, FFCNT(ffid3_22_frames), sizeof(*ffid3_22_frames), fr->id, 3);
	if (id != -1)
		return ffid3_22_framei[id];
	return 0;
}

/** Get frame size. */
static FFINL uint _ffid3_frsize(const void *fr, uint majver)
{
	const ffid3_frhdr22 *fr22;

	switch (majver) {
	case 2:
		fr22 = fr;
		return (fr22->size[0] << 16) | (fr22->size[1] << 8) | fr22->size[2];
	case 3:
		return ffint_ntoh32(((ffid3_frhdr*)fr)->size);
	default:
		return i28_i32(((ffid3_frhdr*)fr)->size);
	}
}


enum {
	ID3_EOK,
	ID3_ESYS,
	ID3_EVER,
	ID3_EFLAGS,
	ID3_ESMALL,
	ID3_EFRFLAGS,
	ID3_EFRBIG,
};

static const char* const _ffid3_errs[] = {
	"",
	"",
	"unsupported version",
	"unsupported header flags",
	"tag is too small",
	"unsupported frame flags",
	"frame is too large",
};

const char* ffid3_errstr(int e)
{
	if (e == ID3_ESYS)
		return fferr_strp(fferr_last());
	if ((uint)e >= FFCNT(_ffid3_errs))
		return "";
	return _ffid3_errs[e];
}


enum {
	I_HDR, I_GATHER, I_EXTHDR_SIZE, I_EXTHDR_DATA,
	I_FR, I_FRHDR, I_FR_DATALEN, I_TXTENC, I_DATA, I_TRKTOTAL, I_UNSYNC_00, I_FRDONE,
	I_PADDING
};

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
	int r = FFID3_RERR, rr;

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
			p->frsize += ffs_append(&p->h, p->frsize, sizeof(ffid3_hdr), data, end - data);
			if (p->frsize != sizeof(ffid3_hdr))
				return FFID3_RMORE;
			if (!ffid3_valid(&p->h)) {
				*len = 0;
				return FFID3_RNO; //not a valid ID3v2
			}
			p->frsize = 0;

			data += sizeof(ffid3_hdr);
			p->size = ffid3_size(&p->h);
			end = data + p->size;

			if (p->h.ver[0] < 2 || p->h.ver[0] > 4) /*v2.2-v2.4*/ {
				p->err = ID3_EVER;
				goto done;
			}
			if ((p->h.ver[0] == 3 && (p->h.flags & ~(FFID3_FHDR_UNSYNC | FFID3_FHDR_EXTHDR)) != 0)
				|| (p->h.ver[0] != 3 && (p->h.flags & ~FFID3_FHDR_UNSYNC) != 0)) {
				p->err = ID3_EFLAGS;
				goto done;
			}

			p->state = I_FR;
			if (p->h.ver[0] == 3 && (p->h.flags & FFID3_FHDR_EXTHDR)) {
				p->gsize = sizeof(ffid3_exthdr);
				p->state = I_GATHER,  p->gstate = I_EXTHDR_SIZE;
			}

			r = FFID3_RHDR;
			goto done;

		case I_GATHER:
			if (p->data.len == 0 && p->size < p->gsize) {
				p->err = ID3_ESMALL;
				return FFID3_RERR;
			}

			rr = ffarr_gather(&p->data, data, end - data, p->gsize);
			if (rr < 0) {
				p->err = ID3_ESYS;
				return FFID3_RERR;
			}
			data += rr;
			p->size -= rr;
			if (p->data.len != p->gsize)
				return FFID3_RMORE;
			p->gsize = 0;
			p->state = p->gstate;
			continue;

		case I_EXTHDR_SIZE: {
			p->data.len = 0;
			ffid3_exthdr *eh = (void*)p->data.ptr;
			p->gsize = ffint_ntoh32(eh->size);
			p->state = I_GATHER,  p->gstate = I_EXTHDR_DATA;
			continue;
		}

		case I_EXTHDR_DATA:
			p->data.len = 0;
			p->state = I_FR;
			continue;

		case I_FR:
			if (*data == 0x00) {
				p->state = I_PADDING;
				break;
			}
			p->gsize = (p->h.ver[0] == 2) ? sizeof(ffid3_frhdr22) : sizeof(ffid3_frhdr);
			p->state = I_GATHER,  p->gstate = I_FRHDR;
			continue;

		case I_FRHDR:
			ffmemcpy(&p->fr, p->data.ptr, p->data.len);
			p->data.len = 0;
			p->frsize = _ffid3_frsize(&p->fr, p->h.ver[0]);

			FFDBG_PRINTLN(10, "frame %*s  size:%u  flags:%2xb"
				, (p->h.ver[0] == 2) ? (size_t)3 : (size_t)4, p->fr.id
				, p->frsize
				, (p->h.ver[0] == 2) ? "\0\0" : (char*)p->fr.flags);

			if (p->frsize > p->size) {
				p->err = ID3_EFRBIG;
				goto done; //frame size is too large
			}

			if (p->h.ver[0] == 2)
				p->frame = _ffid3_frame22(&p->fr22);
			else
				p->frame = _ffid3_frame(&p->fr);

			if (((p->h.ver[0] == 4) && (p->fr.flags[1] & ~(FFID324_FFR1_DATALEN | FFID324_FFR1_UNSYNC)) != 0)
				|| ((p->h.ver[0] != 4) && p->fr.flags[1] != 0)) {
				p->err = ID3_EFRFLAGS;
				goto done;
			}

			if (p->fr.id[0] == 'T' || p->frame == FFMMTAG_COMMENT)
				p->state = I_TXTENC;
			else
				p->state = I_DATA;

			if ((p->h.ver[0] == 4) && (p->fr.flags[1] & FFID324_FFR1_DATALEN)) {
				p->gsize = 4;
				p->nxstate = p->state;
				p->state = I_GATHER,  p->gstate = I_FR_DATALEN;
			}

			r = FFID3_RFRAME;
			goto done;

		case I_FR_DATALEN:
			p->frsize -= 4;
			p->data.len = 0;
			p->state = p->nxstate;
			continue;

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
			if ((p->fr.flags[1] & FFID324_FFR1_UNSYNC) || (p->h.flags & FFID3_FHDR_UNSYNC)) {
				if (0 != _ffid3_unsync(p, data, n)) {
					p->err = ID3_ESYS;
					goto done;
				}

			} else if ((p->flags & FFID3_FWHOLE) && (n != p->frsize || p->data.len != 0)) {
				if (NULL == ffarr_append(&p->data, (char*)data, n)) {
					p->err = ID3_ESYS;
					goto done;
				}

			} else {
				ffarr_free(&p->data);
				ffarr_set(&p->data, (char*)data, ffmin(p->frsize, end - data));
			}
			data += n;
			p->size -= n;
			p->frsize -= n;

			if ((p->flags & FFID3_FWHOLE) && p->frsize != 0)
				break; //wait until we copy data completely

			if (p->frame == FFMMTAG_TRACKNO)
				p->state = I_TRKTOTAL;

			r = FFID3_RDATA;
			goto done;

		case I_TRKTOTAL:
			p->state = I_DATA;
			if (NULL == ffs_findc(p->data.ptr, p->data.len, '/'))
				continue;
			p->frame = FFMMTAG_TRACKTOTAL;
			r = FFID3_RDATA;
			goto done;

		case I_FRDONE:
			p->txtenc = -1;
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

int ffid3_getdata(int frame, const char *data, size_t len, int txtenc, uint codepage, ffstr3 *dst)
{
	const char *end = data + len, *slash;
	uint f, igenre;
	ssize_t n;

	if (txtenc == -1) {
		ffarr_free(dst);
		ffarr_set(dst, (char*)data, len);
		return (int)len;
	}

	switch (frame) {
	case FFMMTAG_COMMENT:
		if (len >= FFSLEN("LNG\0")) {
			data += FFSLEN("LNG");

			//skip short description
			switch (txtenc) {
			case FFID3_ANSI:
			case FFID3_UTF8:
				if (NULL == (data = ffs_findc(data, end - data, '\0')))
					return -1; //no end of short description
				data++;
				break;

			case FFID3_UTF16BOM:
			case FFID3_UTF16BE:
				if (0 > (n = ffutf16_findc(data, end - data, 0)))
					return -1; //no end of short description
				data += n + 2;
				break;
			}

			len = end - data;
		}
		break;
	}

	switch (txtenc) {
	case FFID3_UTF8:
		ffarr_free(dst);
		ffarr_set(dst, (char*)data, len);
		break;

	case FFID3_UTF16BOM:
		f = ffutf_bom(data, &len);
		if (f != FFU_UTF16LE && f != FFU_UTF16BE)
			return -1; //invalid BOM
		data += 2;
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

	if (txtenc != FFID3_UTF8
		&& 0 == ffutf8_strencode(dst, data, end - data, f))
		return 0;

	switch (frame) {
	case FFMMTAG_TRACKNO:
		if (NULL != (slash = ffs_findc(dst->ptr, dst->len, '/')))
			dst->len = slash - dst->ptr;
		break;

	case FFMMTAG_TRACKTOTAL:
		if (NULL != (slash = ffs_findc(dst->ptr, dst->len, '/'))) {
			_ffarr_rmleft(dst, slash + FFSLEN("/") - dst->ptr, sizeof(char));
		}
		break;

	case FFMMTAG_GENRE:
		if ((n = ffs_fmatch(dst->ptr, dst->len, "(%u)", &igenre)) > 0) {

			if ((size_t)n == dst->len && igenre < FFCNT(id3_genres)) {
				ffarr_free(dst);
				ffstr_setz(dst, id3_genres[igenre]);
				return dst->len;
			}

			_ffarr_rmleft(dst, n, sizeof(char));
		}
		break;
	}

	if (dst->len != 0 && ffarr_back(dst) == '\0')
		dst->len--;

	return dst->len;
}


uint ffid3_add(ffid3_cook *id3, uint id, const char *data, size_t len)
{
	if (id == FFMMTAG_TRACKNO) {
		ffsz_copy(id3->trackno, sizeof(id3->trackno), data, len);
		return 1;
	} else if (id == FFMMTAG_TRACKTOTAL) {
		ffsz_copy(id3->tracktotal, sizeof(id3->tracktotal), data, len);
		return 1;
	}
	if (-1 == (int)(id = ffint_find1(ffid3_framei, FFCNT(ffid3_framei), id)))
		return 0;
	return ffid3_addframe(id3, ffid3_frames[id], data, len, 0);
}

uint ffid3_addframe(ffid3_cook *id3, const char id[4], const char *data, size_t len, uint flags)
{
	if (id3->buf.len == 0) {
		// reserve space for ID3v2 header
		if (NULL == ffarr_alloc(&id3->buf, sizeof(ffid3_hdr)))
			return 0;
		id3->buf.len = sizeof(ffid3_hdr);
	}

	size_t n = sizeof(ffid3_frhdr) + 1 + len;

	if (!ffs_cmp(id, "COMM", 4))
		n += FFSLEN("LNG\0");

	if (n > (uint)-1 || NULL == ffarr_grow(&id3->buf, n, 0))
		return 0;

	char *p = ffarr_end(&id3->buf);
	ffid3_frhdr *fr = (void*)p;
	ffmemcpy(fr->id, id, 4);
	i32_i28(fr->size, n - sizeof(ffid3_frhdr));
	fr->flags[0] = 0; fr->flags[1] = 0;
	p += sizeof(ffid3_frhdr);

	*p++ = FFID3_UTF8;

	if (!ffs_cmp(id, "COMM", 4))
		p = ffmem_copy(p, "eng\0", 4);

	ffmemcpy(p, data, len);

	id3->buf.len += n;
	return (uint)n;
}

/** Prepare TRCK frame data: "TRACKNO [/ TRACKTOTAL]" */
static uint _ffid3_trackno(char *trackno, size_t trackno_cap, const char *tracktotal)
{
	char *d = trackno;
	if (trackno[0] != '\0')
		d = trackno + ffsz_len(trackno);
	else
		*d++ = '0';

	if (tracktotal[0] != '\0') {
		d = ffs_copyc(d, trackno + trackno_cap, '/');
		d = ffs_copyz(d, trackno + trackno_cap, tracktotal);
	}
	return d - trackno;
}

uint ffid3_flush(ffid3_cook *id3)
{
	if (id3->trackno[0] != '\0' || id3->tracktotal[0] != '\0') {
		uint n = _ffid3_trackno(id3->trackno, sizeof(id3->trackno), id3->tracktotal);
		uint i = ffint_find1(ffid3_framei, FFCNT(ffid3_framei), FFMMTAG_TRACKNO);
		return ffid3_addframe(id3, ffid3_frames[i], id3->trackno, n, 0);
	}
	return 0;
}

int ffid3_padding(ffid3_cook *id3, size_t len)
{
	if (NULL == ffarr_grow(&id3->buf, len, 0))
		return -1;

	ffmem_zero(ffarr_end(&id3->buf), len);
	id3->buf.len += len;
	return 0;
}

void ffid3_fin(ffid3_cook *id3)
{
	if (id3->buf.len < sizeof(ffid3_hdr)) {
		if (NULL == ffarr_alloc(&id3->buf, sizeof(ffid3_hdr)))
			return;
		id3->buf.len = sizeof(ffid3_hdr);
	}

	ffid3_hdr *h = (void*)id3->buf.ptr;
	h->id3[0] = 'I'; h->id3[1] = 'D'; h->id3[2] = '3';
	h->ver[0] = 4; h->ver[1] = 0;
	h->flags = 0;
	i32_i28(h->size, id3->buf.len - sizeof(ffid3_hdr));
}
