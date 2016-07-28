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

enum {
	CUE_LINE, CUE_KEY, CUE_VAL,
	CUE_GLOB, CUE_GLOB_NXLINE, CUE_REM_VAL,
	CUE_FILE, CUE_FILE_NXLINE, CUE_FILETYPE,
	CUE_FILE_TRACK, CUE_FILE_TRACK_NXLINE,
};

void ffcue_init(ffparser *p)
{
	ffpars_init(p);
	p->line = 0;
	p->state = CUE_LINE,  p->nextst = CUE_GLOB;
}

int ffcue_parse(ffparser *p, const char *data, size_t *len)
{
	ssize_t r;
	int cmd = 0;
	ffstr s = p->tmp;
	const char *d = data, *end = data + *len, *pos;

	for (;;) {
	switch (p->state) {

	case CUE_LINE:
		pos = ffs_find(d, end - d, '\n');
		r = ffarr_append_until(&p->buf, d, end - d, p->buf.len + pos - d + 1);
		if (r == 0)
			return FFPARS_MORE;
		else if (r < 0)
			return -FFPARS_ESYS;

		d += r;
		ffstr_set2(&s, &p->buf);
		p->buf.len = 0;
		pos = ffs_rskipof(s.ptr, s.len, "\r\n", 2);
		s.len = pos - s.ptr;
		p->line++;
		p->state = CUE_KEY;
		continue;

	case CUE_KEY:
		pos = ffs_skipof(s.ptr, s.len, " \t", 2);
		ffstr_shift(&s, pos - s.ptr);
		ffstr_nextval3(&s, &p->val, ' ');
		cmd = ffszarr_ifindsorted(_ffcue_dict, FFCNT(_ffcue_dict), p->val.ptr, p->val.len);
		p->state = p->nextst;
		continue;

	case CUE_VAL:
		ffstr_nextval3(&s, &p->val, ' ' | FFSTR_NV_DBLQUOT);
		p->state = p->nextst;
		r = p->ret;
		goto done;


	case CUE_GLOB:
		switch (cmd) {
		case CMD_REM:
			p->state = CUE_VAL,  p->nextst = CUE_REM_VAL;
			p->ret = FFCUE_REM_NAME;
			continue;

		case CMD_PERFORMER:
		case CMD_TITLE:
			p->ret = (cmd == CMD_PERFORMER) ? FFCUE_PERFORMER : FFCUE_TITLE;
			p->state = CUE_VAL,  p->nextst = CUE_GLOB_NXLINE;
			continue;

		case CMD_FILE:
			p->state = CUE_VAL,  p->nextst = CUE_FILETYPE;
			p->ret = FFCUE_FILE;
			continue;
		}
		// break

	case CUE_GLOB_NXLINE:
		p->state = CUE_LINE,  p->nextst = CUE_GLOB;
		continue;

	case CUE_REM_VAL:
		p->state = CUE_VAL,  p->nextst = CUE_GLOB_NXLINE;
		p->ret = FFCUE_REM_VAL;
		continue;


	case CUE_FILETYPE:
		p->state = CUE_VAL,  p->nextst = CUE_FILE_NXLINE;
		p->ret = FFCUE_FILETYPE;
		continue;

	case CUE_FILE:
		switch (cmd) {
		case CMD_TRACK:
			ffstr_nextval3(&s, &p->val, ' ');
			if (p->val.len != 2 || 2 != ffs_toint(p->val.ptr, p->val.len, &p->intval, FFS_INT64))
				return -FFPARS_EBADVAL;
			p->state = CUE_LINE,  p->nextst = CUE_FILE_TRACK;
			r = FFCUE_TRACKNO;
			goto done;
		}
		// break

	case CUE_FILE_NXLINE:
		p->state = CUE_LINE,  p->nextst = CUE_FILE;
		continue;


	case CUE_FILE_TRACK:
		switch (cmd) {
		case CMD_PERFORMER:
		case CMD_TITLE:
			p->ret = (cmd == CMD_PERFORMER) ? FFCUE_TRK_PERFORMER : FFCUE_TRK_TITLE;
			p->state = CUE_VAL,  p->nextst = CUE_FILE_TRACK_NXLINE;
			continue;

		case CMD_INDEX: {
			uint idx, m, sec, f;
			if (s.len != ffs_fmatch(s.ptr, s.len, "%2u %2u:%2u:%2u", &idx, &m, &sec, &f))
				return -FFPARS_EBADVAL;
			p->intval = (int64)(m * 60 + sec) * 75 + f;
			p->state = CUE_FILE_TRACK_NXLINE;
			r = (idx == 0) ? FFCUE_TRK_INDEX00 : FFCUE_TRK_INDEX;
			goto done;
		}

		case CMD_TRACK:
			p->state = CUE_FILE;
			continue;

		case CMD_FILE:
			p->state = CUE_GLOB;
			continue;
		}
		// break

	case CUE_FILE_TRACK_NXLINE:
		p->state = CUE_LINE,  p->nextst = CUE_FILE_TRACK;
		continue;

	}
	}

done:
	*len = d - data;
	p->tmp = s;
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
