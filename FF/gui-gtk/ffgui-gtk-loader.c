/**
Copyright (c) 2019 Simon Zolin
*/

#include <FF/gui-gtk/gtk.h>
#include <FF/data/conf.h>
#include <FF/path.h>


int ffui_ldr_loadfile(ffui_loader *g, const char *fn)
{
	int r;
	ffstr s;
	char *buf = NULL;
	size_t n;
	fffd f = FF_BADFD;
	ffconf p;
	ffparser_schem ps;

	r = ffconf_scheminit(&ps, &p, &g->ctx);
	if (r != 0)
		goto done;

	if (FF_BADFD == (f = fffile_open(fn, O_RDONLY))) {
		r = FFPARS_ESYS;
		goto done;
	}

	if (NULL == (buf = ffmem_alloc(4096))) {
		r = FFPARS_ESYS;
		goto done;
	}

	ffpath_split2(fn, ffsz_len(fn), &g->path, NULL);
	g->path.len += FFSLEN("/");

	for (;;) {
		n = fffile_read(f, buf, 4096);
		if (n == (size_t)-1) {
			r = FFPARS_ESYS;
			goto done;
		} else if (n == 0)
			break;
		ffstr_set(&s, buf, n);

		while (s.len != 0) {
			n = s.len;
			r = ffconf_parse(&p, s.ptr, &n);
			ffstr_shift(&s, n);
			r = ffconf_schemrun(&ps);

			if (ffpars_iserr(r))
				goto done;
		}
	}

	r = ffconf_schemfin(&ps);

done:
	if (ffpars_iserr(r)) {
		ffstr3 s = {0};
		n = ffstr_catfmt(&s, "%u:%u near \"%S\" : %s%Z"
			, p.line, p.ch, &p.val, ffpars_errstr(r));
		if (r == FFPARS_ESYS) {
			if (s.len != 0)
				s.len--;
			ffstr_catfmt(&s, " : %E%Z", fferr_last());
		}
		g->errstr = (s.len != 0) ? s.ptr : "";
	}

	ffconf_parseclose(&p);
	ffpars_schemfree(&ps);
	ffmem_safefree(buf);
	if (f != FF_BADFD)
		fffile_close(f);
	return r;
}

void* ffui_ldr_findctl(const ffui_ldr_ctl *ctx, void *ctl, const ffstr *name)
{
	ffstr s = *name, sctl;

	while (0 != ffstr_nextval3(&s, &sctl, '.' | FFS_NV_KEEPWHITE)) {
		for (uint i = 0; ; i++) {
			if (ctx[i].name == NULL)
				return NULL;

			if (ffstr_eqz(&sctl, ctx[i].name)) {
				uint off = ctx[i].flags;
				ctl = (char*)ctl + off;
				if (s.len == 0)
					return ctl;

				if (ctx[i].children == NULL)
					return NULL;
				ctx = ctx[i].children;
				break;
			}
		}
	}
	return NULL;
}

static void* ldr_getctl(ffui_loader *g, const ffstr *name)
{
	char buf[255], *end = buf + sizeof(buf);
	ffstr s;
	s.ptr = buf;
	s.len = ffs_fmt(buf, end, "%s.%S", g->wndname, name);
	return g->getctl(g->udata, &s);
}


// ICON
// MENU ITEM
// MENU
// LABEL
// VIEW COL
// VIEW
// WINDOW

// ICON
static int ico_done(ffparser_schem *ps, void *obj)
{
	return 0;
}

static const ffpars_arg icon_args[] = {
	{ "filename",	FFPARS_TCHARPTR | FFPARS_FCOPY | FFPARS_FSTRZ, FFPARS_DSTOFF(_ffui_ldr_icon, fn) },
	{ NULL,	FFPARS_TCLOSE, FFPARS_DST(&ico_done) },
};


// MENU ITEM
static int mi_submenu(ffparser_schem *ps, void *obj, const ffstr *val)
{
	ffui_loader *g = obj;
	ffui_menu *sub;

	if (NULL == (sub = g->getctl(g->udata, val)))
		return FFPARS_EBADVAL;

	ffui_menu_setsubmenu(g->mi, sub, g->wnd);
	return 0;
}

static int mi_action(ffparser_schem *ps, void *obj, const ffstr *val)
{
	ffui_loader *g = obj;
	int id;

	if (0 == (id = g->getcmd(g->udata, val)))
		return FFPARS_EBADVAL;

	ffui_menu_setcmd(g->mi, id);
	return 0;
}

static int mi_done(ffparser_schem *ps, void *obj)
{
	ffui_loader *g = obj;
	ffui_menu_ins(g->menu, g->mi, -1);
	return 0;
}

static const ffpars_arg mi_args[] = {
	{ "submenu",	FFPARS_TSTR, FFPARS_DST(&mi_submenu) },
	{ "action",	FFPARS_TSTR, FFPARS_DST(&mi_action) },
	{ NULL,	FFPARS_TCLOSE, FFPARS_DST(&mi_done) },
};

static int mi_new(ffparser_schem *ps, void *obj, ffpars_ctx *ctx)
{
	ffui_loader *g = obj;
	const ffstr *name = &ps->vals[0];

	if (ffstr_eqcz(name, "-"))
		g->mi = ffui_menu_newsep();
	else {
		char *sz = ffsz_alcopystr(name);
		g->mi = ffui_menu_new(sz);
		ffmem_free(sz);
	}

	ffpars_setargs(ctx, g, mi_args, FFCNT(mi_args));
	return 0;
}


// MENU
static const ffpars_arg menu_args[] = {
	{ "item",	FFPARS_TOBJ | FFPARS_FOBJ1 | FFPARS_FMULTI, FFPARS_DST(&mi_new) },
};

static int menu_new(ffparser_schem *ps, void *obj, ffpars_ctx *ctx)
{
	ffui_loader *g = obj;

	if (NULL == (g->menu = g->getctl(g->udata, &ps->vals[0])))
		return FFPARS_EBADVAL;

	if (0 != ffui_menu_create(g->menu))
		return FFPARS_ESYS;

	ffpars_setargs(ctx, g, menu_args, FFCNT(menu_args));
	return 0;
}

static int mmenu_new(ffparser_schem *ps, void *obj, ffpars_ctx *ctx)
{
	ffui_loader *g = obj;
	if (NULL == (g->menu = ldr_getctl(g, &ps->vals[0])))
		return FFPARS_EBADVAL;

	if (0 != ffui_menu_createmain(g->menu))
		return FFPARS_ESYS;

	ffui_wnd_setmenu(g->wnd, g->menu);
	ffpars_setargs(ctx, g, menu_args, FFCNT(menu_args));
	return 0;
}


// LABEL
static int lbl_text(ffparser_schem *ps, void *obj, const ffstr *val)
{
	ffui_loader *g = obj;
	ffui_lbl_settextstr((ffui_label*)g->ctl, val);
	return 0;
}

static const ffpars_arg lbl_args[] = {
	{ "text",	FFPARS_TSTR, FFPARS_DST(&lbl_text) },
};

static int lbl_new(ffparser_schem *ps, void *obj, ffpars_ctx *ctx)
{
	ffui_loader *g = obj;

	g->ctl = ldr_getctl(g, &ps->vals[0]);
	if (g->ctl == NULL)
		return FFPARS_EBADVAL;

	if (0 != ffui_lbl_create(g->lbl, g->wnd))
		return FFPARS_ESYS;

	ffpars_setargs(ctx, g, lbl_args, FFCNT(lbl_args));
	return 0;
}


// TRACKBAR
static int trkbar_range(ffparser_schem *ps, void *obj, const int64 *val)
{
	ffui_loader *g = obj;
	ffui_trk_setrange(g->trkbar, *val);
	return 0;
}

static int trkbar_onscroll(ffparser_schem *ps, void *obj, const ffstr *val)
{
	ffui_loader *g = obj;
	if (0 == (g->trkbar->scroll_id = g->getcmd(g->udata, val)))
		return FFPARS_EBADVAL;
	return 0;
}

static const ffpars_arg trkbar_args[] = {
	{ "range",	FFPARS_TINT, FFPARS_DST(&trkbar_range) },
	{ "onscroll",	FFPARS_TSTR, FFPARS_DST(&trkbar_onscroll) },
};

static int trkbar_new(ffparser_schem *ps, void *obj, ffpars_ctx *ctx)
{
	ffui_loader *g = obj;
	if (NULL == (g->trkbar = ldr_getctl(g, &ps->vals[0])))
		return FFPARS_EBADVAL;

	if (0 != ffui_trk_create(g->trkbar, g->wnd))
		return FFPARS_ESYS;
	ffpars_setargs(ctx, g, trkbar_args, FFCNT(trkbar_args));
	return 0;
}


// VIEW COL
static int viewcol_width(ffparser_schem *ps, void *obj, const int64 *val)
{
	ffui_loader *g = obj;
	ffui_viewcol_setwidth(&g->vicol, *val);
	return 0;
}

static int viewcol_done(ffparser_schem *ps, void *obj)
{
	ffui_loader *g = obj;
	ffui_view_inscol(g->vi, -1, &g->vicol);
	return 0;
}

static const ffpars_arg viewcol_args[] = {
	{ "width",	FFPARS_TINT, FFPARS_DST(&viewcol_width) },
	{ NULL,	FFPARS_TCLOSE, FFPARS_DST(&viewcol_done) },
};

static int viewcol_new(ffparser_schem *ps, void *obj, ffpars_ctx *ctx)
{
	ffstr *name = &ps->vals[0];
	ffui_loader *g = obj;
	ffui_viewcol_reset(&g->vicol);
	ffui_viewcol_setwidth(&g->vicol, 100);
	ffui_viewcol_settext(&g->vicol, name->ptr, name->len);
	ffpars_setargs(ctx, g, viewcol_args, FFCNT(viewcol_args));
	return 0;
}

// VIEW

enum {
	VIEW_STYLE_GRID_LINES,
	VIEW_STYLE_MULTI_SELECT,
};
static const char *const view_styles[] = {
	"grid_lines",
	"multi_select",
};
static int view_style(ffparser_schem *ps, void *obj, const ffstr *val)
{
	ffui_loader *g = obj;

	if (((ffconf*)ffpars_schem_backend(ps))->type == FFCONF_TVAL) {
		// reset to default
		ffui_view_style(g->vi, (uint)-1, 0);
	}

	int n = ffszarr_ifindsorted(view_styles, FFCNT(view_styles), val->ptr, val->len);
	switch (n) {

	case VIEW_STYLE_GRID_LINES:
		ffui_view_style(g->vi, FFUI_VIEW_GRIDLINES, FFUI_VIEW_GRIDLINES);
		break;

	case VIEW_STYLE_MULTI_SELECT:
		ffui_view_style(g->vi, FFUI_VIEW_MULTI_SELECT, FFUI_VIEW_MULTI_SELECT);
		break;

	default:
		return FFPARS_EBADVAL;
	}
	return 0;
}

static int view_dblclick(ffparser_schem *ps, void *obj, const ffstr *val)
{
	ffui_loader *g = obj;
	if (0 == (g->vi->dblclick_id = g->getcmd(g->udata, val)))
		return FFPARS_EBADVAL;
	return 0;
}

static const ffpars_arg view_args[] = {
	{ "style",	FFPARS_TSTR | FFPARS_FLIST, FFPARS_DST(&view_style) },
	{ "dblclick",	FFPARS_TSTR, FFPARS_DST(&view_dblclick) },
	{ "column",	FFPARS_TOBJ | FFPARS_FOBJ1 | FFPARS_FMULTI, FFPARS_DST(&viewcol_new) },
};

static int view_new(ffparser_schem *ps, void *obj, ffpars_ctx *ctx)
{
	ffui_loader *g = obj;

	g->ctl = ldr_getctl(g, &ps->vals[0]);
	if (g->ctl == NULL)
		return FFPARS_EBADVAL;

	if (0 != ffui_view_create(g->vi, g->wnd))
		return FFPARS_ESYS;

	ffpars_setargs(ctx, g, view_args, FFCNT(view_args));
	return 0;
}

// WINDOW
static int wnd_title(ffparser_schem *ps, void *obj, const ffstr *val)
{
	ffui_loader *g = obj;
	ffui_wnd_settextstr(g->wnd, val);
	return 0;
}

static int wnd_position(ffparser_schem *ps, void *obj, const int64 *v)
{
	ffui_loader *g = obj;
	int *i = &g->r.x;
	if (ps->list_idx == 4)
		return FFPARS_EBIGVAL;
	i[ps->list_idx] = (int)*v;
	if (ps->list_idx == 3) {
		ffui_wnd_setplacement(g->wnd, 0, &g->r);
	}
	return 0;
}

static int wnd_icon(ffparser_schem *ps, void *obj, ffpars_ctx *ctx)
{
	ffui_loader *g = obj;
	ffmem_zero(&g->ico, sizeof(_ffui_ldr_icon));
	ffpars_setargs(ctx, &g->ico, icon_args, FFCNT(icon_args));
	return 0;
}

static int wnd_done(ffparser_schem *ps, void *obj)
{
	ffui_loader *g = obj;
	if (g->ico.fn != NULL) {
		ffui_icon ico;
		char *sz = ffsz_alfmt("%S/%s", &g->path, g->ico.fn);
		ffui_icon_load(&ico, sz);
		ffmem_free(sz);
		ffui_wnd_seticon(g->wnd, &ico);
	}
	return 0;
}

static const ffpars_arg wnd_args[] = {
	{ "title",	FFPARS_TSTR, FFPARS_DST(&wnd_title) },
	{ "position",	FFPARS_TINT | FFPARS_FSIGN | FFPARS_FLIST, FFPARS_DST(&wnd_position) },
	{ "icon",	FFPARS_TOBJ, FFPARS_DST(&wnd_icon) },

	{ "mainmenu",	FFPARS_TOBJ | FFPARS_FOBJ1, FFPARS_DST(&mmenu_new) },
	{ "label",	FFPARS_TOBJ | FFPARS_FOBJ1 | FFPARS_FMULTI, FFPARS_DST(&lbl_new) },
	{ "trackbar",	FFPARS_TOBJ | FFPARS_FOBJ1 | FFPARS_FMULTI, FFPARS_DST(&trkbar_new) },
	{ "listview",	FFPARS_TOBJ | FFPARS_FOBJ1 | FFPARS_FMULTI, FFPARS_DST(&view_new) },
	{ NULL,	FFPARS_TCLOSE, FFPARS_DST(&wnd_done) },
};

static int wnd_new(ffparser_schem *ps, void *obj, ffpars_ctx *ctx)
{
	ffui_loader *g = obj;
	ffui_wnd *wnd;

	if (NULL == (wnd = g->getctl(g->udata, &ps->vals[0])))
		return FFPARS_EBADVAL;
	ffmem_zero((byte*)g + FFOFF(ffui_loader, wnd), sizeof(ffui_loader) - FFOFF(ffui_loader, wnd));
	g->wnd = wnd;
	if (NULL == (g->wndname = ffsz_alcopy(ps->vals[0].ptr, ps->vals[0].len)))
		return FFPARS_ESYS;
	g->ctl = (ffui_ctl*)wnd;
	if (0 != ffui_wnd_create(wnd))
		return FFPARS_ESYS;

	ffpars_setargs(ctx, g, wnd_args, FFCNT(wnd_args));
	return 0;
}

static const ffpars_arg top_args[] = {
	{ "window",	FFPARS_TOBJ | FFPARS_FOBJ1 | FFPARS_FMULTI, FFPARS_DST(&wnd_new) },
	{ "menu",	FFPARS_TOBJ | FFPARS_FOBJ1 | FFPARS_FMULTI, FFPARS_DST(&menu_new) },
};

void ffui_ldr_init2(ffui_loader *g, ffui_ldr_getctl_t getctl, ffui_ldr_getcmd_t getcmd, void *udata)
{
	ffmem_tzero(g);
	g->getctl = getctl;
	g->getcmd = getcmd;
	g->udata = udata;
	ffpars_setargs(&g->ctx, g, top_args, FFCNT(top_args));
}
