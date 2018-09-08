/** GUI based on Windows API.
Copyright (c) 2014 Simon Zolin
*/

#pragma once

#include <FF/array.h>

#include <commctrl.h>
#include <uxtheme.h>


// HOTKEYS
// POINT
// CURSOR
// FONT
// ICON
// DIALOG
// MESSAGE DIALOG
// CONTROL
// FILE OPERATIONS
// EDITBOX
// COMBOBOX
// BUTTON
// CHECKBOX
// RADIOBUTTON
// LABEL
// IMAGE
// TRAY
// PANED
// STATUS BAR
// TRACKBAR
// PROGRESS BAR
// TAB
// LISTVIEW
// TREEVIEW
// WINDOW
// MESSAGE LOOP
// CLIPBOARD

typedef struct ffui_wnd ffui_wnd;

FF_EXTN int ffui_init(void);
FF_EXTN void ffui_uninit(void);


// HOTKEYS
typedef uint ffui_hotkey;

/** Parse hotkey string, e.g. "Ctrl+Alt+Shift+Q".
Return: low-word: char key or vkey, hi-word: control flags;  0 on error. */
FF_EXTN ffui_hotkey ffui_hotkey_parse(const char *s, size_t len);

/** Register global hotkey.
Return 0 on error. */
FF_EXTN int ffui_hotkey_register(void *ctl, ffui_hotkey hk);

/** Unregister global hotkey. */
FF_EXTN void ffui_hotkey_unreg(void *ctl, int id);


// POINT
typedef struct ffui_point {
	int x, y;
} ffui_point;

#define ffui_screen2client(ctl, pt)  ScreenToClient((ctl)->h, (POINT*)pt)


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

enum FFUI_FONT {
	FFUI_FONT_BOLD = 1,
	FFUI_FONT_ITALIC = 2,
	FFUI_FONT_UNDERLINE = 4,
};

/** Set font attributes.
flags: enum FFUI_FONT */
FF_EXTN void ffui_font_set(ffui_font *fnt, const ffstr *name, int height, uint flags);

/** Create font.
Return NULL on error. */
static FFINL HFONT ffui_font_create(ffui_font *fnt)
{
	return CreateFontIndirect(&fnt->lf);
}


typedef struct ffui_pos {
	int x, y
		, cx, cy;
} ffui_pos;

#define ffui_screenarea(r)  SystemParametersInfo(SPI_GETWORKAREA, 0, r, 0)

FF_EXTN void ffui_pos_limit(ffui_pos *r, const ffui_pos *screen);

FF_EXTN uint ffui_dpi();
FF_EXTN void ffui_dpi_set(uint dpi);
FF_EXTN int ffui_dpi_scale(int x);
FF_EXTN int ffui_dpi_descale(int x);
FF_EXTN void ffui_dpi_scalepos(ffui_pos *r);
FF_EXTN void ffui_dpi_descalepos(ffui_pos *r);


// ICON
typedef struct ffui_icon {
	HICON h;
} ffui_icon;

#define ffui_icon_destroy(ico)  DestroyIcon((ico)->h)

enum FFUI_ICON_FLAGS {
	FFUI_ICON_DPISCALE = 1,
	FFUI_ICON_SMALL = 2,
};

FF_EXTN int ffui_icon_load_q(ffui_icon *ico, const ffsyschar *filename, uint index, uint flags);
FF_EXTN int ffui_icon_load(ffui_icon *ico, const char *filename, uint index, uint flags);

/** Load icon with the specified dimensions (resize if needed).
@flags: enum FFUI_ICON_FLAGS */
FF_EXTN int ffui_icon_loadimg_q(ffui_icon *ico, const ffsyschar *filename, uint cx, uint cy, uint flags);
FF_EXTN int ffui_icon_loadimg(ffui_icon *ico, const char *filename, uint cx, uint cy, uint flags);

enum FFUI_ICON {
	_FFUI_ICON_STD = 0x10000,
	FFUI_ICON_APP = _FFUI_ICON_STD | OIC_SAMPLE,
	FFUI_ICON_ERR = _FFUI_ICON_STD | OIC_ERROR,
	FFUI_ICON_QUES = _FFUI_ICON_STD | OIC_QUES,
	FFUI_ICON_WARN = _FFUI_ICON_STD | OIC_WARNING,
	FFUI_ICON_INFO = _FFUI_ICON_STD | OIC_INFORMATION,
#if (FF_WIN >= 0x0600)
	FFUI_ICON_SHIELD = _FFUI_ICON_STD | OIC_SHIELD,
#endif

	_FFUI_ICON_IMAGERES = 0x20000,
	FFUI_ICON_FILE = _FFUI_ICON_IMAGERES | 2,
	FFUI_ICON_DIR = _FFUI_ICON_IMAGERES | 3,
};

/** Load system standard icon.
@tag: enum FFUI_ICON */
FF_EXTN int ffui_icon_loadstd(ffui_icon *ico, uint tag);


// ICON LIST
typedef struct ffui_iconlist {
	HIMAGELIST h;
} ffui_iconlist;

FF_EXTN int ffui_iconlist_create(ffui_iconlist *il, uint width, uint height);

#define ffui_iconlist_add(il, ico)  ImageList_AddIcon((il)->h, (ico)->h)

static FFINL void ffui_iconlist_addstd(ffui_iconlist *il, uint tag)
{
	ffui_icon ico;
	ffui_icon_loadstd(&ico, tag);
	ffui_iconlist_add(il, &ico);
}


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

FF_EXTN void ffui_dlg_title(ffui_dialog *d, const char *title, size_t len);

#define ffui_dlg_titlez(d, sz)  ffui_dlg_title(d, sz, ffsz_len(sz))

static FFINL void ffui_dlg_filter(ffui_dialog *d, const char *title, size_t len)
{
	ffmem_safefree((void*)d->of.lpstrFilter);
	d->of.lpstrFilter = ffs_utow(NULL, NULL, title, len);
}

/** Set filter index. */
#define ffui_dlg_nfilter(d, filter_index)  ((d)->of.nFilterIndex = (filter_index) + 1)

#define ffui_dlg_multisel(d)  ((d)->of.Flags |= OFN_ALLOWMULTISELECT)

/**
Return file name;  NULL on error. */
FF_EXTN char* ffui_dlg_open(ffui_dialog *d, ffui_wnd *parent);

/**
@fn: default filename */
FF_EXTN char* ffui_dlg_save(ffui_dialog *d, ffui_wnd *parent, const char *fn, size_t fnlen);

FF_EXTN void ffui_dlg_destroy(ffui_dialog *d);

/** Get the next file name (for a dialog with multiselect). */
FF_EXTN char* ffui_dlg_nextname(ffui_dialog *d);


// MESSAGE DIALOG
enum FFUI_MSGDLG {
	FFUI_MSGDLG_INFO = MB_ICONINFORMATION,
	FFUI_MSGDLG_WARN = MB_ICONWARNING,
	FFUI_MSGDLG_ERR = MB_ICONERROR,
};

FF_EXTN int ffui_msgdlg_show(const char *title, const char *text, size_t len, uint flags);
#define ffui_msgdlg_showz(title, text, flags)  ffui_msgdlg_show(title, text, ffsz_len(text), flags)


// CONTROL
enum FFUI_UID {
	FFUI_UID_WINDOW = 1,
	FFUI_UID_LABEL,
	FFUI_UID_IMAGE,
	FFUI_UID_EDITBOX,
	FFUI_UID_TEXT,
	FFUI_UID_COMBOBOX,
	FFUI_UID_BUTTON,
	FFUI_UID_CHECKBOX,
	FFUI_UID_RADIO,

	FFUI_UID_TRACKBAR,
	FFUI_UID_PROGRESSBAR,
	FFUI_UID_STATUSBAR,

	FFUI_UID_TAB,
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

enum FFUI_FPOS {
	FFUI_FPOS_DPISCALE = 1,
	FFUI_FPOS_CLIENT = 2,
	FFUI_FPOS_REL = 4,
};

/**
@flags: enum FFUI_FPOS */
FF_EXTN void ffui_getpos2(void *ctl, ffui_pos *r, uint flags);
#define ffui_getpos(ctl, r) \
	ffui_getpos2(ctl, r, FFUI_FPOS_DPISCALE | FFUI_FPOS_REL)

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

#define ffui_show(c, show)  ShowWindow((c)->h, (show) ? SW_SHOW : SW_HIDE)

#define ffui_redraw(c, redraw)  ffui_ctl_send(c, WM_SETREDRAW, redraw, 0)

#define ffui_setfocus(c)  SetFocus((c)->h)

FF_EXTN int ffui_ctl_destroy(void *c);

#define ffui_styleset(h, style_bit) \
	SetWindowLong(h, GWL_STYLE, GetWindowLong(h, GWL_STYLE) | (style_bit))

#define ffui_styleclear(h, style_bit) \
	SetWindowLong(h, GWL_STYLE, GetWindowLong(h, GWL_STYLE) & ~(style_bit))

/** Get parent control object. */
FF_EXTN void* ffui_ctl_parent(void *c);

FF_EXTN int ffui_ctl_setcursor(void *c, HCURSOR h);


// FILE OPERATIONS
typedef struct ffui_fdrop {
	HDROP hdrop;
	uint idx;
	char *fn;
} ffui_fdrop;

#define ffui_fdrop_accept(c, enable)  DragAcceptFiles((c)->h, enable)

FF_EXTN const char* ffui_fdrop_next(ffui_fdrop *df);


FF_EXTN int ffui_openfolder(const char *const *items, size_t selcnt);

/** Create .lnk file. */
FF_EXTN int ffui_createlink(const char *target, const char *linkname);

/**
@flags: SW_* */
FF_EXTN int ffui_shellexec(const char *filename, uint flags);


enum FFUI_FOP_F {
	FFUI_FOP_ALLOWUNDO = FOF_ALLOWUNDO,
};

/** Delete a file.
@flags: enum FFUI_FOP_F */
FF_EXTN int ffui_fop_del(const char *const *names, size_t cnt, uint flags);


// EDITBOX
typedef struct ffui_edit {
	FFUI_CTL;
	HFONT font;
	uint change_id;
	uint focus_id;
} ffui_edit;

FF_EXTN int ffui_edit_create(ffui_ctl *c, ffui_wnd *parent);
FF_EXTN int ffui_text_create(ffui_ctl *c, ffui_wnd *parent);

#define ffui_edit_password(e) \
	ffui_ctl_send(e, EM_SETPASSWORDCHAR, (wchar_t)0x25CF, 0)

#define ffui_edit_readonly(e, val) \
	ffui_ctl_send(e, EM_SETREADONLY, val, 0)

FF_EXTN void ffui_edit_addtext_q(HWND h, const wchar_t *text);
FF_EXTN int ffui_edit_addtext(ffui_edit *c, const char *text, size_t len);

#define ffui_edit_sel(e, off, n)  ffui_send((e)->h, EM_SETSEL, off, (off) + (n))
#define ffui_edit_selall(e)  ffui_send((e)->h, EM_SETSEL, 0, -1)


// COMBOBOX
typedef struct ffui_combx {
	FFUI_CTL;
	HFONT font;
	uint change_id;
	uint popup_id;
	uint edit_change_id;
	uint edit_update_id;
} ffui_combx;

FF_EXTN int ffui_combx_create(ffui_ctl *c, ffui_wnd *parent);

/** Insert an item */
static FFINL void ffui_combx_ins_q(ffui_combx *c, const ffsyschar *txt, int idx)
{
	uint msg = CB_INSERTSTRING;
	if (idx == -1) {
		idx = 0;
		msg = CB_ADDSTRING;
	}
	ffui_ctl_send(c, msg, idx, txt);
}

/** Remove item */
#define ffui_combx_rm(c, idx)  ffui_ctl_send(c, CB_DELETESTRING, idx, 0)

/** Remove all items */
#define ffui_combx_clear(c)  ffui_ctl_send(c, CB_RESETCONTENT, 0, 0)

/** Get number of items */
#define ffui_combx_count(c)  ((uint)ffui_ctl_send(c, CB_GETCOUNT, 0, 0))

/** Set/get active index */
#define ffui_combx_set(c, idx)  ffui_ctl_send(c, CB_SETCURSEL, idx, 0)
#define ffui_combx_active(c)  ((uint)ffui_ctl_send(c, CB_GETCURSEL, 0, 0))

/** Get text */
#define ffui_combx_text_q(c, idx, buf) \
	ffui_ctl_send(c, CB_GETLBTEXT, idx, buf)

FF_EXTN int ffui_combx_textstr(ffui_ctl *c, uint idx, ffstr *dst);

/** Show/hide drop down list */
#define ffui_combx_popup(c, show)  ffui_ctl_send(c, CB_SHOWDROPDOWN, val, 0)


// BUTTON
typedef struct ffui_btn {
	FFUI_CTL;
	HFONT font;
	uint action_id;
} ffui_btn;

FF_EXTN int ffui_btn_create(ffui_ctl *c, ffui_wnd *parent);


// CHECKBOX
typedef struct ffui_chbox {
	FFUI_CTL;
	HFONT font;
	uint action_id;
} ffui_chbox;

FF_EXTN int ffui_chbox_create(ffui_ctl *c, ffui_wnd *parent);

#define ffui_chbox_check(c, val)  ffui_ctl_send(c, BM_SETCHECK, val, 0)
#define ffui_chbox_checked(c)  ffui_ctl_send(c, BM_GETCHECK, 0, 0)


// RADIOBUTTON
typedef struct ffui_radio {
	FFUI_CTL;
	HFONT font;
	uint action_id;
} ffui_radio;

FF_EXTN int ffui_radio_create(ffui_ctl *c, ffui_wnd *parent);

#define ffui_radio_check(c)  ffui_chbox_check(c)
#define ffui_radio_checked(c)  ffui_chbox_checked(c)


// LABEL
typedef struct ffui_label {
	FFUI_CTL;
	HFONT font;
	HCURSOR cursor;
	uint color;
	uint click_id;
	uint click2_id;
	WNDPROC oldwndproc;
} ffui_label;

FF_EXTN int ffui_lbl_create(ffui_label *c, ffui_wnd *parent);


// IMAGE
typedef struct ffui_img {
	FFUI_CTL;
	uint click_id;
} ffui_img;

FF_EXTN int ffui_img_create(ffui_img *im, ffui_wnd *parent);

static FFINL void ffui_img_set(ffui_img *im, ffui_icon *ico)
{
	ffui_send(im->h, STM_SETIMAGE, IMAGE_ICON, ico->h);
}


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
	uint visible :1;
} ffui_trayicon;

FF_EXTN void ffui_tray_create(ffui_trayicon *t, ffui_wnd *wnd);

#define ffui_tray_visible(t)  ((t)->visible)

#define ffui_tray_settooltip(t, s, len) \
do { \
	(t)->nid.uFlags |= NIF_TIP; \
	ffqz_copys((t)->nid.szTip, FFCNT((t)->nid.szTip), s, len); \
} while (0)

#define ffui_tray_settooltipz(t, sz)  ffui_tray_settooltip(t, sz, ffsz_len(sz))

#define ffui_tray_seticon(t, ico) \
do { \
	(t)->nid.uFlags |= NIF_ICON; \
	(t)->nid.hIcon = (ico)->h; \
} while (0)

#define ffui_tray_setinfo(t, title, title_len, text, text_len) \
do { \
	(t)->nid.uFlags |= NIF_INFO; \
	(t)->nid.dwInfoFlags |= NIIF_INFO; \
	ffqz_copys((t)->nid.szInfoTitle, FFCNT((t)->nid.szInfoTitle), title, title_len); \
	ffqz_copys((t)->nid.szInfo, FFCNT((t)->nid.szInfo), text, text_len); \
} while (0)

static FFINL int ffui_tray_set(ffui_trayicon *t, uint show)
{
	return 0 == Shell_NotifyIcon(NIM_MODIFY, &t->nid);
}

FF_EXTN int ffui_tray_show(ffui_trayicon *t, uint show);


// PANED
typedef struct ffui_paned ffui_paned;
struct ffui_paned {
	struct {
		ffui_ctl *it;
		uint x :1
			, y :1
			, cx :1
			, cy :1;
	} items[2];
	ffui_paned *next;
};

FF_EXTN void ffui_paned_create(ffui_paned *pn, ffui_wnd *parent);


// STATUS BAR
typedef struct ffui_stbar {
	FFUI_CTL;
} ffui_stbar;

FF_EXTN int ffui_stbar_create(ffui_stbar *c, ffui_wnd *parent);

#define ffui_stbar_setparts(sb, n, parts)  ffui_send((sb)->h, SB_SETPARTS, n, parts)

#define ffui_stbar_settext_q(h, idx, text)  ffui_send(h, SB_SETTEXT, idx, text)
FF_EXTN void ffui_stbar_settext(ffui_stbar *sb, int idx, const char *text, size_t len);
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

#define ffui_trk_setdelta(t, delta) \
	ffui_trk_set(t, (int)ffui_trk_val(t) + delta)

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


// TAB
typedef struct ffui_tab {
	FFUI_CTL;
	HFONT font;
	int chsel_id;
} ffui_tab;

FF_EXTN int ffui_tab_create(ffui_tab *t, ffui_wnd *parent);

#define ffui_tab_active(t)  ffui_ctl_send(t, TCM_GETCURSEL, 0, 0)
#define ffui_tab_setactive(t, idx)  ffui_ctl_send(t, TCM_SETCURSEL, idx, 0)

#define ffui_tab_count(t)  ffui_ctl_send(t, TCM_GETITEMCOUNT, 0, 0)

#define ffui_tab_del(t, idx)  ffui_ctl_send(t, TCM_DELETEITEM, idx, 0)
#define ffui_tab_clear(t)  ffui_ctl_send(t, TCM_DELETEALLITEMS, 0, 0)

typedef struct ffui_tabitem {
	TCITEM item;
	ffsyschar wtext[255];
	ffsyschar *w;
} ffui_tabitem;

static FFINL void ffui_tab_reset(ffui_tabitem *it)
{
	if (it->w != it->wtext)
		ffmem_safefree(it->w);
	ffmem_tzero(it);
}

#define ffui_tab_settext_q(it, txt) \
do { \
	(it)->item.mask |= TCIF_TEXT; \
	(it)->item.pszText = txt; \
} while (0)

#define ffui_tab_gettext(it) \
do { \
	(it)->item.mask |= TCIF_TEXT; \
	(it)->item.pszText = (it)->wtext; \
	(it)->item.cchTextMax = FFCNT((it)->wtext); \
} while (0)

FF_EXTN void ffui_tab_settext(ffui_tabitem *it, const char *txt, size_t len);
#define ffui_tab_settextz(it, sz)  ffui_tab_settext(it, sz, ffsz_len(sz))

static FFINL int ffui_tab_ins(ffui_tab *t, int idx, ffui_tabitem *it)
{
	int r = ffui_ctl_send(t, TCM_INSERTITEM, idx, &it->item);
	ffui_tab_reset(it);
	return r;
}

#define ffui_tab_append(t, it)  ffui_tab_ins(t, ffui_tab_count(t), it)

static FFINL ffbool ffui_tab_set(ffui_tab *t, int idx, ffui_tabitem *it)
{
	int r = ffui_ctl_send(t, TCM_SETITEM, idx, &it->item);
	ffui_tab_reset(it);
	return r;
}

static FFINL ffbool ffui_tab_get(ffui_tab *t, int idx, ffui_tabitem *it)
{
	int r = ffui_ctl_send(t, TCM_GETITEM, idx, &it->item);
	ffui_tab_reset(it);
	return r;
}


// LISTVIEW
typedef struct ffui_view {
	FFUI_CTL;
	HFONT font;
	ffui_menu *pmenu;
	int chsel_id;
	int lclick_id;
	int dblclick_id;
	int colclick_id; //"col" is set to column #
	int edit_id; // "text" contains the text edited by user
	int check_id; //checkbox has been (un)checked

	union {
	int idx;
	int col;
	char *text;
	};
} ffui_view;

FF_EXTN int ffui_view_create(ffui_ctl *c, ffui_wnd *parent);

#define ffui_view_settheme(v)  SetWindowTheme((v)->h, L"Explorer", NULL)

/** Get index of the item by screen coordinates. */
FF_EXTN int ffui_view_hittest(ffui_view *v, const ffui_point *pt, int *subitem);

/** Get top visible item index. */
#define ffui_view_topindex(v)  ffui_ctl_send(v, LVM_GETTOPINDEX, 0, 0)

#define ffui_view_makevisible(v, idx)  ffui_ctl_send(v, LVM_ENSUREVISIBLE, idx, /*partial_ok*/ 0)
#define ffui_view_scroll(v, dx, dy)  ffui_ctl_send(v, LVM_SCROLL, dx, dy)

FF_EXTN int ffui_view_itempos(ffui_view *v, uint idx, ffui_pos *pos);


// LISTVIEW COLUMN
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

FF_EXTN void ffui_viewcol_setwidth(ffui_viewcol *vc, uint w);

#define ffui_viewcol_width(vc)  ((vc)->col.cx)

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
	(void)ListView_InsertColumn(v->h, pos, vc);
	ffui_viewcol_reset(vc);
}

#define ffui_view_delcol(v, i)  ListView_DeleteColumn((v)->h, i)

static FFINL void ffui_view_setcol(ffui_view *v, int i, ffui_viewcol *vc)
{
	(void)ListView_SetColumn(v->h, i, &vc->col);
	ffui_viewcol_reset(vc);
}

static FFINL void ffui_view_col(ffui_view *v, int i, ffui_viewcol *vc)
{
	(void)ListView_GetColumn(v->h, i, &vc->col);
}


// LISTVIEW GROUP
typedef struct ffui_viewgrp {
	LVGROUP grp;
	ffsyschar text[255];
} ffui_viewgrp;

static FFINL void ffui_viewgrp_reset(ffui_viewgrp *vg)
{
	vg->grp.cbSize = sizeof(LVGROUP);
	vg->grp.mask = LVGF_GROUPID;
}

#define ffui_viewgrp_settext_q(vg, sz) \
do { \
	(vg)->grp.mask |= LVGF_HEADER; \
	(vg)->grp.pszHeader = (sz); \
} while (0)

#if (FF_WIN >= 0x0600)
#define ffui_viewgrp_setsubtitle_q(vg, sz) \
do { \
	(vg)->grp.mask |= LVGF_SUBTITLE; \
	(vg)->grp.pszSubtitle = (sz); \
} while (0)
#endif

FF_EXTN void ffui_viewgrp_settext(ffui_viewgrp *vg, const char *text, size_t len);
#define ffui_viewgrp_settextz(c, sz)  ffui_viewgrp_settext(c, sz, ffsz_len(sz))
#define ffui_viewgrp_settextstr(c, str)  ffui_viewgrp_settext(c, (str)->ptr, (str)->len)

#define ffui_viewgrp_setcollapsible(vg, val) \
do { \
	(vg)->grp.mask |= LVGF_STATE; \
	(vg)->grp.stateMask |= LVGS_COLLAPSIBLE; \
	(vg)->grp.state |= (val) ? LVGS_COLLAPSIBLE : 0; \
} while (0)


#define ffui_view_cleargroups(v)  ffui_send((v)->h, LVM_REMOVEALLGROUPS, 0, 0)

#define ffui_view_showgroups(v, show)  ffui_send((v)->h, LVM_ENABLEGROUPVIEW, show, 0)

#if (FF_WIN >= 0x0600)
#define ffui_view_ngroups(v)  ffui_send((v)->h, LVM_GETGROUPCOUNT, 0, 0)
#endif

#define ffui_view_delgrp(v, i)  ffui_send((v)->h, LVM_REMOVEGROUP, i, 0)

static FFINL int ffui_view_insgrp(ffui_view *v, int pos, int id, ffui_viewgrp *vg)
{
	vg->grp.iGroupId = id;
	int r = ffui_send(v->h, LVM_INSERTGROUP, pos, &vg->grp);
	ffui_viewgrp_reset(vg);
	return r;
}

static FFINL void ffui_view_setgrp(ffui_view *v, int i, ffui_viewgrp *vg)
{
	ffui_send(v->h, LVM_SETGROUPINFO, i, &vg->grp);
	ffui_viewgrp_reset(vg);
}

static FFINL void ffui_view_grp(ffui_view *v, int i, ffui_viewgrp *vg)
{
	ffui_send(v->h, LVM_GETGROUPINFO, i, &vg->grp);
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

FF_EXTN void ffui_view_itemreset(ffui_viewitem *it);

#define ffui_view_setindex(it, idx) \
	(it)->item.iItem = (idx)

enum {
	_FFUI_VIEW_UNCHECKED = 0x1000,
	_FFUI_VIEW_CHECKED = 0x2000,
};

#define ffui_view_check(it, checked) \
do { \
	(it)->item.mask |= LVIF_STATE; \
	(it)->item.stateMask |= LVIS_STATEIMAGEMASK; \
	(it)->item.state &= ~LVIS_STATEIMAGEMASK; \
	(it)->item.state |= (checked) ? _FFUI_VIEW_CHECKED : _FFUI_VIEW_UNCHECKED; \
} while (0)

#define ffui_view_checked(it)  (((it)->item.state & LVIS_STATEIMAGEMASK) == _FFUI_VIEW_CHECKED)

#define ffui_view_focus(it, focus) \
do { \
	(it)->item.mask |= LVIF_STATE; \
	(it)->item.stateMask |= LVIS_FOCUSED; \
	(it)->item.state |= (focus) ? LVIS_FOCUSED : 0; \
} while (0)

#define ffui_view_select(it, select) \
do { \
	(it)->item.mask |= LVIF_STATE; \
	(it)->item.stateMask |= LVIS_SELECTED; \
	if (select) \
		(it)->item.state |= LVIS_SELECTED; \
	else \
		(it)->item.state &= ~LVIS_SELECTED; \
} while (0)

#define ffui_view_selected(it)  !!((it)->item.state & LVIS_SELECTED)

#define ffui_view_setgroupid(it, grp) \
do { \
	(it)->item.mask |= LVIF_GROUPID; \
	(it)->item.iGroupId = grp; \
} while (0)

#define ffui_view_groupid(it)  ((it)->item.iGroupId)

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

#define ffui_view_gettext(it) \
do { \
	(it)->item.mask |= LVIF_TEXT; \
	(it)->item.pszText = (it)->wtext; \
	(it)->item.cchTextMax = FFCNT((it)->wtext); \
} while (0)

#define ffui_view_textq(it)  ((it)->item.pszText)

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

FF_EXTN int ffui_view_get(ffui_view *v, int sub, ffui_viewitem *it);

#define ffui_view_param(it)  ((it)->item.lParam)

/** Find the first item associated with user data (ffui_view_param()). */
FF_EXTN int ffui_view_search(ffui_view *v, size_t by);

#define ffui_view_focused(v)  (int)ffui_ctl_send(v, LVM_GETNEXTITEM, -1, LVNI_FOCUSED)

#define ffui_view_clear(v)  ListView_DeleteAllItems((v)->h)

#define ffui_view_rm(v, item)  ListView_DeleteItem((v)->h, item)

#define ffui_view_selcount(v)  ffui_ctl_send(v, LVM_GETSELECTEDCOUNT, 0, 0)
#define ffui_view_selnext(v, from)  ffui_ctl_send(v, LVM_GETNEXTITEM, from, LVNI_SELECTED)
#define ffui_view_sel(v, i)  ListView_SetItemState((v)->h, i, LVIS_SELECTED, LVIS_SELECTED)
#define ffui_view_selall(v)  ffui_view_sel(v, -1)
FF_EXTN int ffui_view_sel_invert(ffui_view *v);

#define ffui_view_sort(v, func, udata)  ListView_SortItems((v)->h, func, udata)

#define ffui_view_clr_text(v, val)  ffui_send((v)->h, LVM_SETTEXTCOLOR, 0, val)
#define ffui_view_clr_bg(v, val) \
do { \
	ffui_send((v)->h, LVM_SETBKCOLOR, 0, val); \
	ffui_send((v)->h, LVM_SETTEXTBKCOLOR, 0, val); \
} while (0)

#define _ffui_view_edit(v, i)  ((HWND)ffui_send((v)->h, LVM_EDITLABEL, i, 0))

/** Show edit box with an item's text.
Listview must have LVS_EDITLABELS.
When editing is finished, 'ffui_view.edit_id' is sent. */
FF_EXTN HWND ffui_view_edit(ffui_view *v, uint i, uint sub);

/** Start editing a sub-item if it's under mouse cursor.
Return item index. */
FF_EXTN int ffui_view_edit_hittest(ffui_view *v, uint sub);

/** Set sub-item's text after the editing is done. */
FF_EXTN void ffui_view_edit_set(ffui_view *v, uint i, uint sub);

#define ffui_view_seticonlist(v, il)  ListView_SetImageList((v)->h, (il)->h, LVSIL_SMALL)


// TREEVIEW
FF_EXTN int ffui_tree_create(ffui_ctl *c, void *parent);

typedef struct ffui_tvitem {
	TVITEM ti;
	ffsyschar wtext[255];
	ffsyschar *w;
} ffui_tvitem;

FF_EXTN void ffui_tree_reset(ffui_tvitem *it);

#define ffui_tree_settext_q(it, textz) \
do { \
	(it)->ti.mask |= TVIF_TEXT; \
	(it)->ti.pszText = (ffsyschar*)textz; \
} while (0)

FF_EXTN void ffui_tree_settext(ffui_tvitem *it, const char *text, size_t len);
#define ffui_tree_settextstr(it, str)  ffui_tree_settext(it, (str)->ptr, (str)->len)
#define ffui_tree_settextz(it, sz)  ffui_tree_settext(it, sz, ffsz_len(sz))

#define ffui_tree_setimg(it, img_idx) \
do { \
	(it)->ti.mask |= TVIF_IMAGE | TVIF_SELECTEDIMAGE; \
	(it)->ti.iImage = (img_idx); \
	(it)->ti.iSelectedImage = (img_idx); \
} while (0)

#define ffui_tree_setparam(it, param) \
do { \
	(it)->ti.mask |= TVIF_PARAM; \
	(it)->ti.lParam = (LPARAM)(param); \
} while (0)

#define ffui_tree_setexpand(it, val) \
do { \
	(it)->ti.mask |= TVIF_STATE; \
	(it)->ti.stateMask |= TVIS_EXPANDED; \
	(it)->ti.state |= (val) ? TVIS_EXPANDED : 0; \
} while (0)

#define ffui_tree_param(it)  ((void*)(it)->ti.lParam)

//insert after:
#define FFUI_TREE_FIRST  ((void*)-0x0FFFF)
#define FFUI_TREE_LAST  ((void*)-0x0FFFE)

FF_EXTN void* ffui_tree_ins(ffui_view *v, void *parent, void *after, ffui_tvitem *it);
#define ffui_tree_append(v, parent, it) \
	ffui_tree_ins(v, parent, FFUI_TREE_LAST, it)

#define ffui_tree_remove(t, item)  ffui_ctl_send(t, TVM_DELETEITEM, 0, item)

FF_EXTN void ffui_tree_get(ffui_view *v, void *pitem, ffui_tvitem *it);

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

#define ffui_tree_focused(t)  ((void*)ffui_ctl_send(t, TVM_GETNEXTITEM, TVGN_CARET, NULL))
#define ffui_tree_select(t, item)  ffui_ctl_send(t, TVM_SELECTITEM, TVGN_CARET, item)

#define ffui_tree_seticonlist(t, il)  TreeView_SetImageList((t)->h, (il)->h, TVSIL_NORMAL)


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
	ffui_paned *paned_first;
	ffui_stbar *stbar;
	HACCEL acceltbl;
	ffarr ghotkeys; //struct wnd_ghotkey[]

	void (*on_create)(ffui_wnd *wnd);
	void (*on_destroy)(ffui_wnd *wnd);
	void (*on_action)(ffui_wnd *wnd, int id);
	void (*on_dropfiles)(ffui_wnd *wnd, ffui_fdrop *df);

	uint onclose_id;
	uint onminimize_id;
	uint onmaximize_id;
	uint onactivate_id;
};

FF_EXTN int ffui_wnd_initstyle(void);

FF_EXTN void ffui_wnd_setpopup(ffui_wnd *w);

FF_EXTN int ffui_wndproc(ffui_wnd *wnd, size_t *code, HWND h, uint msg, size_t w, size_t l);

FF_EXTN int ffui_wnd_create(ffui_wnd *w);

#define ffui_desktop(w)  (w)->h = GetDesktopWindow()

#define ffui_wnd_front(w)  (w)->h = GetForegroundWindow()
#define ffui_wnd_setfront(w)  SetForegroundWindow((w)->h)

static FFINL void ffui_wnd_icon(ffui_wnd *w, ffui_icon *big_ico, ffui_icon *small_ico)
{
	big_ico->h = (HICON)ffui_ctl_send(w, WM_GETICON, ICON_BIG, 0);
	small_ico->h = (HICON)ffui_ctl_send(w, WM_GETICON, ICON_SMALL, 0);
}

static FFINL void ffui_wnd_seticon(ffui_wnd *w, const ffui_icon *big_ico, const ffui_icon *small_ico)
{
	ffui_ctl_send(w, WM_SETICON, ICON_SMALL, small_ico->h);
	ffui_ctl_send(w, WM_SETICON, ICON_BIG, big_ico->h);
}

FF_EXTN void ffui_wnd_opacity(ffui_wnd *w, uint percent);

#define ffui_wnd_close(w)  ffui_ctl_send(w, WM_CLOSE, 0, 0)

FF_EXTN int ffui_wnd_destroy(ffui_wnd *w);

#define ffui_wnd_pos(w, pos) \
	ffui_getpos2(w, pos, FFUI_FPOS_DPISCALE)

/** Get window placement.
Return SW_*. */
FF_EXTN uint ffui_wnd_placement(ffui_wnd *w, ffui_pos *pos);

FF_EXTN void ffui_wnd_setplacement(ffui_wnd *w, uint showcmd, const ffui_pos *pos);

FF_EXTN int ffui_wnd_tooltip(ffui_wnd *w, ffui_ctl *ctl, const char *text, size_t len);

typedef struct ffui_wnd_hotkey {
	uint hk;
	uint cmd;
} ffui_wnd_hotkey;

/** Set hotkey table. */
FF_EXTN int ffui_wnd_hotkeys(ffui_wnd *w, const struct ffui_wnd_hotkey *hotkeys, size_t n);

/** Register a global hotkey. */
FF_EXTN int ffui_wnd_ghotkey_reg(ffui_wnd *w, uint hk, uint cmd);

/** Unregister all global hotkeys associated with this window. */
FF_EXTN void ffui_wnd_ghotkey_unreg(ffui_wnd *w);

#undef FFUI_CTL

union ffui_anyctl {
	ffui_ctl *ctl;
	ffui_label *lbl;
	ffui_img *img;
	ffui_btn *btn;
	ffui_edit *edit;
	ffui_combx *combx;
	ffui_paned *paned;
	ffui_trkbar *trkbar;
	ffui_stbar *stbar;
	ffui_tab *tab;
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

typedef void (*ffui_handler)(void *param);

/** Post a task to the thread running GUI message loop. */
FF_EXTN void ffui_thd_post(ffui_handler func, void *udata);


// CLIPBOARD
/** Return 0 on success. */
FF_EXTN int ffui_clipbd_set(const char *s, size_t len);

FF_EXTN int ffui_clipbd_setfile(const char *const *names, size_t cnt);
