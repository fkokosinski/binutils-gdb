#include "sysdep.h"
#include "bfd.h"
#include "libbfd.h"
#include "elf-bfd.h"
#include "elf/pdp1.h"

static reloc_howto_type pdp1_elf_howto_table[] =
{
  /* No relocation.  */
  HOWTO (R_PDP1_NONE,		/* type */
	 0,			/* rightshift */
	 0,			/* size (0 = byte, 1 = short, 2 = long) */
	 0,			/* bitsize */
	 false,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 bfd_elf_generic_reloc,	        /* special_function */
	 "R_PDP1_NONE",	        /* name */
	 false,			/* partial_inplace */
	 0,			/* src_mask */
	 0,			/* dst_mask */
	 false),		/* pcrel_offset */

  HOWTO (R_PDP1_DIR16,	        /* type */
	 0,			/* rightshift */
	 3,			/* size (0 = byte, 1 = short, 2 = long) */
	 24,			/* bitsize */
	 false,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_bitfield, /* complain_on_overflow */
	 bfd_elf_generic_reloc,		/* special_function */
	 "R_PDP1_DIR16",		/* name */
	 false,			/* partial_inplace */
	 0x00000000,		/* src_mask */
	 0x00000fff,		/* dst_mask */
	 false),		/* pcrel_offset */

  HOWTO (R_PDP1_DIR24,	        /* type */
	 0,			/* rightshift */
	 3,			/* size (0 = byte, 1 = short, 2 = long) */
	 24,			/* bitsize */
	 false,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_bitfield, /* complain_on_overflow */
	 bfd_elf_generic_reloc,		/* special_function */
	 "R_PDP1_DIR24",		/* name */
	 false,			/* partial_inplace */
	 0x00000000,		/* src_mask */
	 0x00ffffff,		/* dst_mask */
	 false),		/* pcrel_offset */
};

/* This structure is used to map BFD reloc codes to pdp1 elf relocs.  */

struct elf_reloc_map
{
  bfd_reloc_code_real_type bfd_reloc_val;
  unsigned char elf_reloc_val;
};

/* An array mapping BFD reloc codes to pdp1 elf relocs.  */

static const struct elf_reloc_map pdp1_reloc_map[] =
{
    { BFD_RELOC_NONE, 		R_PDP1_NONE          },
    { BFD_RELOC_16, 		R_PDP1_DIR16         },
    { BFD_RELOC_24, 		R_PDP1_DIR24         }
};

/* Given a BFD reloc code, return the howto structure for the
   corresponding pdp1 elf reloc.  */

static reloc_howto_type *
pdp1_elf_reloc_type_lookup (bfd *abfd ATTRIBUTE_UNUSED,
			     bfd_reloc_code_real_type code)
{
  unsigned int i;

  for (i = 0; i < sizeof (pdp1_reloc_map) / sizeof (struct elf_reloc_map); i++)
    if (pdp1_reloc_map[i].bfd_reloc_val == code)
      return & pdp1_elf_howto_table[(int) pdp1_reloc_map[i].elf_reloc_val];

  return NULL;
}

static reloc_howto_type *
pdp1_elf_reloc_name_lookup (bfd *abfd ATTRIBUTE_UNUSED,
			  const char *r_name)
{
  unsigned int i;

  for (i = 0;
       i < sizeof (pdp1_elf_howto_table) / sizeof (pdp1_elf_howto_table[0]);
       i++)
    if (pdp1_elf_howto_table[i].name != NULL
	&& strcasecmp (pdp1_elf_howto_table[i].name, r_name) == 0)
      return &pdp1_elf_howto_table[i];

  return NULL;
}

/* Given an ELF reloc, fill in the howto field of a relent.  */

static bool
pdp1_elf_info_to_howto (bfd *abfd ATTRIBUTE_UNUSED,
		      arelent *cache_ptr,
		      Elf_Internal_Rela *dst)
{
  unsigned int r;

  r = ELF32_R_TYPE (dst->r_info);

  BFD_ASSERT (r < (unsigned int) R_PDP1_max);

  cache_ptr->howto = &pdp1_elf_howto_table[r];

  return true;
}

#define ELF_ARCH		bfd_arch_pdp1
#define ELF_MACHINE_CODE	EM_PDP1
#define ELF_MAXPAGESIZE		1

#define TARGET_BIG_SYM		pdp1_elf32_vec
#define TARGET_BIG_NAME		"elf32-pdp1"

#define elf_info_to_howto	pdp1_elf_info_to_howto
#define elf_info_to_howto_rel	NULL

#define bfd_elf32_bfd_reloc_type_lookup		pdp1_elf_reloc_type_lookup
#define bfd_elf32_bfd_reloc_name_lookup		pdp1_elf_reloc_name_lookup

#include "elf32-target.h"
