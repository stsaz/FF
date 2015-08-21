/**
Copyright (c) 2014 Simon Zolin
*/

#include <FFOS/test.h>
#include <FF/array.h>
#include <FF/time.h>
#include <FF/timer-queue.h>
#include <test/all.h>

#define x FFTEST_BOOL


int test_time()
{
	char buf[64];
	ffstr s;
	ffdtm dt = {0};
	fftime t;

	FFTEST_FUNC;

	dt.year = 2014;
	dt.month = 5;
	dt.day = 19;
	dt.weekday = 1;
	dt.hour = 8;
	dt.min = 52;
	dt.sec = 36;
	dt.msec = 23;
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
	dt2.msec = dt.msec;
	x(!ffmemcmp(&dt2, &dt, sizeof(ffdtm)));
	x(0 == fftime_fromstr(&dt2, FFSTR("Mon, 19 May 2014 08:52:36 GM"), FFTIME_WDMY));
	x(0 == fftime_fromstr(&dt2, FFSTR("Mon, 19 May 2014 25:52:36 GMT"), FFTIME_WDMY));
	}

	x(fftime_join(&t, &dt, FFTIME_TZUTC)->s == fftime_strtounix(FFSTR("Mon, 19 May 2014 08:52:36 GMT"), FFTIME_WDMY));
	x((uint)-1 == fftime_strtounix(FFSTR("Mon, 19 May 201408:52:36 GMT"), FFTIME_WDMY));

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
	const ffkqu_time *kqtm;
	ffkqu_entry ent;
	int nevents;
	enum { tmr_resol = 100 };
	fftimer_queue tq;
	fftmrq_entry t1, t2, t3, t4;
	int num = 0;

	FFTEST_FUNC;

	kq = ffkqu_create();
	x(kq != FF_BADFD);
	kqtm = ffkqu_settm(&tt, -1);

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
		nevents = ffkqu_wait(kq, &ent, 1, kqtm);

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
