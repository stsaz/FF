/**
Copyright (c) 2016 Simon Zolin
*/

#include <FF/audio/pcm.h>
#include <FF/data/mmtag.h>
#include <FF/array.h>


enum MP4_E {
	MP4_ESYS = -1,
	MP4_ENOFTYP = 1,
	MP4_EFTYP,
	MP4_EDATA,
	MP4_ELARGE,
	MP4_ESMALL,
	MP4_EDUPBOX,
	MP4_ENOREQ,
	MP4_ENOFMT,
	MP4_ENFRAMES,
	MP4_ECO64,
};


struct box {
	byte size[4];
	char type[4];
};

struct box64 {
	byte size[4]; //=1
	char type[4];
	byte largesize[8];
};

struct fullbox {
	byte version;
	byte flags[3];
};

struct hdlr {
	byte unused[4];
	byte type[4]; //"soun"
	byte unused2[12];
};

struct smhd {
	byte unused[4];
};

struct dref {
	byte cnt[4];
};


enum BOX {
	BOX_ANY,
	BOX_FTYP,
	BOX_MOOV,
	BOX_MVHD,
	BOX_MDHD,
	BOX_HDLR,
	BOX_DREF,
	BOX_DREF_URL,
	BOX_TKHD,
	BOX_STSD,
	BOX_STSZ,
	BOX_STSC,
	BOX_STTS,
	BOX_STCO,
	BOX_CO64,
	BOX_STSD_ALAC,
	BOX_STSD_MP4A,
	BOX_ALAC,
	BOX_ESDS,
	BOX_ILST_DATA,

	BOX_ITUNES,
	BOX_ITUNES_MEAN,
	BOX_ITUNES_NAME,
	BOX_ITUNES_DATA,

	BOX_MDAT,

	_BOX_TAG,
	//FFMMTAG_*
	BOX_TAG_GENRE_ID31 = _FFMMTAG_N,
};

enum MP4_F {
	F_WHOLE = 0x100, //wait until the whole box is in memory
	F_FULLBOX = 0x200, //box inherits "struct fullbox"
	F_REQ = 0x400, //mandatory box
	F_MULTI = 0x800, //allow multiple occurrences
};

struct bbox {
	char type[4];
	uint flags; // "minsize" "reserved" "enum MP4_F" "enum BOX"
	const struct bbox *ctx;
};

#define GET_TYPE(f)  ((f) & 0xff)
#define MINSIZE(n)  ((n) << 24)
#define GET_MINSIZE(f)  ((f & 0xff000000) >> 24)

extern const struct bbox mp4_ctx_global[];


int mp4_box_find(const struct bbox *ctx, const char type[4]);
int mp4_box_write(const char *type, char *dst, size_t len);
int mp4_fbox_write(const char *type, char *dst, size_t len);

int mp4_tkhd_write(char *dst, uint id, uint64 total_samples);
int mp4_mvhd_write(char *fbox, uint rate, uint64 total_samples);
int mp4_mdhd_write(char *fbox, uint rate, uint64 total_samples);

int mp4_stsd_write(char *dst);

void mp4_asamp(const char *data, ffpcm *fmt);
uint mp4_asamp_write(char *dst, const ffpcm *fmt);

enum ESDS_DEC_TYPE {
	DEC_MPEG4_AUDIO = 0x40,
};
struct mp4_esds {
	uint type; //enum ESDS_DEC_TYPE
	uint stm_type;
	uint max_brate;
	uint avg_brate;
	const char *conf;
	uint conflen;
};
int mp4_esds(const char *data, uint len, struct mp4_esds *esds);
int mp4_esds_write(char *dst, const struct mp4_esds *esds);

struct seekpt {
	uint64 audio_pos;
	uint size;
	uint chunk_id; //index to ffmp4.chunktab
};
int64 mp4_stts(struct seekpt *sk, uint skcnt, const char *data, uint len);
int mp4_stts_write(char *dst, uint64 total_samples, uint framelen);
int mp4_seek(const struct seekpt *pts, size_t npts, uint64 sample);

int mp4_stsc(struct seekpt *sk, uint skcnt, const char *data, uint len);
int mp4_stsc_write(char *dst, uint64 total_samples, uint frame_samples, uint chunk_bound);

int mp4_stsz(const char *data, uint len, struct seekpt *sk);
int mp4_stsz_size(uint frames);
int mp4_stsz_add(char *dst, uint frsize);

int mp4_stco(const char *data, uint len, uint type, uint64 *chunktab);
int mp4_stco_size(uint type, uint chunks);
int mp4_stco_add(const char *data, uint type, uint64 offset);

int mp4_ilst_data(const char *data, uint len, uint parent_type, ffstr *tagval, char *tagbuf, size_t tagbuf_cap);
int mp4_ilst_trkn(const char *data, ffstr *tagval, char *tagbuf, size_t tagbuf_cap);
const struct bbox* mp4_ilst_find(uint mmtag);
int mp4_ilst_data_write(char *data, const ffstr *val);
int mp4_itunes_smpb(const char *data, size_t len, uint *enc_delay, uint *padding);
