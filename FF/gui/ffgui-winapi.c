/**
Copyright (c) 2014 Simon Zolin
*/

#include <FF/gui/winapi.h>
#include <FF/path.h>
#include <FF/list.h>
#include <FFOS/file.h>
#include <FFOS/process.h>
#include <FFOS/dir.h>

#include <shlobj.h>


static int _ffui_dpi;

enum {
	_FFUI_WNDSTYLE = 1
};
static uint _ffui_flags;

static HWND create(enum FFUI_UID uid, const ffsyschar *text, HWND parent, const ffui_pos *r, uint style, uint exstyle, void *param);
static int ctl_create(ffui_ctl *c, enum FFUI_UID uid, HWND parent);
static void getpos_noscale(HWND h, ffui_pos *r);
static int setpos_noscale(void *ctl, int x, int y, int cx, int cy, int flags);
static HWND base_parent(HWND h);

static void paned_resize(ffui_paned *pn, ffui_wnd *wnd);
static void tray_nfy(ffui_wnd *wnd, ffui_trayicon *t, size_t l);

static void wnd_bordstick(uint stick, WINDOWPOS *ws);
static void wnd_cmd(ffui_wnd *wnd, uint w, HWND h);
static void wnd_nfy(ffui_wnd *wnd, NMHDR *n);
static void wnd_scroll(ffui_wnd *wnd, uint w, HWND h);
static void wnd_onaction(ffui_wnd *wnd, int id);
static LRESULT __stdcall wnd_proc(HWND h, uint msg, WPARAM w, LPARAM l);

static int process_accels(MSG *m);
static size_t arrzz_copy(ffsyschar *dst, size_t cap, const char *const *arr, size_t n);


struct ctlinfo {
	const char *stype;
	const ffsyschar *sid;
	uint style;
	uint exstyle;
};

static const struct ctlinfo ctls[] = {
	{ "",	TEXT(""), 0, 0 },
	{ "window",	TEXT("FF_WNDCLASS"), WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0 },
	{ "label",	TEXT("STATIC"), SS_NOTIFY, 0 },
	{ "editbox",	TEXT("EDIT"), ES_AUTOHSCROLL | ES_AUTOVSCROLL | ES_NOHIDESEL/* | WS_TABSTOP*/
		, WS_EX_CLIENTEDGE },
	{ "text",	TEXT("EDIT"), ES_MULTILINE | ES_AUTOHSCROLL | ES_AUTOVSCROLL | ES_NOHIDESEL | WS_HSCROLL | WS_VSCROLL
		, WS_EX_CLIENTEDGE },
	{ "button",	TEXT("BUTTON"), 0, 0 },

	{ "trackbar",	TEXT("msctls_trackbar32"), 0, 0 },
	{ "progressbar",	TEXT("msctls_progress32"), 0, 0 },
	{ "status_bar",	TEXT("msctls_statusbar32"), SBARS_SIZEGRIP, 0 },

	{ "listview",	TEXT("SysListView32"), WS_BORDER | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS
		| LVS_AUTOARRANGE | LVS_SHAREIMAGELISTS, 0 },
	{ "treeview",	TEXT("SysTreeView32"), WS_BORDER | TVS_SHOWSELALWAYS | TVS_INFOTIP, 0 },
};

#ifdef _DEBUG
static void print(const char *cmd, HWND h, size_t w, size_t l) {
	fffile_fmt(ffstdout, NULL, "%s:\th: %8xL,  w: %8xL,  l: %8xL\n"
		, cmd, (void*)h, (size_t)w, (size_t)l);
}

#else
#define print(cmd, h, w, l)
#endif


#define call_dl(name) \
do { \
	ffdl dl = ffdl_open("user32.dll", 0); \
	void (*f)() = (void(*)())ffdl_addr(dl, #name); \
	f(); \
	ffdl_close(dl); \
} while (0)

int ffui_init(void)
{
	call_dl(SetProcessDPIAware);

	{
	HDC hdc = GetDC(NULL);
	if (hdc != NULL) {
		_ffui_dpi = GetDeviceCaps(hdc, LOGPIXELSX);
		ReleaseDC(NULL, hdc);
	}
	}

	return 0;
}

void ffui_uninit(void)
{
	if (_ffui_flags & _FFUI_WNDSTYLE)
		UnregisterClass(ctls[FFUI_UID_WINDOW].sid, GetModuleHandle(NULL));
}


#define ffui_screenarea(r)  SystemParametersInfo(SPI_GETWORKAREA, 0, r, 0)

#define dpi_descale(x)  (((x) * 96) / _ffui_dpi)
#define dpi_scale(x)  (((x) * _ffui_dpi) / 96)


HIMAGELIST ffui_imglist_create(uint width, uint height)
{
	HIMAGELIST himg = ImageList_Create(dpi_scale(width), dpi_scale(height), ILC_MASK | ILC_COLOR32, 1, 0);
	return himg;
}


void ffui_font_setheight(ffui_font *fnt, int height)
{
	fnt->lf.lfHeight = -(LONG)(height * _ffui_dpi / 72);
}


static int setpos_noscale(void *ctl, int x, int y, int cx, int cy, int flags)
{
	return !SetWindowPos(((ffui_ctl*)ctl)->h, HWND_TOP, x, y, cx, cy, SWP_NOACTIVATE | flags);
}

int ffui_setpos(void *ctl, int x, int y, int cx, int cy, int flags)
{
	return !SetWindowPos(((ffui_ctl*)ctl)->h, HWND_TOP, dpi_scale(x), dpi_scale(y)
		, dpi_scale(cx), dpi_scale(cy), SWP_NOACTIVATE | flags);
}

static void getpos_noscale(HWND h, ffui_pos *r)
{
	HWND parent;
	RECT rect;
	GetWindowRect(h, &rect);
	r->cx = rect.right - rect.left;
	r->cy = rect.bottom - rect.top;
	parent = GetParent(h);

	if (parent != NULL) {
		POINT pt;
		pt.x = rect.left;
		pt.y = rect.top;
		ScreenToClient(parent, &pt);
		r->x = pt.x;
		r->y = pt.y;

	} else {
		r->x = rect.left;
		r->y = rect.top;
	}
}

void ffui_getpos(HWND h, ffui_pos *r)
{
	getpos_noscale(h, r);
	r->x = dpi_descale(r->x);
	r->y = dpi_descale(r->y);
	r->cx = dpi_descale(r->cx);
	r->cy = dpi_descale(r->cy);
}

static HWND create(enum FFUI_UID uid, const ffsyschar *text, HWND parent, const ffui_pos *r, uint style, uint exstyle, void *param)
{
	HINSTANCE inst = NULL;
	if (uid == FFUI_UID_WINDOW)
		inst = GetModuleHandle(NULL);

	return CreateWindowEx(exstyle, ctls[uid].sid, text, style
		, dpi_scale(r->x), dpi_scale(r->y), dpi_scale(r->cx), dpi_scale(r->cy)
		, parent, NULL, inst, param);
}

static int ctl_create(ffui_ctl *c, enum FFUI_UID uid, HWND parent)
{
	ffui_pos r = {0};
	c->uid = uid;
	if (0 == (c->h = create(uid, TEXT(""), parent, &r
		, ctls[uid].style | WS_CHILD
		, ctls[uid].exstyle | 0
		, NULL)))
		return 1;
	ffui_setctl(c->h, c);
	return 0;
}

int ffui_ctl_destroy(void *_c)
{
	ffui_ctl *c = _c;
	int r = 0;
	if (c->font != NULL)
		DeleteObject(c->font);
	if (c->h != NULL)
		r = !DestroyWindow(c->h);
	return r;
}

int ffui_settext(void *c, const char *text, size_t len)
{
	ffsyschar *w, ws[255];
	size_t n = FFCNT(ws) - 1;
	int r;
	if (NULL == (w = ffs_utow(ws, &n, text, len)))
		return -1;
	w[n] = '\0';
	r = ffui_settext_q(((ffui_ctl*)c)->h, w);
	if (w != ws)
		ffmem_free(w);
	return r;
}

int ffui_textstr(void *_c, ffstr *dst)
{
	ffui_ctl *c = _c;
	size_t len = ffui_ctl_send(c, WM_GETTEXTLENGTH, 0, 0);
	ffsyschar *w, ws[255];

	if (len < FFCNT(ws)) {
		w = ws;
	} else {
		if (NULL == (w = ffq_alloc(len + 1)))
			goto fail;
	}
	ffui_text_q(c->h, w, len + 1);

	dst->len = ff_wtou(NULL, 0, w, len, 0);
	if (NULL == (dst->ptr = ffmem_alloc(dst->len + 1)))
		goto fail;

	ff_wtou(dst->ptr, dst->len + 1, w, len + 1, 0);
	if (w != ws)
		ffmem_free(w);
	return (int)dst->len;

fail:
	if (w != ws)
		ffmem_free(w);
	dst->len = 0;
	return -1;
}


const char* ffui_fdrop_next(ffui_fdrop *df)
{
	uint nbuf;
	wchar_t *w, ws[255];

	nbuf = DragQueryFile(df->hdrop, df->idx, NULL, 0);
	if (nbuf == 0)
		return NULL;
	nbuf++;

	if (nbuf < FFCNT(ws))
		w = ws;
	else if (NULL == (w = ffq_alloc(nbuf)))
		return NULL;

	DragQueryFile(df->hdrop, df->idx++, w, nbuf);

	ffmem_safefree(df->fn);
	df->fn = ffsz_alcopyqz(w);
	if (w != ws)
		ffmem_free(w);
	return df->fn;
}


/** Convert '/' -> '\\' */
static void path_backslash(ffsyschar *path)
{
	uint n;
	for (n = 0;  path[n] != '\0';  n++) {
		if (path[n] == '/')
			path[n] = '\\';
	}
}

int ffui_openfolder(const char *const *items, size_t selcnt)
{
	ITEMIDLIST *dir = NULL, **sel;
	size_t i = 0, n;
	int r = -1;
	ffstr path;
	ffsyschar *pathz = NULL;

	if (selcnt != 0) {
		if (NULL == (sel = ffmem_tcalloc(ITEMIDLIST*, selcnt)))
			return -1;

		for (i = 0;  i != selcnt;  i++) {
			if (NULL == (pathz = ffs_utow(NULL, NULL, items[i], -1)))
				goto done;
			path_backslash(pathz);

			if (NULL == (sel[i] = ILCreateFromPath(pathz)))
				goto done;
			ffmem_free(pathz);
			pathz = NULL;
		}

		ffpath_split2(items[0], ffsz_len(items[0]), &path, NULL);
		if (NULL == (pathz = ffs_utow(NULL, &n, path.ptr, path.len)))
			goto done;
		pathz[n] = '\0';

	} else {
		sel = &dir;
		selcnt = 1;
		if (NULL == (pathz = ffs_utow(NULL, NULL, items[0], -1)))
			goto done;
	}

	path_backslash(pathz);
	if (NULL == (dir = ILCreateFromPath(pathz)))
		goto done;

	r = SHOpenFolderAndSelectItems(dir, selcnt, (const ITEMIDLIST **)sel, 0);

done:
	if (dir != NULL)
		ILFree(dir);
	while (i != 0) {
		ILFree(sel[--i]);
	}
	if (sel != &dir)
		ffmem_free(sel);
	ffmem_safefree(pathz);
	return r;
}


int ffui_menu_settext(ffui_menuitem *mi, const char *s, size_t len)
{
	ffsyschar *w;
	size_t n;
	if (NULL == (w = ffs_utow(NULL, &n, s, len)))
		return -1;
	w[n] = '\0';
	ffui_menu_settext_q(mi, w);
	// ffmem_free(w)
	return 0;
}

void ffui_menu_sethotkey(ffui_menuitem *mi, const char *s, size_t len)
{
	ffsyschar *w = mi->dwTypeData;
	size_t textlen = ffq_len(w);
	size_t cap = textlen + FFSLEN("\t") + len + 1;
	if (mi->dwTypeData == NULL)
		return;

	if (NULL == (w = ffmem_realloc(w, cap * sizeof(ffsyschar))))
		return;
	mi->dwTypeData = w;
	w += textlen;
	*w++ = '\t';
	ffqz_copys(w, cap, s, len);
}


void ffui_paned_create(ffui_paned *pn, ffui_wnd *parent)
{
	ffui_paned *p;

	if (parent->paned_first == FFLIST_END) {
		parent->paned_first = pn;
		return;
	}

	for (p = parent->paned_first;  p->next != FFLIST_END;  p = p->next) {
	}
	p->next = pn;
}


int ffui_stbar_create(ffui_ctl *c, ffui_wnd *parent)
{
	if (0 != ctl_create(c, FFUI_UID_STATUSBAR, parent->h))
		return 1;
	parent->stbar = c;
	return 0;
}

void ffui_stbar_settext(ffui_ctl *sb, int idx, const char *text, size_t len)
{
	ffsyschar *w, ws[255];
	size_t n = FFCNT(ws) - 1;
	if (NULL == (w = ffs_utow(ws, &n, text, len)))
		return;
	w[n] = '\0';
	ffui_stbar_settext_q(sb->h, idx, w);
	if (w != ws)
		ffmem_free(w);
}


int ffui_lbl_create(ffui_ctl *c, ffui_wnd *parent)
{
	if (0 != ctl_create(c, FFUI_UID_LABEL, parent->h))
		return 1;

	if (parent->font != NULL)
		ffui_ctl_send(c, WM_SETFONT, parent->font, 0);

	return 0;
}


int ffui_edit_create(ffui_ctl *c, ffui_wnd *parent)
{
	if (0 != ctl_create(c, FFUI_UID_EDITBOX, parent->h))
		return 1;

	if (parent->font != NULL)
		ffui_ctl_send(c, WM_SETFONT, parent->font, 0);

	return 0;
}

int ffui_text_create(ffui_ctl *c, ffui_wnd *parent)
{
	if (0 != ctl_create(c, FFUI_UID_TEXT, parent->h))
		return 1;

	if (parent->font != NULL)
		ffui_ctl_send(c, WM_SETFONT, parent->font, 0);

	return 0;
}

void ffui_edit_addtext_q(HWND h, const wchar_t *text)
{
	size_t len = ffui_send(h, WM_GETTEXTLENGTH, 0, 0);
	ffui_send(h, EM_SETSEL, len, -1);
	ffui_send(h, EM_REPLACESEL, 0 /*can undo*/, text);
}

int ffui_edit_addtext(ffui_edit *c, const char *text, size_t len)
{
	ffsyschar *w, ws[255];
	size_t n = FFCNT(ws) - 1;
	if (NULL == (w = ffs_utow(ws, &n, text, len)))
		return -1;
	w[n] = '\0';
	ffui_edit_addtext_q(c->h, w);
	if (w != ws)
		ffmem_free(w);
	return 0;
}


int ffui_btn_create(ffui_ctl *c, ffui_wnd *parent)
{
	if (0 != ctl_create(c, FFUI_UID_BUTTON, parent->h))
		return 1;

	if (parent->font != NULL)
		ffui_ctl_send(c, WM_SETFONT, parent->font, 0);

	return 0;
}


int ffui_trk_create(ffui_trkbar *t, ffui_wnd *parent)
{
	if (0 != ctl_create((ffui_ctl*)t, FFUI_UID_TRACKBAR, parent->h))
		return 1;
	return 0;
}

void ffui_trk_move(ffui_trkbar *t, uint cmd)
{
	uint pgsize = ffui_ctl_send(t, TBM_GETPAGESIZE, 0, 0);
	uint pos = ffui_trk_val(t);
	switch (cmd) {
	case FFUI_TRK_PGUP:
		pos += pgsize;
		break;
	case FFUI_TRK_PGDN:
		pos -= pgsize;
		break;
	}
	ffui_trk_set(t, pos);
}


int ffui_pgs_create(ffui_ctl *c, ffui_wnd *parent)
{
	if (0 != ctl_create(c, FFUI_UID_PROGRESSBAR, parent->h))
		return 1;
	return 0;
}


int ffui_view_create(ffui_ctl *c, ffui_wnd *parent)
{
	if (0 != ctl_create(c, FFUI_UID_LISTVIEW, parent->h))
		return 1;

	{
	int n = LVS_EX_FULLROWSELECT | LVS_EX_LABELTIP | LVS_EX_DOUBLEBUFFER;
	ListView_SetExtendedListViewStyleEx(c->h, n, n);
	}

	return 0;
}

void ffui_viewcol_settext(ffui_viewcol *vc, const char *text, size_t len)
{
	size_t n;
	if (0 == (n = ff_utow(vc->text, FFCNT(vc->text) - 1, text, len, 0))
		&& len != 0)
		return;
	vc->text[n] = '\0';
	ffui_viewcol_settext_q(vc, vc->text);
}

void ffui_view_settext(ffui_viewitem *it, const char *text, size_t len)
{
	size_t n = FFCNT(it->wtext) - 1;
	if (NULL == (it->w = ffs_utow(it->wtext, &n, text, len)))
		return;
	it->w[n] = '\0';
	ffui_view_settext_q(it, it->w);
}

int ffui_view_ins(ffui_view *v, int pos, ffui_viewitem *it)
{
	it->item.iItem = pos;
	it->item.iSubItem = 0;
	pos = ListView_InsertItem(v->h, it);
	ffui_view_itemreset(it);
	return pos;
}

int ffui_view_set(ffui_view *v, int sub, ffui_viewitem *it)
{
	int r;
	it->item.iSubItem = sub;
	r = (0 == ListView_SetItem(v->h, it));
	ffui_view_itemreset(it);
	return r;
}

int ffui_view_search(ffui_view *v, size_t by)
{
	ffui_viewitem it = {0};
	uint i;
	ffui_view_setparam(&it, 0);
	for (i = 0;  ;  i++) {
		ffui_view_setindex(&it, i);
		if (0 != ffui_view_get(v, 0, &it))
			break;
		if (by == (size_t)ffui_view_param(&it))
			return i;
	}
	return -1;
}

int ffui_view_sel_invert(ffui_view *v)
{
	uint i, cnt = 0;
	ffui_viewitem it = {0};

	for (i = 0;  ;  i++) {
		ffui_view_setindex(&it, i);
		ffui_view_select(&it, 0);
		if (0 != ffui_view_get(v, 0, &it))
			break;

		if (ffui_view_selected(&it)) {
			ffui_view_select(&it, 0);
		} else {
			ffui_view_select(&it, 1);
			cnt++;
		}

		ffui_view_set(v, 0, &it);
	}

	return cnt;
}

int ffui_view_hittest(HWND h, const ffui_point *pt, int item)
{
	LVHITTESTINFO ht = {0};
	ht.pt.x = pt->x;
	ht.pt.y = pt->y;
	ht.iItem = item;
	ListView_SubItemHitTest(h, &ht);
	return ht.iSubItem;
}

void ffui_view_edit(ffui_view *v, int i, int sub)
{
	ffui_viewitem it;

	ffui_view_iteminit(&it);
	ffui_view_setindex(&it, i);
	ffui_view_gettext(&it);
	ffui_view_get(v, sub, &it);

	ffui_edit e;
	e.h = _ffui_view_edit(v, i);
	ffui_settext_q(e.h, ffui_view_textq(&it));
	ffui_view_itemreset(&it);
	ffui_edit_selall(&e);
}


int ffui_tree_create(ffui_ctl *c, void *parent)
{
	if (0 != ctl_create(c, FFUI_UID_TREEVIEW, ((ffui_ctl*)parent)->h))
		return 1;

	{
	int n = TVS_EX_DOUBLEBUFFER;
	TreeView_SetExtendedStyle(c->h, n, n);
	}

	return 0;
}

void* ffui_tree_ins_q(HWND h, void *parent, void *after, const ffsyschar *text, int img_idx)
{
	TVINSERTSTRUCT ins = { 0 };
	ins.hParent = (HTREEITEM)parent;
	ins.hInsertAfter = (HTREEITEM)after;
	ins.item.mask = TVIF_TEXT;
	ins.item.pszText = (ffsyschar*)text;

	if (img_idx != -1) {
		ins.item.mask |= TVIF_IMAGE | TVIF_SELECTEDIMAGE;
		ins.item.iImage = img_idx;
		ins.item.iSelectedImage = img_idx;
	}

	return (void*)ffui_send(h, TVM_INSERTITEMW, 0, &ins);
}

void* ffui_tree_ins(HWND h, void *parent, void *after, const char *text, size_t len, int img_idx)
{
	ffsyschar *w, ws[255];
	size_t n = FFCNT(ws) - 1;
	void *r;
	if (NULL == (w = ffs_utow(ws, &n, text, len)))
		return NULL;
	w[n] = '\0';
	r = ffui_tree_ins_q(h, parent, after, w, img_idx);
	if (w != ws)
		ffmem_free(w);
	return r;
}

char* ffui_tree_text(ffui_view *t, void *item)
{
	ffsyschar buf[255];
	TVITEM it = {0};
	it.mask = TVIF_TEXT;
	it.pszText = buf;
	it.cchTextMax = FFCNT(buf);
	it.hItem = (HTREEITEM)item;
	if (ffui_ctl_send(t, TVM_GETITEM, 0, &it))
		return ffsz_alcopyqz(buf);
	return NULL;
}


void ffui_dlg_destroy(ffui_dialog *d)
{
	ffmem_safefree(d->names);
	ffmem_safefree(d->name);
	ffmem_safefree((void*)d->of.lpstrTitle);
	ffmem_safefree((void*)d->of.lpstrFilter);
}

/* multisel: "dir \0 name1 \0 name2 \0 \0"
   singlesel: "name \0" */
char* ffui_dlg_open(ffui_dialog *d, ffui_wnd *parent)
{
	ffsyschar *w;
	size_t cap = ((d->of.Flags & OFN_ALLOWMULTISELECT) ? 64 * 1024 : 4096);
	if (NULL == (w = ffq_alloc(cap)))
		return NULL;
	w[0] = '\0';

	d->of.hwndOwner = parent->h;
	d->of.lpstrFile = w;
	d->of.nMaxFile = cap;
	if (!GetOpenFileName(&d->of)) {
		ffmem_free(w);
		return NULL;
	}

	ffmem_safefree(d->names);
	d->names = d->pname = w;

	if ((d->of.Flags & OFN_ALLOWMULTISELECT) && w[d->of.nFileOffset - 1] == '\0') {
		d->pname += d->of.nFileOffset; //skip directory

	} else {
		d->pname += ffq_len(w); //for ffui_dlg_nextname() to return NULL
		ffmem_safefree(d->name);
		if (NULL == (d->name = ffsz_alcopyqz(w)))
			return NULL;
		return d->name;
	}

	return ffui_dlg_nextname(d);
}

char* ffui_dlg_save(ffui_dialog *d, ffui_wnd *parent, const char *fn, size_t fnlen)
{
	ffsyschar ws[4096];
	size_t n = 0;

	if (fn != NULL)
		n = ff_utow(ws, FFCNT(ws), fn, fnlen, 0);
	ws[n] = '\0';

	d->of.hwndOwner = parent->h;
	d->of.lpstrFile = ws;
	d->of.nMaxFile = FFCNT(ws);
	if (!GetSaveFileName(&d->of))
		return NULL;

	ffmem_safefree(d->name);
	if (NULL == (d->name = ffsz_alcopyqz(ws)))
		return NULL;
	return d->name;
}

char* ffui_dlg_nextname(ffui_dialog *d)
{
	size_t cap, namelen = ffq_len(d->pname);

	ffmem_safefree(d->name); //free the previous name

	if (namelen == 0) {
		ffmem_safefree(d->names);
		d->names = NULL;
		d->name = NULL;
		return NULL;
	}

	cap = d->of.nFileOffset + ff_wtou(NULL, 0, d->pname, namelen, 0) + 1;
	if (NULL == (d->name = ffmem_alloc(cap)))
		return NULL;

	ffs_fmt(d->name, d->name + cap, "%q\\%*q", d->names, namelen + 1, d->pname);
	d->pname += namelen + 1;
	return d->name;
}


int ffui_msgdlg_show(const char *title, const char *text, size_t len, uint flags)
{
	int r = -1;
	ffsyschar *w = NULL, *wtit = NULL;
	size_t n;

	if (NULL == (w = ffs_utow(NULL, &n, text, len)))
		goto done;
	w[n] = '\0';

	if (NULL == (wtit = ffs_utow(NULL, NULL, title, -1)))
		goto done;

	r = MessageBox(NULL, w, wtit, flags);

done:
	ffmem_safefree(wtit);
	ffmem_safefree(w);
	return r;
}


static uint tray_id;

enum {
	WM_USER_TRAY = WM_USER + 1000
};

void ffui_tray_create(ffui_trayicon *t, ffui_wnd *wnd)
{
	t->nid.cbSize = sizeof(NOTIFYICONDATA);
	t->nid.hWnd = wnd->h;
	t->nid.uID = tray_id++;
	t->nid.uFlags = NIF_MESSAGE;
	t->nid.uCallbackMessage = WM_USER_TRAY;
	wnd->trayicon = t;
}

static FFINL void tray_nfy(ffui_wnd *wnd, ffui_trayicon *t, size_t l)
{
	switch (l) {
	case WM_LBUTTONUP:
		if (t->lclick_id != 0)
			wnd->on_action(wnd, t->lclick_id);
		break;

	case WM_RBUTTONUP:
		if (t->pmenu != NULL) {
			ffui_point pt;
			ffui_wnd_setfront(wnd);
			ffui_cur_pos(&pt);
			ffui_menu_show(t->pmenu, pt.x, pt.y, wnd->h);
		}
		break;
	}
}


static FFINL void paned_resize(ffui_paned *pn, ffui_wnd *wnd)
{
	RECT r;
	ffui_pos cr[2];
	uint i, x = 0, y = 0, cx, cy, f;

	GetClientRect(wnd->h, &r);

	if (wnd->stbar != NULL) {
		getpos_noscale(wnd->stbar->h, &cr[0]);
		r.bottom -= cr[0].cy;
	}

	for (i = 0;  i < 2;  i++) {
		if (pn->items[i].it != NULL)
			getpos_noscale(pn->items[i].it->h, &cr[i]);
	}

	for (i = 0;  i < 2;  i++) {
		if (pn->items[i].it == NULL)
			continue;
		f = SWP_NOMOVE;

		if (pn->items[i].cx)
			cx = r.right - cr[i].x;
		else
			cx = cr[i].cx;

		if (pn->items[i].x) {
			x = r.right - cr[i].cx;
			y = cr[i].y;
			f = 0;
		}

		if (pn->items[i].cy)
			cy = r.bottom - cr[i].y;
		else
			cy = cr[i].cy;

		if (i == 0 && pn->items[0].cx && pn->items[1].it != NULL)
			cx = r.right -	cr[0].x - cr[1].cx;

		setpos_noscale(pn->items[i].it, x, y, cx, cy, f);
	}
}


int ffui_wnd_initstyle(void)
{
	WNDCLASSEX cls = {0};
	cls.cbSize = sizeof(WNDCLASSEX);
	cls.hInstance = GetModuleHandle(NULL);
	cls.lpfnWndProc = &wnd_proc;
	cls.lpszClassName = ctls[FFUI_UID_WINDOW].sid;
	cls.hIcon = LoadIcon(cls.hInstance, 0);
	cls.hIconSm = cls.hIcon;
	cls.hCursor = LoadCursor(NULL, IDC_ARROW);
	cls.hbrBackground = (HBRUSH)COLOR_WINDOW;
	if (RegisterClassEx(&cls)) {
		_ffui_flags |= _FFUI_WNDSTYLE;
		return 0;
	}
	return -1;
}

int ffui_wnd_create(ffui_wnd *w)
{
	ffui_pos r = { 0, 0, CW_USEDEFAULT, CW_USEDEFAULT };
	w->uid = FFUI_UID_WINDOW;
	w->on_action = &wnd_onaction;
	return 0 == create(FFUI_UID_WINDOW, TEXT(""), NULL, &r
		, ctls[FFUI_UID_WINDOW].style | WS_OVERLAPPEDWINDOW
		, ctls[FFUI_UID_WINDOW].exstyle | 0
		, w);
}

int ffui_wnd_destroy(ffui_wnd *w)
{
	if (w->trayicon != NULL)
		ffui_tray_show(w->trayicon, 0);

	if (w->acceltbl != NULL)
		DestroyAcceleratorTable(w->acceltbl);

	if (w->ttip != NULL)
		DestroyWindow(w->ttip);

	return ffui_ctl_destroy(w);
}

void ffui_wnd_setpopup(ffui_wnd *w)
{
	LONG st = GetWindowLong(w->h, GWL_STYLE) & ~WS_OVERLAPPEDWINDOW;
	SetWindowLong(w->h, GWL_STYLE, st | WS_POPUPWINDOW | WS_CAPTION | WS_THICKFRAME);
	w->popup = 1;
}

void ffui_wnd_opacity(ffui_wnd *w, uint percent)
{
	LONG_PTR L = GetWindowLongPtr(w->h, GWL_EXSTYLE);

	if (percent >= 100) {
		SetWindowLongPtr(w->h, GWL_EXSTYLE, L & ~WS_EX_LAYERED);
		return;
	}

	if (!(L & WS_EX_LAYERED))
		SetWindowLongPtr(w->h, GWL_EXSTYLE, L | WS_EX_LAYERED);

	SetLayeredWindowAttributes(w->h, 0, 255 * percent / 100, LWA_ALPHA);
}

int ffui_wnd_tooltip(ffui_wnd *w, ffui_ctl *ctl, const char *text, size_t len)
{
	TTTOOLINFO ti = {0};
	ffsyschar *pw, ws[255];
	size_t n = FFCNT(ws) - 1;

	if (w->ttip == NULL
		&& NULL == (w->ttip = CreateWindowEx(0, TOOLTIPS_CLASS, NULL
			, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP
			, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT
			, NULL, NULL, NULL, NULL)))
		return -1;

	if (NULL == (pw = ffs_utow(ws, &n, text, len)))
		return -1;
	pw[n] = '\0';

	ti.cbSize = sizeof(TTTOOLINFO);
	ti.uFlags = TTF_SUBCLASS | TTF_IDISHWND;
	ti.hwnd = ctl->h;
	ti.uId = (UINT_PTR)ctl->h;
	ti.lpszText = pw;
	ffui_send(w->ttip, TTM_ADDTOOL, 0, &ti);

	if (pw != ws)
		ffmem_free(pw);
	return 0;
}

static FFINL void wnd_bordstick(uint stick, WINDOWPOS *ws)
{
	RECT r;
	ffui_screenarea(&r);

	if (stick >= ffabs(r.left - ws->x))
		ws->x = r.left;
	else if (stick >= ffabs(r.right - (ws->x + ws->cx)))
		ws->x = r.right - ws->cx;

	if (stick >= ffabs(r.top - ws->y))
		ws->y = r.top;
	else if (stick >= ffabs(r.bottom - (ws->y + ws->cy)))
		ws->y = r.bottom - ws->cy;
}

static FFINL void wnd_cmd(ffui_wnd *wnd, uint w, HWND h)
{
	uint id;
	union ffui_anyctl ctl;

	if (NULL == (ctl.ctl = ffui_getctl(h)))
		return;

	switch (HIWORD(w)) {
	case BN_CLICKED:
		id = ctl.btn->action_id;
		break;

	case EN_CHANGE:
		id = ctl.edit->change_id;
		break;

	case EN_SETFOCUS:
		id = ctl.edit->focus_id;
		break;

	default:
		return;
	}

	if (id != 0)
		wnd->on_action(wnd, id);
}

static FFINL void wnd_nfy(ffui_wnd *wnd, NMHDR *n)
{
	uint id = 0;
	union ffui_anyctl ctl;

#ifdef _DEBUG
	// fffile_fmt(ffstdout, NULL, "WM_NOTIFY:\th: %8xL,  code: %8xL\n"
	// 	, (void*)n->hwndFrom, (size_t)n->code);
#endif

	if (NULL == (ctl.ctl = ffui_getctl(n->hwndFrom)))
		return;

	switch (n->code) {
	case LVN_ITEMACTIVATE:
		id = ctl.view->dblclick_id;
		break;

	case LVN_ITEMCHANGED:
		id = ctl.view->chsel_id;
		break;

	case LVN_COLUMNCLICK:
		id = ctl.view->colclick_id;
		ctl.view->col = ((NM_LISTVIEW*)n)->iSubItem;
		break;

	case LVN_ENDLABELEDIT:
		{
		const LVITEM *it = &((NMLVDISPINFO*)n)->item;
		FFDBG_PRINT(10, "LVN_ENDLABELEDIT: item:%u, text:%q\n"
			, it->iItem, (it->pszText == NULL) ? L"" : it->pszText);
		if (it->pszText != NULL && ctl.view->edit_id != 0) {
			ctl.view->text = ffsz_alcopyqz(it->pszText);
			wnd->on_action(wnd, ctl.view->edit_id);
			ffmem_free0(ctl.view->text);
		}
		}
		break;

	case NM_CLICK:
		if (ctl.ctl->uid == FFUI_UID_LISTVIEW) {
			id = ctl.view->lclick_id;
		}
		break;

	case NM_RCLICK:
		if (ctl.ctl->uid == FFUI_UID_LISTVIEW && ctl.view->pmenu != NULL) {
			ffui_point pt;
			ffui_wnd_setfront(wnd);
			ffui_cur_pos(&pt);
			ffui_menu_show(ctl.view->pmenu, pt.x, pt.y, wnd->h);
		}
		break;

	case TVN_SELCHANGED:
		id = ctl.view->chsel_id;
		break;

	default:
		return;
	}

	if (id != 0)
		wnd->on_action(wnd, id);
}

static FFINL void wnd_scroll(ffui_wnd *wnd, uint w, HWND h)
{
	union ffui_anyctl ctl;

	if (NULL == (ctl.ctl = ffui_getctl(h)))
		return;

	switch (LOWORD(w)) {
	case SB_THUMBTRACK:
	case SB_LINELEFT:
	case SB_LINERIGHT:
	case SB_PAGELEFT:
	case SB_PAGERIGHT:
		if (!ctl.trkbar->thumbtrk)
			ctl.trkbar->thumbtrk = 1;
		// break;
	case SB_THUMBPOSITION: //note: SB_ENDSCROLL isn't sent
		if (ctl.trkbar->scrolling_id != 0)
			wnd->on_action(wnd, ctl.trkbar->scrolling_id);
		break;

	case SB_ENDSCROLL:
		if (ctl.trkbar->thumbtrk)
			ctl.trkbar->thumbtrk = 0;
		if (ctl.trkbar->scroll_id != 0) {
			wnd->on_action(wnd, ctl.trkbar->scroll_id);
		}
		break;
	}
}

static void wnd_onaction(ffui_wnd *wnd, int id)
{
	(void)wnd;
	(void)id;
}

int ffui_wndproc(ffui_wnd *wnd, size_t *code, HWND h, uint msg, size_t w, size_t l)
{
	switch (msg) {

	case WM_COMMAND:
		print("WM_COMMAND", h, w, l);

		if (l == 0) { //menu
			/* HIWORD(w): 0 - msg sent by menu. 1 - msg sent by hot key */
			wnd->on_action(wnd, LOWORD(w));
			break;
		}

		wnd_cmd(wnd, (int)w, (HWND)l);
		break;

	case WM_NOTIFY:
		wnd_nfy(wnd, (NMHDR*)l);
		break;

	case WM_HSCROLL:
	case WM_VSCROLL:
		print("WM_HSCROLL", h, w, l);
		wnd_scroll(wnd, (int)w, (HWND)l);
		break;

	case WM_SYSCOMMAND:
		print("WM_SYSCOMMAND", h, w, l);

		if ((w & 0xfff0) == SC_CLOSE) {
			wnd->on_action(wnd, wnd->onclose_id);

			if (wnd->hide_on_close) {
				ffui_show(wnd, 0);
				return 1;
			}
		}
		break;

	case WM_ACTIVATE:
		print("WM_ACTIVATE", h, w, l);
		switch (w) {
		case WA_INACTIVE:
			wnd->focused = GetFocus();
			break;

		case WA_ACTIVE:
		case WA_CLICKACTIVE:
			if (wnd->focused != NULL) {
				SetFocus(wnd->focused);
				wnd->focused = NULL;
				*code = 0;
				return 1;
			}
			break;
		}
		break;

	case WM_KEYDOWN:
		print("WM_KEYDOWN", h, w, l);
		break;

	case WM_KEYUP:
		print("WM_KEYUP", h, w, l);
		break;

	case WM_SIZE:
		// print("WM_SIZE", h, w, l);
		if (l == 0)
			break; //window has been minimized

		ffui_paned *p;
		FFLIST_WALKNEXT(wnd->paned_first, p) {
			paned_resize(p, wnd);
		}

		if (wnd->stbar != NULL)
			ffui_send(wnd->stbar->h, WM_SIZE, 0, 0);
		break;

	case WM_WINDOWPOSCHANGING:
		// print("WM_WINDOWPOSCHANGING", h, w, l);
		if (wnd->bordstick != 0) {
			wnd_bordstick(dpi_scale(wnd->bordstick), (WINDOWPOS *)l);
			return 0;
		}
		break;

	case WM_DROPFILES:
		print("WM_DROPFILES", h, w, l);
		if (wnd->on_dropfiles != NULL) {
			ffui_fdrop d;
			d.hdrop = (void*)w;
			d.idx = 0;
			d.fn = NULL;
			wnd->on_dropfiles(wnd, &d);
			ffmem_safefree(d.fn);
		}
		DragFinish((void*)w);
		break;

	case WM_USER_TRAY:
		print("WM_USER_TRAY", h, w, l);

		if (wnd->trayicon != NULL && w == wnd->trayicon->nid.uID)
			tray_nfy(wnd, wnd->trayicon, l);
		break;

	case WM_CLOSE:
	//case WM_QUERYENDSESSION:
		print("WM_CLOSE", h, w, l);
		break;

	case WM_DESTROY:
		print("WM_DESTROY", h, w, l);
		if (wnd->on_destroy != NULL)
			wnd->on_destroy(wnd);

		if (wnd->top)
			ffui_quitloop();
		break;
	}

	return 0;
}

/* handle the creation of non-modal window. */
static LRESULT __stdcall wnd_proc(HWND h, uint msg, WPARAM w, LPARAM l)
{
	ffui_wnd *wnd = (void*)ffui_getctl(h);

	if (wnd != NULL) {
		size_t code = 0;
		if (0 != ffui_wndproc(wnd, &code, h, msg, w, l))
			return code;

	} else if (msg == WM_CREATE) {
		const CREATESTRUCT *cs = (void*)l;
		wnd = (void*)cs->lpCreateParams;
		wnd->h = h;
		ffui_setctl(h, wnd);

		if (wnd->on_create != NULL)
			wnd->on_create(wnd);
	}

	return DefWindowProc(h, msg, w, l);
}


/** Get handle of the window. */
static HWND base_parent(HWND h)
{
	HWND parent;
	ffui_wnd *w;

	while ((NULL == (w = ffui_getctl(h)) || w->uid != FFUI_UID_WINDOW)
		&& NULL != (parent = GetParent(h))) {
		h = parent;
	}
	return h;
}

static int process_accels(MSG *m)
{
	HWND h;
	ffui_wnd *w;

	if (m->hwnd == NULL)
		return 0;

	h = base_parent(m->hwnd);
	if (NULL != (w = ffui_getctl(h))) {
		if (w->acceltbl != NULL
			&& 0 != TranslateAccelerator(h, w->acceltbl, m))
			return 1;
	}
	return 0;
}

int ffui_runonce(void)
{
	MSG m;

	if (!GetMessage(&m, NULL, 0, 0))
		return 1;

	if (0 != process_accels(&m))
		return 0;

	TranslateMessage(&m);
	DispatchMessage(&m);
	return 0;
}


int ffui_clipbd_set(const char *s, size_t len)
{
	HGLOBAL glob;
	char *buf;

	if (NULL == (glob = GlobalAlloc(GMEM_SHARE | GMEM_MOVEABLE, len + 1)))
		return -1;
	buf = (void*)GlobalLock(glob);
	ffmemcpy(buf, s, len);
	buf[len] = '\0';
	GlobalUnlock(glob);

	if (!OpenClipboard(NULL))
		goto fail;
	EmptyClipboard();
	if (!SetClipboardData(CF_TEXT, glob))
		goto fail;
	CloseClipboard();

	return 0;

fail:
	CloseClipboard();
	GlobalFree(glob);
	return -1;
}

/** Prepare double-null terminated string array from char*[].
@dst: "el1 \0 el2 \0 \0" */
static size_t arrzz_copy(ffsyschar *dst, size_t cap, const char *const *arr, size_t n)
{
	ffsyschar *pw = dst;
	size_t i;

	if (dst == NULL) {
		cap = 0;
		for (i = 0;  i != n;  i++) {
			cap += ff_utow(NULL, 0, arr[i], -1, 0);
		}
		return cap + 1;
	}

	for (i = 0;  i != n;  i++) {
		pw += ff_utow(pw, cap - (pw - dst), arr[i], -1, 0);
	}
	*pw = '\0';
	return 0;
}

int ffui_clipbd_setfile(const char *const *names, size_t cnt)
{
	HGLOBAL glob;
	size_t cap = 0;
	struct {
		DROPFILES df;
		ffsyschar names[0];
	} *s;

	cap = arrzz_copy(NULL, 0, names, cnt);
	if (NULL == (glob = GlobalAlloc(GMEM_SHARE | GMEM_MOVEABLE, sizeof(DROPFILES) + cap * sizeof(ffsyschar))))
		return -1;

	s = (void*)GlobalLock(glob);
	ffmem_tzero(&s->df);
	s->df.pFiles = sizeof(DROPFILES);
	s->df.fWide = 1;
	arrzz_copy(s->names, cap, names, cnt);
	GlobalUnlock(glob);

	if (!OpenClipboard(NULL))
		goto fail;
	EmptyClipboard();
	if (!SetClipboardData(CF_HDROP, glob))
		goto fail;
	CloseClipboard();

	return 0;

fail:
	CloseClipboard();
	GlobalFree(glob);
	return -1;
}


int ffui_fop_del(const char *const *names, size_t cnt, uint flags)
{
	int r;
	size_t cap = 0, i;
	SHFILEOPSTRUCT fs, *f = &fs;

	ffmem_tzero(f);

	if (flags & FFUI_FOP_ALLOWUNDO) {
		for (i = 0;  i != cnt;  i++) {
			if (!ffpath_abs(names[i], ffsz_len(names[i])))
				return -1; //protect against permanently deleting files with non-absolute names
		}
	}

	cap = arrzz_copy(NULL, 0, names, cnt);
	if (NULL == (f->pFrom = ffq_alloc(cap)))
		return -1;
	arrzz_copy((void*)f->pFrom, cap, names, cnt);

	f->wFunc = FO_DELETE;
	f->fFlags = flags;
	r = SHFileOperation(f);

	ffmem_free((void*)f->pFrom);
	return r;
}
