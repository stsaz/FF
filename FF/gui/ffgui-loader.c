/**
Copyright (c) 2014 Simon Zolin
*/

#include <FF/gui/loader.h>
#include <FF/data/conf.h>
#include <FF/path.h>
#include <FF/pic/pic.h>
#include <FFOS/error.h>
#include <FFOS/file.h>


static void* ldr_getctl(ffui_loader *g, const ffstr *name);
static uint ffui_hotkeyparse(const char *s, size_t len);

// ICON
static int ico_size(ffparser_schem *ps, void *obj, const ffstr *val);
static int ico_done(ffparser_schem *ps, void *obj);
static const ffpars_arg icon_args[] = {
	{ "filename",	FFPARS_TSTR | FFPARS_FCOPY, FFPARS_DSTOFF(_ffui_ldr_icon_t, fn) },
	{ "index",	FFPARS_TINT, FFPARS_DSTOFF(_ffui_ldr_icon_t, idx) },
	{ "size",	FFPARS_TSTR, FFPARS_DST(&ico_size) },
	{ NULL,	FFPARS_TCLOSE, FFPARS_DST(&ico_done) },
};

// MENU ITEM
static int mi_submenu(ffparser_schem *ps, void *obj, const ffstr *val);
static int mi_style(ffparser_schem *ps, void *obj, const ffstr *val);
static int mi_action(ffparser_schem *ps, void *obj, const ffstr *val);
static int mi_hotkey(ffparser_schem *ps, void *obj, const ffstr *val);
static int mi_done(ffparser_schem *ps, void *obj);
static const ffpars_arg menuitem_args[] = {
	{ "submenu",	FFPARS_TSTR, FFPARS_DST(&mi_submenu) },
	{ "style",	FFPARS_TSTR | FFPARS_FLIST, FFPARS_DST(&mi_style) },
	{ "action",	FFPARS_TSTR, FFPARS_DST(&mi_action) },
	{ "hotkey",	FFPARS_TSTR, FFPARS_DST(&mi_hotkey) },
	{ NULL,	FFPARS_TCLOSE, FFPARS_DST(&mi_done) },
};

// MENU
static int new_menuitem(ffparser_schem *ps, void *obj, ffpars_ctx *ctx);
static const ffpars_arg menu_args[] = {
	{ "item",	FFPARS_TOBJ | FFPARS_FOBJ1 | FFPARS_FMULTI, FFPARS_DST(&new_menuitem) },
};
static int new_menu(ffparser_schem *ps, void *obj, ffpars_ctx *ctx);
static int new_mmenu(ffparser_schem *ps, void *obj, ffpars_ctx *ctx);

// STATUS BAR
static int stbar_style(ffparser_schem *ps, void *obj, const ffstr *val);
static int stbar_parts(ffparser_schem *ps, void *obj, const int64 *val);
static int stbar_done(ffparser_schem *ps, void *obj);
static const ffpars_arg stbar_args[] = {
	{ "style",	FFPARS_TSTR | FFPARS_FLIST, FFPARS_DST(&stbar_style) },
	{ "parts",	FFPARS_TINT | FFPARS_FSIGN | FFPARS_FLIST, FFPARS_DST(&stbar_parts) },
	{ NULL,	FFPARS_TCLOSE, FFPARS_DST(&stbar_done) },
};
static int new_stbar(ffparser_schem *ps, void *obj, ffpars_ctx *ctx);

// TRAY
static int tray_style(ffparser_schem *ps, void *obj, const ffstr *val);
static int tray_pmenu(ffparser_schem *ps, void *obj, const ffstr *val);
static int tray_icon(ffparser_schem *ps, void *obj, ffpars_ctx *ctx);
static int tray_lclick(ffparser_schem *ps, void *obj, const ffstr *val);
static int tray_done(ffparser_schem *ps, void *obj);
static const ffpars_arg tray_args[] = {
	{ "style",	FFPARS_TSTR | FFPARS_FLIST, FFPARS_DST(&tray_style) },
	{ "icon",	FFPARS_TOBJ, FFPARS_DST(&tray_icon) },
	{ "popupmenu",	FFPARS_TSTR, FFPARS_DST(&tray_pmenu) },
	{ "lclick",	FFPARS_TSTR, FFPARS_DST(&tray_lclick) },
	{ NULL,	FFPARS_TCLOSE, FFPARS_DST(&tray_done) },
};
static int new_tray(ffparser_schem *ps, void *obj, ffpars_ctx *ctx);

// FONT
static int font_name(ffparser_schem *ps, void *obj, const ffstr *val);
static int font_height(ffparser_schem *ps, void *obj, const int64 *val);
static int font_style(ffparser_schem *ps, void *obj, const ffstr *val);
static int font_done(ffparser_schem *ps, void *obj);
static const ffpars_arg font_args[] = {
	{ "name",	FFPARS_TSTR, FFPARS_DST(&font_name) },
	{ "height",	FFPARS_TINT, FFPARS_DST(&font_height) },
	{ "style",	FFPARS_TSTR | FFPARS_FLIST, FFPARS_DST(&font_style) },
	{ NULL,	FFPARS_TCLOSE, FFPARS_DST(&font_done) },
};

// LABEL
static int label_text(ffparser_schem *ps, void *obj, const ffstr *val);
static int label_font(ffparser_schem *ps, void *obj, ffpars_ctx *ctx);
static int label_style(ffparser_schem *ps, void *obj, const ffstr *val);
static int label_pos(ffparser_schem *ps, void *obj, const int64 *val);
static int label_color(ffparser_schem *ps, void *obj, const ffstr *val);
static int label_action(ffparser_schem *ps, void *obj, const ffstr *val);
static const ffpars_arg label_args[] = {
	{ "text",	FFPARS_TSTR, FFPARS_DST(&label_text) },
	{ "style",	FFPARS_TSTR | FFPARS_FLIST, FFPARS_DST(&label_style) },
	{ "font",	FFPARS_TOBJ, FFPARS_DST(&label_font) },
	{ "color",	FFPARS_TSTR, FFPARS_DST(&label_color) },
	{ "position",	FFPARS_TINT | FFPARS_FSIGN | FFPARS_FLIST, FFPARS_DST(&label_pos) },
	{ "onclick",	FFPARS_TSTR, FFPARS_DST(&label_action) },
};
static int new_label(ffparser_schem *ps, void *obj, ffpars_ctx *ctx);

// BUTTON
static int btn_action(ffparser_schem *ps, void *obj, const ffstr *val);
static int ctl_tooltip(ffparser_schem *ps, void *obj, const ffstr *val);
static const ffpars_arg btn_args[] = {
	{ "text",	FFPARS_TSTR, FFPARS_DST(&label_text) },
	{ "style",	FFPARS_TSTR | FFPARS_FLIST, FFPARS_DST(&label_style) },
	{ "font",	FFPARS_TOBJ, FFPARS_DST(&label_font) },
	{ "position",	FFPARS_TINT | FFPARS_FSIGN | FFPARS_FLIST, FFPARS_DST(&label_pos) },
	{ "tooltip",	FFPARS_TSTR, FFPARS_DST(&ctl_tooltip) },
	{ "action",	FFPARS_TSTR, FFPARS_DST(&btn_action) },
};
static int new_button(ffparser_schem *ps, void *obj, ffpars_ctx *ctx);

// EDITBOX
static int edit_style(ffparser_schem *ps, void *obj, const ffstr *val);
static int edit_action(ffparser_schem *ps, void *obj, const ffstr *val);
static const ffpars_arg editbox_args[] = {
	{ "text",	FFPARS_TSTR, FFPARS_DST(&label_text) },
	{ "style",	FFPARS_TSTR | FFPARS_FLIST, FFPARS_DST(&edit_style) },
	{ "font",	FFPARS_TOBJ, FFPARS_DST(&label_font) },
	{ "position",	FFPARS_TINT | FFPARS_FSIGN | FFPARS_FLIST, FFPARS_DST(&label_pos) },
	{ "onchange",	FFPARS_TSTR, FFPARS_DST(&edit_action) },
};
static int new_editbox(ffparser_schem *ps, void *obj, ffpars_ctx *ctx);

// COMBOBOX
static int combx_style(ffparser_schem *ps, void *obj, const ffstr *val);
static const ffpars_arg combx_args[] = {
	{ "style",	FFPARS_TSTR | FFPARS_FLIST, FFPARS_DST(&combx_style) },
	{ "font",	FFPARS_TOBJ, FFPARS_DST(&label_font) },
	{ "position",	FFPARS_TINT | FFPARS_FSIGN | FFPARS_FLIST, FFPARS_DST(&label_pos) },
};
static int new_combobox(ffparser_schem *ps, void *obj, ffpars_ctx *ctx);

// TRACKBAR
static int trkbar_style(ffparser_schem *ps, void *obj, const ffstr *val);
static int trkbar_pagesize(ffparser_schem *ps, void *obj, const int64 *val);
static int trkbar_range(ffparser_schem *ps, void *obj, const int64 *val);
static int trkbar_val(ffparser_schem *ps, void *obj, const int64 *val);
static int trkbar_onscroll(ffparser_schem *ps, void *obj, const ffstr *val);
static int trkbar_onscrolling(ffparser_schem *ps, void *obj, const ffstr *val);
static const ffpars_arg trkbar_args[] = {
	{ "style",	FFPARS_TSTR | FFPARS_FLIST, FFPARS_DST(&trkbar_style) },
	{ "position",	FFPARS_TINT | FFPARS_FSIGN | FFPARS_FLIST, FFPARS_DST(&label_pos) },
	{ "range",	FFPARS_TINT, FFPARS_DST(&trkbar_range) },
	{ "value",	FFPARS_TINT, FFPARS_DST(&trkbar_val) },
	{ "page_size",	FFPARS_TINT, FFPARS_DST(&trkbar_pagesize) },
	{ "onscroll",	FFPARS_TSTR, FFPARS_DST(&trkbar_onscroll) },
	{ "onscrolling",	FFPARS_TSTR, FFPARS_DST(&trkbar_onscrolling) },
};
static int new_trkbar(ffparser_schem *ps, void *obj, ffpars_ctx *ctx);

// PROGRESS BAR
static int pgsbar_style(ffparser_schem *ps, void *obj, const ffstr *val);
static const ffpars_arg pgsbar_args[] = {
	{ "style",	FFPARS_TSTR | FFPARS_FLIST, FFPARS_DST(&pgsbar_style) },
	{ "position",	FFPARS_TINT | FFPARS_FSIGN | FFPARS_FLIST, FFPARS_DST(&label_pos) },
};
static int new_pgsbar(ffparser_schem *ps, void *obj, ffpars_ctx *ctx);

// TAB
static int tab_onchange(ffparser_schem *ps, void *obj, const ffstr *val);
static const ffpars_arg tab_args[] = {
	{ "style",	FFPARS_TSTR | FFPARS_FLIST, FFPARS_DST(&label_style) },
	{ "position",	FFPARS_TINT | FFPARS_FSIGN | FFPARS_FLIST, FFPARS_DST(&label_pos) },
	{ "font",	FFPARS_TOBJ, FFPARS_DST(&label_font) },
	{ "onchange",	FFPARS_TSTR, FFPARS_DST(&tab_onchange) },
};
static int new_tab(ffparser_schem *ps, void *obj, ffpars_ctx *ctx);

// VIEW
static int viewcol_width(ffparser_schem *ps, void *obj, const int64 *val);
static int viewcol_align(ffparser_schem *ps, void *obj, const ffstr *val);
static int viewcol_order(ffparser_schem *ps, void *obj, const int64 *val);
static int viewcol_done(ffparser_schem *ps, void *obj);
static const ffpars_arg viewcol_args[] = {
	{ "width",	FFPARS_TINT, FFPARS_DST(&viewcol_width) },
	{ "align",	FFPARS_TSTR, FFPARS_DST(&viewcol_align) },
	{ "order",	FFPARS_TINT, FFPARS_DST(&viewcol_order) },
	{ NULL,	FFPARS_TCLOSE, FFPARS_DST(&viewcol_done) },
};
static int view_column(ffparser_schem *ps, void *obj, ffpars_ctx *ctx);

static int view_style(ffparser_schem *ps, void *obj, const ffstr *val);
static int view_color(ffparser_schem *ps, void *obj, const ffstr *val);
static int view_pmenu(ffparser_schem *ps, void *obj, const ffstr *val);
static int view_chsel(ffparser_schem *ps, void *obj, const ffstr *val);
static int view_lclick(ffparser_schem *ps, void *obj, const ffstr *val);
static int view_dblclick(ffparser_schem *ps, void *obj, const ffstr *val);
static const ffpars_arg view_args[] = {
	{ "style",	FFPARS_TSTR | FFPARS_FLIST, FFPARS_DST(&view_style) },
	{ "position",	FFPARS_TINT | FFPARS_FSIGN | FFPARS_FLIST, FFPARS_DST(&label_pos) },
	{ "color",	FFPARS_TSTR, FFPARS_DST(&view_color) },
	{ "bgcolor",	FFPARS_TSTR, FFPARS_DST(&view_color) },
	{ "popupmenu",	FFPARS_TSTR, FFPARS_DST(&view_pmenu) },

	{ "chsel",	FFPARS_TSTR, FFPARS_DST(&view_chsel) },
	{ "lclick",	FFPARS_TSTR, FFPARS_DST(&view_lclick) },
	{ "dblclick",	FFPARS_TSTR, FFPARS_DST(&view_dblclick) },

	{ "column",	FFPARS_TOBJ | FFPARS_FOBJ1 | FFPARS_FMULTI, FFPARS_DST(&view_column) },
};
static int new_listview(ffparser_schem *ps, void *obj, ffpars_ctx *ctx);
static int new_treeview(ffparser_schem *ps, void *obj, ffpars_ctx *ctx);

// PANED
static int pnchild_move(ffparser_schem *ps, void *obj, const ffstr *val);
static int pnchild_resize(ffparser_schem *ps, void *obj, const ffstr *val);
static const ffpars_arg paned_child_args[] = {
	{ "move",	FFPARS_TSTR | FFPARS_FLIST, FFPARS_DST(&pnchild_move) },
	{ "resize",	FFPARS_TSTR | FFPARS_FLIST, FFPARS_DST(&pnchild_resize) },
};
static int paned_child(ffparser_schem *ps, void *obj, ffpars_ctx *ctx);
static const ffpars_arg paned_args[] = {
	{ "child",	FFPARS_TOBJ | FFPARS_FOBJ1 | FFPARS_FMULTI, FFPARS_DST(&paned_child) },
};
static int new_paned(ffparser_schem *ps, void *obj, ffpars_ctx *ctx);

// DIALOG
static int dlg_title(ffparser_schem *ps, void *obj, const ffstr *val);
static int dlg_filter(ffparser_schem *ps, void *obj, const ffstr *val);
static const ffpars_arg dlg_args[] = {
	{ "title",	FFPARS_TSTR, FFPARS_DST(&dlg_title) },
	{ "filter",	FFPARS_TSTR, FFPARS_DST(&dlg_filter) },
};
static int new_dlg(ffparser_schem *ps, void *obj, ffpars_ctx *ctx);

// WINDOW
static int wnd_title(ffparser_schem *ps, void *obj, const ffstr *val);
static int wnd_position(ffparser_schem *ps, void *obj, const int64 *v);
static int wnd_placement(ffparser_schem *ps, void *obj, const int64 *v);
static int wnd_icon(ffparser_schem *ps, void *obj, ffpars_ctx *ctx);
static int wnd_opacity(ffparser_schem *ps, void *obj, const int64 *val);
static int wnd_borderstick(ffparser_schem *ps, void *obj, const int64 *val);
static int wnd_style(ffparser_schem *ps, void *obj, const ffstr *val);
static int wnd_onclose(ffparser_schem *ps, void *obj, const ffstr *val);
static int wnd_parent(ffparser_schem *ps, void *obj, const ffstr *val);
static int wnd_done(ffparser_schem *ps, void *obj);
static const ffpars_arg wnd_args[] = {
	{ "title",	FFPARS_TSTR, FFPARS_DST(&wnd_title) },
	{ "position",	FFPARS_TINT | FFPARS_FSIGN | FFPARS_FLIST, FFPARS_DST(&wnd_position) },
	{ "placement",	FFPARS_TINT | FFPARS_FSIGN | FFPARS_FLIST, FFPARS_DST(&wnd_placement) },
	{ "opacity",	FFPARS_TINT, FFPARS_DST(&wnd_opacity) },
	{ "borderstick",	FFPARS_TINT | FFPARS_F8BIT, FFPARS_DST(&wnd_borderstick) },
	{ "icon",	FFPARS_TOBJ, FFPARS_DST(&wnd_icon) },
	{ "style",	FFPARS_TSTR | FFPARS_FLIST, FFPARS_DST(&wnd_style) },
	{ "parent",	FFPARS_TSTR, FFPARS_DST(&wnd_parent) },
	{ "font",	FFPARS_TOBJ, FFPARS_DST(&label_font) },
	{ "onclose",	FFPARS_TSTR, FFPARS_DST(&wnd_onclose) },

	{ "mainmenu",	FFPARS_TOBJ | FFPARS_FOBJ1, FFPARS_DST(&new_mmenu) },
	{ "label",	FFPARS_TOBJ | FFPARS_FOBJ1 | FFPARS_FMULTI, FFPARS_DST(&new_label) },
	{ "editbox",	FFPARS_TOBJ | FFPARS_FOBJ1 | FFPARS_FMULTI, FFPARS_DST(&new_editbox) },
	{ "text",	FFPARS_TOBJ | FFPARS_FOBJ1 | FFPARS_FMULTI, FFPARS_DST(&new_editbox) },
	{ "combobox",	FFPARS_TOBJ | FFPARS_FOBJ1 | FFPARS_FMULTI, FFPARS_DST(&new_combobox) },
	{ "button",	FFPARS_TOBJ | FFPARS_FOBJ1 | FFPARS_FMULTI, FFPARS_DST(&new_button) },
	{ "trackbar",	FFPARS_TOBJ | FFPARS_FOBJ1 | FFPARS_FMULTI, FFPARS_DST(&new_trkbar) },
	{ "progressbar",	FFPARS_TOBJ | FFPARS_FOBJ1 | FFPARS_FMULTI, FFPARS_DST(&new_pgsbar) },
	{ "tab",	FFPARS_TOBJ | FFPARS_FOBJ1 | FFPARS_FMULTI, FFPARS_DST(&new_tab) },
	{ "listview",	FFPARS_TOBJ | FFPARS_FOBJ1 | FFPARS_FMULTI, FFPARS_DST(&new_listview) },
	{ "treeview",	FFPARS_TOBJ | FFPARS_FOBJ1 | FFPARS_FMULTI, FFPARS_DST(&new_treeview) },
	{ "paned",	FFPARS_TOBJ | FFPARS_FOBJ1 | FFPARS_FMULTI, FFPARS_DST(&new_paned) },
	{ "trayicon",	FFPARS_TOBJ | FFPARS_FOBJ1, FFPARS_DST(&new_tray) },
	{ "statusbar",	FFPARS_TOBJ | FFPARS_FOBJ1, FFPARS_DST(&new_stbar) },
	{ NULL,	FFPARS_TCLOSE, FFPARS_DST(&wnd_done) },
};
static int new_wnd(ffparser_schem *ps, void *obj, ffpars_ctx *ctx);
static const ffpars_arg top_args[] = {
	{ "window",	FFPARS_TOBJ | FFPARS_FOBJ1 | FFPARS_FMULTI, FFPARS_DST(&new_wnd) },
	{ "menu",	FFPARS_TOBJ | FFPARS_FOBJ1 | FFPARS_FMULTI, FFPARS_DST(&new_menu) },
	{ "dialog",	FFPARS_TOBJ | FFPARS_FOBJ1 | FFPARS_FMULTI, FFPARS_DST(&new_dlg) },
};


static int ico_size(ffparser_schem *ps, void *obj, const ffstr *val)
{
	_ffui_ldr_icon_t *ico = obj;
	if (ffstr_eqcz(val, "small"))
		ico->small = 1;
	return 0;
}

static int ico_done(ffparser_schem *ps, void *obj)
{
	_ffui_ldr_icon_t *ico = obj;
	HICON *lrg = &ico->h, *sml = NULL;
	ffsyschar *p, fn[FF_MAXPATH];

	if (ico->small) {
		lrg = NULL;
		sml = &ico->h;
	}

	p = ffq_copys(fn, fn + FFCNT(fn), ico->ldr->path.ptr, ico->ldr->path.len);
	ffqz_copys(p, fn + FFCNT(fn) - p, ico->fn.ptr, ico->fn.len);
	ffstr_free(&ico->fn);
	if (!ExtractIconEx(fn, ico->idx, lrg, sml, 1))
		return FFPARS_ESYS;
	return 0;
}


static int mi_submenu(ffparser_schem *ps, void *obj, const ffstr *val)
{
	ffui_loader *g = obj;
	ffui_menu *sub;

	if (NULL == (sub = g->getctl(g->udata, val)))
		return FFPARS_EBADVAL;

	ffui_menu_setsubmenu(&g->menuitem.mi, sub->h);
	return 0;
}

static int mi_style(ffparser_schem *ps, void *obj, const ffstr *val)
{
	ffui_loader *g = obj;

	if (ffstr_eqcz(val, "checked"))
		ffui_menu_addstate(&g->menuitem.mi, FFUI_MENU_CHECKED);

	else if (ffstr_eqcz(val, "default"))
		ffui_menu_addstate(&g->menuitem.mi, FFUI_MENU_DEFAULT);

	else if (ffstr_eqcz(val, "disabled"))
		ffui_menu_addstate(&g->menuitem.mi, FFUI_MENU_DISABLED);

	else if (ffstr_eqcz(val, "radio"))
		ffui_menu_settype(&g->menuitem.mi, FFUI_MENU_RADIOCHECK);

	else
		return FFPARS_EBADVAL;

	return 0;
}

static int mi_action(ffparser_schem *ps, void *obj, const ffstr *val)
{
	ffui_loader *g = obj;
	int id;

	if (0 == (id = g->getcmd(g->udata, val)))
		return FFPARS_EBADVAL;

	ffui_menu_setcmd(&g->menuitem.mi, id);
	return 0;
}

static const byte ikeys[] = {
	VK_OEM_7,
	VK_OEM_2,
	VK_OEM_4,
	VK_OEM_5,
	VK_OEM_6,
	VK_OEM_3,
	VK_PAUSE,
	VK_DELETE,
	VK_DOWN,
	VK_RETURN,
	VK_ESCAPE,
	VK_F1,
	VK_F10,
	VK_F11,
	VK_F12,
	VK_F2,
	VK_F3,
	VK_F4,
	VK_F5,
	VK_F6,
	VK_F7,
	VK_F8,
	VK_F9,
	VK_INSERT,
	VK_LEFT,
	VK_RIGHT,
	VK_SPACE,
	VK_TAB,
	VK_UP,
};

static const char *const skeys[] = {
	"'",
	"/",
	"[",
	"\\",
	"]",
	"`",
	"break",
	"delete",
	"down",
	"enter",
	"escape",
	"f1",
	"f10",
	"f11",
	"f12",
	"f2",
	"f3",
	"f4",
	"f5",
	"f6",
	"f7",
	"f8",
	"f9",
	"insert",
	"left",
	"right",
	"space",
	"tab",
	"up",
};

/** Parse hotkey string.
e.g. "Ctrl+Alt+Shift+Q"
Return: low-word: char key or vkey.  hi-word: control flags
Return 0 on error. */
static uint ffui_hotkeyparse(const char *s, size_t len)
{
	int r = 0, f;
	const char *end = s + len;
	ffstr v;
	enum {
		fctrl = FCONTROL << 16,
		fshift = FSHIFT << 16,
		falt = FALT << 16,
	};

	if (s == end)
		goto fail;

	while (s != end) {
		s += ffstr_nextval(s, end - s, &v, '+');

		if (ffstr_ieqcz(&v, "ctrl"))
			f = fctrl;
		else if (ffstr_ieqcz(&v, "alt"))
			f = falt;
		else if (ffstr_ieqcz(&v, "shift"))
			f = fshift;
		else {
			if (s != end)
				goto fail; //the 2nd key is an error
			break;
		}

		if (r & f)
			goto fail;
		r |= f;
	}

	if (v.len == 1 && (ffchar_isletter(v.ptr[0]) || ffchar_isdigit(v.ptr[0])))
		r |= v.ptr[0];

	else {
		ssize_t ikey = ffszarr_ifindsorted(skeys, FFCNT(skeys), v.ptr, v.len);
		if (ikey == -1)
			goto fail; //unknown key
		r |= ikeys[ikey];
	}

	return r;

fail:
	return 0;
}

static int mi_hotkey(ffparser_schem *ps, void *obj, const ffstr *val)
{
	ffui_loader *g = obj;
	ACCEL *a;
	uint hk;

	if (NULL == ffarr_grow(&g->accels, 1, 3))
		return FFPARS_ESYS;
	a = ffarr_push(&g->accels, ACCEL);

	if (0 == (hk = ffui_hotkeyparse(val->ptr, val->len)))
		return FFPARS_EBADVAL;

	a->fVirt = (byte)(hk >> 16) | FVIRTKEY;
	a->key = (byte)hk;
	a->cmd = 0;

	ffui_menu_sethotkey(&g->menuitem.mi, val->ptr, val->len);
	g->menuitem.iaccel = 1;
	return 0;
}

static int mi_done(ffparser_schem *ps, void *obj)
{
	ffui_loader *g = obj;

	if (g->menuitem.iaccel && g->menuitem.mi.wID != 0)
		ffarr_back(&g->accels).cmd = g->menuitem.mi.wID;

	if (0 != ffui_menu_append(g->menu, &g->menuitem.mi))
		return FFPARS_ESYS;
	return 0;
}

static int new_menuitem(ffparser_schem *ps, void *obj, ffpars_ctx *ctx)
{
	ffui_loader *g = obj;

	ffmem_tzero(&g->menuitem);

	if (ffstr_eqcz(&ps->vals[0], "-"))
		ffui_menu_settype(&g->menuitem.mi, FFUI_MENU_SEPARATOR);
	else
		ffui_menu_settextstr(&g->menuitem.mi, &ps->vals[0]);

	ffpars_setargs(ctx, g, menuitem_args, FFCNT(menuitem_args));
	return 0;
}

static int new_menu(ffparser_schem *ps, void *obj, ffpars_ctx *ctx)
{
	ffui_loader *g = obj;

	if (NULL == (g->menu = g->getctl(g->udata, &ps->vals[0])))
		return FFPARS_EBADVAL;

	if (0 != ffui_menu_create(g->menu))
		return FFPARS_ESYS;
	ffpars_setargs(ctx, g, menu_args, FFCNT(menu_args));
	return 0;
}

static int new_mmenu(ffparser_schem *ps, void *obj, ffpars_ctx *ctx)
{
	ffui_loader *g = obj;
	if (NULL == (g->menu = ldr_getctl(g, &ps->vals[0])))
		return FFPARS_EBADVAL;

	if (NULL == (g->menu->h = CreateMenu()))
		return FFPARS_ESYS;

	if (!SetMenu(g->wnd->h, g->menu->h))
		return FFPARS_ESYS;

	ffpars_setargs(ctx, g, menu_args, FFCNT(menu_args));
	return 0;
}


static int trkbar_style(ffparser_schem *ps, void *obj, const ffstr *val)
{
	ffui_loader *g = obj;
	if (ffstr_eqcz(val, "visible"))
		ffui_show(g->trkbar, 1);
	else if (ffstr_eqcz(val, "no_ticks"))
		ffui_styleset(g->trkbar->h, TBS_NOTICKS);
	else if (ffstr_eqcz(val, "both"))
		ffui_styleset(g->trkbar->h, TBS_BOTH);
	return 0;
}

static int trkbar_range(ffparser_schem *ps, void *obj, const int64 *val)
{
	ffui_loader *g = obj;
	ffui_trk_setrange(g->trkbar, *val);
	return 0;
}

static int trkbar_val(ffparser_schem *ps, void *obj, const int64 *val)
{
	ffui_loader *g = obj;
	ffui_trk_set(g->trkbar, *val);
	return 0;
}

static int trkbar_pagesize(ffparser_schem *ps, void *obj, const int64 *val)
{
	ffui_loader *g = obj;
	ffui_trk_setpage(g->trkbar, *val);
	return 0;
}

static int trkbar_onscroll(ffparser_schem *ps, void *obj, const ffstr *val)
{
	ffui_loader *g = obj;
	if (0 == (g->trkbar->scroll_id = g->getcmd(g->udata, val)))
		return FFPARS_EBADVAL;
	return 0;
}

static int trkbar_onscrolling(ffparser_schem *ps, void *obj, const ffstr *val)
{
	ffui_loader *g = obj;
	if (0 == (g->trkbar->scrolling_id = g->getcmd(g->udata, val)))
		return FFPARS_EBADVAL;
	return 0;
}

static int new_trkbar(ffparser_schem *ps, void *obj, ffpars_ctx *ctx)
{
	ffui_loader *g = obj;
	if (NULL == (g->trkbar = ldr_getctl(g, &ps->vals[0])))
		return FFPARS_EBADVAL;

	if (0 != ffui_trk_create(g->trkbar, g->wnd))
		return FFPARS_ESYS;
	ffpars_setargs(ctx, g, trkbar_args, FFCNT(trkbar_args));
	return 0;
}


static int pgsbar_style(ffparser_schem *ps, void *obj, const ffstr *val)
{
	ffui_loader *g = obj;
	if (ffstr_eqcz(val, "visible"))
		ffui_show(g->ctl, 1);
	return 0;
}

static int new_pgsbar(ffparser_schem *ps, void *obj, ffpars_ctx *ctx)
{
	ffui_loader *g = obj;
	if (NULL == (g->ctl = ldr_getctl(g, &ps->vals[0])))
		return FFPARS_EBADVAL;

	if (0 != ffui_pgs_create(g->ctl, g->wnd))
		return FFPARS_ESYS;
	ffpars_setargs(ctx, g, pgsbar_args, FFCNT(pgsbar_args));
	return 0;
}


static int stbar_style(ffparser_schem *ps, void *obj, const ffstr *val)
{
	ffui_loader *g = obj;
	if (ffstr_eqcz(val, "visible"))
		ffui_show(g->ctl, 1);
	return 0;
}

static int stbar_parts(ffparser_schem *ps, void *obj, const int64 *val)
{
	ffui_loader *g = obj;
	int *it = ffarr_push(&g->sb_parts, int);
	if (it == NULL)
		return FFPARS_ESYS;
	*it = (int)*val;
	return 0;
}

static int stbar_done(ffparser_schem *ps, void *obj)
{
	ffui_loader *g = obj;
	ffui_stbar_setparts(g->ctl, g->sb_parts.len, g->sb_parts.ptr);
	ffarr_free(&g->sb_parts);
	return 0;
}

static int new_stbar(ffparser_schem *ps, void *obj, ffpars_ctx *ctx)
{
	ffui_loader *g = obj;
	if (NULL == (g->ctl = ldr_getctl(g, &ps->vals[0])))
		return FFPARS_EBADVAL;

	if (0 != ffui_stbar_create(g->actl.stbar, g->wnd))
		return FFPARS_ESYS;
	ffarr_null(&g->sb_parts);
	ffpars_setargs(ctx, g, stbar_args, FFCNT(stbar_args));
	return 0;
}


static int tray_style(ffparser_schem *ps, void *obj, const ffstr *val)
{
	ffui_loader *g = obj;
	if (ffstr_eqcz(val, "visible"))
		g->tr.show = 1;
	else
		return FFPARS_EBADVAL;
	return 0;
}

static int tray_pmenu(ffparser_schem *ps, void *obj, const ffstr *val)
{
	ffui_loader *g = obj;
	ffui_menu *m = g->getctl(g->udata, val);
	if (m == NULL)
		return FFPARS_EBADVAL;
	g->wnd->trayicon->pmenu = m;
	return 0;
}

static int tray_icon(ffparser_schem *ps, void *obj, ffpars_ctx *ctx)
{
	ffui_loader *g = obj;
	ffmem_zero(&g->tr.ico, sizeof(_ffui_ldr_icon_t));
	g->tr.ico.ldr = g;
	ffpars_setargs(ctx, &g->tr.ico, icon_args, FFCNT(icon_args));
	return 0;
}

static int tray_lclick(ffparser_schem *ps, void *obj, const ffstr *val)
{
	ffui_loader *g = obj;
	int id = g->getcmd(g->udata, val);
	if (id == 0)
		return FFPARS_EBADVAL;
	g->wnd->trayicon->lclick_id = id;
	return 0;
}

static int tray_done(ffparser_schem *ps, void *obj)
{
	ffui_loader *g = obj;

	if (g->tr.ico.h != NULL)
		ffui_tray_seticon(g->tray, g->tr.ico.h);

	if (g->tr.show && 0 != ffui_tray_show(g->tray, 1))
		return FFPARS_ESYS;

	return 0;
}

static int new_tray(ffparser_schem *ps, void *obj, ffpars_ctx *ctx)
{
	ffui_loader *g = obj;

	if (NULL == (g->tray = ldr_getctl(g, &ps->vals[0])))
		return FFPARS_EBADVAL;

	ffui_tray_create(g->tray, g->wnd);
	ffpars_setargs(ctx, g, tray_args, FFCNT(tray_args));
	return 0;
}


static int font_name(ffparser_schem *ps, void *obj, const ffstr *val)
{
	ffui_loader *g = obj;
	ffqz_copys(g->fnt.lf.lfFaceName, FFCNT(g->fnt.lf.lfFaceName), val->ptr, val->len);
	return 0;
}

static int font_height(ffparser_schem *ps, void *obj, const int64 *val)
{
	ffui_loader *g = obj;
	ffui_font_setheight(&g->fnt, (int)*val);
	return 0;
}

static int font_style(ffparser_schem *ps, void *obj, const ffstr *val)
{
	ffui_loader *g = obj;
	if (ffstr_eqcz(val, "bold"))
		g->fnt.lf.lfWeight = FW_BOLD;
	else if (ffstr_eqcz(val, "italic"))
		g->fnt.lf.lfItalic = 1;
	else if (ffstr_eqcz(val, "underline"))
		g->fnt.lf.lfUnderline = 1;
	else
		return FFPARS_EBADVAL;
	return 0;
}

static int font_done(ffparser_schem *ps, void *obj)
{
	ffui_loader *g = obj;
	HFONT f;
	g->fnt.lf.lfCharSet = OEM_CHARSET;
	g->fnt.lf.lfQuality = PROOF_QUALITY;
	f = CreateFontIndirect(&g->fnt.lf);
	if (f == NULL)
		return FFPARS_ESYS;
	if (g->ctl == (void*)g->wnd)
		g->wnd->font = f;
	else {
		ffui_ctl_send(g->ctl, WM_SETFONT, f, 0);
		g->ctl->font = f;
	}
	return 0;
}

static int label_text(ffparser_schem *ps, void *obj, const ffstr *val)
{
	ffui_loader *g = obj;
	ffui_settextstr(g->ctl, val);
	return 0;
}

static int label_font(ffparser_schem *ps, void *obj, ffpars_ctx *ctx)
{
	ffui_loader *g = obj;
	ffmem_zero(&g->fnt, sizeof(LOGFONT));
	g->fnt.lf.lfHeight = 15;
	g->fnt.lf.lfWeight = FW_NORMAL;
	ffpars_setargs(ctx, g, font_args, FFCNT(font_args));
	return 0;
}

static int label_style(ffparser_schem *ps, void *obj, const ffstr *val)
{
	ffui_loader *g = obj;
	if (ffstr_eqcz(val, "visible"))
		ffui_show(g->ctl, 1);
	else
		return FFPARS_EBADVAL;

	return 0;
}

static int label_pos(ffparser_schem *ps, void *obj, const int64 *val)
{
	ffui_loader *g = obj;
	int *i = &g->r.x;
	if (ps->p->type == FFCONF_TVAL)
		g->ir = 0;
	i[g->ir++] = (int)*val;
	if (g->ir == 4) {
		ffui_setposrect(g->ctl, &g->r, 0);
	}
	return 0;
}

static int label_color(ffparser_schem *ps, void *obj, const ffstr *val)
{
	ffui_loader *g = obj;
	uint clr;

	if ((uint)-1 == (clr = ffpic_color(val->ptr, val->len)))
		return FFPARS_EBADVAL;

	g->actl.lbl->color = clr;
	return 0;
}

static int label_action(ffparser_schem *ps, void *obj, const ffstr *val)
{
	ffui_loader *g = obj;
	int id = g->getcmd(g->udata, val);
	if (id == 0)
		return FFPARS_EBADVAL;

	g->actl.lbl->click_id = id;
	return 0;
}


static int ctl_tooltip(ffparser_schem *ps, void *obj, const ffstr *val)
{
	ffui_loader *g = obj;
	ffui_wnd_tooltip(g->wnd, g->ctl, val->ptr, val->len);
	return 0;
}

static int btn_action(ffparser_schem *ps, void *obj, const ffstr *val)
{
	ffui_loader *g = obj;
	int id = g->getcmd(g->udata, val);
	if (id == 0)
		return FFPARS_EBADVAL;

	g->btn->action_id = id;
	return 0;
}


static int edit_style(ffparser_schem *ps, void *obj, const ffstr *val)
{
	ffui_loader *g = obj;
	if (ffstr_eqcz(val, "visible"))
		ffui_show(g->ctl, 1);

	else if (ffstr_eqcz(val, "password"))
		ffui_edit_password(g->ctl);

	else if (ffstr_eqcz(val, "readonly"))
		ffui_edit_readonly(g->ctl, 1);

	else
		return FFPARS_EBADVAL;

	return 0;
}

static int edit_action(ffparser_schem *ps, void *obj, const ffstr *val)
{
	ffui_loader *g = obj;
	int id = g->getcmd(g->udata, val);
	if (id == 0)
		return FFPARS_EBADVAL;

	g->ed->change_id = id;
	return 0;
}


static int combx_style(ffparser_schem *ps, void *obj, const ffstr *val)
{
	ffui_loader *g = obj;
	if (ffstr_eqcz(val, "visible"))
		ffui_show(g->ctl, 1);
	else
		return FFPARS_EBADVAL;

	return 0;
}


static int new_label(ffparser_schem *ps, void *obj, ffpars_ctx *ctx)
{
	ffui_loader *g = obj;

	g->ctl = ldr_getctl(g, &ps->vals[0]);
	if (g->ctl == NULL)
		return FFPARS_EBADVAL;

	if (0 != ffui_lbl_create(g->ctl, g->wnd))
		return FFPARS_ESYS;

	ffpars_setargs(ctx, g, label_args, FFCNT(label_args));
	return 0;
}

static int new_editbox(ffparser_schem *ps, void *obj, ffpars_ctx *ctx)
{
	ffui_loader *g = obj;
	int r;

	g->ctl = ldr_getctl(g, &ps->vals[0]);
	if (g->ctl == NULL)
		return FFPARS_EBADVAL;

	if (!ffsz_cmp(ps->curarg->name, "text"))
		r = ffui_text_create(g->ctl, g->wnd);
	else
		r = ffui_edit_create(g->ctl, g->wnd);
	if (r != 0)
		return FFPARS_ESYS;

	ffpars_setargs(ctx, g, editbox_args, FFCNT(editbox_args));
	return 0;
}

static int new_combobox(ffparser_schem *ps, void *obj, ffpars_ctx *ctx)
{
	ffui_loader *g = obj;

	g->ctl = ldr_getctl(g, &ps->vals[0]);
	if (g->ctl == NULL)
		return FFPARS_EBADVAL;

	if (0 != ffui_combx_create(g->ctl, g->wnd))
		return FFPARS_ESYS;

	ffpars_setargs(ctx, g, combx_args, FFCNT(combx_args));
	return 0;
}

static int new_button(ffparser_schem *ps, void *obj, ffpars_ctx *ctx)
{
	ffui_loader *g = obj;
	void *ctl;

	ctl = ldr_getctl(g, &ps->vals[0]);
	if (ctl == NULL)
		return FFPARS_EBADVAL;
	g->ctl = ctl;

	if (0 != ffui_btn_create(g->ctl, g->wnd))
		return FFPARS_ESYS;

	ffpars_setargs(ctx, g, btn_args, FFCNT(btn_args));
	return 0;
}


static int new_tab(ffparser_schem *ps, void *obj, ffpars_ctx *ctx)
{
	ffui_loader *g = obj;
	if (NULL == (g->actl.tab = ldr_getctl(g, &ps->vals[0])))
		return FFPARS_EBADVAL;

	if (0 != ffui_tab_create(g->actl.tab, g->wnd))
		return FFPARS_ESYS;
	ffpars_setargs(ctx, g, tab_args, FFCNT(tab_args));
	return 0;
}

static int tab_onchange(ffparser_schem *ps, void *obj, const ffstr *val)
{
	ffui_loader *g = obj;
	int id = g->getcmd(g->udata, val);
	if (id == 0)
		return FFPARS_EBADVAL;

	g->actl.tab->chsel_id = id;
	return 0;
}


enum {
	VIEW_STYLE_EDITLABELS,
	VIEW_STYLE_EXPLORER_THEME,
	VIEW_STYLE_FULL_ROW_SELECT,
	VIEW_STYLE_GRID_LINES,
	VIEW_STYLE_HAS_BUTTONS,
	VIEW_STYLE_HAS_LINES,
	VIEW_STYLE_MULTI_SELECT,
	VIEW_STYLE_TRACK_SELECT,
	VIEW_STYLE_VISIBLE,
};

static const char *const view_styles[] = {
	"edit_labels",
	"explorer_theme",
	"full_row_select",
	"grid_lines",
	"has_buttons",
	"has_lines",
	"multi_select",
	"track_select",
	"visible",
};

static int view_style(ffparser_schem *ps, void *obj, const ffstr *val)
{
	ffui_loader *g = obj;
	switch (ffszarr_ifindsorted(view_styles, FFCNT(view_styles), val->ptr, val->len)) {
	case VIEW_STYLE_VISIBLE:
		ffui_show(g->ctl, 1);
		break;

	case VIEW_STYLE_EDITLABELS:
		ffui_styleset(g->ctl->h, LVS_EDITLABELS);
		break;

	case VIEW_STYLE_MULTI_SELECT:
		ffui_styleclear(g->ctl->h, LVS_SINGLESEL);
		break;

	case VIEW_STYLE_GRID_LINES:
		ListView_SetExtendedListViewStyleEx(g->ctl->h, LVS_EX_GRIDLINES, LVS_EX_GRIDLINES);
		break;

	case VIEW_STYLE_EXPLORER_THEME:
		ffui_view_settheme(g->ctl);
		break;

	case VIEW_STYLE_FULL_ROW_SELECT:
		ffui_styleset(g->ctl->h, TVS_FULLROWSELECT);
		break;

	case VIEW_STYLE_TRACK_SELECT:
		ffui_styleset(g->ctl->h, TVS_TRACKSELECT);
		break;

	case VIEW_STYLE_HAS_LINES:
		ffui_styleset(g->ctl->h, TVS_HASLINES);
		break;

	case VIEW_STYLE_HAS_BUTTONS:
		ffui_styleset(g->ctl->h, TVS_HASBUTTONS);
		break;

	default:
		return FFPARS_EBADVAL;
	}
	return 0;
}

static int view_color(ffparser_schem *ps, void *obj, const ffstr *val)
{
	ffui_loader *g = obj;
	uint clr;

	if ((uint)-1 == (clr = ffpic_color(val->ptr, val->len)))
		return FFPARS_EBADVAL;

	if (!ffsz_cmp(ps->curarg->name, "color"))
		ffui_view_clr_text(g->vi, clr);
	else
		ffui_view_clr_bg(g->vi, clr);
	return 0;
}

static int view_pmenu(ffparser_schem *ps, void *obj, const ffstr *val)
{
	ffui_loader *g = obj;
	ffui_menu *m = g->getctl(g->udata, val);
	if (m == NULL)
		return FFPARS_EBADVAL;
	g->vi->pmenu = m;
	return 0;
}

static int view_chsel(ffparser_schem *ps, void *obj, const ffstr *val)
{
	ffui_loader *g = obj;
	int id = g->getcmd(g->udata, val);
	if (id == 0)
		return FFPARS_EBADVAL;

	g->vi->chsel_id = id;
	return 0;
}

static int view_lclick(ffparser_schem *ps, void *obj, const ffstr *val)
{
	ffui_loader *g = obj;
	int id = g->getcmd(g->udata, val);
	if (id == 0)
		return FFPARS_EBADVAL;

	g->vi->lclick_id = id;
	return 0;
}

static int view_dblclick(ffparser_schem *ps, void *obj, const ffstr *val)
{
	ffui_loader *g = obj;
	int id = g->getcmd(g->udata, val);
	if (id == 0)
		return FFPARS_EBADVAL;
	g->vi->dblclick_id = id;
	return 0;
}


static int viewcol_width(ffparser_schem *ps, void *obj, const int64 *val)
{
	ffui_loader *g = obj;
	ffui_viewcol_setwidth(&g->vicol, *val);
	return 0;
}

static int viewcol_align(ffparser_schem *ps, void *obj, const ffstr *val)
{
	ffui_loader *g = obj;
	uint a;
	if (ffstr_eqcz(val, "left"))
		a = HDF_LEFT;
	else if (ffstr_eqcz(val, "right"))
		a = HDF_RIGHT;
	else if (ffstr_eqcz(val, "center"))
		a = HDF_CENTER;
	else
		return FFPARS_EBADVAL;
	ffui_viewcol_setalign(&g->vicol, a);
	return 0;
}

static int viewcol_order(ffparser_schem *ps, void *obj, const int64 *val)
{
	ffui_loader *g = obj;
	ffui_viewcol_setorder(&g->vicol, *val);
	return 0;
}

static int viewcol_done(ffparser_schem *ps, void *obj)
{
	ffui_loader *g = obj;
	ffui_view_inscol(g->vi, ffui_view_ncols(g->vi), &g->vicol);
	return 0;
}

static int view_column(ffparser_schem *ps, void *obj, ffpars_ctx *ctx)
{
	ffstr *name = &ps->vals[0];
	ffui_loader *g = obj;
	ffui_viewcol_reset(&g->vicol);
	ffui_viewcol_setwidth(&g->vicol, 100);
	ffui_viewcol_settext(&g->vicol, name->ptr, name->len);
	ffpars_setargs(ctx, g, viewcol_args, FFCNT(viewcol_args));
	return 0;
}

static int new_listview(ffparser_schem *ps, void *obj, ffpars_ctx *ctx)
{
	ffui_loader *g = obj;

	g->ctl = ldr_getctl(g, &ps->vals[0]);
	if (g->ctl == NULL)
		return FFPARS_EBADVAL;

	if (0 != ffui_view_create(g->ctl, g->wnd))
		return FFPARS_ESYS;

	ffpars_setargs(ctx, g, view_args, FFCNT(view_args));
	return 0;
}

static int new_treeview(ffparser_schem *ps, void *obj, ffpars_ctx *ctx)
{
	ffui_loader *g = obj;

	g->ctl = ldr_getctl(g, &ps->vals[0]);
	if (g->ctl == NULL)
		return FFPARS_EBADVAL;

	if (0 != ffui_tree_create(g->ctl, g->wnd))
		return FFPARS_ESYS;

	ffpars_setargs(ctx, g, view_args, FFCNT(view_args));
	return 0;
}


static int pnchild_resize(ffparser_schem *ps, void *obj, const ffstr *val)
{
	ffui_loader *g = obj;
	if (ffstr_eqcz(val, "cx"))
		g->paned->items[g->ir - 1].cx = 1;
	else if (ffstr_eqcz(val, "cy"))
		g->paned->items[g->ir - 1].cy = 1;
	else
		return FFPARS_EBADVAL;
	return 0;
}

static int pnchild_move(ffparser_schem *ps, void *obj, const ffstr *val)
{
	ffui_loader *g = obj;
	if (ffstr_eqcz(val, "x"))
		g->paned->items[g->ir - 1].x = 1;
	else if (ffstr_eqcz(val, "y"))
		g->paned->items[g->ir - 1].y = 1;
	else
		return FFPARS_EBADVAL;
	return 0;
}

static int paned_child(ffparser_schem *ps, void *obj, ffpars_ctx *ctx)
{
	ffui_loader *g = obj;
	void *ctl;

	if (g->ir == 2)
		return FFPARS_EBIGVAL;

	ctl = ldr_getctl(g, &ps->vals[0]);
	if (ctl == NULL)
		return FFPARS_EBADVAL;

	g->paned->items[g->ir++].it = ctl;
	ffpars_setargs(ctx, g, paned_child_args, FFCNT(paned_child_args));
	return 0;
}

static int new_paned(ffparser_schem *ps, void *obj, ffpars_ctx *ctx)
{
	ffui_loader *g = obj;
	void *ctl;

	ctl = ldr_getctl(g, &ps->vals[0]);
	if (ctl == NULL)
		return FFPARS_EBADVAL;
	ffmem_zero(ctl, sizeof(ffui_paned));
	g->paned = ctl;
	ffui_paned_create(ctl, g->wnd);

	g->ir = 0;
	ffpars_setargs(ctx, g, paned_args, FFCNT(paned_args));
	return 0;
}


// DIALOG
static int dlg_title(ffparser_schem *ps, void *obj, const ffstr *val)
{
	ffui_loader *g = obj;
	ffui_dlg_title(g->dlg, val->ptr, val->len);
	return 0;
}

static int dlg_filter(ffparser_schem *ps, void *obj, const ffstr *val)
{
	ffui_loader *g = obj;
	ffui_dlg_filter(g->dlg, val->ptr, val->len);
	return 0;
}

static int new_dlg(ffparser_schem *ps, void *obj, ffpars_ctx *ctx)
{
	ffui_loader *g = obj;
	if (NULL == (g->dlg = g->getctl(g->udata, &ps->vals[0])))
		return FFPARS_EBADVAL;
	ffui_dlg_init(g->dlg);
	ffpars_setargs(ctx, g, dlg_args, FFCNT(dlg_args));
	return 0;
}


static int wnd_title(ffparser_schem *ps, void *obj, const ffstr *val)
{
	ffui_loader *g = obj;
	ffui_settextstr(g->wnd, val);
	return 0;
}

static int wnd_position(ffparser_schem *ps, void *obj, const int64 *v)
{
	ffui_loader *g = obj;
	int *i = &g->r.x;
	if (ps->p->type == FFCONF_TVAL)
		g->ir = 0;
	else if (g->ir == 4)
		return FFPARS_EBIGVAL;
	i[g->ir++] = (int)*v;
	if (g->ir == 4) {
		ffui_setposrect(g->wnd, &g->r, 0);
	}
	return 0;
}

static int wnd_placement(ffparser_schem *ps, void *obj, const int64 *v)
{
	ffui_loader *g = obj;

	if (ps->p->type == FFCONF_TVAL) {
		g->ir = 0;
		g->showcmd = *v;
		return 0;
	} else if (g->ir == 4)
		return FFPARS_EBIGVAL;

	int *i = &g->r.x;
	i[g->ir++] = (int)*v;

	if (g->ir == 4)
		ffui_wnd_setplacement(g->wnd, g->showcmd, &g->r);
	return 0;
}

static int wnd_icon(ffparser_schem *ps, void *obj, ffpars_ctx *ctx)
{
	ffui_loader *g = obj;
	ffmem_zero(&g->ico, sizeof(_ffui_ldr_icon_t));
	g->ico.ldr = g;
	ffpars_setargs(ctx, &g->ico, icon_args, FFCNT(icon_args));
	return 0;
}

/** 'percent': Opacity value, 10-100 */
static int wnd_opacity(ffparser_schem *ps, void *obj, const int64 *val)
{
	ffui_loader *g = obj;
	uint percent = (uint)*val;

	if (!(percent >= 10 && percent <= 100))
		return FFPARS_EBIGVAL;

	ffui_wnd_opacity(g->wnd, percent);
	return 0;
}

static int wnd_borderstick(ffparser_schem *ps, void *obj, const int64 *val)
{
	ffui_loader *g = obj;
	g->wnd->bordstick = (byte)*val;
	return 0;
}

static int wnd_style(ffparser_schem *ps, void *obj, const ffstr *val)
{
	ffui_loader *g = obj;

	if (ffstr_eqcz(val, "popup"))
		ffui_wnd_setpopup(g->wnd);

	else if (ffstr_eqcz(val, "visible"))
		g->vis = 1;

	else
		return FFPARS_EBADVAL;
	return 0;
}

static int wnd_onclose(ffparser_schem *ps, void *obj, const ffstr *val)
{
	ffui_loader *g = obj;
	if (0 == (g->wnd->onclose_id = g->getcmd(g->udata, val)))
		return FFPARS_EBADVAL;
	return 0;
}

static int wnd_parent(ffparser_schem *ps, void *obj, const ffstr *val)
{
	ffui_loader *g = obj;
	ffui_ctl *parent = g->getctl(g->udata, val);
	if (parent == NULL)
		return FFPARS_EBADVAL;
	(void)SetWindowLongPtr(g->wnd->h, GWLP_HWNDPARENT, (LONG_PTR)parent->h);
	return 0;
}

static int wnd_done(ffparser_schem *ps, void *obj)
{
	ffui_loader *g = obj;

	if (g->ico.h != NULL) {
		ffui_wnd_seticon(g->wnd, g->ico.h);

	} else {
		HWND hparent = (HWND)GetWindowLongPtr(g->wnd->h, GWLP_HWNDPARENT);
		if (hparent != NULL) {
			ffui_wnd *parent = ffui_getctl(hparent);
			HICON ico = (HICON)ffui_ctl_send(parent, WM_GETICON, ICON_BIG, 0);
			ffui_wnd_seticon(g->wnd, ico);
		}
	}

	{
	//main menu isn't visible until the window is resized
	HMENU mm = GetMenu(g->wnd->h);
	if (mm != NULL)
		SetMenu(g->wnd->h, mm);
	}

	if (g->accels.len != 0) {
		HACCEL a = CreateAcceleratorTable(g->accels.ptr, FF_TOINT(g->accels.len));
		g->accels.len = 0;
		if (a == NULL)
			return FFPARS_ESYS;
		g->wnd->acceltbl = a;
	}

	if (g->vis) {
		g->vis = 0;
		ffui_show(g->wnd, 1);
	}

	g->wnd = NULL;
	ffmem_safefree0(g->wndname);
	return 0;
}

static int new_wnd(ffparser_schem *ps, void *obj, ffpars_ctx *ctx)
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


void ffui_ldr_init(ffui_loader *g)
{
	ffmem_tzero(g);
	ffpars_setargs(&g->ctx, g, top_args, FFCNT(top_args));
}

void ffui_ldr_fin(ffui_loader *g)
{
	ffarr_free(&g->accels);
	ffmem_safefree(g->errstr);
	ffmem_safefree0(g->wndname);
}

static void* ldr_getctl(ffui_loader *g, const ffstr *name)
{
	char buf[255], *end = buf + sizeof(buf);
	ffstr s;
	s.ptr = buf;
	s.len = ffs_fmt(buf, end, "%s.%S", g->wndname, name);
	return g->getctl(g->udata, &s);
}

int ffui_ldr_loadfile(ffui_loader *g, const char *fn)
{
	int r;
	ffstr s;
	char *buf = NULL;
	size_t n;
	fffd f = FF_BADFD;
	ffparser p;
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

	ffpars_free(&p);
	ffpars_schemfree(&ps);
	ffmem_safefree(buf);
	if (f != FF_BADFD)
		fffile_close(f);
	return r;
}


void ffui_ldrw_fin(ffui_loaderw *ldr)
{
	ffarr_free(&ldr->buf);
}

void ffui_ldr_setv(ffui_loaderw *ldr, const char *const *names, size_t nn, uint flags)
{
	char buf[128];
	size_t n;
	uint i, f, set;
	ffui_ctl *c;
	ffstr settname, ctlname, val;
	ffarr s = {0};

	for (i = 0;  i != nn;  i++) {
		ffstr_setz(&settname, names[i]);
		ffs_rsplit2by(settname.ptr, settname.len, '.', &ctlname, &val);

		if (NULL == (c = ldr->getctl(ldr->udata, &ctlname)))
			continue;

		set = 0;
		f = 0;
		switch (c->uid) {

		case FFUI_UID_WINDOW:
			if (ffstr_eqcz(&val, "position")) {
				ffui_pos pos;
				ffui_wnd_pos((ffui_wnd*)c, &pos);
				n = ffs_fmt(buf, buf + sizeof(buf), "%d %d %u %u", pos.x, pos.y, pos.cx, pos.cy);
				ffstr_set(&s, buf, n);
				set = 1;

			} else if (ffstr_eqcz(&val, "placement")) {
				ffui_pos pos;
				uint cmd = ffui_wnd_placement((ffui_wnd*)c, &pos);
				n = ffs_fmt(buf, buf + sizeof(buf), "%u %d %d %u %u", cmd, pos.x, pos.y, pos.cx, pos.cy);
				ffstr_set(&s, buf, n);
				set = 1;
			}
			break;

		case FFUI_UID_EDITBOX:
			if (ffstr_eqcz(&val, "text")) {
				ffstr ss;
				ffui_textstr(c, &ss);
				ffarr_set3(&s, ss.ptr, ss.len, ss.len);
				f = FFUI_LDR_FSTR;
				set = 1;
			}
			break;

		case FFUI_UID_TRACKBAR:
			if (ffstr_eqcz(&val, "value")) {
				n = ffs_fmt(buf, buf + sizeof(buf), "%u", ffui_trk_val(c));
				ffstr_set(&s, buf, n);
				set = 1;
			}
			break;

		default:
			continue;
		}

		if (!set)
			continue;

		ffui_ldr_set(ldr, settname.ptr, s.ptr, s.len, f);
		ffarr_free(&s);
	}
}

void ffui_ldr_set(ffui_loaderw *ldr, const char *name, const char *val, size_t len, uint flags)
{
	if (flags & FFUI_LDR_FSTR)
		ffstr_catfmt(&ldr->buf, "%s \"%*s\"\n", name, len, val);
	else
		ffstr_catfmt(&ldr->buf, "%s %*s\n", name, len, val);
}

int ffui_ldr_write(ffui_loaderw *ldr, const char *fn)
{
	int r = -1;
	fffd f;

	if (FF_BADFD == (f = fffile_open(fn, O_CREAT | O_TRUNC | O_WRONLY)))
		goto done;
	if (ldr->buf.len != (size_t)fffile_write(f, ldr->buf.ptr, ldr->buf.len))
		goto done;
	r = 0;

done:
	if (f != FF_BADFD && 0 != fffile_close(f))
		return -1;
	return r;
}

void ffui_ldr_loadconf(ffui_loader *g, const char *fn)
{
	ffstr3 buf = {0};
	ffstr s, line, name, val;
	fffd f = FF_BADFD;

	if (FF_BADFD == (f = fffile_open(fn, O_RDONLY)))
		goto done;

	if (NULL == ffarr_alloc(&buf, fffile_size(f)))
		goto done;

	fffile_read(f, buf.ptr, buf.cap);
	ffstr_set(&s, buf.ptr, buf.cap);

	while (s.len != 0) {
		size_t n = ffstr_nextval(s.ptr, s.len, &line, '\n');
		ffstr_shift(&s, n);

		if (line.len == 0)
			continue;

		ffstr_set(&name, line.ptr, 0);
		ffstr_null(&val);
		while (line.len != 0) {
			const char *pos = ffs_findof(line.ptr, line.len, ". ", 2);
			if (pos == ffarr_end(&line))
				break;
			if (*pos == ' ') {
				val = line;
				break;
			}
			name.len = pos - name.ptr;
			ffstr_shift(&line, pos + 1 - line.ptr);
		}
		if (name.len == 0 || val.len == 0)
			continue;

		g->ctl = g->getctl(g->udata, &name);
		if (g->ctl != NULL) {
			ffparser p;
			ffparser_schem ps;
			ffpars_ctx ctx = {0};

			switch (g->ctl->uid) {
			case FFUI_UID_WINDOW:
				g->wnd = (void*)g->ctl;
				ffpars_setargs(&ctx, g, wnd_args, FFCNT(wnd_args));
				break;

			case FFUI_UID_EDITBOX:
				ffpars_setargs(&ctx, g, editbox_args, FFCNT(editbox_args));
				break;

			case FFUI_UID_TRACKBAR:
				ffpars_setargs(&ctx, g, trkbar_args, FFCNT(trkbar_args));
				break;

			default:
				continue;
			}
			ffconf_scheminit(&ps, &p, &ctx);

			ffbool lf = 0;
			while (val.len != 0) {
				n = val.len;
				int r = ffconf_parse(&p, val.ptr, &n);
				ffstr_shift(&val, n);
				r = ffconf_schemrun(&ps);
				if (ffpars_iserr(r))
					break;
				if (r == FFPARS_MORE && !lf) {
					ffstr_setcz(&val, "\n");
					lf = 1;
				}
			}

			ffconf_schemfin(&ps);
			ffpars_free(&p);
			ffpars_schemfree(&ps);
		}
	}

done:
	FF_SAFECLOSE(f, FF_BADFD, fffile_close);
	ffarr_free(&buf);
}

void* ffui_ldr_findctl(const ffui_ldr_ctl *ctx, void *ctl, const ffstr *name)
{
	uint i;
	ffstr s = *name, sctl;

	while (0 != ffstr_nextval3(&s, &sctl, '.' | FFS_NV_KEEPWHITE)) {
		for (i = 0; ; i++) {
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
