#define TC_PDP1 1
#define TARGET_BYTES_BIG_ENDIAN 1

#define OCTETS_PER_BYTE 3

#define WORKING_DOT_WORD 1

#define TARGET_FORMAT "elf32-pdp1"
#define TARGET_ARCH bfd_arch_pdp1

#define md_undefined_symbol(NAME) 0

#define md_estimate_size_before_relax(A, B) (as_fatal(_("estimate size\n")), 0)
#define md_pcrel_from(FIX)		    ((FIX)->fx_where + (FIX)->fx_frag->fr_address -1)
#define md_section_align(SEGMENT, SIZE)	    (SIZE)
