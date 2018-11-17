/** Regular expression processor.
Copyright (c) 2015 Simon Zolin
*/

#include <FF/string.h>


enum RX_ST {
	RX_MATCH, RX_NMATCH_1, RX_NMATCH
	, RX_BRACKET_1, RX_BRACKET, RX_BRACKET_RANGE, RX_BRACKET_2
};

struct brkt_s {
	int prev
		, last_state;
	unsigned match :1;
};

struct rx_s {
	enum RX_ST st;
	const char *rx
		, *rx_end
		, *input
		, *input_end;
	struct brkt_s brkt;
	unsigned esc :1;
};

#define RX_ESC_CHARS "\\.|?[-]"

static int _ffs_regex_bracket(struct rx_s *r)
{
	if (r->esc) {
		if (NULL == ffs_findc(RX_ESC_CHARS, FFSLEN(RX_ESC_CHARS), *r->rx))
			goto inv_regex; //unknown escape sequence

		r->esc = 0;
		goto basic;
	}

	switch (*r->rx) {
	case '\\':
		r->esc = 1;
		return 0;

	case ']':
		if (r->st != RX_BRACKET_2)
			goto inv_regex; //incorrect bracket close

		r->st = r->brkt.last_state;
		if (r->st == RX_MATCH && !r->brkt.match) {
			r->st = RX_NMATCH_1;
			return 0;
		}
		r->brkt.match = 0;
		return 0;

	case '[':
		if (r->st == RX_BRACKET_1)
			break;
		//fallthrough

	case '|':
	case '.':
	case '?':
		goto inv_regex; //invalid character within brackets
	}

	switch (r->st) {

	case RX_BRACKET_1:
		switch (*r->rx) {
		case '-':
			goto inv_regex; //missing a starting range character, e.g. "[-"
		}
		break;

	case RX_BRACKET_2:
	case RX_BRACKET:
		if (*r->rx == '-') {
			r->st = RX_BRACKET_RANGE;
			return 0;
		}
		break;

	case RX_BRACKET_RANGE:
		if (*r->rx == '-')
			goto inv_regex; //two consecutive minus characters, e.g. "[a--"
		break;

	default:
		break;
	}


basic:
	switch (r->st) {

	case RX_BRACKET_1:
	case RX_BRACKET_2:
		if (r->rx_end - r->rx > 1 && r->rx[1] == '-') {
			r->brkt.prev = *r->rx;
			r->st = RX_BRACKET_RANGE;
			r->rx++;
			return 0;
		}
		r->st = RX_BRACKET;
		//fallthrough

	case RX_BRACKET:
	case RX_BRACKET_RANGE:
		if (r->brkt.last_state != RX_MATCH || r->brkt.match) {

		} else if (r->input == r->input_end) {
			r->brkt.last_state = RX_NMATCH_1;

		} else if (r->st == RX_BRACKET_RANGE) {
			int from = r->brkt.prev, to = *r->rx;
			if (*r->input >= from && *r->input <= to) {
				r->input++;
				r->brkt.match = 1;
			}

		} else if (*r->rx == *r->input) {
			r->brkt.prev = *r->rx;
			r->input++;
			r->brkt.match = 1;
		}

		r->st = RX_BRACKET_2;
		break;

	default:
		break;
	}

	return 0;

inv_regex:
	return -1;
}

int ffs_regex(const char *regexp, size_t regexp_len, const char *s, size_t len, uint flags)
{
	struct rx_s r = {0};
	int n;

	r.st = RX_MATCH;
	r.rx = regexp;
	r.rx_end = regexp + regexp_len;
	r.input = s;
	r.input_end = s + len;

	for (;  r.rx != r.rx_end;  r.rx++) {

		if (r.st >= RX_BRACKET_1 && r.st <= RX_BRACKET_2) {
			n = _ffs_regex_bracket(&r);
			if (n < 0)
				goto inv_regex;
			continue;
		}

		if (r.esc) {
			if (NULL == ffs_findc(RX_ESC_CHARS, FFSLEN(RX_ESC_CHARS), *r.rx))
				goto inv_regex; //unknown escape sequence

			r.esc = 0;
			goto basic;
		}

		//process special characters
		switch (*r.rx) {
		case '\\':
			//the next char is escaped
			r.esc = 1;
			continue;

		case '?':
			//the previous char was optional
			if (r.st == RX_NMATCH_1) {
				//the previous char was a mismatch
				r.st = RX_MATCH;
			}
			continue;

		case '[':
			r.brkt.last_state = r.st;
			r.st = RX_BRACKET_1;
			continue;

		case ']':
			goto inv_regex; //unmatched bracket

		case '|':
			if (r.st == RX_MATCH && r.input == r.input_end)
				return 0; //the previous word has matched

			//try the next word after a pipe
			r.input = s;
			r.st = RX_MATCH;
			continue;

		case '.':
			if (r.st == RX_MATCH) {
				if (r.input != r.input_end)
					r.input++;
				else
					r.st = RX_NMATCH_1;
				continue;
			}
			break;
		}

basic:
		switch (r.st) {
		case RX_MATCH:
			if (r.input != r.input_end && *r.rx == *r.input)
				r.input++;
			else
				r.st = RX_NMATCH_1;
			break;

		case RX_NMATCH_1:
			r.st = RX_NMATCH;
			//fallthrough

		case RX_NMATCH:
			break;

		default:
			break;
		}
	}

	if (r.st == RX_MATCH)
		return (r.input == r.input_end) ? 0 : 1;

	return (r.st == RX_NMATCH || r.st == RX_NMATCH_1) ? 1 : -1;

inv_regex:
	return -1;
}
