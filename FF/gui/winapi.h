/** GUI based on Windows API.
Copyright (c) 2014 Simon Zolin
*/

#pragma once

#include <FF/array.h>

#include <commctrl.h>
#include <uxtheme.h>


typedef struct ffui_wnd ffui_wnd;

FF_EXTN int ffui_init(void);
FF_EXTN void ffui_uninit(void);


// POINT
typedef struct ffui_point {
	int x, y;
} ffui_point;

#define ffui_screen2client(h, pt)  ScreenToClient(h, (POINT*)pt)


// CURSOR
#define ffui_cur_pos(pt)  GetCursorPos((POINT*)pt)
#define ffui_cur_setpos(x, y)  SetCursorPos(x, y)

enum FFUI_CUR {
	FFUI_CUR_ARROW = OCR_NORMAL,
	FFUI_CUR_IBEAM = OCR_IBEAM,
	FFUI_CUR_WAIT = OCR_WAIT,
	FFUI_CUR_HAND = OCR_HAND,
};

/** @type: enum FFUI_CUR. */
#define ffui_cur_set(type)  SetCursor(LoadCursorW(NULL, (wchar_t*)(type)))


// FONT
typedef struct ffui_font {
	LOGFONT lf;
} ffui_font;

FF_EXTN void ffui_font_setheight(ffui_font *fnt, int height);


typedef struct ffui_pos {
	int x, y
		, cx, cy;
} ffui_pos;


// DIALOG
typedef struct ffui_dialog {
	OPENFILENAME of;
	ffsyschar *names
		, *pname;
	char *name;
} ffui_dialog;

static FFINL void ffui_dlg_init(ffui_dialog *d)
{
	ffmem_tzero(d);
	d->of.lStructSize = sizeof(OPENFILENAME);
	d->of.Flags = OFN_EXPLORER | OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT;
}

static FFINL void ffui_dlg_title(ffui_dialog *d, const char *title, size_t len)
{
	ffmem_safefree((void*)d->of.lpstrTitle);
	d->of.lpstrTitle = ffs_utow(NULL, NULL, title, len);
}

#define ffui_dlg_titlez(d, sz)  ffui_dlg_title(d, sz, ffsz_len(sz))

static FFINL void ffui_dlg_filter(ffui_dialog *d, const char *title, size_t len)
{
	ffmem_safefree((void*)d->of.lpstrFilter);
	d->of.lpstrFilter = ffs_utow(NULL, NULL, title, len);
}

#define ffui_dlg_multisel(d)  ((d)->of.Flags |= OFN_ALLOWMULTISELECT)

/**
Return 1 on success. */
FF_EXTN char* ffui_dlg_open(ffui_dialog *d, ffui_wnd *parent);

FF_EXTN char* ffui_dlg_save(ffui_dialog *d, ffui_wnd *parent, const char *fn);

FF_EXTN void ffui_dlg_destroy(ffui_dialog *d);

FF_EXTN char* ffui_dlg_nextname(ffui_dialog *d);


// MESSAGE DIALOG
enum FFUI_MSGDLG {
	FFUI_MSGDLG_INFO = MB_ICONINFORMATION,
	FFUI_MSGDLG_WARN = MB_ICONWARNING,
	FFUI_MSGDLG_ERR = MB_ICONERROR,
};

FF_EXTN int ffui_msgdlg_show(const char *title, const char *text, size_t len, uint flags);


enum FFUI_UID {
	FFUI_UID_WINDOW = 1,
	FFUI_UID_LABEL,
	FFUI_UID_EDITBOX,
	FFUI_UID_BUTTON,

	FFUI_UID_TRACKBAR,
	FFUI_UID_PROGRESSBAR,
	FFUI_UID_STATUSBAR,

	FFUI_UID_LISTVIEW,
	FFUI_UID_TREEVIEW,
};

#define FFUI_CTL \
	HWND h; \
	enum FFUI_UID uid; \
	const char *name

typedef struct ffui_ctl {
	FFUI_CTL;
	HFONT font;
} ffui_ctl;

#define ffui_getctl(h)  ((void*)GetWindowLongPtr(h, GWLP_USERDATA))
#define ffui_setctl(h, udata)  SetWindowLongPtr(h, GWLP_USERDATA, (LONG_PTR)(udata))

#define ffui_send(h, msg, w, l)  SendMessage(h, msg, (size_t)(w), (size_t)(l))
#define ffui_post(h, msg, w, l)  PostMessage(h, msg, (size_t)(w), (size_t)(l))
#define ffui_ctl_send(c, msg, w, l)  ffui_send((c)->h, msg, w, l)

FF_EXTN void ffui_getpos(HWND h, ffui_pos *r);

FF_EXTN int ffui_setpos(void *ctl, int x, int y, int cx, int cy, int flags);
#define ffui_setposrect(ctl, rect, flags) \
	ffui_setpos(ctl, (rect)->x, (rect)->y, (rect)->cx, (rect)->cy, flags)

#define ffui_settext_q(h, text)  ffui_send(h, WM_SETTEXT, NULL, text)
FF_EXTN int ffui_settext(void *c, const char *text, size_t len);
#define ffui_settextz(c, sz)  ffui_settext(c, sz, ffsz_len(sz))
#define ffui_settextstr(c, str)  ffui_settext(c, (str)->ptr, (str)->len)
#define ffui_cleartext(c)  ffui_settext_q((c)->h, TEXT(""))

#define ffui_text_q(h, buf, cap)  ffui_send(h, WM_GETTEXT, cap, buf)
FF_EXTN int ffui_textstr(void *c, ffstr *dst);

#define ffui_show(c, show)  ShowWindow((c)->h, (show) ? SW_SHOWNORMAL : SW_HIDE)

#define ffui_redraw(c, redraw)  ffui_ctl_send(c, WM_SETREDRAW, redraw, 0)

#define ffui_setfocus(c)  SetFocus((c)->h)

FF_EXTN int ffui_ctl_destroy(void *c);

#define ffui_styleset(h, style_bit) \
	SetWindowLong(h, GWL_STYLE, GetWindowLong(h, GWL_STYLE) | (style_bit))

#define ffui_styleclear(h, style_bit) \
	SetWindowLong(h, GWL_STYLE, GetWindowLong(h, GWL_STYLE) & ~(style_bit))


typedef struct ffui_fdrop {
	HDROP hdrop;
	uint idx;
	char *fn;
} ffui_fdrop;

#define ffui_fdrop_accept(c, enable)  DragAcceptFiles((c)->h, enable)

FF_EXTN const char* ffui_fdrop_next(ffui_fdrop *df);


FF_EXTN int ffui_openfolder(const char *const *items, size_t selcnt);


FF_EXTN HIMAGELIST ffui_imglist_create(uint width, uint height);


// EDITBOX
typedef struct ffui_edit {
	FFUI_CTL;
	HFONT font;
	uint change_id;
	uint focus_id;
} ffui_edit;

FF_EXTN int ffui_edit_create(ffui_ctl *c, ffui_wnd *parent);

#define ffui_edit_password(e) \
	ffui_ctl_send(e, EM_SETPASSWORDCHAR, (wchar_t)0x25CF, 0)

#define ffui_edit_readonly(e, val) \
	ffui_ctl_send(e, EM_SETREADONLY, val, 0)


// BUTTON
typedef struct ffui_btn {
	FFUI_CTL;
	HFONT font;
	uint action_id;
} ffui_btn;

FF_EXTN int ffui_btn_create(ffui_ctl *c, ffui_wnd *parent);


// LABEL
FF_EXTN int ffui_lbl_create(ffui_ctl *c, ffui_wnd *parent);


//MENU
typedef struct ffui_menu {
	HMENU h;
} ffui_menu;

static FFINL int ffui_menu_create(ffui_menu *m)
{
	return 0 == (m->h = CreatePopupMenu());
}

#define ffui_menu_show(m, x, y, hwnd) \
	TrackPopupMenuEx((m)->h, 0, x, y, hwnd, NULL)


typedef MENUITEMINFO ffui_menuitem;

static FFINL void ffui_menu_itemreset(ffui_menuitem *mi)
{
	if (mi->dwTypeData != NULL)
		ffmem_free(mi->dwTypeData);
	ffmem_tzero(mi);
}

#define ffui_menu_setcmd(mi, cmd) \
do { \
	(mi)->fMask |= MIIM_ID; \
	(mi)->wID = (cmd); \
} while(0)

#define ffui_menu_setsubmenu(mi, hsub) \
do { \
	(mi)->fMask |= MIIM_SUBMENU; \
	(mi)->hSubMenu = (hsub); \
} while(0)

enum FFUI_MENUSTATE {
	FFUI_MENU_CHECKED = MFS_CHECKED,
	FFUI_MENU_DEFAULT = MFS_DEFAULT,
	FFUI_MENU_DISABLED = MFS_DISABLED,
};

#define ffui_menu_addstate(mi, state) \
do { \
	(mi)->fMask |= MIIM_STATE; \
	(mi)->fState |= (state); \
} while(0)

#define ffui_menu_clearstate(mi, state) \
do { \
	(mi)->fMask |= MIIM_STATE; \
	(mi)->fState &= ~(state); \
} while(0)

enum FFUI_MENUTYPE {
	FFUI_MENU_SEPARATOR = MFT_SEPARATOR,
	FFUI_MENU_RADIOCHECK = MFT_RADIOCHECK,
};

#define ffui_menu_settype(mi, type) \
do { \
	(mi)->fMask |= MIIM_FTYPE; \
	(mi)->fType = (type); \
} while(0)

#define ffui_menu_settext_q(mi, sz) \
do { \
	(mi)->fMask |= MIIM_STRING; \
	(mi)->dwTypeData = (sz); \
} while(0)

FF_EXTN int ffui_menu_settext(ffui_menuitem *mi, const char *s, size_t len);
#define ffui_menu_settextz(mi, sz)  ffui_menu_settext(mi, sz, ffsz_len(sz))
#define ffui_menu_settextstr(mi, str)  ffui_menu_settext(mi, (str)->ptr, (str)->len)

/** Append hotkey to menu item text */
FF_EXTN void ffui_menu_sethotkey(ffui_menuitem *mi, const char *s, size_t len);

static FFINL int ffui_menu_ins(ffui_menu *m, int pos, ffui_menuitem *mi)
{
	mi->cbSize = sizeof(MENUITEMINFO);
	int r = !InsertMenuItem(m->h, pos, 1, mi);
	ffui_menu_itemreset(mi);
	return r;
}

#define ffui_menu_append(m, mi)  ffui_menu_ins(m, -1, mi)

static FFINL int ffui_menu_set(ffui_menu *m, int pos, ffui_menuitem *mi)
{
	mi->cbSize = sizeof(MENUITEMINFO);
	int r = !SetMenuItemInfo(m->h, pos, 1, mi);
	ffui_menu_itemreset(mi);
	return r;
}

static FFINL int ffui_menu_destroy(ffui_menu *m)
{
	int r = 0;
	if (m->h != 0) {
		r = !DestroyMenu(m->h);
		m->h = 0;
	}
	return r;
}


// TRAY
typedef struct ffui_trayicon {
	NOTIFYICONDATA nid;
	ffui_menu *pmenu;
	uint lclick_id;
} ffui_trayicon;

FF_EXTN void ffui_tray_create(ffui_trayicon *t, ffui_wnd *wnd);

#define ffui_tray_settooltip(t, s, len) \
do { \
	(t)->nid.uFlags |= NIF_TIP; \
	ffqz_copys((t)->nid.szTip, FFCNT((t)->nid.szTip), s, len); \
} while (0)

#define ffui_tray_settooltipz(t, sz)  ffui_tray_settooltip(t, sz, ffsz_len(sz))

#define ffui_tray_seticon(t, ico) \
do { \
	(t)->nid.uFlags |= NIF_ICON; \
	(t)->nid.hIcon = (HICON)(ico); \
} while (0)

static FFINL int ffui_tray_set(ffui_trayicon *t, uint show)
{
	return 0 == Shell_NotifyIcon(NIM_MODIFY, &t->nid);
}

static FFINL int ffui_tray_show(ffui_trayicon *t, uint show)
{
	return 0 == Shell_NotifyIcon(show ? NIM_ADD : NIM_DELETE, &t->nid);
}


// PANED
typedef struct ffui_paned {
	struct {
		ffui_ctl *it;
		uint cx :1
			, cy :1;
	} items[2];
} ffui_paned;

FF_EXTN void ffui_paned_create(ffui_paned *pn, ffui_wnd *parent);


// STATUS BAR
FF_EXTN int ffui_stbar_create(ffui_ctl *c, ffui_wnd *parent);

#define ffui_stbar_setparts(sb, n, parts)  ffui_send((sb)->h, SB_SETPARTS, n, parts)

#define ffui_stbar_settext_q(h, idx, text)  ffui_send(h, SB_SETTEXT, idx, text)
FF_EXTN void ffui_stbar_settext(ffui_ctl *sb, int idx, const char *text, size_t len);
#define ffui_stbar_settextstr(sb, idx, str)  ffui_stbar_settext(sb, idx, (str)->ptr, (str)->len)
#define ffui_stbar_settextz(sb, idx, sz)  ffui_stbar_settext(sb, idx, sz, ffsz_len(sz))


// TRACKBAR
typedef struct ffui_trkbar {
	FFUI_CTL;
	uint scroll_id;
	uint scrolling_id;
	uint thumbtrk :1; //prevent trackbar from updating position while user's holding it
} ffui_trkbar;

FF_EXTN int ffui_trk_create(ffui_trkbar *t, ffui_wnd *parent);

#define ffui_trk_setrange(t, max)  ffui_ctl_send(t, TBM_SETRANGEMAX, 1, max)

#define ffui_trk_setpage(t, pagesize)  ffui_ctl_send(t, TBM_SETPAGESIZE, 0, pagesize)

#define ffui_trk_set(t, val) \
	((!(t)->thumbtrk) ? ffui_ctl_send(t, TBM_SETPOS, 1, val) : (void)0)

#define ffui_trk_val(t)  ffui_ctl_send(t, TBM_GETPOS, 0, 0)

enum FFUI_TRK_MOVE {
	FFUI_TRK_PGUP,
	FFUI_TRK_PGDN,
};

/** @cmd: enum FFUI_TRK_MOVE. */
FF_EXTN void ffui_trk_move(ffui_trkbar *t, uint cmd);


// PROGRESS BAR
FF_EXTN int ffui_pgs_create(ffui_ctl *c, ffui_wnd *parent);

#define ffui_pgs_set(p, val) \
	ffui_ctl_send(p, PBM_SETPOS, val, 0)

#define ffui_pgs_setrange(p, max) \
	ffui_ctl_send(p, PBM_SETRANGE, 0, MAKELPARAM(0, max))


// LISTVIEW
typedef struct ffui_view {
	FFUI_CTL;
	HFONT font;
	ffui_menu *pmenu;
	int chsel_id;
	int lclick_id;
	int dblclick_id;
	int colclick_id; //"col" is set to column #

	union {
	int col;
	};
} ffui_view;

FF_EXTN int ffui_view_create(ffui_ctl *c, ffui_wnd *parent);

#define ffui_view_settheme(v)  SetWindowTheme((v)->h, L"Explorer", NULL)

FF_EXTN int ffui_view_hittest(HWND h, const ffui_point *pt, int item);

#define ffui_view_makevisible(v, item)  ffui_ctl_send(v, LVM_ENSUREVISIBLE, item, 0)


/** Get the number of columns. */
#define ffui_view_ncols(v) \
	ffui_send((HWND)ffui_ctl_send(v, LVM_GETHEADER, 0, 0), HDM_GETITEMCOUNT, 0, 0)

typedef struct ffui_viewcol {
	LVCOLUMN col;
	ffsyschar text[255];
} ffui_viewcol;

static FFINL void ffui_viewcol_reset(ffui_viewcol *vc)
{
	vc->col.mask = LVCF_WIDTH;
	vc->col.cx = 100;
}

#define ffui_viewcol_settext_q(vc, sz) \
do { \
	(vc)->col.mask |= LVCF_TEXT; \
	(vc)->col.pszText = (sz); \
} while (0)

FF_EXTN void ffui_viewcol_settext(ffui_viewcol *vc, const char *text, size_t len);

#define ffui_viewcol_setwidth(vc, w)  ((vc)->col.cx = (w))

#define ffui_viewcol_setalign(vc, a) \
do { \
	(vc)->col.mask |= LVCF_FMT; \
	(vc)->col.fmt = (a); \
} while (0)

#define ffui_viewcol_setorder(vc, ord) \
do { \
	(vc)->col.mask |= LVCF_ORDER; \
	(vc)->col.iOrder = (ord); \
} while (0)

static FFINL void ffui_view_inscol(ffui_view *v, int pos, ffui_viewcol *vc)
{
	ListView_InsertColumn(v->h, pos, vc);
	ffui_viewcol_reset(vc);
}


#define ffui_view_nitems(v)  ListView_GetItemCount((v)->h)

typedef struct ffui_viewitem {
	LVITEM item;
	ffsyschar wtext[255];
	ffsyschar *w;
} ffui_viewitem;

static FFINL void ffui_view_iteminit(ffui_viewitem *it)
{
	it->item.mask = 0;
	it->w = NULL;
}

static FFINL void ffui_view_itemreset(ffui_viewitem *it)
{
	it->item.mask = 0;
	if (it->w != it->wtext && it->w != NULL) {
		ffmem_free(it->w);
		it->w = NULL;
	}
}

#define ffui_view_setindex(it, idx) \
	(it)->item.iItem = (idx)

#define ffui_view_focus(it, focus) \
do { \
	(it)->item.mask |= LVIF_STATE; \
	(it)->item.stateMask = LVIS_FOCUSED; \
	(it)->item.state = (focus) ? LVIS_FOCUSED : 0; \
} while (0)

#define ffui_view_select(it, select) \
do { \
	(it)->item.mask |= LVIF_STATE; \
	(it)->item.stateMask = LVIS_SELECTED; \
	(it)->item.state = (select) ? LVIS_SELECTED : 0; \
} while (0)

#define ffui_view_groupid(it, grp) \
do { \
	(it)->item.mask |= LVIF_GROUPID; \
	(it)->item.iGroupId = grp; \
} while (0)

#define ffui_view_setparam(it, param) \
do { \
	(it)->item.mask |= LVIF_PARAM; \
	(it)->item.lParam = (LPARAM)(param); \
} while (0)

#define ffui_view_setimg(it, img_idx) \
do { \
	(it)->item.mask |= LVIF_IMAGE; \
	(it)->item.iImage = (img_idx); \
} while (0)

#define ffui_view_settext_q(it, sz) \
do { \
	(it)->item.mask |= LVIF_TEXT; \
	(it)->item.pszText = (ffsyschar*)(sz); \
} while (0)

FF_EXTN void ffui_view_settext(ffui_viewitem *it, const char *text, size_t len);
#define ffui_view_settextz(it, sz)  ffui_view_settext(it, sz, ffsz_len(sz))
#define ffui_view_settextstr(it, str)  ffui_view_settext(it, (str)->ptr, (str)->len)

FF_EXTN int ffui_view_ins(ffui_view *v, int pos, ffui_viewitem *it);

#define ffui_view_append(v, it)  ffui_view_ins(v, ffui_view_nitems(v), it)

FF_EXTN int ffui_view_set(ffui_view *v, int sub, ffui_viewitem *it);

static FFINL int ffui_view_get(ffui_view *v, int sub, ffui_viewitem *it)
{
	it->item.iSubItem = sub;
	return (ListView_GetItem(v->h, &it->item)) ? 0 : -1;
}

#define ffui_view_param(it)  ((it)->item.lParam)

FF_EXTN int ffui_view_search(ffui_view *v, size_t by);


#define ffui_view_focused(v)  (int)ffui_ctl_send(v, LVM_GETNEXTITEM, -1, LVNI_FOCUSED)

#define ffui_view_clear(v)  ListView_DeleteAllItems((v)->h)

#define ffui_view_rm(v, item)  ListView_DeleteItem((v)->h, item)

#define ffui_view_selcount(v)  ffui_ctl_send(v, LVM_GETSELECTEDCOUNT, 0, 0)
#define ffui_view_selnext(v, from)  ffui_ctl_send(v, LVM_GETNEXTITEM, from, LVNI_SELECTED)
#define ffui_view_sel(v, i)  ListView_SetItemState((v)->h, i, LVIS_SELECTED, LVIS_SELECTED)

#define ffui_view_sort(v, func, udata)  ListView_SortItems((v)->h, func, udata)

#define ffui_view_clr_text(v, val)  ffui_send((v)->h, LVM_SETTEXTCOLOR, 0, val)
#define ffui_view_clr_bg(v, val) \
do { \
	ffui_send((v)->h, LVM_SETBKCOLOR, 0, val); \
	ffui_send((v)->h, LVM_SETTEXTBKCOLOR, 0, val); \
} while (0)


// TREEVIEW
FF_EXTN int ffui_tree_create(ffui_ctl *c, void *parent);

//insert after:
#define FFUI_TREE_FIRST  ((void*)-0x0FFFF)
#define FFUI_TREE_LAST  ((void*)-0x0FFFE)

FF_EXTN void* ffui_tree_ins_q(HWND h, void *parent, void *after, const ffsyschar *text, int img_idx);
FF_EXTN void* ffui_tree_ins(HWND h, void *parent, void *after, const char *text, size_t len, int img_idx);
#define ffui_tree_insstr(h, parent, after, str, img_idx) \
	ffui_tree_ins(h, parent, after, (str)->ptr, (str)->len, img_idx)

#define ffui_tree_remove(t, item)  ffui_ctl_send(t, TVM_DELETEITEM, 0, item)

#define ffui_tree_itemfocused(t)  ((void*)ffui_ctl_send(t, TVM_GETNEXTITEM, TVGN_CARET, NULL))

FF_EXTN char* ffui_tree_text(ffui_view *t, void *item);

#define ffui_tree_clear(t)  ffui_ctl_send(t, TVM_DELETEITEM, 0, TVI_ROOT)

#define ffui_tree_count(t)  ffui_ctl_send(t, TVM_GETCOUNT, 0, 0)

#define ffui_tree_expand(t, item)  ffui_ctl_send(t, TVM_EXPAND, TVE_EXPAND, item)
#define ffui_tree_collapse(t, item)  ffui_ctl_send(t, TVM_EXPAND, TVE_COLLAPSE, item)

#define ffui_tree_makevisible(t, item)  ffui_ctl_send(t, TVM_ENSUREVISIBLE, 0, item)

#define ffui_tree_parent(t, item)  ((void*)ffui_ctl_send(t, TVM_GETNEXTITEM, TVGN_PARENT, item))
#define ffui_tree_prev(t, item)  ((void*)ffui_ctl_send(t, TVM_GETNEXTITEM, TVGN_PREVIOUS, item))
#define ffui_tree_next(t, item)  ((void*)ffui_ctl_send(t, TVM_GETNEXTITEM, TVGN_NEXT, item))
#define ffui_tree_child(t, item)  ((void*)ffui_ctl_send(t, TVM_GETNEXTITEM, TVGN_CHILD, item))

#define ffui_tree_select(t, item)  ffui_ctl_send(t, TVM_SELECTITEM, TVGN_CARET, item)


// WINDOW
struct ffui_wnd {
	FFUI_CTL;
	HFONT font;
	uint top :1 //quit message loop if the window is closed
		, hide_on_close :1 //window doesn't get destroyed when it's closed
		, popup :1;
	byte bordstick; //stick window to screen borders

	HWND ttip;
	HWND focused; //restore focus when the window is activated again
	ffui_trayicon *trayicon;
	ffui_paned *paned[2];
	ffui_ctl *stbar;
	HACCEL acceltbl;

	void (*on_create)(ffui_wnd *wnd);
	void (*on_destroy)(ffui_wnd *wnd);
	void (*on_action)(ffui_wnd *wnd, int id);
	void (*on_dropfiles)(ffui_wnd *wnd, ffui_fdrop *df);

	uint onclose_id;
};

FF_EXTN int ffui_wnd_initstyle(void);

FF_EXTN void ffui_wnd_setpopup(ffui_wnd *w);

FF_EXTN int ffui_wndproc(ffui_wnd *wnd, size_t *code, HWND h, uint msg, size_t w, size_t l);

FF_EXTN int ffui_wnd_create(ffui_wnd *w);

#define ffui_wnd_setfront(w)  SetForegroundWindow((w)->h)

#define ffui_wnd_seticon(w, ico) \
do { \
	ffui_ctl_send(w, WM_SETICON, ICON_SMALL, ico); \
	ffui_ctl_send(w, WM_SETICON, ICON_BIG, ico); \
} while (0)

FF_EXTN void ffui_wnd_opacity(ffui_wnd *w, uint percent);

#define ffui_wnd_close(w)  ffui_ctl_send(w, WM_CLOSE, 0, 0)

FF_EXTN int ffui_wnd_destroy(ffui_wnd *w);

FF_EXTN int ffui_wnd_tooltip(ffui_wnd *w, ffui_ctl *ctl, const char *text, size_t len);

#undef FFUI_CTL

union ffui_anyctl {
	ffui_ctl *ctl;
	ffui_btn *btn;
	ffui_edit *edit;
	ffui_paned *paned;
	ffui_trkbar *trkbar;
	ffui_view *view;
	ffui_menu *menu;
};


// MESSAGE LOOP
#define ffui_quitloop()  PostQuitMessage(0)

FF_EXTN int ffui_runonce(void);

static FFINL void ffui_run(void)
{
	while (0 == ffui_runonce())
		;
}


/** Return 0 on success. */
FF_EXTN int ffui_clipbd_set(const char *s, size_t len);

FF_EXTN int ffui_clipbd_setfile(const char *const *names, size_t cnt);


enum FFUI_FOP_F {
	FFUI_FOP_ALLOWUNDO = FOF_ALLOWUNDO,
};

/** Delete a file.
@flags: enum FFUI_FOP_F */
FF_EXTN int ffui_fop_del(const char *const *names, size_t cnt, uint flags);
