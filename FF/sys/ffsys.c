/**
Copyright (c) 2014 Simon Zolin
*/

#include <FFOS/time.h>
#include <FFOS/file.h>
#include <FF/data/parse.h>
#include <FF/sys/filemap.h>
#include <FF/sys/taskqueue.h>
#include <FF/sys/dir.h>
#include <FF/path.h>
#include <FF/number.h>
#include <FFOS/process.h>
#include <FFOS/error.h>


static const char s_fmode[] = ".pc.d.b.-.l.s...";
static const char s_fperm_set[] = "rwxrwxrwx";

uint fffile_unixattr_tostr(char *dst, size_t cap, uint mode)
{
	if (cap < 10)
		return 0;

	uint n;
	n = (mode & FFUNIX_FILE_TYPEMASK) >> 12;
	*dst++ = s_fmode[n];

	ffs_fill(dst, dst + cap, '-', 9);
	n = mode & 0777;
	while (n != 0) {
		uint i = ffbit_ffs32(n) - 1;
		dst[8 - i] = s_fperm_set[8 - i];
		ffbit_reset32(&n, i);
	}
	return 10;
}

size_t fffile_fmt(fffd fd, ffarr *buf, const char *fmt, ...)
{
	size_t r;
	va_list args;
	ffarr dst = {0};

	if (buf == NULL) {
		if (NULL == ffarr_realloc(&dst, 1024))
			return 0;
		buf = &dst;
	}
	else
		buf->len = 0;

	va_start(args, fmt);
	r = ffstr_catfmtv(buf, fmt, args);
	va_end(args);

	if (r != 0)
		r = fffile_write(fd, buf->ptr, r);

	ffarr_free(&dst);
	return r;
}

int fffile_cmp(const char *fn1, const char *fn2, uint64 limit)
{
	int r;
	ssize_t n;
	fffd f1 = FF_BADFD, f2 = FF_BADFD;
	ffarr a1 = {}, a2 = {};
	if (NULL == ffarr_alloc(&a1, 64 * 1024)
		|| NULL == ffarr_alloc(&a2, 64 * 1024)) {
		r = -2;
		goto end;
	}

	f1 = fffile_open(fn1, O_RDONLY);
	f2 = fffile_open(fn2, O_RDONLY);
	if (f1 == FF_BADFD || f2 == FF_BADFD) {
		r = -2;
		goto end;
	}

	if (limit == 0)
		limit = (uint64)-1;

	for (;;) {
		n = fffile_read(f1, a1.ptr, a1.cap);
		if (n < 0) {
			r = -2;
			break;
		}
		a1.len = n;

		n = fffile_read(f2, a2.ptr, a2.cap);
		if (n < 0) {
			r = -2;
			break;
		}
		a2.len = n;

		ffint_setmin(a1.len, limit);
		ffint_setmin(a2.len, limit);
		limit -= a1.len;

		r = ffstr_cmp2((ffstr*)&a1, (ffstr*)&a2);
		if (r != 0
			|| a1.len == 0)
			break;
	}

end:
	ffarr_free(&a1);
	ffarr_free(&a2);
	fffile_safeclose(f1);
	fffile_safeclose(f2);
	return r;
}


void fffile_mapclose(fffilemap *fm)
{
	if (fm->map != NULL)
		ffmap_unmap(fm->map, fm->mapsz);
	if (fm->hmap != 0)
		ffmap_close(fm->hmap);
	fffile_mapinit(fm);
}

int fffile_mapbuf(fffilemap *fm, ffstr *dst)
{
	size_t off = fm->foff & (fm->blocksize - 1);

	if (fm->map == NULL) {
		uint64 effoffs;
		size_t size = (size_t)ffmin64(fm->blocksize, off + fm->fsize);

		if (fm->hmap == 0) {
			fm->hmap = ffmap_create(fm->fd, 0, FFMAP_PAGEREAD);
			if (fm->hmap == 0)
				return 1;
		}

		effoffs = ff_align_floor2(fm->foff, fm->blocksize);
		fm->map = ffmap_open(fm->hmap, effoffs, size, PROT_READ, MAP_SHARED);
		if (fm->map == NULL)
			return 1;
		fm->mapoff = effoffs;
		fm->mapsz = size;
	}

	dst->ptr = fm->map + off;
	dst->len = (size_t)ffmin64(fm->mapsz - off, fm->fsize);
	return 0;
}

int fffile_mapshift(fffilemap *fm, int64 by)
{
	FF_ASSERT(by >= 0 && fm->fsize >= (uint64)by);
	fm->fsize -= by;
	fm->foff += by;

	if (fm->fsize != 0) {
		if (fm->map != NULL
			&& fm->foff >= fm->mapoff + fm->mapsz) {

			ffmap_unmap(fm->map, fm->mapsz);
			fm->mapoff = 0;
			fm->mapsz = 0;
			fm->map = NULL;
		}
		return 1;
	}

	fffile_mapclose(fm);
	return 0;
}


uint fftask_post(fftaskmgr *mgr, fftask *task)
{
	uint r = 0;

	fflk_lock(&mgr->lk);
	if (fftask_active(mgr, task))
		goto done;
	r = fflist_empty(&mgr->tasks);
	fflist_ins(&mgr->tasks, &task->sib);
done:
	fflk_unlock(&mgr->lk);
	return r;
}

void fftask_del(fftaskmgr *mgr, fftask *task)
{
	fflk_lock(&mgr->lk);
	if (!fftask_active(mgr, task))
		goto done;
	fflist_rm(&mgr->tasks, &task->sib);
done:
	fflk_unlock(&mgr->lk);
}

uint fftask_run(fftaskmgr *mgr)
{
	fflist_item *it, *sentl = fflist_sentl(&mgr->tasks);
	uint n, ntasks;

	for (n = mgr->max_run;  n != 0;  n--) {

		it = FF_READONCE(fflist_first(&mgr->tasks));
		if (it == sentl)
			break; //list is empty

		fflk_lock(&mgr->lk);
		FF_ASSERT(mgr->tasks.len != 0);
		_ffchain_link2(it->prev, it->next);
		it->next = NULL;
		ntasks = mgr->tasks.len--;
		fflk_unlock(&mgr->lk);

		fftask *task = FF_GETPTR(fftask, sib, it);

		(void)ntasks;
		FFDBG_PRINTLN(10, "[%L] %p handler=%p, param=%p"
			, ntasks, task, task->handler, task->param);

		task->handler(task->param);
	}

	return mgr->max_run - n;
}


static int _ffdir_cmpfilename(const void *a, const void *b, void *udata)
{
	char *n1 = *(char**)a, *n2 = *(char**)b;

#ifdef FF_UNIX
	return ffsz_cmp(n1, n2);
#else
	return ffsz_icmp(n1, n2);
#endif
}

/*
Windows: Find*() functions also match filenames with short 8.3 names */
int ffdir_expopen(ffdirexp *dex, char *pattern, uint flags)
{
	ffdir dir;
	ffdirentry de;
	int rc = 1;
	uint wcflags;
	char **pname;
	const char *nm;
	ffstr path, wildcard;
	ffarr names = {0};
	ffstr3 fullname = {0};
	size_t len = ffsz_len(pattern), max_namelen = 0;

	ffmem_tzero(dex);
	ffmem_tzero(&de);

	ffpath_split2(pattern, len, &path, &wildcard);

	if (wildcard.len != 0
		&& ((flags & FFDIR_EXP_NOWILDCARD)
			|| 0 > ffstr_findanyz(&wildcard, "*?"))) {
		// "/path" (without the last "/")
		ffstr_set(&path, pattern, len);
		wildcard.len = 0;
	}

#ifdef FF_UNIX
	if (path.len == 0)
		dir = opendir(".");
	else {
		// "/path..." -> "/path\0"
		char *p = path.ptr + path.len;
		int back = *p;
		if (back != '\0')
			*p = '\0';
		dir = opendir(path.ptr);
		if (back != '\0')
			*p = back;
	}

	if (dir == NULL)
		return 1;

#else //windows:
	wchar_t wpatt_s[256], *wpatt;

	if (wildcard.len == 0) {
		// "/dir" -> "/dir\\*"
		int n = ffsz_utow(NULL, 0, pattern);
		if (n <= 0) {
			fferr_set(EINVAL);
			return 1;
		}
		if (NULL == (wpatt = ffmem_alloc((n + 2) * sizeof(wchar_t))))
			return 1;
		n = ffsz_utow(wpatt, n + 2, pattern);

		n--;
		if (wpatt[n - 1] == '/' || wpatt[n - 1] == '\\')
			n--; // "dir/" -> "dir"
		wpatt[n] = '\\';
		wpatt[n + 1] = '*';
		wpatt[n + 2] = '\0';

	} else {
		if (NULL == (wpatt = ffsz_alloc_buf_utow(wpatt_s, FF_COUNT(wpatt_s), pattern)))
			return 1;
	}

	dir = FindFirstFileW(wpatt, &de.find_data);
	if (wpatt != wpatt_s)
		ffmem_free(wpatt);

	if (dir == INVALID_HANDLE_VALUE) {
		if (fferr_last() == ERROR_FILE_NOT_FOUND)
			fferr_set(ENOMOREFILES);
		return 1;
	}
#endif

	wcflags = FFPATH_ICASE ? FFS_WC_ICASE : 0;

	for (;;) {

		if (0 != ffdir_read(dir, &de)) {
			if (fferr_last() == ENOMOREFILES)
				break;
			goto done;
		}

		nm = ffdir_entry_name(&de);
		if (!(flags & FFDIR_EXP_DOT12)
			&& nm[0] == '.' && (nm[1] == '\0'
				|| (nm[1] == '.' && nm[2] == '\0')))
			continue;

		if (NULL == _ffarr_grow(&names, 1, FFARR_GROWQUARTER, sizeof(char*)))
			goto done;

		fullname.len = 0;
		if (0 == ffstr_catfmt(&fullname, "%s%Z", ffdir_entry_name(&de)))
			goto done;
		fullname.len--;

		if (wildcard.len != 0
			&& 0 != ffs_wildcard(wildcard.ptr, wildcard.len, fullname.ptr, fullname.len, wcflags))
			continue;

		if (max_namelen < fullname.len)
			max_namelen = fullname.len;

		pname = ffarr_push(&names, char*);
		*pname = fullname.ptr;
		ffarr_null(&fullname);
	}

	if (names.len == 0)
		goto done;

	dex->pathlen = path.len;
	if (NULL == (dex->path_fn = ffmem_alloc(dex->pathlen + FFSLEN("/") + max_namelen + 1)))
		goto done;
	ffmemcpy(dex->path_fn, path.ptr, path.len);
	if (path.len != 0) {
		char c = path.ptr[dex->pathlen];
		if (!ffpath_slash(c))
			c = FFPATH_SLASH;
		dex->path_fn[dex->pathlen++] = c;
	}

	if (!(flags & FFDIR_EXP_NOSORT)) {
		ffsort(names.ptr, names.len, sizeof(char*), &_ffdir_cmpfilename, NULL);
	}

	dex->flags = flags;
	rc = 0;

done:
	dex->size = names.len;
	dex->names = (char**)names.ptr;
	if (rc != 0)
		ffdir_expclose(dex);
	ffdir_close(dir);
	ffarr_free(&fullname);
	return rc;
}

void ffdir_expclose(ffdirexp *dex)
{
	size_t i;
	for (i = 0;  i < dex->size;  i++) {
		ffmem_free(dex->names[i]);
	}
	ffmem_safefree(dex->names);
	ffmem_safefree(dex->path_fn);
}

const char* ffdir_expread(ffdirexp *dex)
{
	if (dex->cur == dex->size)
		return NULL;

	if (dex->flags & FFDIR_EXP_REL)
		return dex->names[dex->cur++];

	ffsz_fcopyz(dex->path_fn + dex->pathlen, dex->names[dex->cur++]);
	return dex->path_fn;
}
