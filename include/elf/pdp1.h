#ifndef _ELF_PDP1_H
#define _ELF_PDP1_H

#include "elf/reloc-macros.h"

START_RELOC_NUMBERS (elf_pdp1_reloc_type)
  RELOC_NUMBER (R_PDP1_NONE, 0)
  RELOC_NUMBER (R_PDP1_DIR16, 1)
  RELOC_NUMBER (R_PDP1_DIR24, 2)
END_RELOC_NUMBERS (R_PDP1_max)

#endif
