/**
Copyright (c) 2017 Simon Zolin
*/

#include <FF/pack/7z-fmt.h>
#include <FF/crc.h>
#include <FF/path.h>
#include <FF/data/utf8.h>
#include <FF/number.h>


typedef struct z7_bblock z7_bblock;
static int z7_packinfo_read(ffarr *stms, ffstr *d);
static int z7_packsizes_read(ffarr *stms, ffstr *d);
static int z7_stmcoders_read(ffarr *stms, ffstr *d);
static int z7_datasizes_read(ffarr *stms, ffstr *d);
static int z7_datacrcs_read(ffarr *stms, ffstr *d);
static int z7_stmfiles_read(ffarr *stms, ffstr *d);
static int z7_fsizes_read(ffarr *stms, ffstr *d);
static int z7_fcrcs_read(ffarr *stms, ffstr *d);
static int z7_fileinfo_read(ffarr *stms, ffstr *d);
static int z7_names_read(ffarr *stms, ffstr *d);
static int z7_mtimes_read(ffarr *stms, ffstr *d);
static int z7_winattrs_read(ffarr *stms, ffstr *d);
static int z7_emptystms_read(ffarr *stms, ffstr *d);
static int z7_emptyfiles_read(ffarr *stms, ffstr *d);


// 32 bytes
struct z7_ghdr {
	char sig[6];
	byte ver_major;
	byte unused;
	byte crc[4]; //CRC of the following bytes
	byte hdr_off[8]; // hdr = struct z7_ghdr + hdr_off
	byte hdr_size[8];
	byte hdr_crc[4];
};

static const char hdr_signature[] = {'7', 'z', 0xbc, 0xaf, 0x27, 0x1c};

int z7_ghdr_read(struct z7_info *info, const char *data)
{
	const struct z7_ghdr *h = (void*)data;

	if (memcmp(h->sig, hdr_signature, 6))
		return FF7Z_EHDRSIGN;

	if (h->ver_major != 0)
		return FF7Z_EHDRVER;

	uint crc = crc32((void*)h->hdr_off, sizeof(struct z7_ghdr) - FFOFF(struct z7_ghdr, hdr_off), 0);
	if (ffint_ltoh32(h->crc) != crc)
		return FF7Z_EHDRCRC;

	info->hdroff = ffint_ltoh64(h->hdr_off) + sizeof(struct z7_ghdr);
	info->hdrsize = ffint_ltoh64(h->hdr_size);
	info->hdrcrc = ffint_ltoh32(h->hdr_crc);
	FFDBG_PRINTLN(10, "hdr off:%xU  size:%xU", info->hdroff, info->hdrsize);
	return 0;
}


/*
HI1 [LO8..HI2] -> LO8..HI1 (LE) -> host int
0xxxxxxx +0 bytes
10xxxxxx +1 bytes
...
11111111 +8 bytes
*/
int z7_varint(const char *data, size_t len, uint64 *val)
{
	uint size;
	byte b[8];

	if (len == 0)
		return 0;

	size = ffbit_find32(~((uint)(byte)data[0] << 24) & 0xff000000);
	if (size == 0)
		size = 8;
	if (size > len)
		return 0;

	ffmem_zero(b, sizeof(b));
	b[size - 1] = (byte)data[0] & ffbit_max(8 - size);
	ffmemcpy(b, data + 1, size - 1);

	*val = ffint_ltoh64(b);
	return size;
}

static FFINL int z7_readbyte(ffstr *d, uint *val)
{
	if (d->len == 0)
		return -1;
	*val = (byte)d->ptr[0];
	ffarr_shift(d, 1);
	return 0;
}

static FFINL int z7_readint(ffstr *d, uint64 *val)
{
	int r = z7_varint(d->ptr, d->len, val);
	ffarr_shift(d, r);
	return r;
}


int z7_find_block(uint blk_id, const z7_bblock **pblock, struct z7_block *parent)
{
	uint i;
	const z7_bblock *blk;
	for (i = 0;  ;  i++) {

		blk = &parent->children[i];
		uint id = blk->flags & 0xff;

		if (id == blk_id)
			break;
		if (blk->flags & F_SELF)
			continue;

		if (blk->flags & F_LAST)
			return FF7Z_EUKNID;
	}

	if (ffbit_set32(&parent->used, i))
		return FF7Z_EDUPBLOCK;

	uint prio = GET_PRIO(blk->flags);
	if (prio != 0) {
		if (prio > parent->prio + 1)
			return FF7Z_EORDER;
		parent->prio = prio;
	}

	*pblock = blk;
	return 0;
}

int z7_check_req(const struct z7_block *ctx)
{
	for (uint i = 0;  ;  i++) {
		if ((ctx->children[i].flags & F_REQ) && !ffbit_test32(ctx->used, i))
			return FF7Z_ENOREQ;
		if (ctx->children[i].flags & F_LAST)
			break;
	}
	return 0;
}


/*
varint PackPos
varint NumPackStreams
*/
static int z7_packinfo_read(ffarr *stms, ffstr *d)
{
	int r;
	uint64 off, n;

	if (0 == (r = z7_readint(d, &off)))
		return FF7Z_EMORE;
	off += Z7_GHDR_LEN;

	if (0 == (r = z7_readint(d, &n)))
		return FF7Z_EMORE;

	FFDBG_PRINTLN(10, "streams:%U  offset:%xU", n, off);

	if (n == 0)
		return FF7Z_EUNSUPP;

	if (NULL == ffarr_alloczT(stms, n, z7_stream))
		return FF7Z_ESYS;
	stms->len = n;
	z7_stream *s = (void*)stms->ptr;
	s->off = off;
	return 0;
}

/*
varint PackSize[NumPackStreams]
*/
static int z7_packsizes_read(ffarr *stms, ffstr *d)
{
	int r;
	z7_stream *s = (void*)stms->ptr;
	uint64 n, off = s->off;

	for (size_t i = 0;  i != stms->len;  i++) {

		if (0 == (r = z7_readint(d, &n)))
			return FF7Z_EMORE;

		FFDBG_PRINTLN(10, "stream#%L size:%xU", i, n);
		s[i].off = off;
		s[i].pack_size = n;
		off += n;
	}
	return 0;
}

enum FOLDER_F {
	F_ATTRS = 0x20,
};

static const char* const z7_method[] = {
	"\x03\x01\x01",
	"\x03\x03\x01\x03",
	"\x04\x01\x08",
	"\x21",
};

/*
varint NumFolders
byte External
 0:
  {
   varint NumCoders
   {
    byte CodecIdSize :4
    byte Flags :4 //enum FOLDER_F
    byte CodecId[CodecIdSize]
    F_ATTRS:
     varint PropertiesSize
     byte Properties[PropertiesSize]
   } [NumCoders]

   {
    UINT64 InIndex;
    UINT64 OutIndex;
   } [NumCoders - 1]

  } [NumFolders]
*/
static int z7_stmcoders_read(ffarr *stms, ffstr *d)
{
	int r;
	uint i, ic, ext;
	uint64 folders, n, coders_n;
	z7_stream *s = (void*)stms->ptr;

	if (0 == (r = z7_readint(d, &folders)))
		return FF7Z_EMORE;
	FFDBG_PRINTLN(10, "folders:%U", folders);

	if (folders != stms->len)
		return FF7Z_EUNSUPP;

	if (0 != z7_readbyte(d, &ext))
		return FF7Z_EMORE;
	if (!!ext)
		return FF7Z_EUNSUPP;

	for (i = 0;  i != folders;  i++) {

		if (0 == (r = z7_readint(d, &coders_n)))
			return FF7Z_EMORE;
		FFDBG_PRINTLN(10, "coders:%U", coders_n);
		if (coders_n > FFCNT(s[i].coder))
			return FF7Z_EUNSUPP;
		for (ic = 0;  ic != coders_n;  ic++) {

			struct z7_coder *cod = &s[i].coder[ic];
			uint flags;
			if (0 != z7_readbyte(d, &flags))
				return FF7Z_EMORE;

			n = flags & 0x0f;
			flags &= 0xf0;
			if (d->len < n)
				return FF7Z_EMORE;
			FFDBG_PRINTLN(10, " coder:%*xb  flags:%xu", (size_t)n, d->ptr, flags);
			r = ffszarr_findsorted(z7_method, FFCNT(z7_method), d->ptr, n);
			if (r >= 0)
				cod->method = r + 1;
			else if (n == 1 && d->ptr[0] == 0x00)
				cod->method = FF7Z_M_STORE;
			ffstr_shift(d, n);

			if (flags & F_ATTRS) {
				if (0 == (r = z7_readint(d, &n)))
					return FF7Z_EMORE;
				if (d->len < n)
					return FF7Z_EMORE;
				FFDBG_PRINTLN(10, "  props:%*xb", (size_t)n, d->ptr);
				if (n >= FFCNT(cod->props))
					return FF7Z_EUNSUPP;
				ffmemcpy(cod->props, d->ptr, n);
				cod->nprops = n;
				ffstr_shift(d, n);
				flags &= ~F_ATTRS;
			}

			if (flags != 0)
				return FF7Z_EFOLDER_FLAGS;
		}

		for (ic = 0;  ic != coders_n - 1;  ic++) {
			uint64 in, out;
			if (0 == (r = z7_readint(d, &in)))
				return FF7Z_EMORE;
			if (0 == (r = z7_readint(d, &out)))
				return FF7Z_EMORE;
			FFDBG_PRINTLN(10, "  in:%U  out:%U", in, out);
		}
	}

	return 0;
}

/*
varint UnPackSize[folders][folder.coders]
*/
static int z7_datasizes_read(ffarr *stms, ffstr *d)
{
	uint64 n;
	int r;
	z7_stream *s = (void*)stms->ptr;

	for (size_t i = 0;  i != stms->len;  i++) {
		for (uint ic = 0;  ic != FFCNT(s->coder);  ic++) {
			if (s[i].coder[ic].method == 0)
				break;
			if (0 == (r = z7_readint(d, &n)))
				return FF7Z_EMORE;
			FFDBG_PRINTLN(10, "stream#%L  coder#%u  unpacked size:%xU", i, ic, n);
		}

		s[i].unpack_size = n;
	}
	return 0;
}

/*
byte AllAreDefined
 0:
  bit Defined[NumFolders]
uint CRC[NumDefined]
*/
static int z7_datacrcs_read(ffarr *stms, ffstr *d)
{
	uint all;
	z7_stream *s = (void*)stms->ptr;

	if (0 != z7_readbyte(d, &all))
		return FF7Z_EMORE;
	if (!all)
		return FF7Z_EUNSUPP;

	if (d->len < sizeof(int) * stms->len)
		return FF7Z_EMORE;

	for (size_t i = 0;  i != stms->len;  i++) {
		uint n = ffint_ltoh32(d->ptr);
		ffstr_shift(d, sizeof(int));
		FFDBG_PRINTLN(10, "stream#%L CRC:%xu", i, n);
		s[i].crc = n;
	}

	return 0;
}

/*
varint NumUnPackStreamsInFolders[folders]
*/
static int z7_stmfiles_read(ffarr *stms, ffstr *d)
{
	int r;
	uint64 n;
	z7_stream *s = (void*)stms->ptr;

	for (size_t i = 0;  i != stms->len;  i++) {

		if (0 == (r = z7_readint(d, &n)))
			return FF7Z_EMORE;
		FFDBG_PRINTLN(10, "stream#%L  files:%U", i, n);
		if (n == 0)
			return FF7Z_EDATA;

		if (NULL == ffarr_alloczT(&s[i].files, n, ff7zfile))
			return FF7Z_ESYS;
		s[i].files.len = n;
	}

	return 0;
}

/*
varint UnPackSize[streams][stream.files]
*/
static int z7_fsizes_read(ffarr *stms, ffstr *d)
{
	int r;
	size_t i;
	uint64 n, off;
	ff7zfile *f;
	z7_stream *s;

	FFARR_WALKT(stms, s, z7_stream) {
		f = (void*)s->files.ptr;
		off = 0;

		for (i = 0;  i != s->files.len - 1;  i++) {

			if (0 == (r = z7_readint(d, &n)))
				return FF7Z_EMORE;
			f[i].off = off;
			f[i].size = n;
			FFDBG_PRINTLN(10, "file[%L]  size:%xU  off:%xU", i, f[i].size, f[i].off);
			off += n;
			if (off > s->unpack_size)
				return FF7Z_EDATA;
		}

		f[i].size = s->unpack_size - off;
		f[i].off = off;
		FFDBG_PRINTLN(10, "file[%L]  size:%xU  off:%xU", i, f[i].size, f[i].off);
	}

	return 0;
}

/*
byte AllAreDefined
 0:
  bit Defined[NumStreams]
uint CRC[NumDefined]
*/
static int z7_fcrcs_read(ffarr *stms, ffstr *d)
{
	uint all;

	if (0 != z7_readbyte(d, &all))
		return FF7Z_EMORE;
	if (!all)
		return FF7Z_EUNSUPP;

	z7_stream *s;
	FFARR_WALKT(stms, s, z7_stream) {

		if (d->len < sizeof(int) * s->files.len)
			return FF7Z_EMORE;

		ff7zfile *f = (void*)s->files.ptr;
		for (size_t i = 0;  i != s->files.len;  i++) {
			uint n = ffint_ltoh32(d->ptr);
			ffstr_shift(d, sizeof(int));
			f[i].crc = n;
		}
	}

	return 0;
}

/*
varint NumFiles
*/
static int z7_fileinfo_read(ffarr *stms, ffstr *d)
{
	int r;
	uint64 n;
	if (0 == (r = z7_readint(d, &n)))
		return FF7Z_EMORE;
	FFDBG_PRINTLN(10, "files:%U", n);
	if (n == 0)
		return FF7Z_EUNSUPP;

	uint64 all = 0;
	z7_stream *s;
	FFARR_WALKT(stms, s, z7_stream) {
		all += s->files.len;
	}

	if (n < all)
		return FF7Z_EDATA;

	if (n > all) {
		// add one more stream with empty files and directory entries
		if (NULL == (s = ffarr_pushT(stms, z7_stream)))
			return FF7Z_ESYS;
		ffmem_tzero(s);

		if (NULL == ffarr_alloczT(&s->files, n - all, ff7zfile))
			return FF7Z_ESYS;
		s->files.len = n - all;
	}
	return 0;
}

/*
bit IsEmptyStream[NumFiles]
*/
static int z7_emptystms_read(ffarr *stms, ffstr *d)
{
	const z7_stream *s = (void*)stms->ptr;
	size_t all = 0, i;
	uint bit, bit_abs;

	for (i = 0;  i != d->len;  i++) {
		uint n = ffint_ntoh32(d->ptr + i) & 0xff000000;

		while (0 <= (int)(bit = ffbit_find32(n) - 1)) {
			bit_abs = i * 8 + bit;
			FFDBG_PRINTLN(10, "empty stream:%u", bit_abs);

			for (;;) {
				if (s == ffarr_endT(stms, z7_stream))
					return FF7Z_EDATA; // bit index is larger than total files number
				if ((size_t)bit_abs < all + s->files.len) {
					if (s->off != 0)
						return FF7Z_EUNSUPP; // empty file within non-empty stream
					goto done;
				}

				all += s->files.len;
				s++;
			}

			ffbit_reset32(&n, bit);
		}
	}

done:
	ffarr_shift(d, d->len);
	return 0;
}

/*
bit IsEmptyFile[NumEmptyStreams]
*/
static int z7_emptyfiles_read(ffarr *stms, ffstr *d)
{
	ffstr_shift(d, d->len);
	return 0;
}

/*
byte External
{
 ushort Name[]
 ushort 0
} [Files]
*/
static int z7_names_read(ffarr *stms, ffstr *d)
{
	int r;
	uint ext;
	ssize_t n;

	if (0 != z7_readbyte(d, &ext))
		return FF7Z_EMORE;
	if (!!ext)
		return FF7Z_EUNSUPP;

	z7_stream *s;
	FFARR_WALKT(stms, s, z7_stream) {

		ff7zfile *f = (void*)s->files.ptr;

		for (size_t i = 0;  i != s->files.len;  i++) {

			if (0 > (n = ffutf16_findc(d->ptr, d->len, 0)))
				return FF7Z_EMORE;
			n += 2;

			ffarr a;
			if (NULL == ffarr_alloc(&a, n * 3))
				return FF7Z_ESYS;

			if (0 == (r = ffutf8_encodewhole(a.ptr, a.cap, d->ptr, n, FFU_UTF16LE))) {
				ffarr_free(&a);
				return FF7Z_EDATA;
			}
			ffstr_shift(d, n);
			FFDBG_PRINTLN(10, "name[%L]:%*s", i, (size_t)r - 1, a.ptr);
			a.len = ffpath_norm(a.ptr, a.cap, a.ptr, r - 1, FFPATH_MERGEDOTS | FFPATH_FORCESLASH | FFPATH_TOREL);
			a.ptr[a.len] = '\0';
			f[i].name = a.ptr;
		}
	}

	return 0;
}

/*
byte AllAreDefined
 0:
  bit TimeDefined[NumFiles]
byte External
{
 uint32 TimeLow
 uint32 TimeHi
} [NumDefined]
*/
static int z7_mtimes_read(ffarr *stms, ffstr *d)
{
	uint all, ext;

	if (d->len < 2)
		return FF7Z_EMORE;

	z7_readbyte(d, &all);
	if (!all)
		return FF7Z_EUNSUPP;

	z7_readbyte(d, &ext);
	if (!!ext)
		return FF7Z_EUNSUPP;

	z7_stream *s;
	FFARR_WALKT(stms, s, z7_stream) {

		if (d->len < 8 * s->files.len)
			return FF7Z_EMORE;

		ff7zfile *f = (void*)s->files.ptr;
		for (size_t i = 0;  i != s->files.len;  i++) {
			fftime_winftime ft;
			ft.lo = ffint_ltoh32(d->ptr);
			ft.hi = ffint_ltoh32(d->ptr + 4);
			ffstr_shift(d, 8);
			f[i].mtime = fftime_from_winftime(&ft);
		}
	}

	return 0;
}

/*
byte AllAreDefined
 0:
  bit AttributesAreDefined[NumFiles]
byte External
uint32 Attributes[Defined]
}
*/
static int z7_winattrs_read(ffarr *stms, ffstr *d)
{
	uint n, all, ext;

	if (d->len < 2)
		return FF7Z_EMORE;

	z7_readbyte(d, &all);
	if (!all)
		return FF7Z_EUNSUPP;

	z7_readbyte(d, &ext);
	if (!!ext)
		return FF7Z_EUNSUPP;

	z7_stream *s;
	FFARR_WALKT(stms, s, z7_stream) {

		if (d->len < 4 * s->files.len)
			return FF7Z_EMORE;

		ff7zfile *f = (void*)s->files.ptr;
		for (size_t i = 0;  i != s->files.len;  i++) {
			n = ffint_ltoh32(d->ptr);
			ffstr_shift(d, 4);
			FFDBG_PRINTLN(10, "Attributes[%L]:%xu", i, n);
			f[i].attr = n;
		}
	}

	return 0;
}


/* Supported blocks:
Header (0x01)
 MainStreamsInfo (0x04)
  PackInfo (0x06)
   Size (0x09)
  UnPackInfo (0x07)
   Folder (0x0b)
   UnPackSize (0x0c)
   CRC (0x0a)
  SubStreamsInfo (0x08)
   NumUnPackStream (0x0d)
   Size (0x09)
   CRC (0x0a)
 FilesInfo (0x05)
  EmptyStream (0x0e)
  EmptyFile (0x0f)
  Name (0x11)
  MTime (0x14)
  WinAttributes (0x15)
EncodedHeader (0x17)
 PackInfo
  ...
 UnPackInfo
  ...
 SubStreamsInfo
  ...
*/

static const z7_bblock z7_hdr_children[];
static const z7_bblock z7_stminfo_children[];
static const z7_bblock z7_packinfo_children[];
static const z7_bblock z7_unpackinfo_children[];
static const z7_bblock z7_substminfo_children[];
static const z7_bblock z7_fileinfo_children[];

const z7_bblock z7_ctx_top[] = {
	{ T_Header | F_CHILDREN, z7_hdr_children },
	{ T_EncodedHeader | F_CHILDREN | F_LAST, z7_stminfo_children },
};

static const z7_bblock z7_hdr_children[] = {
	{ T_AdditionalStreamsInfo | F_CHILDREN, z7_stminfo_children },
	{ T_MainStreamsInfo | F_REQ | F_CHILDREN | PRIO(1), z7_stminfo_children },
	{ T_FilesInfo | F_CHILDREN | PRIO(2), z7_fileinfo_children },
	{ T_End | F_LAST, NULL },
};

static const z7_bblock z7_stminfo_children[] = {
	{ T_PackInfo | F_REQ | F_CHILDREN | PRIO(1), z7_packinfo_children },
	{ T_UnPackInfo | F_REQ | F_CHILDREN | PRIO(2), z7_unpackinfo_children },
	{ T_SubStreamsInfo | F_CHILDREN | PRIO(3), z7_substminfo_children },
	{ T_End | F_LAST, NULL },
};

static const z7_bblock z7_packinfo_children[] = {
	{ 0xff | F_SELF, &z7_packinfo_read },
	{ T_Size | F_REQ, &z7_packsizes_read },
	{ T_End | F_LAST, NULL },
};

static const z7_bblock z7_unpackinfo_children[] = {
	{ T_Folder | PRIO(1), &z7_stmcoders_read },
	{ T_UnPackSize | PRIO(2), &z7_datasizes_read },
	{ T_CRC, &z7_datacrcs_read },
	{ T_End | F_LAST, NULL },
};

static const z7_bblock z7_substminfo_children[] = {
	{ T_NumUnPackStream | F_ALLOC_FILES | PRIO(1), &z7_stmfiles_read },
	{ T_Size | PRIO(2), &z7_fsizes_read },
	{ T_CRC | F_ALLOC_FILES, &z7_fcrcs_read },
	{ T_End | F_LAST, NULL },
};

static const z7_bblock z7_fileinfo_children[] = {
	{ 0xff | F_SELF, &z7_fileinfo_read },
	{ T_EmptyStream | F_SIZE | PRIO(1), &z7_emptystms_read },
	{ T_EmptyFile | F_SIZE | PRIO(2), &z7_emptyfiles_read },
	{ T_Name | F_REQ | F_SIZE, &z7_names_read },
	{ T_MTime | F_SIZE, &z7_mtimes_read },
	{ T_WinAttributes | F_SIZE, &z7_winattrs_read },
	{ T_End | F_LAST, NULL },
};
