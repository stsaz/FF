/** MPEG format.  Only MPEG-1 Layer 3 is supported.
Copyright (c) 2013 Simon Zolin
*/

/*
MPEG-HEADER  [CRC16]  FRAME-DATA... ...
*/

#pragma once

#include <FFOS/types.h>


enum FFMPG_VER {
	FFMPG_1 = 3
};

enum FFMPG_LAYER {
	FFMPG_L3 = 1
};

enum FFMPG_CHANNEL {
	FFMPG_STEREO
	, FFMPG_JOINT
	, FFMPG_DUAL
	, FFMPG_MONO
};

/** Channel as a string. */
FF_EXTN const char ffmpg_strchannel[4][8];

FF_EXTN const byte ffmpg1l3_kbyterate[16];
FF_EXTN const ushort ffmpg1_sample_rate[4];

//4 bytes
typedef struct ffmpg_hdr {
	byte sync1 :8; //0xff

	byte noprotect :1 //0: protected by CRC
		, layer :2 //enum FFMPG_LAYER
		, ver :2 //enum FFMPG_VER
		, sync2 :3; //0x07

	byte priv :1
		, padding :1 //for L3 +1 byte in frame
		, sample_rate :2
		, bitrate :4;

	byte emphasis :2
		, original :1
		, copyright :1
		, modeext :2 //mode extension (for Joint Stereo)
		, channel :2; //enum FFMPG_CHANNEL
} ffmpg_hdr;

/** Return TRUE if valid MPEG header. */
FF_EXTN ffbool ffmpg_valid(const ffmpg_hdr *h);

/** Get length of MPEG frame data. */
FF_EXTN uint ffmpg_framelen(const ffmpg_hdr *h);

/** Get bitrate (bps). */
#define ffmpg_bitrate(h) \
	((uint)ffmpg1l3_kbyterate[(h)->bitrate] * 8 * 1000)

/** Get sample rate (Hz). */
#define ffmpg_sample_rate(h) \
	((uint)ffmpg1_sample_rate[(h)->sample_rate])

/** Get channels. */
#define ffmpg_channels(h) \
	((h)->channel == FFMPG_MONO ? 1 : 2)
