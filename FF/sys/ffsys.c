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


/** Make directories for a filename. */
int fffile_makepath(char *fn, size_t off)
{
	ffstr dir;
	ffpath_split2(fn, ffsz_len(fn), &dir, NULL);
	char *s = dir.ptr + dir.len;
	char c = *s;
	*s = '\0';
	int r = ffdir_rmake(dir.ptr, off);
	*s = c;
	return r;
}

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

int fffile_readall(ffarr *a, const char *fn, uint64 limit)
{
	fffd fd;
	uint64 fsz;
	int r = -1;

	if (FF_BADFD == (fd = fffile_open(fn, O_RDONLY)))
		return -1;

	fsz = fffile_size(fd);
	if (fsz > limit)
		goto end;

	if (fsz != FF_TOSIZE(fsz))
		goto end;
	if (NULL == ffarr_realloc(a, fsz))
		goto end;

	if (fsz != (size_t)fffile_read(fd, a->ptr, fsz))
		goto end;

	a->len = fsz;
	r = 0;

end:
	fffile_close(fd);
	return r;
}

int fffile_writeall(const char *fn, const char *d, size_t len, uint flags)
{
	fffd f;
	int rc = 1;

	if (flags == 0)
		flags = FFO_CREATE | FFO_TRUNC;

	if (FF_BADFD == (f = fffile_open(fn, flags | O_WRONLY)))
		goto done;

	if (len != (size_t)fffile_write(f, d, len))
		goto done;

	rc = 0;

done:
	if (f != FF_BADFD) {
		rc |= fffile_close(f);
		if (rc != 0)
			fffile_rm(fn);
	}
	return rc;
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

		it = FF_READONCE(mgr->tasks.first);
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


int ffdir_make_path(char *fn, size_t off)
{
	ffstr dir;
	int r, c;

	if (NULL == ffpath_split2(fn, ffsz_len(fn), &dir, NULL))
		return 0; // no slash in filename

	c = dir.ptr[dir.len];
	dir.ptr[dir.len] = '\0';
	r = ffdir_rmake(dir.ptr, off);
	dir.ptr[dir.len] = c;
	return r;
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
	const ffsyschar *nm;
	ffstr path, wildcard;
	ffarr names = {0};
	ffstr3 fullname = {0};
	size_t len = ffsz_len(pattern), max_namelen = 0;

	ffmem_tzero(dex);
	ffmem_tzero(&de);

	ffpath_split2(pattern, len, &path, &wildcard);

	if (wildcard.len != 0
		&& ffarr_end(&wildcard) == ffs_findof(wildcard.ptr, wildcard.len, "*?", 2)) {
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
	if (wildcard.len == 0) {
		if (NULL == (dir = ffdir_open(pattern, 0, &de)))
			return 1;

	} else {

	ffsyschar *wpatt;
	ffsyschar wpatt_s[FF_MAXFN];
	size_t wpatt_len = FFCNT(wpatt_s);

	wpatt = ffs_utow(wpatt_s, &wpatt_len, pattern, -1);
	if (wpatt == NULL)
		return 1;

#if FF_WIN >= 0x0600
	dir = FindFirstFileEx(wpatt, FindExInfoBasic, &de.info, 0, NULL, 0);
#else
	dir = FindFirstFile(wpatt, &de.info);
#endif
	if (wpatt != wpatt_s)
		ffmem_free(wpatt);

	if (dir == INVALID_HANDLE_VALUE) {
		if (fferr_last() == ERROR_FILE_NOT_FOUND)
			fferr_set(ENOMOREFILES);
		return 1;
	}
	}
#endif

	wcflags = FFPATH_ICASE ? FFS_WC_ICASE : 0;

	for (;;) {

		if (0 != ffdir_read(dir, &de)) {
			if (fferr_last() == ENOMOREFILES)
				break;
			goto done;
		}

		nm = ffdir_entryname(&de);
		if (!(flags & FFDIR_EXP_DOT12)
			&& nm[0] == '.' && (nm[1] == '\0'
				|| (nm[1] == '.' && nm[2] == '\0')))
			continue;

		if (NULL == _ffarr_grow(&names, 1, FFARR_GROWQUARTER, sizeof(char*)))
			goto done;

		fullname.len = 0;
		if (0 == ffstr_catfmt(&fullname, "%*q%Z", de.namelen, ffdir_entryname(&de)))
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


#ifdef FF_WIN
int ffenv_init(ffenv *env, char **envptr)
{
	return 0;
}

void ffenv_destroy(ffenv *env)
{
}

#else

int ffenv_init(ffenv *env, char **envptr)
{
	env->ptr = envptr;
	env->n = ffszarr_countz((const char*const*)envptr);
	return 0;
}

void ffenv_destroy(ffenv *env)
{
}

char* ffenv_expand(ffenv *env, char *dst, size_t cap, const char *src)
{
	ffsvar sv;
	const char *end = src + ffsz_len(src);
	size_t n;
	int r;
	ffarr buf = {0};

	if (dst != NULL)
		buf.ptr = dst,  buf.cap = cap;

	while (src != end) {
		n = end - src;
		r = ffsvar_parse(&sv, src, &n);
		src += n;

		if (r == FFSVAR_S) {
			if (NULL != (sv.val.ptr = ffszarr_findkey((const char*const*)env->ptr, env->n, sv.val.ptr, sv.val.len)))
				sv.val.len = ffsz_len(sv.val.ptr);
			else
				sv.val.len = 0;
		}

		if (dst == NULL && NULL == ffarr_grow(&buf, sv.val.len, 256))
			goto err;

		buf.len += ffs_append(buf.ptr, buf.len, buf.cap, sv.val.ptr, sv.val.len);
	}

	if (dst == NULL && NULL == ffarr_grow(&buf, 1, 0))
		goto err;
	ffs_copyc(buf.ptr + buf.len, buf.ptr + buf.cap, '\0');
	return buf.ptr;

err:
	if (dst == NULL)
		ffarr_free(&buf);
	return NULL;
}
#endif
