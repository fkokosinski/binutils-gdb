#include "as.h"
#include "safe-ctype.h"
#include "opcode/pdp1.h"
#include <assert.h>

extern const pdp1_opc_info_t pdp1_opc_info[128];

const char comment_chars[]        = "#";
const char line_separator_chars[] = ";";
const char line_comment_chars[]   = "#";

static int pending_reloc;
static htab_t opcode_hash_control;

const pseudo_typeS md_pseudo_table[] =
{
  { "word", cons, 3 },
  { "long", cons, 3 },
  { "short", cons, 3 },
  { "byte", cons, 3 },
  {0, 0, 0}
};

const char FLT_CHARS[] = "rRsSfFdDxXpP";
const char EXP_CHARS[] = "eE";

void
md_convert_frag (bfd *abfd ATTRIBUTE_UNUSED,
		 asection *sec ATTRIBUTE_UNUSED,
		 fragS *fragP ATTRIBUTE_UNUSED)
{
  abort ();
}

void
md_operand (expressionS *op __attribute__((unused)))
{

}

void
md_begin (void)
{
  const pdp1_opc_info_t *opcode;
  opcode_hash_control = str_htab_create ();

  for (opcode = pdp1_opc_info; opcode->name; opcode++)
    str_hash_insert (opcode_hash_control, opcode->name, (char *) opcode, 0);

  bfd_set_arch_mach (stdoutput, TARGET_ARCH, 0);
}

static char *
skip_whitespace (char *str)
{
  while (*str == ' ' || *str == '\t')
    str++;
  return str;
}

static char *
find_whitespace (char *str)
{
  while (*str != ' ' && *str != '\t' && *str != 0)
    str++;
  return str;
}

static char *
parse_exp_save_ilp (char *s, expressionS *op)
{
  char *save = input_line_pointer;

  input_line_pointer = s;
  expression (op);
  s = input_line_pointer;
  input_line_pointer = save;
  return s;
}

void
md_assemble (char *str)
{
  char *start;
  char *end;
  pdp1_opc_info_t *opcode;
  char *output = frag_more(3);
  int operand;
  int instr;

  start = skip_whitespace (str);
  end = find_whitespace (start);

  /* substitute the first whitespace found in the line with a null byte, so
   * that the str_hash_find can use a proper key */
  *end = '\0';

  /* get the opcode */
  opcode = (pdp1_opc_info_t *) str_hash_find (opcode_hash_control, start);
  if (opcode == NULL)
    {
      as_bad (_("Unknown instruction %s"), start);
    }

  /* memory ref instr - get the address */
  start = end + 1;
  end = find_whitespace (start);
  *end = '\0';

  operand = atoi (start);
  /* encode operand */
  instr = opcode->opcode;
  switch (opcode->type)
    {
      case PDP1_INSTR_MEMREF:
	expressionS arg;
	operand = 0;

	parse_exp_save_ilp (start, &arg);
	fix_new_exp (frag_now,
	             (&output[1] - frag_now->fr_literal),
		     2,
		     &arg,
		     0,
		     BFD_RELOC_16);

	instr |= operand & ~opcode->mask;
	break;
      case PDP1_INSTR_IMM:
	/* TODO: support fixups/relocs here as well */
	int bit = 0;
	if (operand < 0)
	  bit = 0010000;

	operand = abs(operand);
	if (operand > 4096)
	  as_warn ("Value %d too large for imm instruction. Converted to %d", operand, operand & 07777);

	instr |= (operand | bit) & ~opcode->mask;
	break;
      case PDP1_INSTR_SHIFT:
	if (operand < 0 || operand > 9)
	  as_bad ("Shift by %d not in the valid range of 0..9", operand);

	instr |= ((1 << operand) - 1) & ~opcode->mask;
	break;
      case PDP1_INSTR_SKIP:
      case PDP1_INSTR_OP:
      case PDP1_INSTR_IO:
	break;
      case PDP1_INSTR_SKIP_FLAG:
      case PDP1_INSTR_OP_FLAG:
	if (operand < 1 || operand > 7)
	  as_bad ("Operand %d not in the valid range of 1..7", operand);

	instr |= operand & ~opcode->mask;
	break;
      case PDP1_INSTR_SKIP_SWITCH:
      case PDP1_INSTR_OP_SWITCH:
	if (operand < 1 || operand > 7)
	  as_bad ("Operand %d not in the valid range of 1..7", operand);

	instr |= (operand << 3) & ~opcode->mask;
	break;
      default:
	as_bad ("%s: unknown opcode type!", opcode->name);
    }

  /* every PDP-1 instruction is three-bytes long */
  output[0] = (instr >> 16) & 0x00000f;
  output[1] = (instr >>  8) & 0x0000ff;
  output[2] = (instr >>  0) & 0x0000ff;

  if (pending_reloc)
    as_bad ("Something forgot to clean up\n");
}

const char *
md_atof (int type, char *litP, int *sizeP)
{
  return ieee_md_atof (type, litP, sizeP, false);
}

const char *md_shortopts = "";

struct option md_longopts[] =
{
  {NULL, no_argument, NULL, 0}
};
size_t md_longopts_size = sizeof (md_longopts);

/* We have no target specific options yet, so these next
   two functions are empty.  */
int
md_parse_option (int c ATTRIBUTE_UNUSED, const char *arg ATTRIBUTE_UNUSED)
{
  return 0;
}

void
md_show_usage (FILE *stream ATTRIBUTE_UNUSED)
{
}

/* Apply a fixup to the object file.  */

void
md_apply_fix (fixS *fixP, valueT * valP, segT seg ATTRIBUTE_UNUSED)
{
  char *buf = fixP->fx_where + fixP->fx_frag->fr_literal;
  long val = *valP;
  long max, min;

  max = min = 0;
  switch (fixP->fx_r_type)
    {
    case BFD_RELOC_16:
      *buf = (*buf & 0x0000f0) | ((val >> 8) & 0x00000f);
      buf++;
      *buf++ = (val >> 0) & 0x0000ff;
      break;

    case BFD_RELOC_24:
      *buf++ = (val >> 16) & 0x000003;
      *buf++ = (val >>  8) & 0x0000ff;
      *buf++ = (val >>  0) & 0x0000ff;
      break;

    default:
      abort ();
    }

  if (max != 0 && (val < min || val > max))
    as_bad_where (fixP->fx_file, fixP->fx_line, _("offset out of range"));

  if (fixP->fx_addsy == NULL && fixP->fx_pcrel == 0)
    fixP->fx_done = 1;
}

/* Put number into target byte order (big endian).  */

void
md_number_to_chars (char *ptr, valueT use, int nbytes)
{
  number_to_chars_bigendian (ptr, use, nbytes);
}

/* Translate internal representation of relocation info to BFD target
   format.  */

arelent *
tc_gen_reloc (asection *section ATTRIBUTE_UNUSED, fixS *fixP)
{
  arelent *relP;
  bfd_reloc_code_real_type code;
  code = fixP->fx_r_type;

  relP = XNEW (arelent);
  relP->sym_ptr_ptr = XNEW (asymbol *);
  *relP->sym_ptr_ptr = symbol_get_bfdsym (fixP->fx_addsy);
  relP->address = fixP->fx_frag->fr_address + fixP->fx_where;
  relP->addend = fixP->fx_offset;

  /* Due to the funky way PDP-1 does byte addressing, fixups are confused about
   * their belonging. Let's do a dirty hack here */
  switch (code) {
    case BFD_RELOC_16:
      relP->address -= 1;
      relP->address /= 3;
      break;
    case BFD_RELOC_24:
      relP->address /= 3;
      break;
    default:
      /* TODO: handle this! */
      break;
  }

  /* This is the standard place for KLUDGEs to work around bugs in
     bfd_install_relocation (first such note in the documentation
     appears with binutils-2.8).

     That function bfd_install_relocation does the wrong thing with
     putting stuff into the addend of a reloc (it should stay out) for a
     weak symbol.  The really bad thing is that it adds the
     "segment-relative offset" of the symbol into the reloc.  In this
     case, the reloc should instead be relative to the symbol with no
     other offset than the assembly code shows; and since the symbol is
     weak, any local definition should be ignored until link time (or
     thereafter).
     To wit:  weaksym+42  should be weaksym+42 in the reloc,
     not weaksym+(offset_from_segment_of_local_weaksym_definition)

     To "work around" this, we subtract the segment-relative offset of
     "known" weak symbols.  This evens out the extra offset.

     That happens for a.out but not for ELF, since for ELF,
     bfd_install_relocation uses the "special function" field of the
     howto, and does not execute the code that needs to be undone.  */

  if (OUTPUT_FLAVOR == bfd_target_aout_flavour
      && fixP->fx_addsy && S_IS_WEAK (fixP->fx_addsy)
      && ! bfd_is_und_section (S_GET_SEGMENT (fixP->fx_addsy)))
    {
      relP->addend -= S_GET_VALUE (fixP->fx_addsy);
    }

  relP->howto = bfd_reloc_type_lookup (stdoutput, code);
  if (! relP->howto)
    {
      const char *name;

      name = S_GET_NAME (fixP->fx_addsy);
      if (name == NULL)
	name = _("<unknown>");
      as_fatal (_("Cannot generate relocation type for symbol %s, code %s"),
		name, bfd_get_reloc_code_name (code));
    }

  return relP;
}
