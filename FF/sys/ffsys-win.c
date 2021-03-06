/**
Copyright (c) 2020 Simon Zolin
*/

#define COBJMACROS
#include <FF/gui/winapi.h>
#include <FF/path.h>
#include <FFOS/dir.h>
#include <shlobj.h>
#include <objidl.h>
#include <shobjidl.h>
#include <shlguid.h>


static size_t arrzz_copy(ffsyschar *dst, size_t cap, const char *const *arr, size_t n);


int ffui_createlink(const char *target, const char *linkname)
{
	HRESULT r;
	IShellLink *sl = NULL;
	IPersistFile *pf = NULL;
	ffsyschar ws[255], *w = ws;

	if (0 != (r = CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IShellLinkW, (void**)&sl)))
		goto end;

	size_t n = FFCNT(ws);
	if (NULL == (w = ffs_utow(ws, &n, target, -1)))
		goto end;
	IShellLinkW_SetPath(sl, w);
	if (w != ws)
		ffmem_free(w);

	if (0 != (r = IShellLinkW_QueryInterface(sl, &IID_IPersistFile, (void**)&pf)))
		goto end;

	n = FFCNT(ws);
	if (NULL == (w = ffs_utow(ws, &n, linkname, -1)))
		goto end;
	if (0 != (r = IPersistFile_Save(pf, w, TRUE)))
		goto end;

end:
	if (w != ws)
		ffmem_free(w);
	if (sl != NULL)
		IShellLinkW_Release(sl);
	if (pf != NULL)
		IPersistFile_Release(pf);
	return r;
}

int ffui_shellexec(const char *filename, uint flags)
{
	int r;
	ffsyschar *w, ws[FF_MAXFN];
	size_t n = FFCNT(ws);
	if (NULL == (w = ffs_utow(ws, &n, filename, -1)))
		return -1;

	r = (size_t)ShellExecute(NULL, TEXT("open"), w, NULL, NULL, flags);
	if (w != ws)
		ffmem_free(w);
	if (r <= 32)
		return -1;
	return 0;
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

int ffui_clipbd_set(const char *s, size_t len)
{
	HGLOBAL glob;
	ffsyschar *buf;
	size_t n;

	n = ff_utow(NULL, 0, s, len, 0);

	if (NULL == (glob = GlobalAlloc(GMEM_SHARE | GMEM_MOVEABLE, (n + 1) * sizeof(ffsyschar))))
		return -1;
	buf = (void*)GlobalLock(glob);
	n = ff_utow(buf, n + 1, s, len, 0);
	buf[n] = '\0';
	GlobalUnlock(glob);

	if (!OpenClipboard(NULL))
		goto fail;
	EmptyClipboard();
	if (!SetClipboardData(CF_UNICODETEXT, glob))
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
	ITEMIDLIST *dir = NULL;
	int r = -1;
	ffsyschar *pathz = NULL;
	ffarr norm = {};
	ffarr sel = {}; // ITEMIDLIST*[]

	// normalize directory path
	ffstr path;
	ffstr_setz(&path, items[0]);
	if (selcnt != 0)
		ffpath_split2(path.ptr, path.len, &path, NULL);
	if (NULL == ffarr_alloc(&norm, path.len + 1))
		goto done;
	if (0 == ffpath_normstr(&norm, path, FFPATH_MERGEDOTS | FFPATH_FORCEBKSLASH)) {
		fferr_set(EINVAL);
		goto done;
	}
	*ffarr_pushT(&norm, char) = '\\';

	// get directory object
	size_t n;
	if (NULL == (pathz = ffs_utow(NULL, &n, norm.ptr, norm.len)))
		goto done;
	pathz[n] = '\0';
	if (NULL == (dir = ILCreateFromPath(pathz)))
		goto done;

	// fill array with the files to be selected
	if (selcnt == 0)
		selcnt = 1;
	if (NULL == ffarr_allocT(&sel, selcnt, ITEMIDLIST*))
		goto done;
	for (size_t i = 0;  i != selcnt;  i++) {
		if (NULL == (pathz = ffs_utow(NULL, NULL, items[i], -1)))
			goto done;
		path_backslash(pathz);

		ITEMIDLIST *dir;
		if (NULL == (dir = ILCreateFromPath(pathz)))
			goto done;
		*ffarr_pushT(&sel, ITEMIDLIST*) = dir;
		ffmem_free(pathz);
		pathz = NULL;
	}

	r = SHOpenFolderAndSelectItems(dir, selcnt, (const ITEMIDLIST **)sel.ptr, 0);

done:
	if (dir != NULL)
		ILFree(dir);

	ITEMIDLIST **it;
	FFARR_WALKT(&sel, it, ITEMIDLIST*) {
		ILFree(*it);
	}
	ffarr_free(&sel);

	ffarr_free(&norm);
	ffmem_free(pathz);
	return r;
}
