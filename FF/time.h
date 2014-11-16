/** Date and time functions.
Copyright (c) 2013 Simon Zolin
*/

#pragma once

#include <FFOS/time.h>


enum FFTIME_FMT {
	//date:
	FFTIME_DATE_YMD = 2	// yyyy-MM-dd
	, FFTIME_DATE_WDMY = 3	// Wed, 07 Sep 2011

	//time:
	, FFTIME_HMS = 0x20	// hh:mm:ss
	, FFTIME_HMS_MSEC = 0x30 // hh:mm:ss.msc
	, FFTIME_HMS_GMT = 0x40	// hh:mm:ss GMT

	//date & time:
	, FFTIME_YMD = FFTIME_DATE_YMD | FFTIME_HMS	// yyyy-MM-dd hh:mm:ss, ISO 8601
	, FFTIME_WDMY = FFTIME_DATE_WDMY | FFTIME_HMS_GMT	// Wed, 07 Sep 2011 00:00:00 GMT, RFC1123
};

/** Convert date/time to string.
@fmt: enum FFTIME_FMT.
Return 0 on error. */
FF_EXTN size_t fftime_tostr(const ffdtm *dt, char *dst, size_t cap, uint fmt);

/** Convert string to date/time.
Return the number of processed bytes.  Return 0 on error. */
FF_EXTN size_t fftime_fromstr(ffdtm *dt, const char *s, size_t len, uint fmt);

/** Convert string to UNIX timestamp.
Return -1 on error. */
static FFINL uint fftime_strtounix(const char *s, size_t len, uint fmt) {
	ffdtm dt;
	fftime t;
	if (len != fftime_fromstr(&dt, s, len, fmt)
		|| dt.year < 1970)
		return (uint)-1;
	return fftime_join(&t, &dt, FFTIME_TZUTC)->s;
}
