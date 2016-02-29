/**
Copyright (c) 2013 Simon Zolin
*/

#include <FF/audio/id3.h>
#include <FF/data/utf8.h>
#include <FF/number.h>


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

int ffid31_parse(ffid31ex *id31ex, const char *data, size_t *len)
{
	enum { I_COPYTAG, I_TITLE, I_ARTIST, I_ALBUM, I_YEAR, I_COMMENT, I_TRK, I_GENRE, I_DONE };
	ffid31 *id31;
	uint n, *state = &id31ex->state;
	int r = FFID3_RDATA;
	ffstr *val = &id31ex->val;
	const char *dstart = data;

	id31 = (id31ex->ntag != 0) ? &id31ex->tag : (void*)data;

	switch (*state) {

	case I_COPYTAG:
		if (id31ex->ntag != 0 || *len < sizeof(ffid31)) {
			n = ffs_append(&id31ex->tag, id31ex->ntag, sizeof(ffid31), data, *len);
			id31ex->ntag += n;
			if (id31ex->ntag != sizeof(ffid31))
				return FFID3_RMORE;
			data += n;
			id31 = &id31ex->tag;

		} else {
			//the whole tag is in one data block
			id31 = (void*)data;
		}

		if (!ffid31_valid(id31)) {
			r = FFID3_RNO;
			break;
		}

		*state = I_TITLE;
		// break

	case I_TITLE:
		if (id31->title[0] != '\0') {
			*state = I_ARTIST;
			ffstr_setnz(val, id31->title, sizeof(id31->title));
			id31ex->field = FFID3_TITLE;
			break;
		}
		//break

	case I_ARTIST:
		if (id31->artist[0] != '\0') {
			ffstr_setnz(val, id31->artist, sizeof(id31->artist));
			*state = I_ALBUM;
			id31ex->field = FFID3_ARTIST;
			break;
		}
		//break

	case I_ALBUM:
		if (id31->album[0] != '\0') {
			ffstr_setnz(val, id31->album, sizeof(id31->album));
			*state = I_YEAR;
			id31ex->field = FFID3_ALBUM;
			break;
		}
		//break

	case I_YEAR:
		if (id31->year[0] != '\0') {
			ffstr_setnz(val, id31->year, sizeof(id31->year));
			*state = I_COMMENT;
			id31ex->field = FFID3_YEAR;
			break;
		}
		//break

	case I_COMMENT:
		if (id31->comment[0] != '\0') {
			n = (id31->comment30[28] != '\0') ? sizeof(id31->comment30) : sizeof(id31->comment);
			ffstr_setnz(val, id31->comment, n);
			*state = (id31->comment30[28] != '\0') ? I_DONE : I_TRK;
			id31ex->field = FFID3_COMMENT;
			break;
		}
		//break

	case I_TRK:
		if (id31->track_no != 0) {
			n = ffs_fromint(id31->track_no, id31ex->trkno, sizeof(id31ex->trkno), FFINT_ZEROWIDTH | FFINT_WIDTH(2));
			ffstr_set(val, id31ex->trkno, n);
			id31ex->field = FFID3_TRACKNO;
			*state = I_GENRE;
			break;
		}
		//break

	case I_GENRE:
		if (id31->genre < FFCNT(id3_genres)) {
			ffstr_setz(val, id3_genres[id31->genre]);
			id31ex->field = FFID3_GENRE;
			*state = I_DONE;
			break;
		}
		//break

	case I_DONE:
		if (id31 == (void*)data)
			data += sizeof(ffid31);
		r = FFID3_RDONE;
		break;
	}

	*len = data - dstart;
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
	case FFID3_TITLE:
		s = id31->title;
		len = ffmin(len, sizeof(id31->title));
		break;

	case FFID3_ARTIST:
		s = id31->artist;
		len = ffmin(len, sizeof(id31->artist));
		break;

	case FFID3_ALBUM:
		s = id31->album;
		len = ffmin(len, sizeof(id31->album));
		break;

	case FFID3_YEAR:
		s = id31->year;
		len = ffmin(len, sizeof(id31->year));
		break;

	case FFID3_COMMENT:
		s = id31->comment;
		len = ffmin(len, sizeof(id31->comment) - 1);
		break;

	case FFID3_TRACKNO:
		if (len != ffs_toint(data, len, &id31->track_no, FFS_INT8))
			return 0;
		return len;

	case FFID3_GENRE:
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

const char ffid3_frames[][4] = {
	"APIC",
	"COMM",
	"TALB",
	"TCON",
	"TDRC",
	"TENC",
	"TIT2",
	"TLEN",
	"TPE1",
	"TPE2",
	"TRCK",
	"TYER",
};

static const byte ffid3_22_framei[] = {
	FFID3_COMMENT,
	FFID3_PICTURE,
	FFID3_ALBUM,
	FFID3_GENRE,
	FFID3_ENCODEDBY,
	FFID3_LENGTH,
	FFID3_ARTIST,
	FFID3_TRACKNO,
	FFID3_TITLE,
	FFID3_YEAR,
};

static const char ffid3_22_frames[][3] = {
	"COM",
	"PIC",
	"TAL",
	"TCO",
	"TEN",
	"TLE",
	"TP1",
	"TRK",
	"TT2",
	"TYE",
};

/** Return enum FFID3_FRAME. */
static FFINL int _ffid3_frame(const ffid3_frhdr *fr)
{
	return ffcharr_findsorted(ffid3_frames, FFCNT(ffid3_frames), sizeof(*ffid3_frames), fr->id, 4);
}

static FFINL int _ffid3_frame22(const ffid3_frhdr22 *fr)
{
	int id = ffcharr_findsorted(ffid3_22_frames, FFCNT(ffid3_22_frames), sizeof(*ffid3_22_frames), fr->id, 3);
	if (id != -1)
		id = ffid3_22_framei[id];
	return id;
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


enum { I_HDR, I_FR, I_TXTENC, I_DATA, I_TRKTOTAL, I_UNSYNC_00, I_FRDONE, I_PADDING };

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
	uint n, frsz;
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

			if (p->h.ver[0] < 2 || p->h.ver[0] > 4 //v2.2-v2.4
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
			frsz = (p->h.ver[0] == 2) ? sizeof(ffid3_frhdr22) : sizeof(ffid3_frhdr);

			n = ffs_append(&p->fr, p->frsize, frsz, data, end - data);
			if (n > p->size)
				goto done; //no space for frame header within the tag
			p->frsize += n;
			data += n;
			p->size -= n;

			if (p->frsize != frsz) {
				r = FFID3_RMORE; //frame header is split between the 2 data chunks
				goto done;
			}

			p->frsize = _ffid3_frsize(&p->fr, p->h.ver[0]);
			if (p->frsize > p->size)
				goto done; //frame size is too large

			if (p->h.ver[0] == 2)
				p->frame = _ffid3_frame22(&p->fr22);
			else
				p->frame = _ffid3_frame(&p->fr);

			if ((p->fr.flags[1] & ~0x02) != 0)
				goto done; //not supported

			if (p->fr.id[0] == 'T' || p->frame == FFID3_COMMENT)
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

			if (p->h.ver[0] == 2 && p->data.len != 0)
				p->data.len--; //remove last '\0'

			if (p->frame == FFID3_TRACKNO)
				p->state = I_TRKTOTAL;

			r = FFID3_RDATA;
			goto done;

		case I_TRKTOTAL:
			p->state = I_DATA;
			if (NULL == ffs_findc(p->data.ptr, p->data.len, '/'))
				continue;
			p->frame = FFID3_TRACKTOTAL;
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
	uint r, f, igenre;
	ssize_t n;

	if (txtenc == -1) {
		ffarr_free(dst);
		ffarr_set(dst, (char*)data, len);
		return (int)len;
	}

	switch (frame) {
	case FFID3_COMMENT:
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
				data += 2;
				if (data > end)
					return -1; //no space for BOM
				// break

			case FFID3_UTF16BE:
				if (end == (data = ffs_finds(data, end - data, "\0", 2)))
					return -1; //no end of short description
				data += 2;
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
		r = (int)len;
		goto process;

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

	if (0 == (r = (int)ffutf8_strencode(dst, data, end - data, f)))
		goto done;

process:
	switch (frame) {
	case FFID3_TRACKNO:
		if (NULL != (slash = ffs_findc(dst->ptr, dst->len, '/')))
			dst->len = r = slash - dst->ptr;
		break;

	case FFID3_TRACKTOTAL:
		if (NULL != (slash = ffs_findc(dst->ptr, dst->len, '/'))) {
			_ffarr_rmleft(dst, slash + FFSLEN("/") - dst->ptr, sizeof(char));
			r = dst->len;
		}
		break;

	case FFID3_GENRE:
		if ((n = ffs_fmatch(dst->ptr, dst->len, "(%u)", &igenre)) > 0) {

			if ((size_t)n == dst->len && igenre < FFCNT(id3_genres)) {
				ffarr_free(dst);
				ffstr_setz(dst, id3_genres[igenre]);
				return dst->len;
			}

			_ffarr_rmleft(dst, n, sizeof(char));
			r = dst->len;
		}
		break;
	}

done:
	return r;
}
