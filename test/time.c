/**
Copyright (c) 2014 Simon Zolin
*/

#include <FFOS/test.h>
#include <FF/array.h>
#include <FF/time.h>
#include <FF/sys/timer-queue.h>
#include <test/all.h>

#define x FFTEST_BOOL


static fftime_zone tz;

static void test_time_dt(void)
{
	fftime t, t2;
	ffdtm dt = {0}, dt2;

	dt.year = 2014; dt.month = 5; dt.day = 19;
	dt.weekday = 1; dt.yday = 139;
	dt.hour = 8; dt.min = 52; dt.sec = 36; dt.nsec = 23;
	fftime_norm(&dt, 0);
	x(fftime_chk(&dt, 0));

	// day
	dt.year = 2014; dt.month = 2; dt.day = 29;
	x(!fftime_chk(&dt, FFTIME_CHKDATE));
	dt.year = 2012; dt.month = 2; dt.day = 29;
	x(fftime_chk(&dt, FFTIME_CHKDATE));

#if 0
	// weekday
	dt.year = 2014; dt.month = 5; dt.day = 19; dt.weekday = 2;
	x(!fftime_chk(&dt, FFTIME_CHKDATE | FFTIME_CHKWDAY));
	dt.year = 2014; dt.month = 5; dt.day = 19; dt.weekday = 1;
	x(fftime_chk(&dt, FFTIME_CHKDATE | FFTIME_CHKWDAY));
#endif

	// yday
	dt.year = 2014; dt.month = 12; dt.day = 31; dt.yday = 366;
	x(!fftime_chk(&dt, FFTIME_CHKDATE | FFTIME_CHKYDAY));
	dt.year = 2012; dt.month = 12; dt.day = 31; dt.yday = 366;
	x(fftime_chk(&dt, FFTIME_CHKDATE | FFTIME_CHKYDAY));

	dt.year = 1945; dt.month = 11; dt.day = 26; dt.weekday = -1;
	fftime_norm(&dt, 0);
	x(dt.weekday == 1);

	dt.year = 2014; dt.month = 5; dt.day = 19;
	dt.weekday = 1; dt.yday = 139;
	dt.hour = 8; dt.min = 52; dt.sec = 36; fftime_setmsec(&dt, 23);

	fftime_join(&t, &dt, FFTIME_TZUTC);
	x(fftime_sec(&t) == 1400489556 && fftime_msec(&t) == 23);
	fftime_split(&dt2, &t, FFTIME_TZUTC);
	x(!memcmp(&dt, &dt2, sizeof(dt)));

	fftime_join(&t2, &dt, FFTIME_TZLOCAL);
	if (!tz.have_dst)
		x(fftime_sec(&t2) + tz.off == fftime_sec(&t));
	fftime_split(&dt2, &t2, FFTIME_TZLOCAL);
	x(!memcmp(&dt, &dt2, sizeof(dt)));

	dt.year = 2014;  dt.month = 24;
	dt2.year = 2014+1; dt2.month = 12;
	dt2.weekday = 6; dt2.yday = 353;
	fftime_join(&t, &dt, FFTIME_TZUTC);
	fftime_split(&dt, &t, FFTIME_TZUTC);
	x(!memcmp(&dt, &dt2, sizeof(dt)));

	dt.year = 1969;
	fftime_join(&t, &dt, FFTIME_TZUTC);
	x(fftime_sec(&t) == 0 && fftime_msec(&t) == 0);

	dt.year = 1; dt.month = 1; dt.day = 2;
	dt.weekday = 2;  dt.yday = 2;
	dt.hour = 0; dt.min = 0; dt.sec = 0; dt.nsec = 0;
	fftime_join2(&t, &dt, FFTIME_TZUTC);
	x(fftime_sec(&t) == FFTIME_DAY_SECS && fftime_msec(&t) == 0);
	fftime_split2(&dt2, &t, FFTIME_TZUTC);
	x(!memcmp(&dt, &dt2, sizeof(dt)));
}

int test_time()
{
	char buf[64];
	ffstr s;
	ffdtm dt = {0};
	fftime t, t2;

	FFTEST_FUNC;

	fftime_local(&tz);
	fftime_storelocal(&tz);

	t.sec = 1;  t.nsec = 1;
	t2.sec = 2;  t2.nsec = 2;
	x(fftime_cmp(&t, &t2) < 0);
	t.sec = 1;  t.nsec = 2;
	t2.sec = 1;  t2.nsec = 1;
	x(fftime_cmp(&t, &t2) > 0);

	test_time_dt();

	ffmem_tzero(&dt);
	dt.year = 2014;
	dt.month = 5;
	dt.day = 19;
	dt.weekday = 1;
	dt.hour = 8;
	dt.min = 52;
	dt.sec = 36;
	fftime_setmsec(&dt, 23);
	s.ptr = buf;

	s.len = fftime_tostr(&dt, buf, FFCNT(buf), FFTIME_DATE_YMD);
	x(ffstr_eqcz(&s, "2014-05-19"));

	s.len = fftime_tostr(&dt, buf, FFCNT(buf), FFTIME_DATE_YMD | FFTIME_HMS_MSEC);
	x(ffstr_eqcz(&s, "2014-05-19 08:52:36.023"));

	s.len = fftime_tostr(&dt, buf, FFCNT(buf), FFTIME_HMS_MSEC);
	x(ffstr_eqcz(&s, "08:52:36.023"));

	s.len = fftime_tostr(&dt, buf, FFCNT(buf), FFTIME_WDMY);
	x(ffstr_eqcz(&s, "Mon, 19 May 2014 08:52:36 GMT"));

	{
	ffdtm dt2;
	x(FFSLEN("Mon, 19 May 2014 08:52:36 GMT") == fftime_fromstr(&dt2, FFSTR("Mon, 19 May 2014 08:52:36 GMT"), FFTIME_WDMY));
	dt2.nsec = dt.nsec;
	x(!ffmemcmp(&dt2, &dt, sizeof(ffdtm)));
	x(0 == fftime_fromstr(&dt2, FFSTR("Mon, 19 May 2014 08:52:36 GM"), FFTIME_WDMY));
	x(0 == fftime_fromstr(&dt2, FFSTR("Mon, 19 May 2014 25:52:36 GMT"), FFTIME_WDMY));
	}

	fftime_join(&t, &dt, FFTIME_TZUTC);
	x(fftime_sec(&t) == fftime_strtounix(FFSTR("Mon, 19 May 2014 08:52:36 GMT"), FFTIME_WDMY));
	x((time_t)-1 == fftime_strtounix(FFSTR("Mon, 19 May 201408:52:36 GMT"), FFTIME_WDMY));

	return 0;
}


static void t1_func(const fftime *now, void *param) {
	x(0 != (*(int*)param & 1));
	*(int*)param += 0x0100;
}

static void t2_func(const fftime *now, void *param) {
	x(*(int*)param == 0);
	*(int*)param |= 1;
}

static void t3_func(const fftime *now, void *param) {
	x(*(int*)param == 0x0101);
	*(int*)param |= 2;
}

static void t4_func(const fftime *now, void *param) {
	x(0); //this handler must not be called
}

int test_timerq()
{
	fffd kq;
	ffkqu_time tt;
	ffkqu_entry ent;
	int nevents;
	enum { tmr_resol = 100 };
	fftimer_queue tq;
	fftmrq_entry t1, t2, t3, t4;
	int num = 0;

	FFTEST_FUNC;

	kq = ffkqu_create();
	x(kq != FF_BADFD);
	ffkqu_settm(&tt, -1);

	fftmrq_init(&tq);
	t1.handler = &t1_func;
	t2.handler = &t2_func;
	t3.handler = &t3_func;
	t4.handler = &t4_func;
	t1.param = t2.param = t3.param = &num;
	fftmrq_add(&tq, &t4, 750);
	fftmrq_add(&tq, &t1, 150);
	fftmrq_add(&tq, &t2, -50);
	fftmrq_add(&tq, &t3, -200);

	x(0 == fftmrq_start(&tq, kq, tmr_resol));

	for (;;) {
		nevents = ffkqu_wait(kq, &ent, 1, &tt);

		if (nevents > 0) {
			ffkev_call(&ent);
		}

		if (nevents == -1 && fferr_last() != EINTR) {
			x(0);
			break;
		}

		if (num == 0x0303)
			break;
	}

	fftmrq_destroy(&tq, kq);
	x(0 == ffkqu_close(kq));
	return 0;
}
