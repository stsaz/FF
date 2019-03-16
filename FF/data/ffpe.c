/**
Copyright (c) 2019 Simon Zolin
*/

#include <FF/data/pe.h>
#include <FF/data/pe-fmt.h>
#include <FF/number.h>


enum PE_E {
	PE_EOK,
	PE_ESYS,
	PE_ESIG,
	PE_EFLAG,
	PE_EOHDR,
	PE_EMAGIC,
	PE_ELARGE,
	PE_ESMALL,
	PE_ERVA,
};

static const char* const serr[] = {
	"",
	"",
	"bad signature", //PE_ESIG
	"bad flags", //PE_EFLAG
	"bad optional header", //PE_EOHDR
	"bad magic code", //PE_EMAGIC
	"value is too large", //PE_ELARGE
	"not enough data", //PE_ESMALL
	"bad RVA", //PE_ERVA
};

const char* ffpe_errstr(ffpe *p)
{
	if (p->err == PE_ESYS)
		return fferr_strp(fferr_last());
	if ((uint)p->err >= FFCNT(serr))
		return "";
	return serr[p->err];
}

int ffpe_open(ffpe *p)
{
	return 0;
}

void ffpe_close(ffpe *p)
{
	ffarr_free(&p->buf);
	ffarr_free(&p->imp_lkp);
	ffarr_free(&p->imp_dir);
	ffarr_free(&p->sections);
}

#define ERR(p, e) \
	(p)->err = (e),  FFPE_ERR

#define GATHER(p, next_state, size) \
	(p)->next = (next_state),  (p)->gather_size = (size),  (p)->state = R_GATHER

static int real_offset(ffpe *p, uint vaddr)
{
	struct ffpe_sect *s;
	FFARR_WALKT(&p->sections, s, struct ffpe_sect) {
		if (ffint_within(vaddr, s->vaddr, s->vaddr + s->vsize)) {
			if (s->vsize > s->raw_size)
				return -1;
			FFDBG_PRINTLN(10, "%xU -> %xu (%xu)"
			, vaddr, s->raw_off + vaddr - s->vaddr, s->raw_off);
			return s->raw_off + vaddr - s->vaddr;
		}
	}
	return -1;
}

enum R {
	R_INIT, R_HDR, R_PE, R_OPTHDR,
	R_DD,
	R_SECT_GATHER, R_SECT, R_SECT2,
	R_GATHER,
	R_IDATA_SEEK, R_IDATA_COPY, R_IDATA,
	R_EDATA_SEEK, R_EDATA,
};

#ifdef _DEBUG
static const char* const state_str[] = {
	"R_INIT", "R_HDR", "R_PE", "R_OPTHDR",
	"R_DD",
	"R_SECT_GATHER", "R_SECT", "R_SECT2",
	"R_GATHER",
	"R_IDATA_SEEK", "R_IDATA_COPY", "R_IDATA",
	"R_EDATA_SEEK", "R_EDATA",
};
#endif

static int pe_coffhdr(ffpe *p)
{
	const struct pe_sig *pe = (void*)p->dat.ptr;
	if (!!ffmemcmp(pe->sig, "PE\0\0", 4))
		return PE_ESIG;

	const struct coff_hdr *h = (void*)(p->dat.ptr + sizeof(struct pe_sig));
	uint f = ffint_ltoh16(h->flags);
	if (!(f & 0x02))
		return PE_EFLAG;
	p->info.machine = ffint_ltoh16(h->machine);
	p->info.tm_created = ffint_ltoh32(h->tm_created);
	p->info.sections = ffint_ltoh16(h->sections);

	uint ohsz = ffint_ltoh16(h->opt_hdr_size);
	if (ohsz == 0)
		return PE_EOHDR;
	GATHER(p, R_OPTHDR, ohsz);
	return 0;
}

static int pe_opthdr(ffpe *p)
{
	if (p->dat.len < sizeof(struct pe_hdr32p))
		return PE_ELARGE;

	struct ffpe_info *i = &p->info;
	const struct pe_hdr32p *h = (void*)p->dat.ptr;
	const struct pe_data_dir *dd;
	uint dd_count;

	i->linker_ver[0] = h->linker_ver[0];
	i->linker_ver[1] = h->linker_ver[1];
	i->code_size = ffint_ltoh32(h->code_size);
	i->init_data_size = ffint_ltoh32(h->init_data_size);
	i->uninit_data_size = ffint_ltoh32(h->uninit_data_size);
	i->entry_addr = ffint_ltoh32(h->entry_addr);

	uint m = ffint_ltoh16(h->magic);
	switch (m) {
	case 0x010b: {
		const struct pe_win *ow;
		if (p->dat.len < sizeof(struct pe_hdr32))
			return PE_EOHDR;
		ow = (void*)(p->dat.ptr + sizeof(struct pe_hdr32));
		i->stack_size_res = ffint_ltoh32(ow->stack_size_res);
		i->stack_size_commit = ffint_ltoh32(ow->stack_size_commit);
		dd_count = ffint_ltoh32(ow->data_dir_entries);
		dd = (void*)(ow + 1);
		break;
	}

	case 0x020b: {
		const struct pe_win32p *owp;
		i->pe32plus = 1;
		owp = (void*)(p->dat.ptr + sizeof(struct pe_hdr32p));
		i->stack_size_res = ffint_ltoh64(owp->stack_size_res);
		i->stack_size_commit = ffint_ltoh64(owp->stack_size_commit);
		dd_count = ffint_ltoh32(owp->data_dir_entries);
		dd = (void*)(owp + 1);
		break;
	}

	default:
		return PE_EMAGIC;
	}

	if (dd_count * sizeof(struct pe_data_dir) > (size_t)(ffarr_end(&p->dat) - (char*)dd)
		|| dd_count < 2)
		return PE_EOHDR;

	p->edata.vaddr = ffint_ltoh32(dd[0].vaddr);
	p->edata.vsize = ffint_ltoh32(dd[0].size);

	p->idata.vaddr = ffint_ltoh32(dd[1].vaddr);
	p->idata.vsize = ffint_ltoh32(dd[1].size);

	p->dd = dd;
	p->dd_end = dd + dd_count;
	return 0;
}

/* PE reader:
. Read PE offset from DOS header; seek to it
. Read COFF header
. Read PE header (FFPE_HDR)
. Read sections (FFPE_SECT)

. Pre-read import directory

. Pre-read export directory
*/
int ffpe_read(ffpe *p)
{
	int r;

	for (;;) {

	FFDBG_PRINTLN(10, "state:%u (%s)  next:%u  in:%L  buf:%L  dat:%L"
		, p->state, state_str[p->state], p->next, p->in.len, p->buf.len, p->dat.len);

	switch ((enum R)p->state) {

	case R_GATHER: {
		ssize_t r = ffarr_gather(&p->buf, p->in.ptr, p->in.len, p->gather_size);
		if (r < 0)
			return ERR(p, PE_ESYS);
		ffarr_shift(&p->in, r);
		p->off += r;
		if (p->buf.len != p->gather_size)
			return FFPE_MORE;
		ffstr_set2(&p->dat, &p->buf);
		p->state = p->next;
		p->next = 0;
		p->gather_size = 0;
		p->buf.len = 0;
		break;
	}

	case R_INIT:
		GATHER(p, R_HDR, sizeof(struct dos_hdr));
		break;

	case R_HDR: {
		const struct dos_hdr *h = (void*)p->dat.ptr;
		if (!!ffmemcmp(h->sig, "MZ", 2))
			return ERR(p, PE_ESIG);
		p->off = ffint_ltoh32(h->pe_off);
		GATHER(p, R_PE, sizeof(struct pe_sig) + sizeof(struct coff_hdr));
		return FFPE_SEEK;
	}

	case R_PE:
		if (0 != (r = pe_coffhdr(p)))
			return ERR(p, r);
		//p->state = R_OPTHDR
		break;

	case R_OPTHDR:
		if (0 != (r = pe_opthdr(p)))
			return ERR(p, r);
		p->state = R_DD;
		return FFPE_HDR;

	case R_DD:
		if (p->dd == p->dd_end) {
			p->state = R_SECT_GATHER;
			continue;
		}
		p->data_dir.vaddr = ffint_ltoh32(p->dd->vaddr);
		p->data_dir.vsize = ffint_ltoh32(p->dd->size);
		p->dd++;
		return FFPE_DD;

	case R_SECT_GATHER:
		GATHER(p, R_SECT, p->info.sections * sizeof(struct coff_sect_hdr));
		if (p->gather_size > 64 * 1024)
			return ERR(p, PE_ELARGE);
		if (NULL == ffarr_allocT(&p->sections, p->info.sections, struct ffpe_sect))
			return ERR(p, PE_ESYS);
		continue;

	case R_SECT2:
		ffstr_shift(&p->dat, sizeof(struct coff_sect_hdr));
		//fallthrough
	case R_SECT: {
		if (p->dat.len == 0) {
			p->state = R_IDATA_SEEK;
			continue;
		}

		const struct coff_sect_hdr *h = (void*)p->dat.ptr;
		p->section.name = h->name;
		p->section.vaddr = ffint_ltoh32(h->addr);
		p->section.vsize = ffint_ltoh32(h->size);
		p->section.raw_off = ffint_ltoh32(h->raw_addr);
		p->section.raw_size = ffint_ltoh32(h->raw_size);
		p->section.flags = ffint_ltoh32(h->flags);

		struct ffpe_sect *s = ffarr_pushT(&p->sections, struct ffpe_sect);
		ffmemcpy(s, &p->section, sizeof(struct ffpe_sect));

		p->state = R_SECT2;
		return FFPE_SECT;
	}

	case R_IDATA_COPY: {
		if (p->dat.len < sizeof(struct coff_imp_dir))
			return ERR(p, PE_ELARGE);

		const struct coff_imp_dir *id;
		for (id = (void*)p->dat.ptr;  id + 1 != (void*)p->dat.len;  id++) {
			if (*(uint*)id->lookups_rva == 0 && *(uint*)id->addrs_rva == 0)
				break;
		}
		p->state = R_IDATA;
		if (id == (void*)p->dat.ptr)
			continue;
		if (NULL == ffarr_append(&p->imp_dir, p->dat.ptr, (char*)id - p->dat.ptr))
			return ERR(p, PE_ESYS);

		return FFPE_IMPDIR;
	}

	case R_IDATA:
		p->state = R_EDATA_SEEK;
		continue;

	case R_IDATA_SEEK:
		if (p->idata.vsize == 0) {
			p->state = R_EDATA_SEEK;
			continue;
		}
		GATHER(p, R_IDATA_COPY, p->idata.vsize);
		p->off = real_offset(p, p->idata.vaddr);
		if ((int)p->off < 0)
			return ERR(p, PE_ELARGE);
		return FFPE_SEEK;

	case R_EDATA_SEEK:
		if (p->edata.vsize == 0)
			return FFPE_DONE;
		GATHER(p, R_EDATA, p->edata.vsize);
		p->off = real_offset(p, p->edata.vaddr);
		if ((int)p->off < 0)
			return ERR(p, PE_ELARGE);
		return FFPE_SEEK;

	case R_EDATA:
		return FFPE_DONE;

	}
	}
}
