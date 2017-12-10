/**
Copyright (c) 2013 Simon Zolin
*/

#include <FF/time.h>
#include <FF/array.h>
#include <FFOS/error.h>


/** Get year days passed before this month (1: March). */
#define mon_ydays(mon)  (367 * (mon) / 12 - 30)

/** Get month by year day (1: March). */
#define yday_mon(yday)  (((yday) + 31) * 10 / 306)

static const char week_days[][4] = {
	"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
};

static const char month_names[][4] = {
	"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

static const byte month_days[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

ffbool fftime_chk(const ffdtm *dt, uint flags)
{
	if (flags == 0)
		flags = 0xff;

	if ((flags & FFTIME_CHKTIME)
		&& !(dt->hour <= 23
			&& dt->min <= 59
			&& dt->sec <= 59
			&& dt->msec <= 999))
		return 0;

	if ((flags & FFTIME_CHKDATE)
		&& !(dt->year != 0
			&& (uint)dt->month - 1 < 12
			&& ((uint)dt->day - 1 < month_days[dt->month - 1]
				|| (dt->day == 29 && dt->month == 2 && fftime_leapyear(dt->year)))))
		return 0;

	if ((flags & FFTIME_CHKWDAY)
		&& !(dt->weekday <= 6))
		return 0;

	if (flags & FFTIME_CHKYDAY) {
		uint mon = dt->month - 2;
		if ((int)mon <= 0)
			mon += 12;
		if (!(dt->yday == mon_ydays(mon) + 31 + 28 + fftime_leapyear(dt->year) + dt->day))
			return 0;
	}

	return 1;
}

void fftime_norm(ffdtm *dt, uint flags)
{
	if (fftime_chk(dt, flags))
		return;

	if (flags == FFTIME_CHKTIME)
		flags = FFTIME_TZNODATE;
	else
		flags = 0;

	fftime t;
	fftime_join2(&t, dt, flags);
	fftime_split2(dt, &t, 0);
}

void fftime_normalize(fftime *t)
{
	t->sec += t->nsec / 1000000000;
	t->nsec = t->nsec % 1000000000;
}

int fftime_cmp(const fftime *t1, const fftime *t2)
{
	if (t1->sec == t2->sec) {
		if (t1->nsec == t2->nsec)
			return 0;
		if (t1->nsec < t2->nsec)
			return -1;
	} else if (t1->sec < t2->sec)
		return -1;
	return 1;
}

void fftime_addms(fftime *t, uint64 ms)
{
	fftime t2;
	t2.sec = ms / 1000;
	fftime_setmsec(&t2, ms);
	fftime_add(t, &t2);
}

void fftime_add(fftime *t, const fftime *t2)
{
	t->sec += t2->sec;
	t->nsec += t2->nsec;
	if (t->nsec >= 1000000000) {
		t->nsec -= 1000000000;
		t->sec++;
	}
}

void fftime_sub(fftime *t, const fftime *t2)
{
	t->sec -= t2->sec;
	t->nsec -= t2->nsec;
	if ((int)t->nsec < 0) {
		t->nsec += 1000000000;
		t->sec--;
	}
}

void fftime_totm(struct tm *tm, const ffdtm *dt)
{
	tm->tm_year = ffmax(dt->year - 1900, 0);
	tm->tm_mon = dt->month - 1;
	tm->tm_mday = dt->day;
	tm->tm_hour = dt->hour;
	tm->tm_min = dt->min;
	tm->tm_sec = dt->sec;
}

void fftime_fromtm(ffdtm *dt, const struct tm *tm)
{
	dt->year = tm->tm_year + 1900;
	dt->month = tm->tm_mon + 1;
	dt->day = tm->tm_mday;
	dt->weekday = tm->tm_wday;
	dt->yday = tm->tm_yday + 1;
	dt->hour = tm->tm_hour;
	dt->min = tm->tm_min;
	dt->sec = tm->tm_sec;
	dt->msec = 0;
}

static int _fftzone_off;
static uint _fftzone_fast;

void fftime_storelocal(const fftime_zone *tz)
{
	_fftzone_fast = 0;
	if (!tz->have_dst) {
		_fftzone_off = tz->off;
		_fftzone_fast = 1;
	}
}

static void fftime_splitlocal(struct tm *tm, time_t t)
{
#if defined FF_MSVC || defined FF_MINGW
	localtime_s(tm, &t);
#else
	localtime_r(&t, tm);
#endif
}

static time_t fftime_joinlocal(struct tm *tm)
{
	time_t t;
	tm->tm_isdst = -1;
	t = mktime(tm);
	if (t == (time_t)-1)
		t = 0;
	return t;
}

/*
Split timestamp algorithm:
. Get day of week (1/1/1 was Monday).
. Get year and the days passed since its Mar 1:
 . Get days passed since Mar 1, 1 AD
 . Get approximate year (days / ~365.25).
 . Get days passed during the year.
. Get month and its day:
 . Get month by year day
 . Get year days passed before this month
 . Get day of month
. Shift New Year from Mar 1 to Jan 1
 . If year day is within Mar..Dec, add 2 months
 . If year day is within Jan..Feb, also increment year
*/
void fftime_split2(ffdtm *dt, const fftime *t, uint flags)
{
	uint year, mon, days, daysec, yday, mday;
	uint64 sec;

	if (flags == FFTIME_TZLOCAL && !_fftzone_fast)
		return;

	sec = t->sec;
	if (flags == FFTIME_TZLOCAL)
		sec += _fftzone_off;

	days = sec / FFTIME_DAY_SECS;
	daysec = sec % FFTIME_DAY_SECS;
	dt->weekday = (1 + days) % 7;

	days += 306; //306: days from Mar before Jan
	year = 1 + days * 400 / fftime_absdays(400);
	yday = days - fftime_absdays(year);
	if ((int)yday < 0) {
		yday += 365 + fftime_leapyear(year);
		year--;
	}

	mon = yday_mon(yday);
	mday = yday - mon_ydays(mon);

	if (yday >= 306) {
		year++;
		mon -= 10;
		yday -= 306;
	} else {
		mon += 2;
		yday += 31 + 28 + fftime_leapyear(year);
	}

	dt->year = year;
	dt->month = mon;
	dt->yday = yday + 1;
	dt->day = mday + 1;

	dt->hour = daysec / (60*60);
	dt->min = (daysec % (60*60)) / 60;
	dt->sec = daysec % 60;
	dt->msec = fftime_msec(t);
}

static uint mon_norm(uint mon, uint *year)
{
	if (mon > 12) {
		mon--;
		*year += mon / 12;
		mon = (mon % 12) + 1;
	}
	return mon;
}

fftime* fftime_join2(fftime *t, const ffdtm *dt, uint flags)
{
	uint year, mon, days;

	if (flags == FFTIME_TZNODATE) {
		days = 0;
		goto set;
	}

	if (flags == FFTIME_TZLOCAL && !_fftzone_fast) {
		fftime_null(t);
		return t;
	}

	if (dt->year <= 0) {
		fftime_null(t);
		return t;
	}

	year = dt->year;
	mon = mon_norm(dt->month, &year) - 2; //Jan -> Mar
	if ((int)mon <= 0) {
		mon += 12;
		year--;
	}

	days = fftime_absdays(year) - fftime_absdays(1)
		+ mon_ydays(mon) + (31 + 28)
		+ dt->day - 1;

set:
	t->sec = (int64)days * FFTIME_DAY_SECS + dt->hour * 60*60 + dt->min * 60 + dt->sec;
	fftime_setmsec(t, dt->msec);

	if (flags == FFTIME_TZLOCAL) {
		t->sec -= _fftzone_off;
	}

	return t;
}

void fftime_split(ffdtm *dt, const fftime *t, enum FF_TIMEZONE tz)
{
	if (tz == FFTIME_TZLOCAL && !_fftzone_fast) {
		struct tm tm;
		fftime_splitlocal(&tm, t->sec);
		fftime_fromtm(dt, &tm);
		dt->msec = fftime_msec(t);
		return;
	}

	fftime tt = *t;
	tt.sec += (int64)fftime_absdays(1970 - 1) * FFTIME_DAY_SECS;
	fftime_split2(dt, &tt, tz);
}

fftime* fftime_join(fftime *t, const ffdtm *dt, enum FF_TIMEZONE tz)
{
	if (tz == FFTIME_TZLOCAL && !_fftzone_fast) {
		struct tm tm;
		fftime_totm(&tm, dt);
		t->sec = fftime_joinlocal(&tm);
		fftime_setmsec(t, dt->msec);
		return t;
	}

	fftime_join2(t, dt, tz);
	if (tz == FFTIME_TZNODATE)
		return t;
	t->sec -= (int64)fftime_absdays(1970 - 1) * FFTIME_DAY_SECS;
	if (t->sec < 0)
		fftime_null(t);
	return t;
}


size_t fftime_tostr(const ffdtm *dt, char *dst, size_t cap, uint flags)
{
	const char *dst_o = dst;
	const char *end = dst + cap;

	// add date
	switch (flags & 0x0f) {
	case FFTIME_DATE_YMD:
		dst += ffs_fmt(dst, end, "%04u-%02u-%02u"
			, dt->year, dt->month, dt->day);
		break;

	case FFTIME_DATE_MDY0:
		dst += ffs_fmt(dst, end, "%02u/%02u/%04u"
			, dt->month, dt->day, dt->year);
		break;

	case FFTIME_DATE_MDY:
		dst += ffs_fmt(dst, end, "%u/%u/%04u"
			, dt->month, dt->day, dt->year);
		break;

	case FFTIME_DATE_DMY:
		dst += ffs_fmt(dst, end, "%02u.%02u.%04u"
			, dt->day, dt->month, dt->year);
		break;

	case FFTIME_DATE_WDMY:
		dst += ffs_fmt(dst, end, "%s, %02u %s %04u"
			, week_days[dt->weekday], dt->day, month_names[dt->month - 1], dt->year);
		break;

	case 0:
		break; //no date

	default:
		goto fail;
	}

	if ((flags & 0x0f) && (flags & 0xf0))
		dst = ffs_copyc(dst, end, ' ');

	// add time
	switch (flags & 0xf0) {
	case FFTIME_HMS:
		dst += ffs_fmt(dst, end, "%02u:%02u:%02u"
			, dt->hour, dt->min, dt->sec);
		break;

	case FFTIME_HMS_MSEC:
		dst += ffs_fmt(dst, end, "%02u:%02u:%02u.%03u"
			, dt->hour, dt->min, dt->sec, dt->msec);
		break;

	case FFTIME_HMS_GMT:
		dst += ffs_fmt(dst, end, "%02u:%02u:%02u GMT"
			, dt->hour, dt->min, dt->sec);
		break;

	case 0:
		break; //no time

	default:
		goto fail;
	}

	if (dst == end) {
		fferr_set(EOVERFLOW);
		return 0;
	}

	return dst - dst_o;

fail:
	fferr_set(EINVAL);
	return 0;
}

size_t fftime_now_tostrz(char *dst, size_t cap, uint fmt)
{
	ffdtm dt;
	fftime t;
	fftime_now(&t);
	fftime_split(&dt, &t, FFTIME_TZLOCAL);
	size_t n = fftime_tostr(&dt, dst, cap, fmt);
	if (n != 0)
		dst[n] = '\0';
	return n;
}

static int date_fromstr(ffdtm *t, const ffstr *ss, uint fmt)
{
	const char *s = ss->ptr, *s_end = s + ss->len;
	char sweekday[4], smon[4];
	ssize_t i;

	switch (fmt & 0x0f) {
	case FFTIME_DATE_YMD:
		i = ffs_fmatch(s, s_end - s, "%4u-%2u-%2u"
			, &t->year, &t->month, &t->day);
		if (i < 0)
			goto fail;
		s += i;

		t->weekday = 0;
		break;

	case FFTIME_DATE_MDY0:
		i = ffs_fmatch(s, s_end - s, "%2u/%2u/%4u"
			, &t->year, &t->month, &t->day);
		if (i < 0)
			goto fail;
		s += i;

		t->weekday = 0;
		break;

	case FFTIME_DATE_MDY:
		i = ffs_fmatch(s, s_end - s, "%u/%u/%4u"
			, &t->year, &t->month, &t->day);
		if (i < 0)
			goto fail;
		s += i;

		t->weekday = 0;
		break;

	case FFTIME_DATE_DMY:
		i = ffs_fmatch(s, s_end - s, "%2u.%2u.%4u"
			, &t->year, &t->month, &t->day);
		if (i < 0)
			goto fail;
		s += i;

		t->weekday = 0;
		break;

	case FFTIME_DATE_WDMY:
		i = ffs_fmatch(s, s_end - s, "%3s, %2u %3s %4u"
			, sweekday, &t->day, &smon, &t->year);
		if (i < 0)
			goto fail;
		s += i;

		t->month = 1 + ffs_findarr3(month_names, smon, 3);

		if (-1 != (i = ffs_findarr3(week_days, sweekday, 3)))
			t->weekday = i;
		break;

	case 0:
		return 0; //no date

	default:
		goto fail;
	}

	if (!fftime_chk(t, FFTIME_CHKDATE))
		goto fail;

	return s - ss->ptr;

fail:
	return -1;
}

static int time_fromstr(ffdtm *t, const ffstr *ss, uint fmt)
{
	const char *s = ss->ptr, *s_end = s + ss->len;
	ssize_t i;

	switch (fmt & 0xf0) {
	case FFTIME_HMS:
		i = ffs_fmatch(s, s_end - s, "%2u:%2u:%2u"
			, &t->hour, &t->min, &t->sec);
		if (i < 0)
			goto fail;
		s += i;
		break;

	case FFTIME_HMS_GMT:
		i = ffs_fmatch(s, s_end - s, "%2u:%2u:%2u GMT"
			, &t->hour, &t->min, &t->sec);
		if (i < 0)
			goto fail;
		s += i;
		break;

	case FFTIME_HMS_MSEC_VAR:
		if (0 == (i = ffs_toint(s, s_end - s, &t->sec, FFS_INT32)))
			goto fail;
		s += i;
		if (s == s_end || *s != ':')
			goto msec;
		s++;
		t->min = t->sec;

		if (0 == (i = ffs_toint(s, s_end - s, &t->sec, FFS_INT32)))
			goto fail;
		s += i;
		if (s == s_end || *s != ':')
			goto msec;
		s++;
		t->hour = t->min;
		t->min = t->sec;

		if (0 == (i = ffs_toint(s, s_end - s, &t->sec, FFS_INT32)))
			goto fail;
		s += i;

msec:
		if (s != s_end || *s == '.') {
			s++;
			if (0 == (i = ffs_toint(s, s_end - s, &t->msec, FFS_INT32)))
				goto fail;
			s += i;
		}

		fftime_norm(t, FFTIME_CHKTIME);
		break;

	case 0:
		return 0; //no time

	default:
		goto fail;
	}

	if (!fftime_chk(t, FFTIME_CHKTIME))
		return -1;

	return s - ss->ptr;

fail:
	return -1;
}

size_t fftime_fromstr(ffdtm *dt, const char *s, size_t len, uint fmt)
{
	ffdtm t = {0};
	int i;
	ffstr ss;

	ffstr_set(&ss, s, len);
	if (0 > (i = date_fromstr(&t, &ss, fmt)))
		goto fail;
	ffstr_shift(&ss, i);

	if ((fmt & 0x0f) && (fmt & 0xf0)) {
		if (ss.len == 0 || ffstr_popfront(&ss) != ' ')
			goto fail;
	}

	if (0 > (i = time_fromstr(&t, &ss, fmt)))
		goto fail;
	ffstr_shift(&ss, i);

	if (ss.len != 0)
		goto fail;

	*dt = t;
	return len;

fail:
	fferr_set(EINVAL);
	return 0;
}
