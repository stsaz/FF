/** ff: DNS constants and message read/write functions
2020, Simon Zolin
*/

/*
ffdns_header_read
ffdns_header_write
ffdns_name_read
ffdns_name_write
ffdns_question_destroy
ffdns_question_read
ffdns_question_write
ffdns_answer_destroy
ffdns_answer_read
ffdns_answer_write
*/

/* Message format:
HDR
QUESTION(NAME TYPE CLASS)
[ANSWER(NAME TYPE CLASS TTL LENGTH DATA)]...
[NS]...
[ADD]...

NAME:
 (LENGTH SUBNAME)... 0x00
*/

#pragma once

#include <FF/string.h>
#include <ffbase/vector.h>


/** Response code. */
enum FFDNS_R {
	FFDNS_NOERROR,
	FFDNS_FORMERR,
	FFDNS_SERVFAIL,
	FFDNS_NXDOMAIN,
	FFDNS_NOTIMP,
	FFDNS_REFUSED,
};

/** Response code string */
static inline const char* ffdns_rcode_str(ffuint rcode)
{
	static const char *const rcode_str[] = {
		"NOERROR",
		"FORMERR",
		"SERVFAIL",
		"NXDOMAIN",
		"NOTIMP",
		"REFUSED",
	};
	if (rcode > FF_COUNT(rcode_str))
		return "unknown";
	return rcode_str[rcode];
}

enum FFDNS_OP {
	FFDNS_OPQUERY = 0,
};

/** Header structure. */
struct ffdns_hdr {
	ffbyte id[2]; //query ID

#if defined FF_BIG_ENDIAN
	ffbyte qr :1 //response flag
		, opcode :4 //operation type
		, aa :1 //authoritive answer
		, tc :1 //truncation
		, rd :1 //recursion desired

		, ra :1 //recursion available
		, reserved :1
		, ad :1 //authenticated data (DNSSEC)
		, cd :1 //checking disabled (DNSSEC)
		, rcode :4; //response code

#elif defined FF_LITTLE_ENDIAN
	ffbyte rd :1
		, tc :1
		, aa :1
		, opcode :4
		, qr :1

		, rcode :4
		, cd :1
		, ad :1
		, reserved :1
		, ra :1;
#endif

	ffbyte qdcount[2] //question entries
		, ancount[2] //answer entries
		, nscount[2] //authority entries
		, arcount[2]; //additional entries
};

/** Type. */
enum FFDNS_TYPE {
	FFDNS_A = 1,
	FFDNS_AAAA = 28,
	FFDNS_NS = 2,
	FFDNS_CNAME = 5,
	FFDNS_PTR = 12, // for ip=1.2.3.4 ques.name = 4.3.2.1.in-addr.arpa
	FFDNS_OPT = 41, //EDNS
};

/** Class. */
enum FFDNS_CLASS {
	FFDNS_IN = 1,
};

/** Question. */
struct ffdns_ques {
	//char name[]
	ffbyte type[2];
	ffbyte clas[2];
};

/** Answer. */
struct ffdns_ans {
	//char name[]
	ffbyte type[2];
	ffbyte clas[2];
	ffbyte ttl[4]; //31-bit value
	ffbyte len[2];
	//char data[]
};

struct ffdns_opt {
	ffbyte type[2];
	ffbyte maxmsg[2];
	ffbyte extrcode;
	ffbyte ver;
	ffbyte flags[2]; //dnssec[1] flags[15]

	ffbyte len[2];
};

/** Initialize OPT record. */
static inline int ffdns_optinit(void *buf, ffuint max_size)
{
	char *d = (char*)buf;
	d[0] = 0x00;
	struct ffdns_opt *opt = (struct ffdns_opt*)(&d[1]);
	ffmem_zero_obj(opt);
	opt->type[1] = FFDNS_OPT;
	*(ffushort*)opt->maxmsg = ffint_be_cpu16(max_size);
	return 1 + sizeof(struct ffdns_opt);
}

enum FFDNS_CONST {
	FFDNS_MAXNAME = 255 //max length of binary representation
	, FFDNS_MAXLABEL = 63
	, FFDNS_MAXMSG = 512 //maximum for DNS.  minimum for EDNS.
	, FFDNS_PORT = 53
};

/** Skip name and shift the current position. */
FF_EXTN void ffdns_name_skip(const char *begin, size_t len, const char **pos);


typedef struct ffdns_header {
	ffuint id;

	union {
	ffbyte flags[2];
	struct {
#if defined FF_BIG_ENDIAN
	ffbyte response :1 //response flag
		, opcode :4 // enum FFDNS_OP
		, authoritive :1
		, truncation :1 //truncation
		, recursion_desired :1

		, recursion_available :1
		, reserved :1
		, ad :1 //authenticated data (DNSSEC)
		, cd :1 //checking disabled (DNSSEC)
		, rcode :4; // enum FFDNS_R

#elif defined FF_LITTLE_ENDIAN
	ffbyte recursion_desired :1
		, truncation :1
		, authoritive :1
		, opcode :4
		, response :1

		, rcode :4
		, cd :1
		, ad :1
		, reserved :1
		, recursion_available :1;
#endif
	};
	};

	ffuint questions,
		answers,
		nss,
		additionals;
} ffdns_header;

/** Parse DNS header
Return offset of the next record;
  <0 on error */
static inline int ffdns_header_read(ffdns_header *h, ffstr data)
{
	if (data.len < sizeof(struct ffdns_hdr))
		return -1; // too small message
	if (h == NULL)
		return sizeof(struct ffdns_hdr);
	const struct ffdns_hdr *hn = (struct ffdns_hdr*)data.ptr;
	h->id = ffint_be_cpu16_ptr(hn->id);
	*(ffushort*)h->flags = *(ffushort*)&data.ptr[2];
	h->questions = ffint_be_cpu16_ptr(hn->qdcount);
	h->answers= ffint_be_cpu16_ptr(hn->ancount);
	h->nss = ffint_be_cpu16_ptr(hn->nscount);
	h->additionals = ffint_be_cpu16_ptr(hn->arcount);
	return sizeof(struct ffdns_hdr);
}

/** Write DNS header
Return N of bytes written;
  -1 on error */
static inline int ffdns_header_write(void *dst, ffsize cap, const ffdns_header *h)
{
	if (sizeof(struct ffdns_hdr) > cap)
		return -1;
	struct ffdns_hdr *hn = (struct ffdns_hdr*)dst;
	*(ffushort*)hn->id = ffint_be_cpu16(h->id);
	char *d = (char*)dst;
	*(ffushort*)&d[2] = *(ffushort*)h->flags;
	*(ffushort*)hn->qdcount = ffint_be_cpu16(h->questions);
	*(ffushort*)hn->ancount = ffint_be_cpu16(h->answers);
	*(ffushort*)hn->nscount = ffint_be_cpu16(h->nss);
	*(ffushort*)hn->arcount = ffint_be_cpu16(h->additionals);
	return sizeof(struct ffdns_hdr);
}

/** Read name from DNS record
Uncompress data if necessary
dst: name buffer;  NULL: just skip data
Return N of bytes read;
  -1 on error */
static inline int ffdns_name_read(ffvec *dst, ffstr msg, ffuint offset)
{
	if (dst == NULL) {
		for (ffuint i = offset;  i < msg.len;) {
			int c = msg.ptr[i];

			if (c & 0xc0) { // compressed

				if (i + 1 == msg.len)
					return -1; // incomplete data

				return i + 2 - offset;
			}

			if (c == '\0')
				return i + 1 - offset;

			i += 1 + c;
		}
		return -1;
	}

	dst->len = 0;
	if (NULL == ffvec_growT(dst, FFDNS_MAXNAME, char))
		return -1; // no mem
	char *d = (char*)dst->ptr;

	ffuint n = 0, jumps = 0;
	for (ffuint i = offset;  i != msg.len;) {
		int c = msg.ptr[i];
		if (n == 0) {
			if (c == 0x00) {
				if (dst->len == 0)
					d[dst->len++] = '.';
				if (jumps == 0)
					offset = i + 1 - offset;
				break;
			}

			if (c & 0xc0) { // compressed

				if (jumps == 0)
					offset = i + 2 - offset;
				jumps++;
				if (jumps == 30)
					goto fail; // too many jumps

				if (i + 1 == msg.len)
					goto fail; // incomplete data
				i = ((c & 0x3f) << 8) | (ffbyte)msg.ptr[i + 1];
				if (i >= msg.len)
					goto fail; // offset too large
				continue;
			}

			n = c; // get label length

		} else {
			if (dst->len == FFDNS_MAXNAME-1)
				goto fail; // too long name
			d[dst->len++] = c;
			n--;
			if (n == 0)
				d[dst->len++] = '.';
		}
		i++;
	}

	if (dst->len == 0 || n != 0) { // incomplete data
fail:
		ffvec_free(dst);
		return -1;
	}

	return offset;
}

/** Write "name.com." in DNS format "\4 name \3 com \0"
name: may or may not have the trailing '.' char
Return the number of bytes written;
  0 on error */
static inline ffuint ffdns_name_write(char *dst, ffsize cap, const char *name, ffsize len)
{
	int i = len - 1;
	// get output length
	ffuint dstlen;
	if (len == 0) {
		dstlen = 1; // ""
	} else if (name[len - 1] == '.') {
		if (len == 1)
			dstlen = 1; // "."
		else
			dstlen = len + 1; // "name."
		i--; // skip last '.'
	} else {
		dstlen = len + 2; // name without last '.'
	}

	if (dstlen > FFDNS_MAXNAME)
		return 0; // too long name
	if (dstlen > cap)
		return 0; // too small buffer

	ffuint k = dstlen, n = 0;
	dst[--k] = 0x00;
	if (i < 0)
		return 1; // "."

	for (;  i >= 0;  i--) {
		if (name[i] == '.') {
			if (n == 0)
				return 0; // ".." is not allowed
			dst[--k] = n;
			n = 0;

		} else {
			if (n == FFDNS_MAXLABEL)
				return 0; // too long label

			dst[--k] = name[i];
			n++;
		}
	}

	if (n == 0)
		return 0; // name starts with '.'

	dst[--k] = n;
	return dstlen;
}

typedef struct ffdns_question {
	ffvec name;
	ffuint type;
	ffuint clas;
} ffdns_question;

static inline void ffdns_question_destroy(ffdns_question *q)
{
	ffvec_free(&q->name);
}

/** Parse DNS question record
Return N of bytes read;
  <0 on error */
static inline int ffdns_question_read(ffdns_question *q, ffstr data)
{
	if (data.len < sizeof(struct ffdns_hdr))
		return -1; // too small message
	const struct ffdns_hdr *h = (struct ffdns_hdr*)data.ptr;
	ffuint nq = ffint_be_cpu16_ptr(h->qdcount);
	if (nq != 1)
		return -1; // must have 1 question

	int r = ffdns_name_read(&q->name, data, sizeof(struct ffdns_hdr));
	if (r < 0)
		return -1;

	if (sizeof(struct ffdns_hdr) + r + sizeof(struct ffdns_ques) > data.len)
		return -1; // too small message
	if (q == NULL)
		return r + 4;

	const char *p = data.ptr + sizeof(struct ffdns_hdr) + r;
	q->type = ffint_be_cpu16_ptr(&p[0]);
	q->clas = ffint_be_cpu16_ptr(&p[2]);
	return r + 4;
}

/** Write DNS question record
Return N of bytes written;
  -1 on error */
static inline int ffdns_question_write(void *dst, ffsize cap, const ffdns_question *q)
{
	char *d = (char*)dst;
	int r = ffdns_name_write(d, cap, (char*)q->name.ptr, q->name.len);
	if (r == 0)
		return -1;
	if (r + sizeof(struct ffdns_ques) > cap)
		return -1; // too small buffer

	struct ffdns_ques *qn = (struct ffdns_ques*)(&d[r]);
	*(ffushort*)qn->type = ffint_be_cpu16(q->type);
	*(ffushort*)qn->clas = ffint_be_cpu16(q->clas);
	return r + sizeof(struct ffdns_ques);
}

typedef struct ffdns_answer {
	ffvec name;
	ffuint type,
		clas,
		ttl;
	ffstr data;
} ffdns_answer;

static inline void ffdns_answer_destroy(ffdns_answer *a)
{
	ffvec_free(&a->name);
}

/** Parse next DNS answer record
Return N of bytes read;
  <0 on error */
static inline int ffdns_answer_read(ffdns_answer *a, ffstr data, ffuint offset)
{
	int r = ffdns_name_read(&a->name, data, offset);
	if (r < 0)
		return -1;
	offset += r;

	if (offset + sizeof(struct ffdns_ans) > data.len)
		return -1; // too small message

	const struct ffdns_ans *an = (struct ffdns_ans*)(data.ptr + offset);
	ffuint len = ffint_be_cpu16_ptr(an->len);
	if (offset + sizeof(struct ffdns_ans) + len > data.len)
		return -1; // too small message

	if (a == NULL)
		return r + sizeof(struct ffdns_ans) + len;

	a->type = ffint_be_cpu16_ptr(an->type);
	a->clas = ffint_be_cpu16_ptr(an->clas);

	a->ttl = ffint_be_cpu32_ptr(an->ttl);
	if ((int)a->ttl < 0)
		a->ttl = 0;

	ffstr_set(&a->data, (char*)an + sizeof(struct ffdns_ans), len);
	return r + sizeof(struct ffdns_ans) + len;
}

/** Write DNS answer record
Return N of bytes written;
  -1 on error */
static inline int ffdns_answer_write(void *dst, ffsize cap, const ffdns_answer *a)
{
	char *d = (char*)dst;
	int r = ffdns_name_write(d, cap, (char*)a->name.ptr, a->name.len);
	if (r == 0)
		return -1;
	if (r + sizeof(struct ffdns_ans) + a->data.len > cap)
		return -1; // too small buffer

	struct ffdns_ans *an = (struct ffdns_ans*)(&d[r]);
	*(ffushort*)an->type = ffint_be_cpu16(a->type);
	*(ffushort*)an->clas = ffint_be_cpu16(a->clas);
	*(ffuint*)an->ttl = ffint_be_cpu32(a->ttl);
	*(ffushort*)an->len = ffint_be_cpu16(a->data.len);

	ffmem_copy(&d[r + sizeof(struct ffdns_ans)], a->data.ptr, a->data.len);

	return r + sizeof(struct ffdns_ans) + a->data.len;
}
