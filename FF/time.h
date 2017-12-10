/** Date and time functions.
Copyright (c) 2013 Simon Zolin
*/

#pragma once

#include <FFOS/time.h>


/** Return 1 if leap year.
Leap year is each 4th and each 400th except each 100th. */
#define fftime_leapyear(year)  ((year) % 4 == 0 && ((year) % 100 != 0 || (year) % 400 == 0))

/** Get absolute number of days passed since 1 AD until 'year'+1. */
#define fftime_absdays(year)  ((year) * 365 + (year) / 4 - (year) / 100 + (year) / 400)

#define FFTIME_DAY_SECS  (60*60*24)

/** Compare two 'fftime' objects. */
FF_EXTN int fftime_cmp(const fftime *t1, const fftime *t2);

/** Normalize time value. */
FF_EXTN void fftime_normalize(fftime *t);

/** Add/subtract a normal time value. */
FF_EXTN void fftime_add(fftime *t, const fftime *t2);
FF_EXTN void fftime_sub(fftime *t, const fftime *t2);

// obsolete
#define fftime_diff(start, stop)  fftime_sub(stop, start)

/** Add milliseconds. */
FF_EXTN void fftime_addms(fftime *t, uint64 ms);


typedef struct ffdtm {
	int year;
	uint month //1..12
		, weekday //0..6 (0:Sunday)
		, day //1..31
		, yday //1..366

		, hour //0..23
		, min  //0..59
		, sec  //0..59
		, msec //0..999
		;
} ffdtm;

enum FFTIME_CHK {
	FFTIME_CHKALL,
	FFTIME_CHKTIME = 1,
	FFTIME_CHKDATE = 2, //check ffdtm.year, month, day
	FFTIME_CHKWDAY = 4, //check ffdtm.weekday
	FFTIME_CHKYDAY = 8, //check ffdtm.yday
};

/** Return TRUE if a valid date-time object.
@flags: enum FFTIME_CHK */
FF_EXTN ffbool fftime_chk(const ffdtm *dt, uint flags);

/** Normalize values that exceed limits.
@flags: enum FFTIME_CHK */
FF_EXTN void fftime_norm(ffdtm *dt, uint flags);

/** Convert 'ffdtm' to 'tm'. */
FF_EXTN void fftime_totm(struct tm *tt, const ffdtm *dt);

/** Convert 'tm' to 'ffdtm'. */
FF_EXTN void fftime_fromtm(ffdtm *dt, const struct tm *tt);

enum FF_TIMEZONE {
	FFTIME_TZUTC,
	FFTIME_TZLOCAL,
	FFTIME_TZNODATE,
};

/** Split the time value into date and time elements. */
FF_EXTN void fftime_split2(ffdtm *dt, const fftime *t, uint flags);

/** Join the time parts.
Note: ffdtm.weekday and ffdtm.yday aren't used or checked. */
FF_EXTN fftime* fftime_join2(fftime *t, const ffdtm *dt, uint flags);

/** Split/join time (using UNIX time - since year 1970). */
FF_EXTN void fftime_split(ffdtm *dt, const fftime *t, enum FF_TIMEZONE tz);
FF_EXTN fftime* fftime_join(fftime *t, const ffdtm *dt, enum FF_TIMEZONE tz);


enum FFTIME_FMT {
	//date:
	FFTIME_DATE_YMD = 2	// yyyy-MM-dd
	, FFTIME_DATE_WDMY = 3	// Wed, 07 Sep 2011
	, FFTIME_DATE_MDY0 // 09/07/2011
	, FFTIME_DATE_MDY // 9/7/2011
	, FFTIME_DATE_DMY // 07.09.2011

	//time:
	, FFTIME_HMS = 0x20	// hh:mm:ss
	, FFTIME_HMS_MSEC = 0x30 // hh:mm:ss.msc
	, FFTIME_HMS_GMT = 0x40	// hh:mm:ss GMT
	, FFTIME_HMS_MSEC_VAR = 0x50 // [h:][m:][s][.ms]

	//date & time:
	, FFTIME_YMD = FFTIME_DATE_YMD | FFTIME_HMS	// yyyy-MM-dd hh:mm:ss, ISO 8601
	, FFTIME_WDMY = FFTIME_DATE_WDMY | FFTIME_HMS_GMT	// Wed, 07 Sep 2011 00:00:00 GMT, RFC1123
};

/** Convert date/time to string.
@fmt: enum FFTIME_FMT.
Return 0 on error. */
FF_EXTN size_t fftime_tostr(const ffdtm *dt, char *dst, size_t cap, uint fmt);

/** Get current time and convert it to a NULL-terminated string. */
FF_EXTN size_t fftime_now_tostrz(char *dst, size_t cap, uint fmt);

/** Convert string to date/time.
@fmt: enum FFTIME_FMT.
Return the number of processed bytes.  Return 0 on error. */
FF_EXTN size_t fftime_fromstr(ffdtm *dt, const char *s, size_t len, uint fmt);

/** Convert string to UNIX timestamp.
@fmt: enum FFTIME_FMT.
Return -1 on error. */
static FFINL time_t fftime_strtounix(const char *s, size_t len, uint fmt)
{
	ffdtm dt;
	fftime t;
	if (len != fftime_fromstr(&dt, s, len, fmt)
		|| dt.year < 1970)
		return (time_t)-1;
	fftime_join(&t, &dt, FFTIME_TZUTC);
	return fftime_sec(&t);
}
