/**
Copyright (c) 2015 Simon Zolin
*/

#include <FF/data/cue.h>


enum {
	CMD_FILE,
	CMD_INDEX,
	CMD_PERFORMER,
	CMD_REM,
	CMD_TITLE,
	CMD_TRACK,
};

static const char *const _ffcue_dict[] = {
	"FILE",
	"INDEX",
	"PERFORMER",
	"REM",
	"TITLE",
	"TRACK",
};

int ffcue_parse(ffparser *p, const char *data, size_t *len)
{
	int ch, st = p->state, st2, r = FFPARS_MORE;
	const char *d = data, *end = data + *len;
	enum { I_WSPACE, I_CMD, I_SKIPLINE, I_CR
		, I_VAL, I_VAL_BARE, I_VAL_QUOT
		, I_REM, I_REM_VAL
		, I_FILE, I_FILE_QUOT, I_FILE_QUOT_AFTER, I_FILE_TYPE
		, I_TRACK, I_TRACKNO, I_TRK_TYPE_START, I_TRK_TYPE, I_TRK_CMD, I_TRK_INDEX
	};

	while (data != end) {
		ch = *data;

		if (ch == '\n' || ch == '\r') {
			st2 = st;
			if (ch == '\n') {
				st = I_WSPACE;
				p->line++;
				ffpars_cleardata(p);
			} else
				st = I_CR;
			data++;

			switch (st2) {
			case I_VAL_BARE:
			case I_FILE_TYPE:
				r = FFPARS_VAL;
				goto done;

			case I_CR:
				if (ch == '\r')
					p->line++;
				break;

			case I_WSPACE:
			case I_SKIPLINE:
			case I_TRK_TYPE:
				break;

			default:
				r = FFPARS_ENOVAL; //incomplete line
				goto done;
			}

			continue;
		}

		switch (st) {
		case I_WSPACE:
			if (ch == ' ' || ch == '\t')
				break;
			if (p->nextst != 0) {
				st = p->nextst;
				p->nextst = 0;
				continue;
			}
			p->val.ptr = (char*)data;
			st = I_CMD;
			// break

		case I_CMD:
			if (ch != ' ')
				goto addchar;
			switch (ffszarr_ifindsorted(_ffcue_dict, FFCNT(_ffcue_dict), p->val.ptr, p->val.len)) {
			case CMD_REM:
				st = I_REM;
				break;
			case CMD_PERFORMER:
				p->type = FFCUE_PERFORMER;
				st = I_VAL;
				break;
			case CMD_TITLE:
				p->type = FFCUE_TITLE;
				st = I_VAL;
				break;
			case CMD_FILE:
				st = I_FILE;
				break;
			default:
				st = I_SKIPLINE;
			}
			ffpars_cleardata(p);
			break;

		case I_CR:
			ffpars_cleardata(p);
			p->line++;
			st = I_WSPACE;
			continue;

		case I_SKIPLINE:
			break;

		case I_REM:
			if (ch != ' ')
				goto addchar;
			st = I_REM_VAL;
			p->type = FFCUE_REM_NAME;
			r = FFPARS_VAL;
			break;

		case I_REM_VAL:
			ffpars_cleardata(p);
			p->type = FFCUE_REM_VAL;
			st = I_VAL;
			continue;

// FILE
		case I_FILE:
			if (ch == '"') {
				st = I_FILE_QUOT;
				p->type = FFCUE_FILE;
			} else
				r = FFPARS_EBADVAL;
			break;

		case I_FILE_QUOT:
			if (ch != '"')
				goto addchar;
			r = FFPARS_VAL;
			st = I_FILE_QUOT_AFTER;
			break;

		case I_FILE_QUOT_AFTER:
			ffpars_cleardata(p);
			if (ch == ' ') {
				st = I_FILE_TYPE;
				p->type = FFCUE_FILETYPE;
				p->nextst = I_TRACK;
			} else
				r = FFPARS_EBADVAL; //expected whitespace
			break;

		case I_FILE_TYPE:
			goto addchar;

// TRACK
		case I_TRACK:
			if (ch != ' ')
				goto addchar;
			if (ffstr_eqcz(&p->val, "TRACK")) {
				st = I_TRACKNO;
				ffpars_cleardata(p);
			} else {
				st = I_CMD;
				continue;
			}
			break;

		case I_TRACKNO:
			if (ch != ' ')
				goto addchar;
			if (2 != ffs_toint(p->val.ptr, p->val.len, &p->intval, FFS_INT64)) {
				r = FFPARS_EBADVAL;
				break;
			}
			r = FFPARS_VAL;
			p->type = FFCUE_TRACKNO;
			p->nextst = I_TRK_CMD;
			st = I_TRK_TYPE_START;
			break;

		case I_TRK_TYPE_START:
			ffpars_cleardata(p);
			st = I_TRK_TYPE;
			break;

		case I_TRK_TYPE:
			break;

		case I_TRK_CMD:
			if (ch != ' ')
				goto addchar;
			switch (ffszarr_ifindsorted(_ffcue_dict, FFCNT(_ffcue_dict), p->val.ptr, p->val.len)) {
			case CMD_PERFORMER:
				p->type = FFCUE_TRK_PERFORMER;
				st = I_VAL;
				p->nextst = I_TRK_CMD;
				break;
			case CMD_TITLE:
				p->type = FFCUE_TRK_TITLE;
				st = I_VAL;
				p->nextst = I_TRK_CMD;
				break;
			case CMD_INDEX:
				st = I_TRK_INDEX;
				break;

			case CMD_TRACK:
				st = I_TRACKNO;
				break;
			case CMD_FILE:
				st = I_FILE;
				break;

			default:
				st = I_SKIPLINE;
				p->nextst = I_TRK_CMD;
			}
			ffpars_cleardata(p);
			break;

		case I_TRK_INDEX:
			if (0 != (r = _ffpars_addchar2(p, data)))
				break;

			if (p->val.len == FFSLEN("00 00:00:00")) {
				uint idx, m, s, f;
				if (p->val.len != ffs_fmatch(p->val.ptr, p->val.len, "%2u %2u:%2u:%2u", &idx, &m, &s, &f)) {
					r = FFPARS_EBADVAL;
					break;
				}
				p->intval = (int64)(m * 60 + s) * 75 + f;
				r = FFPARS_VAL;
				p->type = (idx == 0) ? FFCUE_TRK_INDEX00 : FFCUE_TRK_INDEX;
				p->nextst = I_TRK_CMD;
				st = I_SKIPLINE;
			}
			break;

// VALUE
		case I_VAL:
			if (ch == '"') {
				st = I_VAL_QUOT;
			} else {
				p->val.ptr = (char*)data;
				st = I_VAL_BARE;
				goto addchar;
			}
			break;

		case I_VAL_BARE:
addchar:
			r = _ffpars_addchar2(p, data);
			break;

		case I_VAL_QUOT:
			if (ch == '"') {
				r = FFPARS_VAL;
				st = I_SKIPLINE;
			} else
				goto addchar;
			break;
		}

		data++;

		if (r != 0)
			break;
	}

	if (r == FFPARS_MORE)
		ffpars_savedata(p);

done:
	p->state = st;
	*len = data - d;
	return r;
}


ffcuetrk* ffcue_index(ffcue *c, uint type, uint val)
{
	switch (type) {
	case FFCUE_FILE:
		c->from = -1;
		c->first = 1;
		break;

	case FFCUE_TRACKNO:
		c->trk.from = c->trk.to = -1;
		break;

	case FFCUE_TRK_INDEX00:
		if (c->first) {
			if (c->options == FFCUE_GAPPREV1 || c->options == FFCUE_GAPCURR)
				c->from = val;
			break;
		}

		if (c->options == FFCUE_GAPSKIP)
			c->trk.to = val;
		else if (c->options == FFCUE_GAPCURR) {
			c->trk.to = val;
			c->trk.from = c->from;
			c->from = val;
		}
		break;

	case FFCUE_TRK_INDEX:
		if (c->first) {
			c->first = 0;
			if (c->from == (uint)-1)
				c->from = val;
			break;
		}

		if (c->trk.from == (uint)-1) {
			c->trk.from = c->from;
			c->from = val;
		}

		if (c->trk.to == (uint)-1)
			c->trk.to = val;

		return &c->trk;

	case FFCUE_FIN:
		c->trk.from = c->from;
		c->trk.to = 0;
		return &c->trk;
	}

	return NULL;
}
