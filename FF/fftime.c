/**
Copyright (c) 2013 Simon Zolin
*/

#include <FF/time.h>
#include <FF/array.h>
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

		fftime_norm(t);
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
