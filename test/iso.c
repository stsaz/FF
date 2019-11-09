/** .iso r/w
Copyright (c) 2018 Simon Zolin
*/

#include <FFOS/test.h>
#include <FF/pack/iso.h>

#define x FFTEST_BOOL


static void test_iso_write(ffarr *data)
{
	FFTEST_FUNC;
	ffiso_file f;
	ffiso_cook ck = {0};
	uint64 off;
	int r;

	ffiso_wcreate(&ck, 0);

	// add dir

	ffmem_tzero(&f);
	ffstr_setz(&f.name, "afile.txt");
	f.size = 5;
	ffiso_wfile(&ck, &f);

	ffmem_tzero(&f);
	ffstr_setz(&f.name, "mydirectory");
	f.attr = FFUNIX_FILE_DIR;
	ffiso_wfile(&ck, &f);

	ffmem_tzero(&f);
	ffstr_setz(&f.name, "zfilename.txt");
	f.size = 7;
	ffiso_wfile(&ck, &f);

	ffmem_tzero(&f);
	ffstr_setz(&f.name, "mydirectory/file3.txt");
	f.size = 3;
	ffiso_wfile(&ck, &f);

	// write dir

	for (;;) {
		r = ffiso_write(&ck);
		if (r == FFISO_DATA) {
			ffarr_append(data, ck.out.ptr, ck.out.len);
			continue;
		}
		x(r == FFISO_MORE);
		break;
	}

	// write file

	ffiso_wfilenext(&ck);
	ffstr_setz(&ck.in, "hello");
	for (;;) {
		r = ffiso_write(&ck);
		if (r == FFISO_DATA) {
			ffarr_append(data, ck.out.ptr, ck.out.len);
			continue;
		}
		x(r == FFISO_MORE);
		break;
	}

	// write file

	ffiso_wfilenext(&ck);
	ffstr_setz(&ck.in, "1234567");
	for (;;) {
		r = ffiso_write(&ck);
		if (r == FFISO_DATA) {
			ffarr_append(data, ck.out.ptr, ck.out.len);
			continue;
		}
		x(r == FFISO_MORE);
		break;
	}

	// write file

	ffiso_wfilenext(&ck);
	ffstr_setz(&ck.in, "123");
	for (;;) {
		r = ffiso_write(&ck);
		if (r == FFISO_DATA) {
			ffarr_append(data, ck.out.ptr, ck.out.len);
			continue;
		}
		x(r == FFISO_MORE);
		break;
	}

	// finalize

	ffiso_wfinish(&ck);
	r = ffiso_write(&ck);
	x(r == FFISO_SEEK);
	off = ffiso_woffset(&ck);
	for (;;) {
		r = ffiso_write(&ck);
		if (r == FFISO_DATA) {
			ffmemcpy(data->ptr + off, ck.out.ptr, ck.out.len);
			off += ck.out.len;
			continue;
		}
		x(r == FFISO_DONE);
		break;
	}

	ffiso_wclose(&ck);
}

static void test_iso_read(ffarr *data, uint opt)
{
	FFTEST_FUNC;
	int r;
	ffiso_file *pf;
	ffstr s;
	ffiso iso = {0};
	iso.options = opt;
	ffiso_init(&iso);
	ffiso_input(&iso, data->ptr, data->len);

	r = ffiso_read(&iso);
	x(r == FFISO_SEEK);
	ffiso_input(&iso, data->ptr + ffiso_offset(&iso), data->len - ffiso_offset(&iso));

	r = ffiso_read(&iso);
	x(r == FFISO_HDR);

	//read meta

	r = ffiso_read(&iso);
	x(r == FFISO_SEEK);
	ffiso_input(&iso, data->ptr + ffiso_offset(&iso), data->len - ffiso_offset(&iso));

	r = ffiso_read(&iso);
	x(r == FFISO_FILEMETA);
	pf = ffiso_getfile(&iso);
	x(ffstr_eqz(&pf->name, "afile.txt"));
	x(pf->size == 5);
	ffiso_storefile(&iso);

	r = ffiso_read(&iso);
	x(r == FFISO_FILEMETA);
	pf = ffiso_getfile(&iso);
	x(ffstr_eqz(&pf->name, "mydirectory"));
	x(pf->attr & FFUNIX_FILE_DIR);
	ffiso_storefile(&iso);

	r = ffiso_read(&iso);
	x(r == FFISO_FILEMETA);
	pf = ffiso_getfile(&iso);
	x(ffstr_eqz(&pf->name, "zfilename.txt"));
	x(pf->size == 7);
	ffiso_storefile(&iso);

	r = ffiso_read(&iso);
	x(r == FFISO_SEEK);
	ffiso_input(&iso, data->ptr + ffiso_offset(&iso), data->len - ffiso_offset(&iso));

	r = ffiso_read(&iso);
	x(r == FFISO_FILEMETA);
	pf = ffiso_getfile(&iso);
	x(ffstr_eqz(&pf->name, "mydirectory/file3.txt"));
	x(pf->size == 3);
	ffiso_storefile(&iso);

	r = ffiso_read(&iso);
	x(r == FFISO_LISTEND);

	//read file

	pf = ffiso_nextfile(&iso);
	ffiso_readfile(&iso, pf);
	x(ffstr_eqz(&pf->name, "afile.txt"));

	r = ffiso_read(&iso);
	x(r == FFISO_SEEK);
	ffiso_input(&iso, data->ptr + ffiso_offset(&iso), data->len - ffiso_offset(&iso));

	r = ffiso_read(&iso);
	x(r == FFISO_DATA);
	s = ffiso_output(&iso);
	x(ffstr_eqz(&s, "hello"));
	r = ffiso_read(&iso);
	x(r == FFISO_FILEDONE);

	//read file

	pf = ffiso_nextfile(&iso);
	ffiso_readfile(&iso, pf);
	x(ffstr_eqz(&pf->name, "mydirectory"));

	r = ffiso_read(&iso);
	x(r == FFISO_FILEDONE);

	//read file

	pf = ffiso_nextfile(&iso);
	ffiso_readfile(&iso, pf);
	x(ffstr_eqz(&pf->name, "zfilename.txt"));

	r = ffiso_read(&iso);
	x(r == FFISO_SEEK);
	ffiso_input(&iso, data->ptr + ffiso_offset(&iso), data->len - ffiso_offset(&iso));

	r = ffiso_read(&iso);
	x(r == FFISO_DATA);
	s = ffiso_output(&iso);
	x(ffstr_eqz(&s, "1234567"));
	r = ffiso_read(&iso);
	x(r == FFISO_FILEDONE);

	//read file

	pf = ffiso_nextfile(&iso);
	ffiso_readfile(&iso, pf);
	x(ffstr_eqz(&pf->name, "mydirectory/file3.txt"));

	r = ffiso_read(&iso);
	x(r == FFISO_SEEK);
	ffiso_input(&iso, data->ptr + ffiso_offset(&iso), data->len - ffiso_offset(&iso));

	r = ffiso_read(&iso);
	x(r == FFISO_DATA);
	s = ffiso_output(&iso);
	x(ffstr_eqz(&s, "123"));
	r = ffiso_read(&iso);
	x(r == FFISO_FILEDONE);

	pf = ffiso_nextfile(&iso);
	x(pf == NULL);

	ffiso_close(&iso);
}

void test_iso(void)
{
#ifdef _DEBUG
	ffdbg_mask = 10;
#endif
	ffarr data = {0};

	test_iso_write(&data);
	// fffile_writeall("./fftest.iso", data->ptr, data->len, 0);
	test_iso_read(&data, FFISO_NOJOLIET);
	test_iso_read(&data, FFISO_NORR);

	ffarr_free(&data);
}
