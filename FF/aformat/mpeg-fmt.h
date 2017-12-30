/** MPEG format.
Copyright (c) 2016 Simon Zolin
*/

#pragma once

#include <FFOS/types.h>


enum FFMPG_VER {
	FFMPG_1 = 3,
	FFMPG_2 = 2,
	FFMPG_2_5 = 0,
};

enum FFMPG_LAYER {
	FFMPG_L3 = 1,
	FFMPG_L2,
	FFMPG_L1,
};

enum FFMPG_CHANNEL {
	FFMPG_STEREO,
	FFMPG_JOINT,
	FFMPG_DUAL,
	FFMPG_MONO,
};

/** Channel as a string. */
FF_EXTN const char ffmpg_strchannel[4][8];

//4 bytes
typedef struct ffmpg_hdr {
	byte sync1 :8; //0xff

#if defined FF_BIG_ENDIAN
	byte sync2 :3 //0x07
		, ver :2 //enum FFMPG_VER
		, layer :2 //enum FFMPG_LAYER
		, noprotect :1; //0: protected by CRC

	byte bitrate :4
		, sample_rate :2
		, padding :1 //for L3 +1 byte in frame
		, priv :1;

	byte channel :2 //enum FFMPG_CHANNEL
		, modeext :2 //mode extension (for Joint Stereo)
		, copyright :1
		, original :1
		, emphasis :2;

#else
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
#endif
} ffmpg_hdr;

/** Return TRUE if valid MPEG header. */
FF_EXTN ffbool ffmpg_hdr_valid(const ffmpg_hdr *h);

/** Get length of MPEG frame data. */
FF_EXTN uint ffmpg_hdr_framelen(const ffmpg_hdr *h);

/** Get bitrate (bps). */
FF_EXTN uint ffmpg_hdr_bitrate(const ffmpg_hdr *h);

/** Get sample rate (Hz). */
FF_EXTN uint ffmpg_hdr_sample_rate(const ffmpg_hdr *h);

/** Get channels. */
#define ffmpg_hdr_channels(h) \
	((h)->channel == FFMPG_MONO ? 1 : 2)

FF_EXTN uint ffmpg_hdr_frame_samples(const ffmpg_hdr *h);

enum {
	//bits in each MPEG header that must not change across frames within the same stream
	MPG_HDR_CONST_MASK = 0xfffe0c00, // 1111 1111  1111 1110  0000 1100  0000 0000
};

/** Search for a valid frame.
@h: (optional) a newly found header must match with this one. */
FF_EXTN ffmpg_hdr* ffmpg_framefind(const char *data, size_t len, const ffmpg_hdr *h);


struct ffmpg_info {
	uint frames;
	uint bytes;
	int vbr_scale; //100(worst)..0(best)
	byte toc[100];
	uint vbr :1;
};

/** Convert sample number to stream offset (in bytes). */
FF_EXTN uint64 ffmpg_xing_seekoff(const byte *toc, uint64 sample, uint64 total_samples, uint64 total_size);

/** Parse Xing tag.
Return the number of bytes read;  <0 on error. */
FF_EXTN int ffmpg_xing_parse(struct ffmpg_info *xing, const char *data, size_t len);

/** Write Xing tag.
Note: struct ffmpg_info.toc isn't supported. */
FF_EXTN int ffmpg_xing_write(const struct ffmpg_info *xing, char *data);

/** Parse VBRI tag.
Return the number of bytes read;  <0 on error. */
FF_EXTN int ffmpg_vbri(struct ffmpg_info *info, const char *data, size_t len);


struct ffmpg_lame {
	char id[9]; //e.g. "LAME3.90a"
	ushort enc_delay;
	ushort enc_padding;
};

/** Parse LAME tag.
Return the number of bytes read;  <0 on error. */
FF_EXTN int ffmpg_lame_parse(struct ffmpg_lame *lame, const char *data, size_t len);
