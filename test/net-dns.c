/** ff: dns.h tests
2020, Simon Zolin
*/

#include <FF/net/dns.h>
#include <FFOS/test.h>

static void test_dns_name_read()
{
	ffvec name = {};
	ffstr msg;

	// simple
	ffstr_setcz(&msg, "\3www\2my\3dot\3com\0");
	xieq(msg.len, ffdns_name_read(&name, msg, 0));
	xseq((ffstr*)&name, "www.my.dot.com.");
	ffvec_free(&name);

	// compression: forward
	ffstr_setcz(&msg, "\3www\xc0\6" "\2my\3dot\3com\0");
	xieq(FFS_LEN("\3www\xc0\6"), ffdns_name_read(&name, msg, 0));
	xseq((ffstr*)&name, "www.my.dot.com.");
	ffvec_free(&name);

	// compression: backward
	ffstr_setcz(&msg, "\2my\3dot\3com\0" "\3www\xc0\0");
	xieq(FFS_LEN("\3www\xc0\0"), ffdns_name_read(&name, msg, FFS_LEN("\2my\3dot\3com\0")));
	xseq((ffstr*)&name, "www.my.dot.com.");
	ffvec_free(&name);

	// compression: backward x2
	ffstr_setcz(&msg, "\3dot\3com\0\2my\xc0\0\3www\xc0\x09");
	xieq(FFS_LEN("\3www\xc0\0"), ffdns_name_read(&name, msg, FFS_LEN("\3dot\3com\0\2my\xc0\0")));
	xseq((ffstr*)&name, "www.my.dot.com.");
	ffvec_free(&name);

	// root
	ffstr_setcz(&msg, "\0");
	xieq(1, ffdns_name_read(&name, msg, 0));

	// empty
	ffstr_setcz(&msg, "");
	xieq(-1, ffdns_name_read(&name, msg, 0));

	// too large label
	ffstr_setcz(&msg, "\x40");
	xieq(-1, ffdns_name_read(&name, msg, 0));

	// too small message
	ffstr_setcz(&msg, "\x03");
	xieq(-1, ffdns_name_read(&name, msg, 0));

	// compression: too large offset
	ffstr_setcz(&msg, "\3www\xc0\7");
	xieq(-1, ffdns_name_read(&name, msg, 0));

	// compression: dead loop
	ffstr_setcz(&msg, "\3www\xc0\0\0");
	xieq(-1, ffdns_name_read(&name, msg, 0));

	// skip name
	ffstr_setcz(&msg, "\3www\0asdf");
	xieq(FFS_LEN("\3www\0"), ffdns_name_read(NULL, msg, 0));
	ffstr_setcz(&msg, "\3www\xc0\0asdf");
	xieq(FFS_LEN("\3www\xc0\0"), ffdns_name_read(NULL, msg, 0));
}

static void test_dns_name_write()
{
	char buf[FFDNS_MAXNAME];
	ffstr s;
	s.ptr = buf;

	s.len = ffdns_name_write(buf, FF_COUNT(buf), FFSTR("."));
	x(ffstr_eqcz(&s, "\0"));

	s.len = ffdns_name_write(buf, FF_COUNT(buf), FFSTR(""));
	x(ffstr_eqcz(&s, "\0"));

	s.len = ffdns_name_write(buf, FF_COUNT(buf), FFSTR("www.my.dot.com."));
	x(ffstr_eqcz(&s, "\3www\2my\3dot\3com\0"));

	s.len = ffdns_name_write(buf, FF_COUNT(buf), FFSTR("www.my.dot.com"));
	x(ffstr_eqcz(&s, "\3www\2my\3dot\3com\0"));

	s.len = ffdns_name_write(buf, FF_COUNT(buf), FFSTR("a.012345678901234567890123456789012345678901234567890123012345678."));
	x(ffstr_eqcz(&s, "\1a\077012345678901234567890123456789012345678901234567890123012345678\0"));

	x(0 == ffdns_name_write(buf, FF_COUNT(buf), FFSTR("www.my.dot.com..")));
	x(0 == ffdns_name_write(buf, FF_COUNT(buf), FFSTR(".www.my.dot.com.")));
	x(0 == ffdns_name_write(buf, FF_COUNT(buf), FFSTR("www..my.dot.com.")));
	x(0 == ffdns_name_write(buf, FF_COUNT(buf), FFSTR("0123456789012345678901234567890123456789012345678901230123456789.")));
}

void test_dns_rw()
{
	char buf[FFDNS_MAXMSG];

	ffdns_header h = {};
	h.id = 0x1234;
	h.response = 1;
	h.questions = 1;
	h.answers = 1;

	ffdns_question q = {};
	ffstr_setz(&q.name, "my.host.com");
	q.clas = FFDNS_IN;
	q.type = FFDNS_A;

	ffdns_answer a = {};
	ffstr_setz(&a.name, "my.host.com");
	a.clas = FFDNS_IN;
	a.type = FFDNS_A;
	a.ttl = 12345;
	ffstr_set(&a.data, "\x7f\x00\x00\x01", 4);

	ffuint cap = sizeof(buf);
	int r;
	ffuint i = 0;
	x(0 < (r = ffdns_header_write(&buf[i], cap - i, &h)));
	i += r;
	x(0 < (r = ffdns_question_write(&buf[i], cap - i, &q)));
	i += r;
	x(0 < (r = ffdns_answer_write(&buf[i], cap - i, &a)));
	i += r;

	ffmem_zero_obj(&h);
	ffmem_zero_obj(&q);
	ffmem_zero_obj(&a);
	ffstr data = FFSTR_INITN(buf, i);

	i = 0;
	x(0 < (r = ffdns_header_read(&h, data)));
	i += r;
	x(0 < (r = ffdns_question_read(&q, data)));
	i += r;
	x(0 < (r = ffdns_answer_read(&a, data, i)));
	i += r;

	xieq(0x1234, h.id);
	xieq(1, h.response);
	xieq(1, h.questions);
	xieq(1, h.answers);

	xseq((ffstr*)&q.name, "my.host.com.");
	xieq(FFDNS_IN, q.clas);
	xieq(FFDNS_A, q.type);

	xseq((ffstr*)&a.name, "my.host.com.");
	xieq(FFDNS_IN, a.clas);
	xieq(FFDNS_A, a.type);
	x(ffstr_eq((ffstr*)&a.data, "\x7f\x00\x00\x01", 4));

	ffdns_question_destroy(&q);
	ffdns_answer_destroy(&a);
}

void test_dns()
{
	test_dns_name_read();
	test_dns_name_write();
	test_dns_rw();
}
