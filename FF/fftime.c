/**
Copyright (c) 2013 Simon Zolin
*/

#include <FF/time.h>
#include <FF/string.h>
#include <FFOS/error.h>


static const char week_days[][4] = {
	"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
};

static const char month_names[][4] = {
	"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

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

size_t fftime_fromstr(ffdtm *dt, const char *s, size_t len, uint fmt)
{
	ffdtm t = {0};
	const char *s_end = s + len;
	char sweekday[4];
	char smon[4];
	ssize_t i;

	switch (fmt & 0x0f) {
	case FFTIME_DATE_YMD:
		i = ffs_fmatch(s, s_end - s, "%4u-%2u-%2u"
			, &t.year, &t.month, &t.day);
		if (i < 0)
			goto fail;
		s += i;

		t.weekday = 0;
		break;

	case FFTIME_DATE_WDMY:
		i = ffs_fmatch(s, s_end - s, "%3s, %2u %3s %4u"
			, sweekday, &t.day, &smon, &t.year);
		if (i < 0)
			goto fail;
		s += i;

		t.month = 1 + (int)ffs_findarr(smon, 3, month_names, sizeof(*month_names), FFCNT(month_names));

		t.weekday = (int)ffs_findarr(sweekday, 3, week_days, sizeof(*week_days), FFCNT(week_days));
		if (t.weekday == FFCNT(week_days))
			t.weekday = 0; //ignore invalid weekday
		break;

	case 0:
		break; //no date

	default:
		goto fail;
	}

	if ((fmt & 0x0f) && (fmt & 0xf0)) {
		if (s == s_end || *s++ != ' ')
			goto fail;
	}

	switch (fmt & 0xf0) {
	case FFTIME_HMS:
		i = ffs_fmatch(s, s_end - s, "%2u:%2u:%2u"
			, &t.hour, &t.min, &t.sec);
		if (i < 0)
			goto fail;
		s += i;
		break;

	case FFTIME_HMS_GMT:
		i = ffs_fmatch(s, s_end - s, "%2u:%2u:%2u GMT"
			, &t.hour, &t.min, &t.sec);
		if (i < 0)
			goto fail;
		s += i;
		break;

	case 0:
		break; //no time

	default:
		goto fail;
	}

	if (s != s_end)
		goto fail;

	if (!fftime_chk(&t))
		goto fail;

	*dt = t;
	return len;

fail:
	fferr_set(EINVAL);
	return 0;
}
