/** CUE sheet.
Copyright (c) 2015 Simon Zolin
*/

/*
REM $NAME "VAL"
PERFORMER|TITLE "VAL"
FILE "VAL" $TYPE
  TRACK $TRACKNO AUDIO
    TITLE|PERFORMER "VAL"
    REM $NAME "VAL"
    INDEX 00|01..99 00:00:00
*/

#pragma once

#include <FF/data/parse.h>


enum FFCUE_T {
	FFCUE_REM_NAME = 1,
	FFCUE_REM_VAL,
	FFCUE_PERFORMER,
	FFCUE_TITLE,
	FFCUE_FILE,
	FFCUE_FILETYPE,
	FFCUE_TRACKNO,
	FFCUE_TRK_TITLE,
	FFCUE_TRK_PERFORMER,

	//for these values ffcuep.intval contains the number of CD frames (1/75 sec):
	FFCUE_TRK_INDEX00,
	FFCUE_TRK_INDEX,

	FFCUE_FIN, // for ffcue_index()
};

typedef struct ffcuep {
	uint state, nextst;
	uint ret;
	uint line;
	int64 intval;
	ffstr val;
	ffarr buf;
	ffstr tmp;
} ffcuep;

FF_EXTN void ffcue_init(ffcuep *c);

static FFINL void ffcue_close(ffcuep *c)
{
	ffarr_free(&c->buf);
}

/**
Return enum FFCUE_T;  <0 on error (enum FFPARS_E). */
FF_EXTN int ffcue_parse(ffcuep *c, const char *data, size_t *len);


typedef struct ffcuetrk {
	uint from,
		to;
} ffcuetrk;

enum FFCUE_GAP {
	/* Gap is added to the end of the previous track:
	track01.index01 .. track02.index01 */
	FFCUE_GAPPREV,

	/* Gap is added to the end of the previous track (but track01's pregap is preserved):
	track01.index00 .. track02.index01
	track02.index01 .. track03.index01 */
	FFCUE_GAPPREV1,

	/* Gap is added to the beginning of the current track:
	track01.index00 .. track02.index00 */
	FFCUE_GAPCURR,

	/* Skip pregaps:
	track01.index01 .. track02.index00 */
	FFCUE_GAPSKIP,
};

typedef struct ffcue {
	uint options; // enum FFCUE_GAP
	uint from;
	ffcuetrk trk;
	uint first :1;
	uint next :1;
} ffcue;

/** Get track start/end time from two INDEX values.
@type: enum FFCUE_T.
 FFCUE_FIN: get info for the final track.
Return NULL if the track duration isn't known yet. */
FF_EXTN ffcuetrk* ffcue_index(ffcue *c, uint type, uint val);
