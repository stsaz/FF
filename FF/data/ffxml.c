/**
Copyright (c) 2014 Simon Zolin
*/

#include <FF/data/xml.h>
#include <FF/string.h>


size_t ffxml_escape(char *dst, size_t cap, const char *s, size_t len)
{
	char *p = dst;
	size_t i;

	if (dst == NULL) {
		size_t n = 0;
		for (i = 0;  i < len;  ++i) {
			switch (s[i]) {
			case '<':
				n += FFSLEN("&lt;") - 1;
				break;

			case '>':
				n += FFSLEN("&gt;") - 1;
				break;

			case '&':
				n += FFSLEN("&amp;") - 1;
				break;

			case '"':
				n += FFSLEN("&quot;") - 1;
				break;
			}
		}
		return len + n;
	}

	for (i = 0;  i < len;  ++i) {
		switch (s[i]) {
		case '<':
			p = ffmem_copycz(p, "&lt;");
			break;

		case '>':
			p = ffmem_copycz(p, "&gt;");
			break;

		case '&':
			p = ffmem_copycz(p, "&amp;");
			break;

		case '"':
			p = ffmem_copycz(p, "&quot;");
			break;

		default:
			*p++ = s[i];
		}
	}
	return p - dst;
}
