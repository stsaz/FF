/**
Copyright (c) 2017 Simon Zolin
*/

#include <FF/sys/wreg.h>
#include <FF/number.h>


static const char names_short[][4] = {
	"HKCR",
	"HKCU",
	"HKLM",
	"HKU",
};

static const char names_long[][19] = {
	"HKEY_CLASSES_ROOT",
	"HKEY_CURRENT_USER",
	"HKEY_LOCAL_MACHINE",
	"HKEY_USERS",
};

static const int hkeys[] = {
	(ssize_t)HKEY_CLASSES_ROOT,
	(ssize_t)HKEY_CURRENT_USER,
	(ssize_t)HKEY_LOCAL_MACHINE,
	(ssize_t)HKEY_USERS,
};

int ffwreg_hkey_fromstr(const char *name, size_t len)
{
	ssize_t r;
	if (len <= 4)
		r = ffcharr_findsorted(names_short, FFCNT(names_short), sizeof(*names_short), name, len);
	else
		r = ffcharr_findsorted(names_long, FFCNT(names_long), sizeof(*names_long), name, len);
	if (r < 0)
		return 0;
	return hkeys[r];
}

const char* ffwreg_hkey_tostr(int hkey)
{
	ssize_t r;
	if (0 > (r = ffint_find4((uint*)hkeys, FFCNT(hkeys), hkey)))
		return "";
	return names_long[r];
}


int ffwreg_readbuf(ffwreg k, const char *name, ffarr *buf)
{
	int r;
	ffwreg_val val = {0};
	val.data = buf->ptr;
	val.datalen = buf->cap;
	r = ffwreg_read(k, name, &val);
	if (r == 0) {
		if (NULL == ffarr_realloc(buf, val.datalen))
			return -1;

		val.data = buf->ptr;
		val.datalen = buf->cap;
		r = ffwreg_read(k, name, &val);
		if (r == 0)
			return -1;

	} else if (r == 1) {
		if (buf->ptr == NULL) {
			buf->ptr = val.data;
			buf->cap = val.datalen;
		}
	}

	if (r < 0)
		return -1;

	buf->len = val.datalen;
	return val.type;
}


int ffwreg_enuminit(ffwreg_enum *e, uint flags)
{
	if (NULL == ffarr_alloc(&e->name, 256)
		|| NULL == ffarr_alloc(&e->value, 256)
		|| NULL == ffarr_allocT(&e->wname, 256, ffsyschar)
		|| NULL == ffarr_alloc(&e->wval, 256)) {
		ffwreg_enumclose(e);
		return -1;
	}
	e->flags = flags;
	return 0;
}

void ffwreg_enumclose(ffwreg_enum *e)
{
	ffarr_free(&e->name);
	ffarr_free(&e->value);
	ffarr_free(&e->wname);
	ffarr_free(&e->wval);
}

void ffwreg_enumreset(ffwreg_enum *e)
{
	e->idx = 0;
	e->value.len = 0;
	e->name.len = 0;
}

static int name_tostr(ffwreg_enum *e, ffarr *name, const void *wname, size_t len)
{
	int r;
	uint nname = len * 4;
	char *s, *end;

	if (e->flags & FFWREG_ENUM_FULLPATH)
		nname += ffsz_len(e->path) + FFSLEN("\\");
	if (NULL == ffarr_realloc_grow(name, nname))
		return -1;

	s = name->ptr;
	end = ffarr_edge(name);
	if ((e->flags & FFWREG_ENUM_FULLPATH) && e->path[0] != '\0') {
		s = ffs_copyz(s, end, e->path);
		s = ffs_copyc(s, end, '\\');
	}

	r = ff_wtou(s, end - s, (ffsyschar*)wname, len, 0);
	s += r;
	name->len = s - name->ptr;
	return 0;
}

int ffwreg_nextkey(ffwreg_enum *e, ffwreg key)
{
	int r;
	DWORD n;

	for (;;) {
		n = e->wname.cap;
		r = RegEnumKeyEx(key, e->idx, (ffsyschar*)e->wname.ptr, &n, NULL, NULL, NULL, NULL);
		if (r == 0)
			break;

		else if (r == ERROR_MORE_DATA) {

			// get maximum length of subkey's name
			DWORD subkey_maxlen;
			if (0 != RegQueryInfoKey(key, NULL, NULL, NULL, NULL, &subkey_maxlen, NULL, NULL, NULL, NULL, NULL, NULL))
				return -1;
			if (NULL == ffarr_reallocT(&e->wname, subkey_maxlen + 1, ffsyschar))
				return -1;

		} else if (r == ERROR_NO_MORE_ITEMS)
			return 0;
		else {
			fferr_set(r);
			return -1;
		}
	}

	if (0 != name_tostr(e, &e->name, e->wname.ptr, n))
		return -1;
	e->idx++;
	return 1;
}

static FFINL void val_tostr(ffwreg_enum *e, uint type, ffarr *value)
{
	int r;
	switch (type) {

	case REG_DWORD: {
		const uint *i = (void*)e->wval.ptr;
		r = ffs_fmt2(value->ptr, value->cap, "0x%08xu (%u)", *i, *i);
		if (r > 0)
			value->len = r;
		break;
		}

	case REG_QWORD: {
		const uint64 *i = (void*)e->wval.ptr;
		r = ffs_fmt2(value->ptr, value->cap, "0x%016xU (%U)", *i, *i);
		if (r > 0)
			value->len = r;
		break;
		}

	case REG_BINARY:
		r = ffs_fmt2(value->ptr, value->cap, "%*xb", e->wval.len, e->wval.ptr);
		if (r > 0)
			value->len = r;
		break;

	case REG_SZ:
	case REG_EXPAND_SZ:
		r = ff_wtou(value->ptr, value->cap, (ffsyschar*)e->wval.ptr, (e->wval.len - 1) / sizeof(ffsyschar), 0);
		e->value.len = r;
		break;

	default:
		e->name.len = 0;
	}
}

int ffwreg_nextval(ffwreg_enum *e, ffwreg key)
{
	int r;
	DWORD type, n, nval;

	for (;;) {
		n = e->wname.cap;
		nval = e->wval.cap;
		r = RegEnumValue(key, e->idx++, (ffsyschar*)e->wname.ptr, &n, NULL, &type, (byte*)e->wval.ptr, &nval);
		if (r == 0)
			break;

		else if (r == ERROR_MORE_DATA) {
			// get maximum length of value's name and data
			DWORD name_maxlen, val_maxlen;
			if (0 != RegQueryInfoKey(key, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &name_maxlen, &val_maxlen, NULL, NULL))
				return -1;
			if (NULL == ffarr_realloc_growT(&e->wname, name_maxlen + 1, ffsyschar))
				return -1;
			if (NULL == ffarr_realloc_grow(&e->wval, val_maxlen))
				return -1;

		} else if (r == ERROR_NO_MORE_ITEMS)
			return 0;
		else {
			fferr_set(r);
			return -1;
		}
	}
	e->wval.len = nval;

	if (0 != name_tostr(e, &e->name, e->wname.ptr, n))
		return -1;

	e->val_type = type;
	e->value.len = 0;
	if (e->flags & FFWREG_ENUM_VALSTR)
		val_tostr(e, type, &e->value);

	return 1;
}
