/** CRC.
Copyright (c) 2013 Simon Zolin
*/

#pragma once

#include <FFOS/types.h>
#include <FF/string.h>

static FFINL uint ffcrc32_start() {
	return 0xffffffff;
}

FF_EXTN const uint crc32_table256[];

static FFINL void ffcrc32_update(uint *crc, uint b, int case_insens) {
	if (case_insens != 0 && ffchar_isup(b))
		b = ffchar_lower(b);
	*crc = crc32_table256[(*crc ^ b) & 0xff] ^ (*crc >> 8);
}

static FFINL void ffcrc32_updatestr(uint *crc, const char *p, size_t len, int case_insens) {
	while (len-- != 0) {
		ffcrc32_update(crc, *p++, case_insens);
	}
}

static FFINL void ffcrc32_finish(uint *crc) {
	*crc ^= 0xffffffff;
}

static FFINL uint ffcrc32_get(const char *p, size_t len, int case_insens) {
	uint crc = ffcrc32_start();
	ffcrc32_updatestr(&crc, p, len, case_insens);
	ffcrc32_finish(&crc);
	return crc;
}
