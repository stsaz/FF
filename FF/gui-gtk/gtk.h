/** GUI based on GTK+.
Copyright (c) 2019 Simon Zolin
*/

#pragma once

#include <FF/array.h>
#include <FF/data/parse.h>
#include <gtk/gtk.h>


static inline void ffui_init()
{
	int argc = 0;
	char **argv = NULL;
	gtk_init(&argc, &argv);
}

#define ffui_uninit()


// ICON
// CONTROL
// MENU
// BUTTON
// LABEL
// TRACKBAR
// LISTVIEW COLUMN
// LISTVIEW ITEM
// LISTVIEW
// WINDOW
// MESSAGE LOOP


typedef struct ffui_pos {
	int x, y
		, cx, cy;
} ffui_pos;


// ICON
typedef struct ffui_icon {
	GdkPixbuf *ico;
} ffui_icon;

static inline int ffui_icon_load(ffui_icon *ico, const char *filename)
{
	ico->ico = gdk_pixbuf_new_from_file(filename, NULL);
	return (ico->ico == NULL);
}


// CONTROL

typedef struct ffui_wnd ffui_wnd;

#define _FFUI_CTL_MEMBERS \
	GtkWidget *h; \
	ffui_wnd *wnd;

typedef struct ffui_ctl {
	_FFUI_CTL_MEMBERS
} ffui_ctl;

static inline void ffui_show(void *c, uint show)
{
	if (show)
		gtk_widget_show_all(((ffui_ctl*)c)->h);
	else
		gtk_widget_hide(((ffui_ctl*)c)->h);
}

#define ffui_ctl_destroy(c)  gtk_widget_destroy(((ffui_ctl*)c)->h)


// MENU
typedef struct ffui_menu {
	_FFUI_CTL_MEMBERS
} ffui_menu;

static inline int ffui_menu_createmain(ffui_menu *m)
{
	m->h = gtk_menu_bar_new();
	return (m->h == NULL);
}

static inline int ffui_menu_create(ffui_menu *m)
{
	m->h = gtk_menu_new();
	return (m->h == NULL);
}

#define ffui_menu_new(text)  gtk_menu_item_new_with_mnemonic(text)
#define ffui_menu_newsep()  gtk_separator_menu_item_new()

static inline void ffui_menu_setsubmenu(void *mi, ffui_menu *sub, ffui_wnd *wnd)
{
	gtk_menu_item_set_submenu(mi, sub->h);
	g_object_set_data(G_OBJECT(sub->h), "ffdata", wnd);
}

FF_EXTN void ffui_menu_setcmd(void *mi, uint id);

static inline void ffui_menu_ins(ffui_menu *m, void *mi, int pos)
{
	gtk_menu_shell_insert((void*)m->h, mi, pos);
}


// BUTTON
typedef struct ffui_btn {
	_FFUI_CTL_MEMBERS
	uint action_id;
} ffui_btn;

FF_EXTN int ffui_btn_create(ffui_btn *b, ffui_wnd *parent);


// LABEL
typedef struct ffui_label {
	_FFUI_CTL_MEMBERS
} ffui_label;

FF_EXTN int ffui_lbl_create(ffui_label *l, ffui_wnd *parent);

#define ffui_lbl_settextz(l, text)  gtk_label_set_text(GTK_LABEL((l)->h), text)
static inline void ffui_lbl_settext(ffui_label *l, const char *text, size_t len)
{
	char *sz = ffsz_alcopy(text, len);
	ffui_lbl_settextz(l, sz);
	ffmem_free(sz);
}
#define ffui_lbl_settextstr(l, str)  ffui_lbl_settext(l, str->ptr, str->len)


// TRACKBAR
typedef struct ffui_trkbar {
	_FFUI_CTL_MEMBERS
	uint scroll_id;
} ffui_trkbar;

FF_EXTN int ffui_trk_create(ffui_trkbar *t, ffui_wnd *parent);

FF_EXTN void ffui_trk_setrange(ffui_trkbar *t, uint max);

FF_EXTN void ffui_trk_set(ffui_trkbar *t, uint val);

#define ffui_trk_val(t)  gtk_range_get_value(GTK_RANGE((t)->h))


// LISTVIEW COLUMN
typedef struct ffui_view ffui_view;
typedef struct ffui_viewcol {
	char *text;
	uint width;
} ffui_viewcol;

#define ffui_viewcol_reset(vc)  ffmem_free0((vc)->text)

static inline void ffui_viewcol_settext(ffui_viewcol *vc, const char *text, size_t len)
{
	vc->text = ffsz_alcopy(text, len);
}

#define ffui_viewcol_setwidth(vc, w)  (vc)->width = (w)

FF_EXTN void ffui_view_inscol(ffui_view *v, int pos, ffui_viewcol *vc);


// LISTVIEW ITEM
typedef struct ffui_viewitem {
	char *text;
	int idx;
	uint text_alloc :1;
} ffui_viewitem;

static inline void ffui_view_iteminit(ffui_viewitem *it)
{
	ffmem_tzero(it);
}

static inline void ffui_view_itemreset(ffui_viewitem *it)
{
	if (it->text_alloc) {
		ffmem_free0(it->text);
		it->text_alloc = 0;
	}
}

#define ffui_view_setindex(it, i)  (it)->idx = (i)

#define ffui_view_settextz(it, sz)  (it)->text = (sz)
static inline void ffui_view_settext(ffui_viewitem *it, const char *text, size_t len)
{
	it->text = ffsz_alcopy(text, len);
	it->text_alloc = 1;
}
#define ffui_view_settextstr(it, str)  ffui_view_settext(it, (str)->ptr, (str)->len)


// LISTVIEW
struct ffui_view {
	_FFUI_CTL_MEMBERS
	GtkTreeModel *store;
	GtkCellRenderer *rend;
	uint dblclick_id;
	uint dropfile_id;

	union {
	GtkTreePath *path;
	ffstr drop_data;
	};
};

FF_EXTN int ffui_view_create(ffui_view *v, ffui_wnd *parent);

enum FFUI_VIEW_STYLE {
	FFUI_VIEW_GRIDLINES = 1,
	FFUI_VIEW_MULTI_SELECT = 2,
};
FF_EXTN void ffui_view_style(ffui_view *v, uint flags, uint set);

#define ffui_view_nitems(v)  gtk_tree_model_iter_n_children((void*)(v)->store, NULL)

#define ffui_view_clear(v)  gtk_list_store_clear(GTK_LIST_STORE((v)->store))

#define ffui_view_selall(v)  gtk_tree_selection_select_all(gtk_tree_view_get_selection(GTK_TREE_VIEW((v)->h)))

FF_EXTN void ffui_view_dragdrop(ffui_view *v, uint action_id);

/**
Note: must be called only from wnd.on_action(). */
static inline int ffui_view_focused(ffui_view *v)
{
	int *i = gtk_tree_path_get_indices(v->path);
	return i[0];
}

FF_EXTN void ffui_view_ins(ffui_view *v, int pos, ffui_viewitem *it);

#define ffui_view_append(v, it)  ffui_view_ins(v, -1, it)

FF_EXTN void ffui_view_set(ffui_view *v, int sub, ffui_viewitem *it);

FF_EXTN void ffui_view_rm(ffui_view *v, ffui_viewitem *it);


// WINDOW
struct ffui_wnd {
	GtkWindow *h;
	GtkWidget *vbox;

	void (*on_create)(ffui_wnd *wnd);
	void (*on_destroy)(ffui_wnd *wnd);
	void (*on_action)(ffui_wnd *wnd, int id);

	uint onclose_id;
	uint hide_on_close :1;
};

static inline int ffui_wnd_initstyle()
{
	return 0;
}

FF_EXTN int ffui_wnd_create(ffui_wnd *w);

#define ffui_wnd_close(w)  gtk_window_close((w)->h)

#define ffui_wnd_destroy(w)  gtk_widget_destroy(GTK_WIDGET((w)->h))

static inline void ffui_wnd_setmenu(ffui_wnd *w, ffui_menu *m)
{
	m->wnd = w;
	gtk_box_pack_start(GTK_BOX(w->vbox), m->h, /*expand=*/0, /*fill=*/0, /*padding=*/0);
}

#define ffui_wnd_settextz(w, text)  gtk_window_set_title((w)->h, text)
static inline void ffui_wnd_settextstr(ffui_wnd *w, const ffstr *str)
{
	char *sz = ffsz_alcopystr(str);
	ffui_wnd_settextz(w, sz);
	ffmem_free(sz);
}

#define ffui_wnd_seticon(w, icon)  gtk_window_set_icon((w)->h, (icon)->ico)

static inline void ffui_wnd_setplacement(ffui_wnd *w, uint showcmd, const ffui_pos *pos)
{
	gtk_window_set_default_size(w->h, pos->cx, pos->cy);
}


// MESSAGE LOOP
#define ffui_run()  gtk_main()

#define ffui_quitloop()  gtk_main_quit()

typedef void (*ffui_handler)(void *param);

enum {
	FFUI_POST_WAIT = 1 << 31,
};

/**
flags: FFUI_POST_WAIT */
FF_EXTN void ffui_thd_post(ffui_handler func, void *udata, uint flags);

enum FFUI_MSG {
	FFUI_QUITLOOP,
	FFUI_LBL_SETTEXT,
	FFUI_WND_SETTEXT,
	FFUI_VIEW_RM,
	FFUI_VIEW_CLEAR,
	FFUI_TRK_SETRANGE,
	FFUI_TRK_SET,
};

/**
id: enum FFUI_MSG */
FF_EXTN void ffui_post(void *ctl, uint id, void *udata);
FF_EXTN void ffui_send(void *ctl, uint id, void *udata);

#define ffui_post_quitloop()  ffui_post(NULL, FFUI_QUITLOOP, NULL)
#define ffui_send_lbl_settext(ctl, sz)  ffui_send(ctl, FFUI_LBL_SETTEXT, sz)
#define ffui_send_wnd_settext(ctl, sz)  ffui_send(ctl, FFUI_WND_SETTEXT, sz)
#define ffui_send_view_rm(ctl, it)  ffui_send(ctl, FFUI_VIEW_RM, it)
#define ffui_post_view_clear(ctl)  ffui_send(ctl, FFUI_VIEW_CLEAR, NULL)
#define ffui_post_trk_setrange(ctl, range)  ffui_post(ctl, FFUI_TRK_SETRANGE, (void*)(size_t)range)
#define ffui_post_trk_set(ctl, val)  ffui_post(ctl, FFUI_TRK_SET, (void*)(size_t)val)


typedef void* (*ffui_ldr_getctl_t)(void *udata, const ffstr *name);

/** Get command ID by its name.
Return 0 if not found. */
typedef int (*ffui_ldr_getcmd_t)(void *udata, const ffstr *name);

typedef struct {
	char *fn;
} _ffui_ldr_icon;

typedef struct ffui_loader {
	ffui_ldr_getctl_t getctl;
	ffui_ldr_getcmd_t getcmd;
	void *udata;
	ffpars_ctx ctx;
	ffstr path;

	_ffui_ldr_icon ico;
	ffui_pos r;
	ffui_wnd *wnd;
	ffui_viewcol vicol;
	ffui_menu *menu;
	void *mi;
	union {
		ffui_ctl *ctl;
		ffui_label *lbl;
		ffui_trkbar *trkbar;
		ffui_view *vi;
	};

	char *errstr;
	char *wndname;
} ffui_loader;

/** Initialize GUI loader.
getctl: get a pointer to a UI element by its name.
 Most of the time you just need to call ffui_ldr_findctl() from it.
getcmd: get command ID by its name
udata: user data */
FF_EXTN void ffui_ldr_init2(ffui_loader *g, ffui_ldr_getctl_t getctl, ffui_ldr_getcmd_t getcmd, void *udata);

static inline void ffui_ldr_fin(ffui_loader *g)
{
	ffmem_safefree(g->errstr);
	ffmem_safefree0(g->wndname);
}

#define ffui_ldr_errstr(g)  ((g)->errstr)

/** Load GUI from file. */
FF_EXTN int ffui_ldr_loadfile(ffui_loader *g, const char *fn);


typedef struct ffui_ldr_ctl ffui_ldr_ctl;
struct ffui_ldr_ctl {
	const char *name;
	uint flags; //=offset
	const ffui_ldr_ctl *children;
};

#define FFUI_LDR_CTL(struct_name, ctl) \
	{ #ctl, FFOFF(struct_name, ctl), NULL }

#define FFUI_LDR_CTL3(struct_name, ctl, children) \
	{ #ctl, FFOFF(struct_name, ctl), children }

#define FFUI_LDR_CTL_END  {NULL, 0, NULL}

/** Find control by its name in structured hierarchy.
@name: e.g. "window.control" */
FF_EXTN void* ffui_ldr_findctl(const ffui_ldr_ctl *ctx, void *ctl, const ffstr *name);
