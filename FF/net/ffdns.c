/**
Copyright (c) 2013 Simon Zolin
*/

#include <FF/net/dns.h>


static const char *const dnserrs[] = {
	"ok", "bad format", "server fail"
	, "not found", "not implemented", "refused"
};

const char * ffdns_errstr(uint code)
{
	if (code > FFDNS_REFUSED)
		return "unknown";
	FF_ASSERT(code <= FFCNT(dnserrs));
	return dnserrs[code];
}

void ffdns_hdrtohost(ffdns_hdr_host *host, const void *net)
{
	const ffdns_hdr *hdr = net;
	host->id = ffint_ntoh16(hdr->id);
	host->rcode = hdr->rcode;
	host->qdcount = ffint_ntoh16(hdr->qdcount);
	host->ancount = ffint_ntoh16(hdr->ancount);
	host->nscount = ffint_ntoh16(hdr->nscount);
	host->arcount = ffint_ntoh16(hdr->arcount);
}

void ffdns_anstohost(ffdns_ans_host *host, const void *net)
{
	const ffdns_ans *ans = net;
	host->type = ffint_ntoh16(ans->type);
	host->clas = ffint_ntoh16(ans->clas);

	host->ttl = ffint_ntoh32(ans->ttl);
	if ((int)host->ttl < 0)
		host->ttl = 0;

	host->len = ffint_ntoh16(ans->len);
	host->data = (char*)ans + sizeof(ffdns_ans);
}

void ffdns_skipname(const char *begin, size_t len, const char **pos)
{
	const char *p;
	const char *end = begin + len;
	for (p = *pos;  p < end; ) {
		uint b = (byte)*p;

		if (b & 0xc0) {
			// compressed label
			p += 2;
			break;
		}

		p += 1 + b;

		if (b == '\0')
			break;
	}

	if (p > end)
		p = end;
	*pos = p;
}

uint ffdns_name(char *nm, size_t cap, const char *begin, size_t len, const char **pos)
{
	enum { MAX_JUMPS = 128 };
	const char *p;
	uint sz = 0;
	uint inm = 0;
	uint j = MAX_JUMPS;
	const char *end = begin + len;

	for (p = *pos;  p != end;  p++) {
		uint b = (byte)*p;

		if (sz == 0) {
			if (b == '\0') {
				if (j == MAX_JUMPS)
					*pos = p + 1;
				return inm;
			}

			if ((b & 0xc0) == 0) {
				if (b > FFDNS_MAXLABEL)
					return 0; // too long label

				sz = b; // get label size

			} else {
				// compressed label
				uint off;

				if (p + 1 == end || j == 0)
					return 0; // incomplete data or reached jump limit

				if (j == MAX_JUMPS)
					*pos = p + 2;
				j--;

				off = ((b & 0x3f) << 8) | (byte)p[1];
				if (off >= len)
					return 0; // offset beyond bounds

				p = begin + off - 1; // jump to offset
			}

		} else {
			nm[inm++] = b;
			sz--;
			if (sz == 0 && inm != cap)
				nm[inm++] = '.';
			if (inm == cap)
				return 0; // too small buffer
		}
	}

	return 0; // incomplete data
}

uint ffdns_encodename(char *dst, size_t cap, const char *name, size_t len)
{
	uint sz = 0;
	const char *nm = name + (len - 1);

	if (len == 0)
		return 0;
	if (cap < len + 1)
		return 0; // too small buffer

	if (*name == '.' && len == 1) {
		*dst = '\0';
		return 1;
	}

	dst += len;
	if (*nm == '.') {
		*dst-- = '\0';
		nm--;
	}

	for (;  nm >= name;  nm--) {
		if (*nm == '.') {
			if (sz == 0)
				return 0; // '..' is not allowed
			*dst-- = (byte)sz;
			sz = 0;

		} else {
			if (sz == FFDNS_MAXLABEL)
				return 0; // too long label

			*dst-- = *nm;
			sz++;
		}
	}

	if (sz == 0)
		return 0;
	*dst = (byte)sz;

	return (uint)len + 1;
}

uint ffdns_addquery(char *pbuf, size_t cap, const char *name, size_t len, int type)
{
	ffdns_ques *q;
	uint idst = 0;
	uint sz;

	sz = ffdns_encodename(pbuf, cap, name, len);
	if (sz == 0)
		return 0;
	idst += sz;

	if (idst + sizeof(ffdns_ques) + 1 > cap)
		return 0;

	if (name[len - 1] != '.')
		pbuf[idst++] = '\0';

	q = (ffdns_ques*)(pbuf + idst);
	ffint_hton16(q->type, type);
	ffint_hton16(q->clas, FFDNS_IN);

	return idst + sizeof(ffdns_ques);
}
