#include "sysdep.h"
#include "bfd.h"
#include "libbfd.h"

const bfd_arch_info_type bfd_pdp1_arch =
{
  24,	/* Bits in a word; PDP-1 has 18 bits in a word, but let's pad it to 24
	   bits so that it's divisible by 8 and binutils play nicely */
  16,	/* Bits in an address (up to 16 in extended mode)  */
  24,	/* Bits in a byte; same situation as with bits in a word */
  bfd_arch_pdp1,
  0,	/* Only 1 machine.  */
  "pdp1",
  "pdp1",
  1,	/* Alignment = 24 bit.  */
  true, /* The one and only.  */
  bfd_default_compatible,
  bfd_default_scan,
  bfd_arch_default_fill,
  NULL,
  0     /* Maximum offset of a reloc from the start of an insn */
};
