/** M3U playlist.
Copyright (c) 2015 Simon Zolin
*/

/*
#EXTM3U
#EXTINF:DURSEC,ARTIST - TITLE
\path\filename
*/

#pragma once

#include <FF/data/parse.h>


enum FFM3U_T {
	FFM3U_NAME,
	FFM3U_DUR,
	FFM3U_ARTIST,
	FFM3U_TITLE,
};

/**
Return enum FFPARS_E. */
FF_EXTN int ffm3u_parse(ffparser *p, const char *data, size_t *len);
