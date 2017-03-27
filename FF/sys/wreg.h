/** Windows Registry.
Copyright (c) 2017 Simon Zolin
*/

#pragma once

#include <FFOS/win/reg.h>
#include <FF/array.h>


/** Convert HKEY <-> string. */
FF_EXTN int ffwreg_hkey_fromstr(const char *name, size_t len);
FF_EXTN const char* ffwreg_hkey_tostr(int hkey);

/** Split "HKEY_CURRENT_USER\Key1\Key2" -> {"HKEY_CURRENT_USER", "Key1\Key2"} */
static FFINL void ffwreg_pathsplit(const char *fullpath, size_t len, ffstr *hkey, ffstr *subkey)
{
	ffs_split2by(fullpath, len, '\\', hkey, subkey);
}


/** Read value to a dynamic buffer.
Return value type on success;  <0 on error. */
FF_EXTN int ffwreg_readbuf(ffwreg k, const char *name, ffarr *buf);


enum FFWREG_ENUM_F {
	FFWREG_ENUM_FULLPATH = 1,
	FFWREG_ENUM_VALSTR = 2, //convert value to UTF-8 string
};

typedef struct ffwreg_enum {
	ffarr name;
	ffarr value;
	ffarr wname;
	ffarr wval;
	uint idx;
	uint val_type;
	const char *path;
	uint flags;
} ffwreg_enum;

/**
@flags: enum FFWREG_ENUM_F */
FF_EXTN int ffwreg_enuminit(ffwreg_enum *e, uint flags);

FF_EXTN void ffwreg_enumclose(ffwreg_enum *e);

FF_EXTN void ffwreg_enumreset(ffwreg_enum *e);

/** Get next subkey or value.
Return 1 if the next entry is fetched;  0 if no more entries;  <0 on error. */
FF_EXTN int ffwreg_nextkey(ffwreg_enum *e, ffwreg key);
FF_EXTN int ffwreg_nextval(ffwreg_enum *e, ffwreg key);
