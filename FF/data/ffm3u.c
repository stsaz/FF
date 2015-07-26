/**
Copyright (c) 2015 Simon Zolin
*/

#include <FF/data/m3u.h>


int ffm3u_parse(ffparser *p, const char *data, size_t *len)
{
	enum { I_BOM, I_BOM2, I_BOM3, I_START, I_CMT, I_CR
		, I_SHARP, I_DUR, I_ARTIST_1, I_ARTIST, I_TITLE_1, I_TITLE, I_NAME };
	int r = FFPARS_MORE, st = p->state, st2, ch;
	const char *d = data, *end = data + *len;

	while (data != end) {
		ch = *data;

		if (ch == '\n' || ch == '\r') {
			st2 = st;
			if (ch == '\n') {
				st = I_START;
				p->line++;
			} else
				st = I_CR;
			data++;

			switch (st2) {
			case I_CR:
				if (ch == '\r')
					p->line++;
				break;

			case I_TITLE:
				p->type = FFM3U_TITLE;
				r = FFPARS_VAL;
				goto done;

			case I_NAME:
				p->type = FFM3U_NAME;
				r = FFPARS_VAL;
				goto done;
			}

			continue;
		}

		switch (st) {
		case I_BOM:
			if (ch == 0xef) {
				st = I_BOM2;
				break;
			}
			// st = I_START;
			goto start;

		case I_BOM2:
			st = I_BOM3;
			break;

		case I_BOM3:
			st = I_START;
			break;

		case I_START:
start:
			ffpars_cleardata(p);
			if (ch == '#') {
				st = I_SHARP;
				break;
			}
			st = I_NAME;
			goto addchar;

		case I_CR:
			ffpars_cleardata(p);
			p->line++;
			st = I_START;
			continue;

		case I_CMT:
			break;

		case I_SHARP:
			if (p->val.len == FFSLEN("EXTINF:")) {
				if (ffstr_eqcz(&p->val, "EXTINF:")) {
					ffpars_cleardata(p);
					st = I_DUR;
					goto addchar;
				} else {
					ffpars_cleardata(p);
					st = I_CMT;
				}
			} else
				goto addchar;
			break;

		case I_DUR:
			if (ch != ',')
				goto addchar;
			if (p->val.len != ffs_toint(p->val.ptr, p->val.len, &p->intval, FFS_INT64 | FFS_INTSIGN)) {
				st = I_CMT;
				break;
			}
			r = FFPARS_VAL;
			p->type = FFM3U_DUR;
			st = I_ARTIST_1;
			break;

		case I_ARTIST_1:
			ffpars_cleardata(p);
			st = I_ARTIST;
			// break

		case I_ARTIST:
			if (ch == ' '
				&& p->val.len >= FFSLEN(" -")
				&& p->val.ptr[p->val.len - 1] == '-'
				&& p->val.ptr[p->val.len - 2] == ' ') {
				p->val.len -= FFSLEN(" -");
				r = FFPARS_VAL;
				p->type = FFM3U_ARTIST;
				st = I_TITLE_1;
				break;
			}
			goto addchar;

		case I_TITLE_1:
			ffpars_cleardata(p);
			st = I_TITLE;
			// break

		case I_NAME:
		case I_TITLE:
addchar:
			r = _ffpars_addchar2(p, data);
			break;
		}

		data++;
		if (r != 0)
			break;
	}

	if (r == FFPARS_MORE)
		r = ffpars_savedata(p);

done:
	p->state = st;
	*len = data - d;
	return r;
}
