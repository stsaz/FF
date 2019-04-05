/**
Copyright (c) 2019 Simon Zolin
*/

#include <FF/gui-gtk/gtk.h>
#include <FFOS/atomic.h>


// MENU
// BUTTON
// LABEL
// LISTVIEW
// WINDOW
// MESSAGE LOOP


#define sig_disable(h, func, udata) \
	g_signal_handlers_disconnect_matched(h, G_SIGNAL_MATCH_FUNC | G_SIGNAL_MATCH_DATA, 0, 0, 0, G_CALLBACK(func), udata)


// MENU
static void _ffui_menu_activate(GtkWidget *mi, gpointer udata)
{
	GtkWidget *parent_menu = gtk_widget_get_parent(mi);
	ffui_wnd *wnd = g_object_get_data(G_OBJECT(parent_menu), "ffdata");
	uint id = (size_t)udata;
	wnd->on_action(wnd, id);
}

void ffui_menu_setcmd(void *mi, uint id)
{
	g_signal_connect(mi, "activate", G_CALLBACK(&_ffui_menu_activate), (void*)(size_t)id);
}


// BUTTON
static void _ffui_btn_clicked(GtkWidget *widget, gpointer udata)
{
	ffui_btn *b = udata;
	b->wnd->on_action(b->wnd, b->action_id);
}

int ffui_btn_create(ffui_btn *b, ffui_wnd *parent)
{
	b->h = gtk_button_new_with_label("");
	b->wnd = parent;
	g_signal_connect(b->h, "clicked", G_CALLBACK(&_ffui_btn_clicked), b);
	gtk_box_pack_start(GTK_BOX(parent->vbox), b->h, /*expand=*/0, /*fill=*/0, /*padding=*/0);
	return 0;
}


// LABEL
int ffui_lbl_create(ffui_label *l, ffui_wnd *parent)
{
	l->h = gtk_label_new("");
	l->wnd = parent;
	gtk_box_pack_start(GTK_BOX(parent->vbox), l->h, /*expand=*/0, /*fill=*/0, /*padding=*/0);
	return 0;
}


// TRACKBAR
static void _ffui_trk_value_changed(GtkWidget *widget, gpointer udata)
{
	ffui_trkbar *t = udata;
	t->wnd->on_action(t->wnd, t->scroll_id);
}

int ffui_trk_create(ffui_trkbar *t, ffui_wnd *parent)
{
	t->h = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
	gtk_scale_set_draw_value(GTK_SCALE(t->h), 0);
	t->wnd = parent;
	gtk_box_pack_start(GTK_BOX(parent->vbox), t->h, /*expand=*/0, /*fill=*/0, /*padding=*/0);
	g_signal_connect(t->h, "value-changed", G_CALLBACK(&_ffui_trk_value_changed), t);
	return 0;
}

void ffui_trk_setrange(ffui_trkbar *t, uint max)
{
	sig_disable(t->h, &_ffui_trk_value_changed, t);
	gtk_range_set_range(GTK_RANGE((t)->h), 0, max);
	g_signal_connect(t->h, "value-changed", G_CALLBACK(&_ffui_trk_value_changed), t);
}

void ffui_trk_set(ffui_trkbar *t, uint val)
{
	sig_disable(t->h, &_ffui_trk_value_changed, t);
	gtk_range_set_value(GTK_RANGE(t->h), val);
	g_signal_connect(t->h, "value-changed", G_CALLBACK(&_ffui_trk_value_changed), t);
}


// LISTVIEW
static void _ffui_view_row_activated(void *a, GtkTreePath *path, void *c, gpointer udata)
{
	FFDBG_PRINTLN(10, "udata: %p", udata);
	ffui_view *v = udata;
	v->path = path;
	v->wnd->on_action(v->wnd, v->dblclick_id);
	v->path = NULL;
}

static void _ffui_view_drag_data_received(GtkWidget *wgt, GdkDragContext *context, int x, int y,
	GtkSelectionData *seldata, guint info, guint time, gpointer userdata)
{
	gint len;
	const void *ptr = gtk_selection_data_get_data_with_length(seldata, &len);
	FFDBG_PRINTLN(10, "seldata:[%u] %*s", len, (size_t)len, ptr);
	ffui_view *v = userdata;
	ffstr_set(&v->drop_data, ptr, len);
	v->wnd->on_action(v->wnd, v->dropfile_id);
	ffstr_null(&v->drop_data);
}

int ffui_view_create(ffui_view *v, ffui_wnd *parent)
{
	v->h = gtk_tree_view_new();
	g_signal_connect(v->h, "row-activated", G_CALLBACK(&_ffui_view_row_activated), v);
	v->wnd = parent;
	v->rend = gtk_cell_renderer_text_new();
	GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
	gtk_container_add(GTK_CONTAINER(scroll), v->h);
	gtk_box_pack_start(GTK_BOX(parent->vbox), scroll, /*expand=*/1, /*fill=*/1, /*padding=*/0);
	return 0;
}

void ffui_view_style(ffui_view *v, uint flags, uint set)
{
	uint val;

	if (flags & FFUI_VIEW_GRIDLINES) {
		val = (set & FFUI_VIEW_GRIDLINES)
			? GTK_TREE_VIEW_GRID_LINES_BOTH
			: GTK_TREE_VIEW_GRID_LINES_NONE;
		gtk_tree_view_set_grid_lines(GTK_TREE_VIEW(v->h), val);
	}

	if (flags & FFUI_VIEW_MULTI_SELECT) {
		val = (set & FFUI_VIEW_MULTI_SELECT)
			? GTK_SELECTION_MULTIPLE
			: GTK_SELECTION_SINGLE;
		gtk_tree_selection_set_mode(gtk_tree_view_get_selection(GTK_TREE_VIEW(v->h)), val);
	}
}

void ffui_view_inscol(ffui_view *v, int pos, ffui_viewcol *vc)
{
	FFDBG_PRINTLN(10, "pos:%d", pos);
	FF_ASSERT(v->store == NULL);

	if (pos == -1)
		pos = gtk_tree_view_get_n_columns(GTK_TREE_VIEW(v->h));

	GtkTreeViewColumn *col = gtk_tree_view_column_new();
	gtk_tree_view_column_set_title(col, vc->text);
	gtk_tree_view_column_set_resizable(col, 1);
	if (vc->width != 0)
		gtk_tree_view_column_set_fixed_width(col, vc->width);

	gtk_tree_view_column_pack_start(col, v->rend, 1);
	gtk_tree_view_column_add_attribute(col, v->rend, "text", pos);

	gtk_tree_view_insert_column(GTK_TREE_VIEW(v->h), col, pos);
	ffui_viewcol_reset(vc);
}

static void view_prepare(ffui_view *v)
{
	uint ncol = gtk_tree_view_get_n_columns(GTK_TREE_VIEW(v->h));
	GType *types = ffmem_allocT(ncol, GType);
	for (uint i = 0;  i != ncol;  i++) {
		types[i] = G_TYPE_STRING;
	}
	v->store = (void*)gtk_list_store_newv(ncol, types);
	ffmem_free(types);
	gtk_tree_view_set_model(GTK_TREE_VIEW(v->h), v->store);
	g_object_unref(v->store);
}

void ffui_view_dragdrop(ffui_view *v, uint action_id)
{
	if (v->store == NULL)
		view_prepare(v);

	static const GtkTargetEntry ents[] = {
		{ "text/plain", GTK_TARGET_OTHER_APP, 0 }
	};
	gtk_drag_dest_set(v->h, GTK_DEST_DEFAULT_ALL, ents, FFCNT(ents), GDK_ACTION_COPY);
	g_signal_connect(v->h, "drag_data_received", G_CALLBACK(_ffui_view_drag_data_received), v);
	v->dropfile_id = action_id;
}

void ffui_view_ins(ffui_view *v, int pos, ffui_viewitem *it)
{
	FFDBG_PRINTLN(10, "pos:%d", pos);
	if (v->store == NULL)
		view_prepare(v);

	GtkTreeIter iter;
	if (pos == -1) {
		it->idx = ffui_view_nitems(v);
		gtk_list_store_append(GTK_LIST_STORE(v->store), &iter);
	} else {
		it->idx = pos;
		gtk_list_store_insert(GTK_LIST_STORE(v->store), &iter, pos);
	}
	gtk_list_store_set(GTK_LIST_STORE(v->store), &iter, 0, it->text, -1);
	ffui_view_itemreset(it);
}

void ffui_view_set(ffui_view *v, int sub, ffui_viewitem *it)
{
	FFDBG_PRINTLN(10, "sub:%d", sub);
	GtkTreeIter iter;
	if (gtk_tree_model_iter_nth_child(GTK_TREE_MODEL(v->store), &iter, NULL, it->idx))
		gtk_list_store_set(GTK_LIST_STORE(v->store), &iter, sub, it->text, -1);
	ffui_view_itemreset(it);
}

void ffui_view_rm(ffui_view *v, ffui_viewitem *it)
{
	FFDBG_PRINTLN(10, "idx:%d", it->idx);
	GtkTreeIter iter;
	if (gtk_tree_model_iter_nth_child(GTK_TREE_MODEL(v->store), &iter, NULL, it->idx))
		gtk_list_store_remove(GTK_LIST_STORE(v->store), &iter);
}


// WINDOW
static void _ffui_wnd_onclose(void *a, void *b, gpointer udata)
{
	ffui_wnd *wnd = udata;
	if (wnd->hide_on_close) {
		ffui_show(wnd, 0);
		return;
	}
	wnd->on_action(wnd, wnd->onclose_id);
}

int ffui_wnd_create(ffui_wnd *w)
{
	w->h = (void*)gtk_window_new(GTK_WINDOW_TOPLEVEL);
	w->vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_container_add(GTK_CONTAINER(w->h), w->vbox);
	g_signal_connect(w->h, "delete-event", G_CALLBACK(&_ffui_wnd_onclose), w);
	return 0;
}


// MESSAGE LOOP

struct cmd {
	ffui_handler func;
	void *udata;
	uint id;
	uint ref;
};

static gboolean _ffui_thd_func(gpointer data)
{
	struct cmd *c = data;
	FFDBG_PRINTLN(10, "func:%p  udata:%p", c->func, c->udata);
	c->func(c->udata);
	if (c->ref != 0) {
		ffatom_fence_acq_rel();
		FF_WRITEONCE(c->ref, 0);
	} else
		ffmem_free(c);
	return 0;
}

void ffui_thd_post(ffui_handler func, void *udata, uint id)
{
	FFDBG_PRINTLN(10, "func:%p  udata:%p  id:%xu", func, udata, id);

	if (id & FFUI_POST_WAIT) {
		struct cmd c;
		c.func = func;
		c.udata = udata;
		c.ref = 1;
		ffatom_fence_rel();

		if (0 != gdk_threads_add_idle(&_ffui_thd_func, &c)) {
			ffatom_waitchange(&c.ref, 1);
		}
		return;
	}

	struct cmd *c = ffmem_new(struct cmd);
	c->func = func;
	c->udata = udata;

	if (0 != gdk_threads_add_idle(&_ffui_thd_func, c)) {
	}
}

struct cmd_send {
	void *ctl;
	void *udata;
	uint id;
	uint ref;
};

static gboolean _ffui_send_handler(gpointer data)
{
	struct cmd_send *c = data;
	switch ((enum FFUI_MSG)c->id) {
	case FFUI_QUITLOOP:
		ffui_quitloop();
		break;
	case FFUI_LBL_SETTEXT:
		ffui_lbl_settextz((ffui_label*)c->ctl, c->udata);
		break;
	case FFUI_WND_SETTEXT:
		ffui_wnd_settextz((ffui_wnd*)c->ctl, c->udata);
		break;
	case FFUI_VIEW_RM:
		ffui_view_rm((ffui_view*)c->ctl, c->udata);
		break;
	case FFUI_VIEW_CLEAR:
		ffui_view_clear((ffui_view*)c->ctl);
		break;
	case FFUI_TRK_SETRANGE:
		ffui_trk_setrange((ffui_trkbar*)c->ctl, (size_t)c->udata);
		break;
	case FFUI_TRK_SET:
		ffui_trk_set((ffui_trkbar*)c->ctl, (size_t)c->udata);
		break;
	}

	if (c->ref != 0) {
		ffatom_fence_acq_rel();
		FF_WRITEONCE(c->ref, 0);
	} else {
		ffmem_free(c);
	}
	return 0;
}

static uint quit;
static fflock quit_lk;

static int post_locked(gboolean (*func)(gpointer), void *udata)
{
	int r = 0;
	fflk_lock(&quit_lk);
	if (!quit)
		r = gdk_threads_add_idle(func, udata);
	fflk_unlock(&quit_lk);
	return r;
}

void ffui_send(void *ctl, uint id, void *udata)
{
	FFDBG_PRINTLN(10, "ctl:%p  udata:%p  id:%xu", ctl, udata, id);
	struct cmd_send c;
	c.ctl = ctl;
	c.id = id;
	c.udata = udata;
	c.ref = 1;
	ffatom_fence_rel();

	if (0 != post_locked(&_ffui_send_handler, &c))
		ffatom_waitchange(&c.ref, 1);
}

void ffui_post(void *ctl, uint id, void *udata)
{
	FFDBG_PRINTLN(10, "ctl:%p  udata:%p  id:%xu", ctl, udata, id);
	struct cmd_send *c = ffmem_new(struct cmd_send);
	c->ctl = ctl;
	c->id = id;
	c->udata = udata;
	ffatom_fence_rel();

	if (id == FFUI_QUITLOOP) {
		fflk_lock(&quit_lk);
		if (0 == gdk_threads_add_idle(&_ffui_send_handler, c))
			ffmem_free(c);
		quit = 1;
		fflk_unlock(&quit_lk);
		return;
	}

	if (0 == post_locked(&_ffui_send_handler, c))
		ffmem_free(c);
}
