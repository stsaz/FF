/** DNS.
Copyright (c) 2013 Simon Zolin
*/

#pragma once

#include <FF/number.h>
#include <FFOS/mem.h>


/** Response code. */
enum FFDNS_R {
	FFDNS_NOERROR = 0
	, FFDNS_FORMERR
	, FFDNS_SERVFAIL
	, FFDNS_NXDOMAIN
	, FFDNS_NOTIMP
	, FFDNS_REFUSED
};

/** Get error string. */
FF_EXTN const char * ffdns_errstr(uint code);

enum FFDNS_OP {
	FFDNS_OPQUERY = 0
};

/** Header structure. */
typedef struct ffdns_hdr {
	byte id[2]; //query ID

#if defined FF_BIG_ENDIAN
	byte qr :1 //response flag
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
	byte rd :1
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

	byte qdcount[2] //question entries
		, ancount[2] //answer entries
		, nscount[2] //authority entries
		, arcount[2]; //additional entries
} ffdns_hdr; //12 bytes

/** Initialize header. */
static FFINL void ffdns_initquery(ffdns_hdr *h, uint txid, uint recursive) {
	ffmem_tzero(h);
	ffint_hton16(h->id, txid);
	//h->qr = 0;
	//h->opcode = FFDNS_OPQUERY;
	h->rd = recursive;
	h->qdcount[1] = 1;
}


/** Type. */
enum FFDNS_T {
	FFDNS_A = 1
	, FFDNS_AAAA = 28
	, FFDNS_NS = 2
	, FFDNS_CNAME = 5
	, FFDNS_PTR = 12 // for ip=1.2.3.4 ques.name = 4.3.2.1.in-addr.arpa
	, FFDNS_OPT = 41 //EDNS
};

/** Class. */
enum FFDNS_CL {
	FFDNS_IN = 1
};

/** Question. */
typedef struct ffdns_ques {
	//char name[]
	byte type[2];
	byte clas[2];
} ffdns_ques;

/** Answer. */
typedef struct ffdns_ans {
	byte type[2];
	byte clas[2];
	byte ttl[4]; //31-bit value
	byte len[2];
	//char name[]
} ffdns_ans; //10 bytes

typedef struct ffdns_opt {
	byte type[2];
	byte maxmsg[2];
	byte extrcode;
	byte ver;
	byte flags[2]; //dnssec[1] flags[15]

	byte len[2];
} ffdns_opt; //10 bytes

/** Initialize OPT record. */
static FFINL void ffdns_optinit(ffdns_opt *opt, uint udpsize) {
	ffmem_tzero(opt);
	opt->type[1] = FFDNS_OPT;
	ffint_hton16(opt->maxmsg, udpsize);
}

enum FFDNS_CONST {
	FFDNS_MAXNAME = 255 //max length of binary representation
	, FFDNS_MAXLABEL = 63
	, FFDNS_MAXMSG = 512 //maximum for DNS.  minimum for EDNS.
	, FFDNS_PORT = 53
};


/** Header: host byte order. */
typedef struct ffdns_hdr_host {
	uint id
		, rcode

		, qdcount
		, ancount
		, nscount
		, arcount;
} ffdns_hdr_host;

/** Question: host byte order. */
typedef struct ffdns_ques_host {
	uint type
		, clas;
} ffdns_ques_host;

/** Answer: host byte order. */
typedef struct ffdns_ans_host {
	const char *data;
	uint type
		, clas
		, ttl
		, len;
} ffdns_ans_host;

/** Convert header from network byte order into host byte order. */
FF_EXTN void ffdns_hdrtohost(ffdns_hdr_host *host, const void *net);

/** Convert question from network byte order into host byte order. */
static FFINL void ffdns_questohost(ffdns_ques_host *host, const void *net) {
	const ffdns_ques *ques = (ffdns_ques*)net;
	host->type = ffint_ntoh16(ques->type);
	host->clas = ffint_ntoh16(ques->clas);
}

/** Convert answer from network byte order into host byte order. */
FF_EXTN void ffdns_anstohost(ffdns_ans_host *host, const void *net);


/** Add question.
'name': resource name with or without trailing dot.
Return the number of bytes written.
Return 0 on error. */
FF_EXTN uint ffdns_addquery(char *pbuf, size_t cap, const char *name, size_t len, int type);

/** Convert "name.com." into binary format "\4 name \3 com \0".
Return the number of bytes written.
Return 0 on error. */
FF_EXTN uint ffdns_encodename(char *dst, size_t cap, const char *name, size_t len);

/** Parse binary-formatted name.
'begin' and 'len': pointer to the beginning of DNS message and its length.
'pos': pointer to the name to parse.
Return output name length.  'pos' is shifted.
Return 0 on error. */
FF_EXTN uint ffdns_name(char *nm, size_t cap, const char *begin, size_t len, const char **pos);

/** Skip name and shift the current position. */
FF_EXTN void ffdns_skipname(const char *begin, size_t len, const char **pos);
