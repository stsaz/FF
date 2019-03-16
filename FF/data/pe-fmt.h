/** PE format.
Copyright (c) 2019 Simon Zolin
*/


struct dos_hdr {
	byte sig[2]; //"MZ"
	byte unused1[14];
	byte unused2[32];
	byte unused3[12];
	byte pe_off[4];
};

struct pe_sig {
	byte sig[4]; //"PE\0\0"
};

struct coff_hdr {
	byte machine[2]; //enum FFCOFF_MACHINE
	byte sections[2];
	byte tm_created[4]; //time_t
	byte symtbl_addr[4];
	byte symbols[4];
	byte opt_hdr_size[2];
	byte flags[2]; //0x02:image
};

struct pe_hdr32p {
	byte magic[2]; // 0x010b: PE32, 0x020b: PE32+
	byte linker_ver[2]; // x.x
	byte code_size[4];
	byte init_data_size[4];
	byte uninit_data_size[4];
	byte entry_addr[4];
	byte code_addr[4];
};

struct pe_hdr32 {
	byte ohdr[sizeof(struct pe_hdr32p)];
	byte data_addr[4];
};

enum PE_WIN_SUBSYS {
	PE_WIN_SUBSYS_NATIVE = 1,
	PE_WIN_SUBSYS_GUI = 2,
	PE_WIN_SUBSYS_CUI = 3,
};

struct pe_win {
	byte base[4];
	byte sect_align[4];
	byte file_align[4];
	byte win_ver[4]; // xx.xx
	byte img_ver[4]; // xx.xx
	byte subsys_ver[4]; // xx.xx
	byte reserved1[4];
	byte img_size[4];
	byte hdrs_size[4];
	byte chksum[4];
	byte subsys[2]; // enum PE_WIN_SUBSYS
	byte dll_characteristics[2];
	byte stack_size_res[4];
	byte stack_size_commit[4];
	byte heap_size_res[4];
	byte heap_size_commit[4];
	byte reserved2[4];
	byte data_dir_entries[4];
};

struct pe_win32p {
	byte base[8];
	byte sect_align[4];
	byte file_align[4];
	byte win_ver[4]; // xx.xx
	byte img_ver[4]; // xx.xx
	byte subsys_ver[4]; // xx.xx
	byte reserved1[4];
	byte img_size[4];
	byte hdrs_size[4];
	byte chksum[4];
	byte subsys[2];
	byte dll_characteristics[2];
	byte stack_size_res[8];
	byte stack_size_commit[8];
	byte heap_size_res[8];
	byte heap_size_commit[8];
	byte reserved2[4];
	byte data_dir_entries[4];
};

struct pe_data_dir {
	byte vaddr[4];
	byte size[4];
};

enum COFF_SECT_F {
	COFF_SECT_FCODE = 0x20,
	COFF_SECT_FINITDATA = 0x40,
	COFF_SECT_FUNINITDATA = 0x80,
	COFF_SECT_FSHARED = 0x10000000,
	COFF_SECT_FEXEC = 0x20000000,
	COFF_SECT_FREAD = 0x40000000,
	COFF_SECT_FWRITE = 0x80000000,
};

struct coff_sect_hdr {
	char name[8];
	byte size[4];
	byte addr[4];
	byte raw_size[4];
	byte raw_addr[4];
	byte unused1[4];
	byte unused2[4];
	byte unused3[2];
	byte unused4[2];
	byte flags[4]; // enum COFF_SECT_F
};

struct coff_imp_dir {
	byte lookups_rva[4];
	byte unused1[4];
	byte forwarder[4];
	byte name_rva[4];
	byte addrs_rva[4];
};

union coff_imp_lkp {
	byte ordinal_flag[4]; // flag=1, unused[], ordinal[16]
	byte nameaddr_flag[4]; // flag=0, nameaddr[31]
};

union coff_imp_lkp32p {
	byte ordinal_flag[8]; // flag=1, unused[], ordinal[16]
	byte nameaddr_flag[8]; // flag=0, nameaddr[63]
};

struct coff_imp_name {
	byte hint[2];
	char namez[0];
	//byte pad;
};
