/**
Copyright (c) 2013 Simon Zolin
*/

#include <FFOS/types.h>
#include <FFOS/string.h>


FF_EXTN size_t _test_readfile(const char *fn, char *buf, size_t n);

FF_EXTN int test_str();
FF_EXTN int test_rbt(void);
FF_EXTN int test_rbtlist();
FF_EXTN int test_list();
FF_EXTN int test_htable();

FF_EXTN int test_url();
FF_EXTN int test_http();
FF_EXTN int test_json();
FF_EXTN int test_conf();
FF_EXTN int test_args();

FF_EXTN int test_time();
FF_EXTN int test_timerq();

FF_EXTN int test_fmap(void);
FF_EXTN int test_sendfile(void);
FF_EXTN int test_direxp(void);
FF_EXTN int test_env(void);

#define TESTDIR "./test"
#ifndef TESTDATADIR
#define TESTDATADIR "./test/data"
#endif

#ifdef FF_UNIX
#define TMPDIR "/tmp"
#else
#define TMPDIR "%TMP%"
#endif
