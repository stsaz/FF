/** CRC.
Copyright (c) 2013 Simon Zolin
*/

#pragma once

#include <FFOS/types.h>
#include <FF/string.h>


/** Fast CRC32 implementation using 8k table. */
FF_EXTN uint crc32(const byte *buf, size_t size, uint crc);


static FFINL uint ffcrc32_start() {
	return 0xffffffff;
}

FF_EXTN const uint crc32_table256[];

static FFINL void ffcrc32_update(uint *crc, uint b) {
	*crc = crc32_table256[(*crc ^ b) & 0xff] ^ (*crc >> 8);
}

static FFINL void ffcrc32_iupdate(uint *crc, uint b) {
	ffcrc32_update(crc, ffchar_isup(b) ? ffchar_lower(b) : b);
}

static FFINL void ffcrc32_updatestr(uint *crc, const char *p, size_t len)
{
	size_t i;
	for (i = 0;  i != len;  i++) {
		ffcrc32_update(crc, (byte)p[i]);
	}
}

static FFINL void ffcrc32_finish(uint *crc) {
	*crc ^= 0xffffffff;
}

static FFINL uint ffcrc32_get(const char *p, size_t len)
{
	uint crc = ffcrc32_start();
	for (size_t i = 0;  i != len;  i++) {
		ffcrc32_update(&crc, (byte)p[i]);
	}
	ffcrc32_finish(&crc);
	return crc;
}

static FFINL uint ffcrc32_iget(const char *p, size_t len)
{
	uint crc = ffcrc32_start();
	for (size_t i = 0;  i != len;  i++) {
		ffcrc32_iupdate(&crc, (byte)p[i]);
	}
	ffcrc32_finish(&crc);
	return crc;
}

/** Get CRC32 from a NULL-terminated string. */
static FFINL uint ffcrc32_getz(const char *sz, int case_insens)
{
	uint crc = ffcrc32_start();
	if (case_insens) {
		while (*sz != '\0') {
			ffcrc32_iupdate(&crc, (byte)*sz++);
		}

	} else {
		while (*sz != '\0') {
			ffcrc32_update(&crc, (byte)*sz++);
		}
	}
	ffcrc32_finish(&crc);
	return crc;
}
