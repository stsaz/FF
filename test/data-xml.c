
#include <test/all.h>
#include <FF/data/xml.h>
#include <FFOS/test.h>

#define x FFTEST_BOOL

static void xml_parse()
{
	int r, rr = 0, tag = 0, attr = 0;
	ffxml xml = {};
	ffarr fdata = {};
	fffile_readall(&fdata, TESTDATADIR "/1.html", -1);
	ffstr d;
	ffstr_set2(&d, &fdata);

	while (d.len != 0) {
		r = ffxml_parsestr(&xml, &d);

		if (r == FFPARS_MORE)
			continue;

		switch (xml.type) {
		case FFXML_TEXT:
			x(ffstr_eqz(&xml.val, "TEXT"));
			rr |= 1;
			break;

		case FFXML_TAG_OPEN:
			if (tag == 0) {
				x(ffstr_eqz(&xml.val, "html"));
			} else if (tag == 1) {
				x(ffstr_eqz(&xml.val, "img"));
			}
			tag++;
			rr |= 2;
			break;

		case FFXML_TAG:
			rr |= 0x40;
			break;

		case FFXML_TAG_CLOSE:
			x(ffstr_eqz(&xml.val, ""));
			rr |= 4;
			break;

		case FFXML_TAG_CLOSE_NAME:
			x(ffstr_eqz(&xml.val, "html"));
			rr |= 8;
			break;

		case FFXML_TAG_ATTR:
			if (attr == 0) {
				x(ffstr_eqz(&xml.val, "src"));
			} else if (attr == 1) {
				x(ffstr_eqz(&xml.val, "width"));
			} else if (attr == 2) {
				x(ffstr_eqz(&xml.val, "height"));
			}
			rr |= 0x10;
			break;

		case FFXML_TAG_ATTR_VAL:
			if (attr == 0) {
				x(ffstr_eqz(&xml.val, "img-src"));
			} else if (attr == 1) {
				x(ffstr_eqz(&xml.val, "100"));
			}
			attr++;
			rr |= 0x20;
			break;
		}
	}
	x(ffstr_eqz(&xml.buf, "\n"));
	ffarr_free(&fdata);
	x(rr == 0x7f);
	x(tag == 2);
	x(attr == 3);
	ffxml_close(&xml);
}

int test_xml(void)
{
	char buf[64];
	ffstr s;
	FFTEST_FUNC;

	s.ptr = buf;
	x(FFSLEN("hello&lt;&gt;&amp;&quot;hi") == ffxml_escape(NULL, 0, FFSTR("hello<>&\"hi")));
	s.len = ffxml_escape(buf, FFCNT(buf), FFSTR("hello<>&\"hi"));
	x(ffstr_eqcz(&s, "hello&lt;&gt;&amp;&quot;hi"));

	xml_parse();

	return 0;
}
