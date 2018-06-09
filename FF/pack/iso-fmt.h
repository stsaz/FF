/**
Copyright (c) 2018 Simon Zolin
*/

#include <FF/pack/iso.h>
#include <FF/time.h>


enum { FFISO_SECT = 2048 };

enum ISO_TYPE {
	ISO_T_PRIM = 1,
	ISO_T_JOLIET = 2,
	ISO_T_TERM = 0xff,
};

struct iso_voldesc {
	byte type; //enum ISO_TYPE
	char id[5]; //"CD001"
	byte ver; //=1
	byte data[0];
};

struct iso_date {
	byte year; //+1900
	byte month;
	byte day;
	byte hour;
	byte min;
	byte sec;
	byte gmt15;
};

enum ISO_F {
	ISO_FDIR = 2,
	ISO_FLARGE = 0x80,
};

struct iso_fent {
	byte len;
	byte ext_attr_len;
	byte body_off[8]; //LE,BE
	byte body_len[8]; //LE,BE
	struct iso_date date;
	byte flags; // enum ISO_F
	byte unused[2];
	byte vol_seqnum[4];
	byte namelen;
	byte name[0]; //files: "NAME.EXT;NUM"  dirs: 0x00 (self) | 0x01 (parent) | "NAME"
	// byte pad; //exists if namelen is even
};

struct iso_voldesc_prim {
	byte unused;
	byte system[32];
	byte name[32];
	byte unused3[8];
	byte vol_size[8]; //LE[4],BE[4]
	byte esc_seq[32];
	byte vol_set_size[4];
	byte vol_set_seq[4];
	byte log_blksize[4]; //LE[2],BE[2]
	byte path_tbl_size[8]; //LE,BE
	byte path_tbl1_off[4]; //LE
	byte path_tbl2_off[4]; //LE
	byte path_tbl1_off_be[4]; //BE
	byte unused8[4];
	byte root_dir[34]; //struct iso_fent
};

struct iso_voldesc_prim_host {
	uint type;
	const char *name;
	uint root_dir_off;
	uint root_dir_size;
	uint vol_size;
	uint path_tbl_size;
	uint path_tbl_off;
	uint path_tbl_off_be;
};

/** Parse primary volume descriptor. */
FF_EXTN int iso_voldesc_parse_prim(const void *p);
FF_EXTN void* iso_voldesc_write(void *buf, uint type);
FF_EXTN void iso_voldesc_prim_write(void *buf, const struct iso_voldesc_prim_host *info);

/** Get length of file entry before RR extensions. */
static FFINL uint iso_ent_len(const struct iso_fent *ent)
{
	return FFOFF(struct iso_fent, name) + ent->namelen + !(ent->namelen % 2);
}
static FFINL uint iso_ent_len2(uint namelen)
{
	return FFOFF(struct iso_fent, name) + namelen + !(namelen % 2);
}

FF_EXTN uint iso_ent_len(const struct iso_fent *ent);

/** Parse file entry.
Return entry length;  0: no more entries;  <0 on error */
FF_EXTN int iso_ent_parse(const void *p, size_t len, ffiso_file *f, uint64 off);

/** Get real filename. */
FF_EXTN ffstr iso_ent_name(const ffstr *name);

/** Parse Rock-Ridge extension entries.
Return # of bytes read;  <0 on error */
FF_EXTN int iso_rr_parse(const void *p, size_t len, ffiso_file *f);


enum ENT_WRITE_F {
	ENT_WRITE_RR = 1,
	ENT_WRITE_JLT = 2,
	// ENT_WRITE_CUT = 4, //cut large filenames, don't return error
	ENT_WRITE_RR_SP = 8,
};

/** Write file entry.
@buf: must be filled with zeros.
 if NULL: return output space required.
@flags: enum ENT_WRITE_F
Return bytes written;  <0 on error. */
FF_EXTN int iso_ent_write(void *buf, size_t cap, const struct ffiso_file *f, uint64 off, uint flags);


enum PATHENT_WRITE_F {
	PATHENT_WRITE_BE = 1,
	PATHENT_WRITE_JLT = 2,
};

/** Write 1 dir in path table.
@flags: enum PATHENT_WRITE_F */
FF_EXTN int iso_pathent_write(void *dst, size_t cap, const ffstr *name, uint extent, uint parent, uint flags);
