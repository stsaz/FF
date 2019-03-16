/** Event processing for WinAPI GUI.
Copyright (c) 2019 Simon Zolin
*/

#include <FF/gui/winapi.h>


extern void paned_resize(ffui_paned *pn, ffui_wnd *wnd);
static void tray_nfy(ffui_wnd *wnd, ffui_trayicon *t, size_t l);

extern void ffui_wnd_ghotkey_call(ffui_wnd *w, uint hkid);
static void wnd_bordstick(uint stick, WINDOWPOS *ws);
static void wnd_cmd(ffui_wnd *wnd, uint w, HWND h);
static int wnd_nfy(ffui_wnd *wnd, NMHDR *n, size_t *code);
static void wnd_scroll(ffui_wnd *wnd, uint w, HWND h);


#define dpi_scale(x)  (((x) * _ffui_dpi) / 96)

#ifdef _DEBUG
static void print(const char *cmd, HWND h, size_t w, size_t l) {
	fffile_fmt(ffstdout, NULL, "%s:\th: %8xL,  w: %8xL,  l: %8xL\n"
		, cmd, (void*)h, (size_t)w, (size_t)l);
}

#else
#define print(cmd, h, w, l)
#endif


static void tray_nfy(ffui_wnd *wnd, ffui_trayicon *t, size_t l)
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

	case NIN_BALLOONUSERCLICK:
		if (t->balloon_click_id != 0)
			wnd->on_action(wnd, t->balloon_click_id);
		break;
	}
}

static void wnd_bordstick(uint stick, WINDOWPOS *ws)
{
	RECT r = _ffui_screen_area;
	if (stick >= (uint)ffabs(r.left - ws->x))
		ws->x = r.left;
	else if (stick >= (uint)ffabs(r.right - (ws->x + ws->cx)))
		ws->x = r.right - ws->cx;

	if (stick >= (uint)ffabs(r.top - ws->y))
		ws->y = r.top;
	else if (stick >= (uint)ffabs(r.bottom - (ws->y + ws->cy)))
		ws->y = r.bottom - ws->cy;
}

static void wnd_cmd(ffui_wnd *wnd, uint w, HWND h)
{
	uint id = 0, msg = HIWORD(w);
	union ffui_anyctl ctl;

	if (NULL == (ctl.ctl = ffui_getctl(h)))
		return;

	switch ((int)ctl.ctl->uid) {

	case FFUI_UID_LABEL:
		switch (msg) {
		case STN_CLICKED:
			id = ctl.lbl->click_id;
			break;
		case STN_DBLCLK:
			id = ctl.lbl->click2_id;
			break;
		}
		break;

	case FFUI_UID_BUTTON:
		switch (msg) {
		case BN_CLICKED:
			id = ctl.btn->action_id;
			break;
		}
		break;

	case FFUI_UID_EDITBOX:
	case FFUI_UID_TEXT:
		switch (msg) {
		case EN_CHANGE:
			id = ctl.edit->change_id;
			break;

		case EN_SETFOCUS:
			id = ctl.edit->focus_id;
			break;
		}
		break;

	case FFUI_UID_COMBOBOX:
		switch (msg) {
		case CBN_SELCHANGE:
			id = ctl.combx->change_id;
			break;

		case CBN_DROPDOWN:
			id = ctl.combx->popup_id;
			break;

		case CBN_CLOSEUP:
			id = ctl.combx->closeup_id;
			break;

		case CBN_EDITCHANGE:
			id = ctl.combx->edit_change_id;
			break;

		case CBN_EDITUPDATE:
			id = ctl.combx->edit_update_id;
			break;
		}
		break;
	}

	if (id != 0)
		wnd->on_action(wnd, id);
}

static int wnd_nfy(ffui_wnd *wnd, NMHDR *n, size_t *code)
{
	uint id = 0;
	union ffui_anyctl ctl;

	FFDBG_PRINTLN(10, "WM_NOTIFY:\th: %8xL,  code: %8xL"
		, (void*)n->hwndFrom, (size_t)n->code);

	if (NULL == (ctl.ctl = ffui_getctl(n->hwndFrom)))
		return 0;

	switch (n->code) {
	case LVN_ITEMACTIVATE:
		id = ctl.view->dblclick_id;
		break;

	case LVN_ITEMCHANGED: {
		NM_LISTVIEW *it = (NM_LISTVIEW*)n;
		FFDBG_PRINTLN(10, "LVN_ITEMCHANGED: item:%u.%u, state:%xu->%xu"
			, it->iItem, it->iSubItem, it->uOldState, it->uNewState);
		ctl.view->idx = it->iItem;
		if (0x3000 == ((it->uOldState & LVIS_STATEIMAGEMASK) ^ (it->uNewState & LVIS_STATEIMAGEMASK)))
			id = ctl.view->check_id;
		else if ((it->uOldState & LVIS_SELECTED) != (it->uNewState & LVIS_SELECTED))
			id = ctl.view->chsel_id;
		break;
	}

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

	case LVN_GETDISPINFO:
		if (ctl.ctl->uid == FFUI_UID_LISTVIEW
			&& ctl.view->dispinfo_id != 0) {
			NMLVDISPINFO *di = (NMLVDISPINFO*)n;
			FFDBG_PRINTLN(10, "LVN_GETDISPINFO: mask:%xu  item:%L, subitem:%L"
				, (int)di->item.mask, (size_t)di->item.iItem, (size_t)di->item.iSubItem);
			ctl.view->dispinfo_item = &di->item;
			if ((di->item.mask & LVIF_TEXT) && di->item.cchTextMax != 0)
				di->item.pszText[0] = '\0';
			wnd->on_action(wnd, ctl.view->dispinfo_id);
			*code = 1;
			return 1;
		}
		break;

	case LVN_KEYDOWN:
		if (ctl.ctl->uid == FFUI_UID_LISTVIEW) {
			if (ctl.view->dispinfo_id != 0 && ctl.view->check_id != 0) {
				NMLVKEYDOWN *kd = (void*)n;
				FFDBG_PRINTLN(10, "LVN_KEYDOWN: vkey:%xu  flags:%xu"
					, (int)kd->wVKey, (int)kd->flags);
				if (kd->wVKey == VK_SPACE) {
					ctl.view->idx = ffui_view_focused(ctl.view);
					id = ctl.view->check_id;
				}
			}
		}
		break;

	case NM_CLICK:
		if (ctl.ctl->uid == FFUI_UID_LISTVIEW) {
			id = ctl.view->lclick_id;

			if (ctl.view->dispinfo_id != 0 && ctl.view->check_id != 0) {
				ffui_point pt;
				ffui_cur_pos(&pt);
				uint f = LVHT_ONITEMSTATEICON;
				int i = ffui_view_hittest2(ctl.view, &pt, NULL, &f);
				FFDBG_PRINTLN(10, "NM_CLICK: i:%xu  f:%xu"
					, i, f);
				if (i != -1) {
					ctl.view->idx = i;
					id = ctl.view->check_id;
				}
			}
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
		FFDBG_PRINTLN(10, "TVN_SELCHANGED", 0);
		id = ctl.view->chsel_id;
		break;


	case TCN_SELCHANGING:
		FFDBG_PRINTLN(10, "TCN_SELCHANGING", 0);
		if (ctl.tab->changing_sel_id != 0) {
			wnd->on_action(wnd, ctl.tab->changing_sel_id);
			*code = ctl.tab->changing_sel_keep;
			ctl.tab->changing_sel_keep = 0;
			return 1;
		}
		break;

	case TCN_SELCHANGE:
		id = ctl.tab->chsel_id;
		break;
	}

	if (id != 0)
		wnd->on_action(wnd, id);
	return 0;
}

static void wnd_scroll(ffui_wnd *wnd, uint w, HWND h)
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
		//fallthrough
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

/*
exit
  via Close button: WM_SYSCOMMAND(SC_CLOSE) -> WM_CLOSE -> WM_DESTROY
  when user signs out: WM_QUERYENDSESSION -> WM_CLOSE
*/

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
		*code = 0;
		if (0 != wnd_nfy(wnd, (NMHDR*)l, code))
			return *code;
		if (*code != 0)
			return 1;
		break;

	case WM_HSCROLL:
	case WM_VSCROLL:
		print("WM_HSCROLL", h, w, l);
		wnd_scroll(wnd, (int)w, (HWND)l);
		break;

	case WM_SYSCOMMAND:
		print("WM_SYSCOMMAND", h, w, l);

		switch (w & 0xfff0) {
		case SC_CLOSE:
			wnd->on_action(wnd, wnd->onclose_id);

			if (wnd->hide_on_close) {
				ffui_show(wnd, 0);
				return 1;
			}
			return wnd->manual_close;

		case SC_MINIMIZE:
			if (wnd->onminimize_id != 0) {
				wnd->on_action(wnd, wnd->onminimize_id);
				return 1;
			}
			break;

		case SC_MAXIMIZE:
			wnd->on_action(wnd, wnd->onmaximize_id);
			break;
		}
		break;

	case WM_HOTKEY:
		print("WM_HOTKEY", h, w, l);
		ffui_wnd_ghotkey_call(wnd, w);
		break;

	case WM_ACTIVATE:
		print("WM_ACTIVATE", h, w, l);
		switch (w) {
		case WA_INACTIVE:
			wnd->focused = GetFocus();
			break;

		case WA_ACTIVE:
		case WA_CLICKACTIVE:
			if (wnd->onactivate_id != 0)
				wnd->on_action(wnd, wnd->onactivate_id);
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
		for (p = wnd->paned_first;  p != NULL;  p = p->next) {
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

	case WM_PAINT:
		if (wnd->on_paint != NULL)
			wnd->on_paint(wnd);
		break;

	case WM_ERASEBKGND:
		if (wnd->bgcolor != NULL) {
			HDC hdc = (HDC)w;
			RECT rect;
			GetClientRect(h, &rect);
			FillRect(hdc, &rect, wnd->bgcolor);
			*code = 1;
			return 1;
		}
		break;

	case WM_CTLCOLORBTN:
	case WM_CTLCOLORLISTBOX:
	case WM_CTLCOLORSCROLLBAR:
	case WM_CTLCOLORSTATIC: {
		union ffui_anyctl c;
		c.ctl = ffui_getctl((HWND)l);
		HDC hdc = (HDC)w;
		HBRUSH br = wnd->bgcolor;
		ffbool result = 0;
		if (c.ctl != NULL && c.ctl->uid == FFUI_UID_LABEL && c.lbl->color != 0) {
			SetTextColor(hdc, c.lbl->color);
			result = 1;
		}

		if (result || br != NULL) {
			SetBkMode(hdc, TRANSPARENT);
			if (br == NULL)
				br = GetSysColorBrush(COLOR_BTNFACE);
			*code = (size_t)br;
			return 1;
		}
		break;
	}

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

	case FFUI_WM_USER_TRAY:
		print("FFUI_WM_USER_TRAY", h, w, l);

		if (wnd->trayicon != NULL && w == wnd->trayicon->nid.uID)
			tray_nfy(wnd, wnd->trayicon, l);
		break;

	case WM_CLOSE:
		print("WM_CLOSE", h, w, l);
		break;

	case WM_QUERYENDSESSION:
		print("WM_QUERYENDSESSION", h, w, l);
		wnd->on_action(wnd, wnd->onclose_id);
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
