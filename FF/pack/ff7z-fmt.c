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
static int z7_folder_read(struct z7_folder *fo, ffstr *d, ffarr2 *stms);
static int z7_folders_read(ffarr *stms, ffstr *d);
static int z7_datasizes_read(ffarr *folders, ffstr *d);
static int z7_datacrcs_read(ffarr *folders, ffstr *d);
static int z7_stmfiles_read(ffarr *folders, ffstr *d);
static int z7_fsizes_read(ffarr *folders, ffstr *d);
static int z7_fcrcs_read(ffarr *folders, ffstr *d);
static int z7_fileinfo_read(ffarr *folders, ffstr *d);
static int z7_names_read(ffarr *folders, ffstr *d);
static int z7_mtimes_read(ffarr *folders, ffstr *d);
static int z7_winattrs_read(ffarr *folders, ffstr *d);
static int z7_emptystms_read(ffarr *folders, ffstr *d);
static int z7_emptyfiles_read(ffarr *folders, ffstr *d);
static int z7_dummy_read(ffarr *folders, ffstr *d);


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

	if (ffbit_set32(&parent->used, i) && !(blk->flags & F_MULTI))
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
		if ((ctx->children[i].flags & F_REQ) && !ffbit_test32(&ctx->used, i))
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

	if (NULL == ffarr_alloczT(stms, n, struct z7_stream))
		return FF7Z_ESYS;
	stms->len = n;
	struct z7_stream *s = (void*)stms->ptr;
	s->off = off;
	return 0;
}

/*
varint PackSize[NumPackStreams]
*/
static int z7_packsizes_read(ffarr *stms, ffstr *d)
{
	int r;
	struct z7_stream *s = (void*)stms->ptr;
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
	/** BCJ2 decoder has 4 input streams, 3 of them must be decompressed separately:
	stream0 -> lzma -\
	stream1 -> lzma -\
	stream2 -> lzma -\
	stream3         -> bcj2
	*/
	F_COMPLEXCODER = 0x10,
	F_ATTRS = 0x20,
};

static const char z7_method[][4] = {
	"\x00", //FF7Z_M_STORE
	"\x03\x01\x01", //FF7Z_M_LZMA1
	"\x03\x03\x01\x03", //FF7Z_M_X86
	"\x03\x03\x01\x1b", //FF7Z_M_X86_BCJ2
	"\x04\x01\x08", //FF7Z_M_DEFLATE
	"\x21", //FF7Z_M_LZMA2
};

/** Read data for 1 folder. */
static int z7_folder_read(struct z7_folder *fo, ffstr *d, ffarr2 *stms)
{
	int r;
	uint64 n, coders_n, in_streams;

	if (0 == z7_readint(d, &coders_n))
		return FF7Z_EMORE;
	FFDBG_PRINTLN(10, "coders:%U", coders_n);
	if (coders_n - 1 > Z7_MAX_CODERS)
		return FF7Z_EDATA;

	in_streams = coders_n;
	fo->coders = coders_n;

	for (uint i = 0;  i != coders_n;  i++) {

		struct z7_coder *cod = &fo->coder[i];
		uint flags;
		if (0 != z7_readbyte(d, &flags))
			return FF7Z_EMORE;

		uint methlen = flags & 0x0f;
		flags &= 0xf0;
		if (d->len < methlen)
			return FF7Z_EMORE;
		FFDBG_PRINTLN(10, " coder:%*xb  flags:%xu", (size_t)methlen, d->ptr, flags);
		r = ffcharr_findsorted(z7_method, FFCNT(z7_method), sizeof(*z7_method), d->ptr, methlen);
		if (r >= 0)
			cod->method = r + 1;
		ffstr_shift(d, methlen);

		if (flags & F_COMPLEXCODER) {
			uint64 in;
			if (0 == z7_readint(d, &in))
				return FF7Z_EMORE;
			if (0 == z7_readint(d, &n))
				return FF7Z_EMORE;
			FFDBG_PRINTLN(10, "  complex-coder: in:%U  out:%U", in, n);
			if (in - 1 > Z7_MAX_CODERS || n != 1)
				return FF7Z_EDATA;
			flags &= ~F_COMPLEXCODER;
			in_streams += in - 1;
		}

		if (flags & F_ATTRS) {
			if (0 == z7_readint(d, &n))
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

	uint bonds = coders_n - 1;
	for (uint i = 0;  i != bonds;  i++) {
		uint64 in, out;
		if (0 == z7_readint(d, &in))
			return FF7Z_EMORE;
		if (0 == z7_readint(d, &out))
			return FF7Z_EMORE;
		FFDBG_PRINTLN(10, " bond: in:%U  out:%U", in, out);
		fo->coder[coders_n - 1].input_coders[i] = i + 1;
	}

	uint pack_streams = in_streams - bonds;
	if (pack_streams > stms->len)
		return FF7Z_EDATA;
	if (pack_streams != 1) {
		for (uint i = 0;  i != pack_streams;  i++) {
			if (0 == z7_readint(d, &n))
				return FF7Z_EMORE;
			FFDBG_PRINTLN(10, " pack-stream:%U", n);
		}
	}

	struct z7_stream *stm = (void*)stms->ptr;
	for (uint i = 0;  i != pack_streams;  i++) {
		fo->coder[i].stream.off = stm->off;
		fo->coder[i].stream.pack_size = stm->pack_size;
		stm++;
	}
	ffarr_set(stms, stm, stms->len - pack_streams);
	return 0;
}

/*
varint NumFolders
byte External
 0:
  {
   // set NumInStreams = 0
   varint NumCoders
   {
    byte CodecIdSize :4
    byte Flags :4 //enum FOLDER_F
    byte CodecId[CodecIdSize]

    F_COMPLEXCODER:
     varint +=NumInStreams;
     varint NumOutStreams; //=1
    else
     ++NumInStreams

    F_ATTRS:
     varint PropertiesSize
     byte Properties[PropertiesSize]
   } [NumCoders]

   {
    varint InIndex;
    varint OutIndex;
   } [NumCoders - 1]

   varint PackStream[NumInStreams - (NumCoders - 1)]

  } [NumFolders]
*/
static int z7_folders_read(ffarr *stms, ffstr *d)
{
	int r;
	uint ext;
	uint64 folders;
	struct z7_folder *fo;
	ffarr a = {};
	ffarr2 streams;

	if (0 == z7_readint(d, &folders))
		return FF7Z_EMORE;
	FFDBG_PRINTLN(10, "folders:%U", folders);

	if (0 != z7_readbyte(d, &ext))
		return FF7Z_EMORE;
	if (!!ext)
		return FF7Z_EUNSUPP;

	if (NULL == ffarr_alloczT(&a, folders + 1 /*folder for empty files*/, struct z7_folder))
		return FF7Z_ESYS;
	fo = (void*)a.ptr;

	ffstr_set2(&streams, stms);

	for (size_t i = 0;  i != folders;  i++) {
		if (0 != (r = z7_folder_read(&fo[i], d, &streams))) {
			ffarr_free(&a);
			return r;
		}
	}

	// replace stream[] array with folder[]
	a.len = folders;
	ffarr_free(stms);
	*stms = a;
	return 0;
}

/*
varint UnPackSize[folders][folder.coders]
*/
static int z7_datasizes_read(ffarr *folders, ffstr *d)
{
	uint64 n = 0;
	int r;
	struct z7_folder *fo = (void*)folders->ptr;

	for (size_t i = 0;  i != folders->len;  i++) {
		for (uint ic = 0;  ic != fo[i].coders;  ic++) {
			if (0 == (r = z7_readint(d, &n)))
				return FF7Z_EMORE;
			fo[i].coder[ic].unpack_size = n;
			FFDBG_PRINTLN(10, "folder#%L  coder#%u  unpacked size:%xU", i, ic, n);
		}
		fo[i].unpack_size = n;
	}
	return 0;
}

/*
byte AllAreDefined
 0:
  bit Defined[NumFolders]
uint CRC[NumDefined]
*/
static int z7_datacrcs_read(ffarr *folders, ffstr *d)
{
	uint all;
	struct z7_folder *fo = (void*)folders->ptr;

	if (0 != z7_readbyte(d, &all))
		return FF7Z_EMORE;
	if (!all)
		return FF7Z_EUNSUPP;

	if (d->len < sizeof(int) * folders->len)
		return FF7Z_EMORE;

	for (size_t i = 0;  i != folders->len;  i++) {
		uint n = ffint_ltoh32(d->ptr);
		ffstr_shift(d, sizeof(int));
		FFDBG_PRINTLN(10, "folder#%L CRC:%xu", i, n);
		fo[i].crc = n;
	}

	return 0;
}

/*
varint NumUnPackStreamsInFolders[folders]
*/
static int z7_stmfiles_read(ffarr *folders, ffstr *d)
{
	int r;
	uint64 n;
	struct z7_folder *fo = (void*)folders->ptr;

	for (size_t i = 0;  i != folders->len;  i++) {

		if (0 == (r = z7_readint(d, &n)))
			return FF7Z_EMORE;
		FFDBG_PRINTLN(10, "folder#%L  files:%U", i, n);
		if (n == 0)
			return FF7Z_EDATA;

		if (NULL == ffarr_alloczT(&fo[i].files, n, ff7zfile))
			return FF7Z_ESYS;
		fo[i].files.len = n;
	}

	return 0;
}

/* List of unpacked file size for non-empty files.
varint UnPackSize[folders][folder.files]
*/
static int z7_fsizes_read(ffarr *folders, ffstr *d)
{
	int r;
	size_t i;
	uint64 n, off;
	ff7zfile *f;
	struct z7_folder *fo = (void*)folders->ptr;

	for (size_t ifo = 0;  ifo != folders->len;  ifo++) {
		f = (void*)fo[ifo].files.ptr;
		off = 0;

		for (i = 0;  i != fo[ifo].files.len - 1;  i++) {

			if (0 == (r = z7_readint(d, &n)))
				return FF7Z_EMORE;
			f[i].off = off;
			f[i].size = n;
			FFDBG_PRINTLN(10, "folder#%u  file[%L]  size:%xU  off:%xU"
				, ifo, i, f[i].size, f[i].off);
			off += n;
			if (off > fo[ifo].unpack_size)
				return FF7Z_EDATA;
		}

		n = fo[ifo].unpack_size - off;
		f[i].off = off;
		f[i].size = n;
		FFDBG_PRINTLN(10, "folder#%u  file[%L]  size:%xU  off:%xU"
			, ifo, i, f[i].size, f[i].off);
	}

	return 0;
}

/* List of unpacked file CRC for non-empty files.
byte AllAreDefined
 0:
  bit Defined[NumStreams]
uint CRC[NumDefined]
*/
static int z7_fcrcs_read(ffarr *folders, ffstr *d)
{
	uint all;
	if (0 != z7_readbyte(d, &all))
		return FF7Z_EMORE;
	if (!all)
		return FF7Z_EUNSUPP;

	struct z7_folder *fo;
	FFARR_WALKT(folders, fo, struct z7_folder) {

		if (d->len < sizeof(int) * fo->files.len)
			return FF7Z_EMORE;

		ff7zfile *f = (void*)fo->files.ptr;
		for (size_t i = 0;  i != fo->files.len;  i++) {
			uint n = ffint_ltoh32(d->ptr);
			ffstr_shift(d, sizeof(int));
			f[i].crc = n;
		}
	}

	return 0;
}

/* Get the number of all files (both nonempty and empty) and directories.
varint NumFiles
*/
static int z7_fileinfo_read(ffarr *folders, ffstr *d)
{
	int r;
	uint64 n;
	if (0 == (r = z7_readint(d, &n)))
		return FF7Z_EMORE;
	FFDBG_PRINTLN(10, "files:%U", n);
	if (n == 0)
		return FF7Z_EUNSUPP;

	uint64 all = 0;
	struct z7_folder *fo;
	FFARR_WALKT(folders, fo, struct z7_folder) {
		all += fo->files.len;
	}

	if (n < all)
		return FF7Z_EDATA;

	if (n > all) {
		// add one more stream with empty files and directory entries
		fo = ffarr_pushT(folders, struct z7_folder);
		if (NULL == ffarr_alloczT(&fo->files, n - all, ff7zfile))
			return FF7Z_ESYS;
		fo->files.len = n - all;
		if (NULL == ffarr_alloczT(&fo->empty, ffbit_nbytes(n), byte))
			return FF7Z_ESYS;
	}
	return 0;
}

/* List of empty files.
bit IsEmptyStream[NumFiles]
*/
static int z7_emptystms_read(ffarr *folders, ffstr *d)
{
	size_t i, bit_abs, n0 = 0;
	uint bit;

	for (i = 0;  i != d->len;  i++) {
		uint n = ffint_ntoh32(d->ptr + i) & 0xff000000;

		while (0 <= (int)(bit = ffbit_find32(n) - 1)) {
			bit_abs = i * 8 + bit;
			FFDBG_PRINTLN(10, "empty:%L", bit_abs);
			(void)bit_abs;
			ffbit_reset32(&n, 31 - bit);
			n0++;
		}
	}

	struct z7_folder *fo_empty = ffarr_lastT(folders, struct z7_folder);
	if ((n0 != 0 && fo_empty->empty.cap == 0)
		|| n0 != fo_empty->files.len)
		return FF7Z_EDATA; // invalid number of empty files

	i = ffmin(d->len, fo_empty->empty.cap);
	ffmemcpy(fo_empty->empty.ptr, d->ptr, i);

	ffarr_shift(d, d->len);
	return 0;
}

/*
bit IsEmptyFile[NumEmptyStreams]
*/
static int z7_emptyfiles_read(ffarr *folders, ffstr *d)
{
	ffstr_shift(d, d->len);
	return 0;
}

static int z7_dummy_read(ffarr *folders, ffstr *d)
{
	ffstr_shift(d, d->len);
	return 0;
}

/* Read filenames.  Put names of empty files into the last stream.
byte External
{
 ushort Name[]
 ushort 0
} [Files]
*/
static int z7_names_read(ffarr *folders, ffstr *d)
{
	int r;
	uint ext;
	ssize_t n, cnt = 0;

	if (0 != z7_readbyte(d, &ext))
		return FF7Z_EMORE;
	if (!!ext)
		return FF7Z_EUNSUPP;

	struct z7_folder *fo_empty = ffarr_lastT(folders, struct z7_folder);
	ff7zfile *fem = (void*)fo_empty->files.ptr;
	size_t ifem = 0;
	if (fo_empty->empty.cap == 0)
		fo_empty = NULL;

	struct z7_folder *fo = (void*)folders->ptr;
	for (size_t ifo = 0;  ifo != folders->len;  ifo++) {

		ff7zfile *f = (void*)fo[ifo].files.ptr;
		size_t i = 0;
		if (&fo[ifo] == fo_empty) {
			i = ifem;
			fo_empty = NULL;
		}

		while (i != fo[ifo].files.len) {

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
			FFDBG_PRINTLN(10, "folder#%L name[%L](%L):%*s"
				, ifo, i, cnt, (size_t)r - 1, a.ptr);
			a.len = ffpath_norm(a.ptr, a.cap, a.ptr, r - 1, FFPATH_MERGEDOTS | FFPATH_FORCESLASH | FFPATH_TOREL);
			a.ptr[a.len] = '\0';

			if (fo_empty != NULL && ffbit_ntest(fo_empty->empty.ptr, cnt))
				fem[ifem++].name = a.ptr;
			else
				f[i++].name = a.ptr;
			cnt++;
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
static int z7_mtimes_read(ffarr *folders, ffstr *d)
{
	uint all, ext, cnt = 0;

	if (d->len < 2)
		return FF7Z_EMORE;

	z7_readbyte(d, &all);
	if (!all)
		return FF7Z_EUNSUPP;

	z7_readbyte(d, &ext);
	if (!!ext)
		return FF7Z_EUNSUPP;

	struct z7_folder *fo_empty = ffarr_lastT(folders, struct z7_folder);
	ff7zfile *fem = (void*)fo_empty->files.ptr;
	size_t ifem = 0;
	if (fo_empty->empty.cap == 0)
		fo_empty = NULL;

	struct z7_folder *fo;
	FFARR_WALKT(folders, fo, struct z7_folder) {

		ff7zfile *f = (void*)fo->files.ptr;
		size_t i = 0;
		if (fo == fo_empty) {
			i = ifem;
			fo_empty = NULL;
		}

		while (i != fo->files.len) {

			if (d->len < 8)
				return FF7Z_EMORE;

			fftime_winftime ft;
			ft.lo = ffint_ltoh32(d->ptr);
			ft.hi = ffint_ltoh32(d->ptr + 4);
			ffstr_shift(d, 8);

			fftime t = fftime_from_winftime(&ft);
			if (fo_empty != NULL && ffbit_ntest(fo_empty->empty.ptr, cnt))
				fem[ifem++].mtime = t;
			else
				f[i++].mtime = t;
			cnt++;
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
static int z7_winattrs_read(ffarr *folders, ffstr *d)
{
	uint n, all, ext, cnt = 0;

	if (d->len < 2)
		return FF7Z_EMORE;

	z7_readbyte(d, &all);
	if (!all)
		return FF7Z_EUNSUPP;

	z7_readbyte(d, &ext);
	if (!!ext)
		return FF7Z_EUNSUPP;

	struct z7_folder *fo_empty = ffarr_lastT(folders, struct z7_folder);
	ff7zfile *fem = (void*)fo_empty->files.ptr;
	size_t ifem = 0;
	if (fo_empty->empty.cap == 0)
		fo_empty = NULL;

	struct z7_folder *fo;
	FFARR_WALKT(folders, fo, struct z7_folder) {

		ff7zfile *f = (void*)fo->files.ptr;
		size_t i = 0;
		if (fo == fo_empty) {
			i = ifem;
			fo_empty = NULL;
		}

		while (i != fo->files.len) {

			if (d->len < 4)
				return FF7Z_EMORE;

			n = ffint_ltoh32(d->ptr);
			ffstr_shift(d, 4);
			FFDBG_PRINTLN(10, "Attributes[%L]:%xu", i, n);

			if (fo_empty != NULL && ffbit_ntest(fo_empty->empty.ptr, cnt))
				fem[ifem++].attr = n;
			else
				f[i++].attr = n;
			cnt++;
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
  Dummy (0x19)
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
	{ T_Folder | PRIO(1), &z7_folders_read },
	{ T_UnPackSize | PRIO(2), &z7_datasizes_read },
	{ T_CRC, &z7_datacrcs_read },
	{ T_End | F_LAST, NULL },
};

static const z7_bblock z7_substminfo_children[] = {
	{ T_NumUnPackStream | PRIO(1), &z7_stmfiles_read },
	{ T_Size | PRIO(2), &z7_fsizes_read },
	{ T_CRC, &z7_fcrcs_read },
	{ T_End | F_LAST, NULL },
};

static const z7_bblock z7_fileinfo_children[] = {
	{ 0xff | F_SELF, &z7_fileinfo_read },
	{ T_EmptyStream | F_SIZE | PRIO(1), &z7_emptystms_read },
	{ T_EmptyFile | F_SIZE | PRIO(2), &z7_emptyfiles_read },
	{ T_Name | F_REQ | F_SIZE, &z7_names_read },
	{ T_MTime | F_SIZE, &z7_mtimes_read },
	{ T_WinAttributes | F_SIZE, &z7_winattrs_read },
	{ T_Dummy | F_SIZE | F_MULTI, &z7_dummy_read },
	{ T_End | F_LAST, NULL },
};
