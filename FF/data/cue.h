/** CUE sheet.
Copyright (c) 2015 Simon Zolin
*/

/*
REM $NAME "VAL"
PERFORMER|TITLE "VAL"
FILE "VAL" $TYPE
  TRACK $TRACKNO AUDIO
    TITLE|PERFORMER "VAL"
    INDEX 00|01..99 00:00:00
*/

#pragma once

#include <FF/data/parse.h>


enum FFCUE_T {
	FFCUE_REM_NAME,
	FFCUE_REM_VAL,
	FFCUE_PERFORMER,
	FFCUE_TITLE,
	FFCUE_FILE,
	FFCUE_FILETYPE,
	FFCUE_TRACKNO,
	FFCUE_TRK_TITLE,
	FFCUE_TRK_PERFORMER,
	FFCUE_TRK_INDEX00,
	FFCUE_TRK_INDEX,
};

FF_EXTN int ffcue_parse(ffparser *p, const char *data, size_t *len);
