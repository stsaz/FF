/** XML.
Copyright (c) 2014 Simon Zolin
*/

#pragma once

#include <FFOS/types.h>


/** Escape special XML characters: <>&"
Return the number of bytes written. */
FF_EXTN size_t ffxml_escape(char *dst, size_t cap, const char *s, size_t len);
