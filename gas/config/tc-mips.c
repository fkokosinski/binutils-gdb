/* tc-mips.c -- assemble code for a MIPS chip.
   Copyright (C) 1993 Free Software Foundation, Inc.
   Contributed by the OSF and Ralph Campbell.
   Written by Keith Knowles and Ralph Campbell, working independently.
   Modified for ECOFF and R4000 support by Ian Lance Taylor of Cygnus
   Support.

   This file is part of GAS.

   GAS is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   GAS is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GAS; see the file COPYING.  If not, write to
   the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.  */

#include "as.h"
#include "config.h"

#include <ctype.h>

#ifndef __STDC__
#ifndef NO_STDARG
#define NO_STDARG
#endif
#endif

#ifndef NO_STDARG
#include <stdarg.h>
#else
#ifndef NO_VARARGS
#include <varargs.h>
#endif /* NO_VARARGS */
#endif /* NO_STDARG */

#include "opcode/mips.h"

#ifdef OBJ_ELF
#include "elf/mips.h"

static char *mips_regmask_frag;
#endif

#define AT  1
#define PIC_CALL_REG 25
#define GP  28
#define SP  29
#define FP  30
#define RA  31

/* Decide whether to do GP reference optimizations based on the object
   file format.  */
#undef GPOPT
#ifdef OBJ_ECOFF
#define GPOPT
#endif
#ifdef OBJ_ELF
#define GPOPT
#endif

/* These variables are filled in with the masks of registers used.
   The object format code reads them and puts them in the appropriate
   place.  */
unsigned long mips_gprmask;
unsigned long mips_cprmask[4];

/* MIPS ISA (Instruction Set Architecture) level.  */
static int mips_isa = -1;

/* MIPS PIC level.  0 is normal, non-PIC code.  2 means to generate
   SVR4 ABI PIC calls.  1 doesn't mean anything.  */
static int mips_pic;

static int mips_warn_about_macros;
static int mips_noreorder;
static int mips_any_noreorder;
static int mips_nomove;
static int mips_noat;
static int mips_nobopt;

#ifdef GPOPT
/* The size of the small data section.  */
static int g_switch_value = 8;
#endif

#define N_RMASK 0xc4
#define N_VFP   0xd4

/* handle of the OPCODE hash table */
static struct hash_control *op_hash = NULL;

/* This array holds the chars that always start a comment.  If the
    pre-processor is disabled, these aren't very useful */
const char comment_chars[] = "#";

/* This array holds the chars that only start a comment at the beginning of
   a line.  If the line seems to have the form '# 123 filename'
   .line and .file directives will appear in the pre-processed output */
/* Note that input_file.c hand checks for '#' at the beginning of the
   first line of the input file.  This is because the compiler outputs
   #NO_APP at the beginning of its output. */
/* Also note that C style comments are always supported.  */
const char line_comment_chars[] = "#";

/* This array holds machine specific line separator characters. */
const char line_separator_chars[] = "";

/* Chars that can be used to separate mant from exp in floating point nums */
const char EXP_CHARS[] = "eE";

/* Chars that mean this number is a floating point constant */
/* As in 0f12.456 */
/* or    0d1.2345e12 */
const char FLT_CHARS[] = "rRsSfFdDxXpP";

/* Also be aware that MAXIMUM_NUMBER_OF_CHARS_FOR_FLOAT may have to be
   changed in read.c .  Ideally it shouldn't have to know about it at all,
   but nothing is ideal around here.
 */

static char *insn_error;

static int byte_order = BYTE_ORDER;

static int auto_align = 1;

/* Symbol labelling the current insn.  */
static symbolS *insn_label;

/* When outputting SVR4 PIC code, the assembler needs to know the
   offset in the stack frame from which to restore the $gp register.
   This is set by the .cprestore pseudo-op, and saved in this
   variable.  */
static offsetT mips_cprestore_offset = -1;

/* This is the register which holds the stack frame, as set by the
   .frame pseudo-op.  This is needed to implement .cprestore.  */
static int mips_frame_reg = SP;

/* To output NOP instructions correctly, we need to keep information
   about the previous two instructions.  */

/* Whether we are optimizing.  The default value of 2 means to remove
   unneeded NOPs and swap branch instructions when possible.  A value
   of 1 means to not swap branches.  A value of 0 means to always
   insert NOPs.  */
static int mips_optimize = 2;

/* The previous instruction.  */
static struct mips_cl_insn prev_insn;

/* The instruction before prev_insn.  */
static struct mips_cl_insn prev_prev_insn;

/* If we don't want information for prev_insn or prev_prev_insn, we
   point the insn_mo field at this dummy integer.  */
static const struct mips_opcode dummy_opcode = { 0 };

/* Non-zero if prev_insn is valid.  */
static int prev_insn_valid;

/* The frag for the previous instruction.  */
static struct frag *prev_insn_frag;

/* The offset into prev_insn_frag for the previous instruction.  */
static long prev_insn_where;

/* The reloc for the previous instruction, if any.  */
static fixS *prev_insn_fixp;

/* Non-zero if the previous instruction was in a delay slot.  */
static int prev_insn_is_delay_slot;

/* Non-zero if the previous instruction was in a .set noreorder.  */
static int prev_insn_unreordered;

/* Non-zero if the previous previous instruction was in a .set
   noreorder.  */
static int prev_prev_insn_unreordered;

/* Since the MIPS does not have multiple forms of PC relative
   instructions, we do not have to do relaxing as is done on other
   platforms.  However, we do have to handle GP relative addressing
   correctly, which turns out to be a similar problem.

   Every macro that refers to a symbol can occur in (at least) two
   forms, one with GP relative addressing and one without.  For
   example, loading a global variable into a register generally uses
   an macroinstruction like this:
     lw $4,i
   If i can be addressed off the GP register (this is true if it is in
   the .sbss or .sdata section, or if it is known to be smaller than
   the -G argument) this will generate the following instruction:
     lw $4,i($gp)
   This instruction will use a GPREL reloc.  If i can not be addressed
   off the GP register, the following instruction sequence will be used:
     lui $at,i
     lw $4,i($at)
   In this case the first instruction will have a HI16 reloc, and the
   second reloc will have a LO16 reloc.  Both relocs will be against
   the symbol i.

   The issue here is that we may not know whether i is GP addressable
   until after we see the instruction that uses it.  Therefore, we
   want to be able to choose the final instruction sequence only at
   the end of the assembly.  This is similar to the way other
   platforms choose the form of a PC relative instruction only at the
   end of assembly.

   When generating position independent code we do not use GP
   addressing in the same way, but the issue still arises as external
   symbols and local symbols must be handled differently.

   We handle these issues by actually generating both possible
   instruction sequences.  The longer one is put in a frag_var with
   type rs_machine_dependent.  We encode what to do with the frag in
   the subtype field.  We encode (1) the number of existing bytes to
   replace, (2) the number of new bytes to use, (3) the offset from
   the start of the existing bytes to the first reloc we must generate
   (that is, the offset is applied from the start of the existing
   bytes after they are replaced by the new bytes, if any), (4) the
   offset from the start of the existing bytes to the second reloc,
   (5) whether a third reloc is needed (the third reloc is always four
   bytes after the second reloc), and (6) whether to warn if this
   variant is used (this is sometimes needed if .set nomacro or .set
   noat is in effect).  All these numbers are reasonably small.

   Generating two instruction sequences must be handled carefully to
   ensure that delay slots are handled correctly.  Fortunately, the
   issue only arises in a restricted number of cases.  When the second
   instruction sequence is generated, append_insn is directed to
   maintain the existing delay slot information, so it continues to
   apply to any code after the second instruction sequence.  This
   means that the second instruction sequence must not impose any
   requirements not required by the first instruction sequence.

   These variant frags are then handled in functions called by the
   machine independent code.  md_estimate_size_before_relax returns
   the final size of the frag.  md_convert_frag sets up the final form
   of the frag.  tc_gen_reloc adjust the first reloc and adds a second
   one if needed.  */
#define RELAX_ENCODE(old, new, reloc1, reloc2, reloc3, warn) \
  ((relax_substateT) \
   (((old) << 24) \
    | ((new) << 16) \
    | (((reloc1) + 64) << 9) \
    | (((reloc2) + 64) << 2) \
    | ((reloc3) ? (1 << 1) : 0) \
    | ((warn) ? 1 : 0)))
#define RELAX_OLD(i) (((i) >> 24) & 0xff)
#define RELAX_NEW(i) (((i) >> 16) & 0xff)
#define RELAX_RELOC1(i) ((((i) >> 9) & 0x7f) - 64)
#define RELAX_RELOC2(i) ((((i) >> 2) & 0x7f) - 64)
#define RELAX_RELOC3(i) (((i) >> 1) & 1)
#define RELAX_WARN(i) ((i) & 1)

/* Prototypes for static functions.  */

#ifdef __STDC__
#define internalError() \
    as_fatal ("internal Error, line %d, %s", __LINE__, __FILE__)
#else
#define internalError() as_fatal ("MIPS internal Error");
#endif

static int insn_uses_reg PARAMS ((struct mips_cl_insn *ip,
				  unsigned int reg, int fpr));
static void append_insn PARAMS ((char *place,
				 struct mips_cl_insn * ip,
				 expressionS * p,
				 bfd_reloc_code_real_type r));
static void mips_no_prev_insn PARAMS ((void));
static void mips_emit_delays PARAMS ((void));
static void macro_build PARAMS ((char *place, int *counter, expressionS * ep,
				 const char *name, const char *fmt,
				 ...));
static void macro_build_lui PARAMS ((char *place, int *counter,
				     expressionS * ep, int regnum));
static void set_at PARAMS ((int *counter, int reg, int unsignedp));
static void check_absolute_expr PARAMS ((struct mips_cl_insn * ip,
					 expressionS *));
static void load_register PARAMS ((int *counter, int reg, expressionS * ep));
static void load_address PARAMS ((int *counter, int reg, expressionS *ep));
static void macro PARAMS ((struct mips_cl_insn * ip));
#ifdef LOSING_COMPILER
static void macro2 PARAMS ((struct mips_cl_insn * ip));
#endif
static void mips_ip PARAMS ((char *str, struct mips_cl_insn * ip));
static int my_getSmallExpression PARAMS ((expressionS * ep, char *str));
static void my_getExpression PARAMS ((expressionS * ep, char *str));
static symbolS *get_symbol PARAMS ((void));
static void mips_align PARAMS ((int to, int fill));
static void s_align PARAMS ((int));
static void s_stringer PARAMS ((int));
static void s_change_sec PARAMS ((int));
static void s_cons PARAMS ((int));
static void s_err PARAMS ((int));
static void s_extern PARAMS ((int));
static void s_float_cons PARAMS ((int));
static void s_option PARAMS ((int));
static void s_mipsset PARAMS ((int));
static void s_mips_space PARAMS ((int));
static void s_abicalls PARAMS ((int));
static void s_cpload PARAMS ((int));
static void s_cprestore PARAMS ((int));
static void s_gpword PARAMS ((int));
static void s_cpadd PARAMS ((int));
#ifndef ECOFF_DEBUGGING
static void md_obj_begin PARAMS ((void));
static void md_obj_end PARAMS ((void));
static long get_number PARAMS ((void));
static void s_ent PARAMS ((int));
static void s_mipsend PARAMS ((int));
static void s_file PARAMS ((int));
#if 0
static void s_frame PARAMS ((int));
static void s_loc PARAMS ((int));
static void s_mask PARAMS ((char));
#endif
#endif

/* Pseudo-op table.

   The following pseudo-ops from the Kane and Heinrich MIPS book
   should be defined here, but are currently unsupported: .alias,
   .galive, .gjaldef, .gjrlive, .livereg, .noalias.

   The following pseudo-ops from the Kane and Heinrich MIPS book are
   specific to the type of debugging information being generated, and
   should be defined by the object format: .aent, .begin, .bend,
   .bgnb, .end, .endb, .ent, .fmask, .frame, .loc, .mask, .verstamp,
   .vreg.

   The following pseudo-ops from the Kane and Heinrich MIPS book are
   not MIPS CPU specific, but are also not specific to the object file
   format.  This file is probably the best place to define them, but
   they are not currently supported: .asm0, .endr, .lab, .repeat,
   .struct, .weakext.  */

const pseudo_typeS md_pseudo_table[] =
{
 /* MIPS specific pseudo-ops.  */
  {"option", s_option, 0},
  {"set", s_mipsset, 0},
  {"rdata", s_change_sec, 'r'},
  {"sdata", s_change_sec, 's'},
  {"livereg", s_ignore, 0},
  { "abicalls", s_abicalls, 0},
  { "cpload", s_cpload, 0},
  { "cprestore", s_cprestore, 0},
  { "gpword", s_gpword, 0},
  { "cpadd", s_cpadd, 0},

 /* Relatively generic pseudo-ops that happen to be used on MIPS
     chips.  */
  {"asciiz", s_stringer, 1},
  {"bss", s_change_sec, 'b'},
  {"err", s_err, 0},
  {"half", s_cons, 1},
  {"dword", s_cons, 3},

 /* These pseudo-ops are defined in read.c, but must be overridden
     here for one reason or another.  */
  {"align", s_align, 0},
  {"ascii", s_stringer, 0},
  {"asciz", s_stringer, 1},
  {"byte", s_cons, 0},
  {"data", s_change_sec, 'd'},
  {"double", s_float_cons, 'd'},
  {"extern", s_extern, 0},
  {"float", s_float_cons, 'f'},
  {"space", s_mips_space, 0},
  {"text", s_change_sec, 't'},
  {"word", s_cons, 2},

#ifndef ECOFF_DEBUGGING
 /* These pseudo-ops should be defined by the object file format.
    However, a.out doesn't support them, so we have versions here.  */
  {"aent", s_ent, 1},
  {"bgnb", s_ignore, 0},
  {"end", s_mipsend, 0},
  {"endb", s_ignore, 0},
  {"ent", s_ent, 0},
  {"file", s_file, 0},
  {"fmask", s_ignore, 'F'},
  {"frame", s_ignore, 0},
  {"loc", s_ignore, 0},
  {"mask", s_ignore, 'R'},
  {"verstamp", s_ignore, 0},
#endif

 /* Sentinel.  */
  {NULL}
};

const relax_typeS md_relax_table[] =
{
  { 0 }
};


static char *expr_end;

static expressionS imm_expr;
static expressionS offset_expr;
static bfd_reloc_code_real_type imm_reloc;
static bfd_reloc_code_real_type offset_reloc;

/* FIXME: This should be handled in a different way.  */
extern int target_big_endian;

/*
 * This function is called once, at assembler startup time.  It should
 * set up all the tables, etc. that the MD part of the assembler will need.
 */
void
md_begin ()
{
  boolean ok = false;
  register const char *retval = NULL;
  register unsigned int i = 0;

  if (mips_isa == -1)
    {
      if (strcmp (TARGET_CPU, "mips") == 0)
	mips_isa = 1;
      else if (strcmp (TARGET_CPU, "r6000") == 0
	       || strcmp (TARGET_CPU, "mips2") == 0)
	mips_isa = 2;
      else if (strcmp (TARGET_CPU, "mips64") == 0
	       || strcmp (TARGET_CPU, "r4000") == 0
	       || strcmp (TARGET_CPU, "mips3") == 0)
	mips_isa = 3;
      else
	mips_isa = 1;
    }

  switch (mips_isa)
    {
    case 1:
      ok = bfd_set_arch_mach (stdoutput, bfd_arch_mips, 3000);
      break;
    case 2:
      ok = bfd_set_arch_mach (stdoutput, bfd_arch_mips, 6000);
      break;
    case 3:
      ok = bfd_set_arch_mach (stdoutput, bfd_arch_mips, 4000);
      break;
    }
  if (! ok)
    as_warn ("Could not set architecture and machine");

  op_hash = hash_new ();

  for (i = 0; i < NUMOPCODES;)
    {
      const char *name = mips_opcodes[i].name;

      retval = hash_insert (op_hash, name, (PTR) &mips_opcodes[i]);
      if (retval != NULL)
	{
	  fprintf (stderr, "internal error: can't hash `%s': %s\n",
		   mips_opcodes[i].name, retval);
	  as_fatal ("Broken assembler.  No assembly attempted.");
	}
      do
	{
	  if (mips_opcodes[i].pinfo != INSN_MACRO
	      && ((mips_opcodes[i].match & mips_opcodes[i].mask)
		  != mips_opcodes[i].match))
	    {
	      fprintf (stderr, "internal error: bad opcode: `%s' \"%s\"\n",
		       mips_opcodes[i].name, mips_opcodes[i].args);
	      as_fatal ("Broken assembler.  No assembly attempted.");
	    }
	  ++i;
	}
      while ((i < NUMOPCODES) && !strcmp (mips_opcodes[i].name, name));
    }

  mips_no_prev_insn ();

  mips_gprmask = 0;
  mips_cprmask[0] = 0;
  mips_cprmask[1] = 0;
  mips_cprmask[2] = 0;
  mips_cprmask[3] = 0;

  /* set the default alignment for the text section (2**2) */
  record_alignment (text_section, 2);

  /* FIXME: This should be handled in a different way.  */
  target_big_endian = byte_order == BIG_ENDIAN;

#ifdef GPOPT
  bfd_set_gp_size (stdoutput, g_switch_value);
#endif

#ifdef OBJ_ELF
  /* Sections must be aligned to 16 byte boundaries.  */
  (void) bfd_set_section_alignment (stdoutput, text_section, 4);
  (void) bfd_set_section_alignment (stdoutput, data_section, 4);
  (void) bfd_set_section_alignment (stdoutput, bss_section, 4);

  /* Create a .reginfo section for register masks and a .mdebug
     section for debugging information.  */
  {
    segT seg;
    subsegT subseg;
    segT sec;

    seg = now_seg;
    subseg = now_subseg;
    sec = subseg_new (".reginfo", (subsegT) 0);

    /* I don't know why this section should be loaded, but the ABI
       says that SHF_ALLOC should be set.  */
    (void) bfd_set_section_flags (stdoutput, sec,
				  (SEC_ALLOC | SEC_LOAD
				   | SEC_READONLY | SEC_DATA));
    (void) bfd_set_section_alignment (stdoutput, sec, 2);

    mips_regmask_frag = frag_more (sizeof (Elf32_External_RegInfo));

#ifdef ECOFF_DEBUGGING
    sec = subseg_new (".mdebug", (subsegT) 0);
    (void) bfd_set_section_flags (stdoutput, sec,
				  SEC_HAS_CONTENTS | SEC_READONLY);
    (void) bfd_set_section_alignment (stdoutput, sec, 2);
#endif

    subseg_set (seg, subseg);
  }
#endif /* OBJ_ELF */

#ifndef ECOFF_DEBUGGING
  md_obj_begin ();
#endif
}

void
md_mips_end ()
{
#ifndef ECOFF_DEBUGGING
  md_obj_end ();
#endif
}

void
md_assemble (str)
     char *str;
{
  struct mips_cl_insn insn;

  imm_expr.X_op = O_absent;
  offset_expr.X_op = O_absent;

  mips_ip (str, &insn);
  if (insn_error)
    {
      as_bad ("%s `%s'", insn_error, str);
      return;
    }
  if (insn.insn_mo->pinfo == INSN_MACRO)
    {
      macro (&insn);
    }
  else
    {
      if (imm_expr.X_op != O_absent)
	append_insn ((char *) NULL, &insn, &imm_expr, imm_reloc);
      else if (offset_expr.X_op != O_absent)
	append_insn ((char *) NULL, &insn, &offset_expr, offset_reloc);
      else
	append_insn ((char *) NULL, &insn, NULL, BFD_RELOC_UNUSED);
    }
}

/* See whether instruction IP reads register REG.  If FPR is non-zero,
   REG is a floating point register.  */

static int
insn_uses_reg (ip, reg, fpr)
     struct mips_cl_insn *ip;
     unsigned int reg;
     int fpr;
{
  /* Don't report on general register 0, since it never changes.  */
  if (! fpr && reg == 0)
    return 0;

  if (fpr)
    {
      /* If we are called with either $f0 or $f1, we must check $f0.
	 This is not optimal, because it will introduce an unnecessary
	 NOP between "lwc1 $f0" and "swc1 $f1".  To fix this we would
	 need to distinguish reading both $f0 and $f1 or just one of
	 them.  Note that we don't have to check the other way,
	 because there is no instruction that sets both $f0 and $f1
	 and requires a delay.  */
      if ((ip->insn_mo->pinfo & INSN_READ_FPR_S)
	  && (((ip->insn_opcode >> OP_SH_FS) & OP_MASK_FS)
	      == (reg &~ (unsigned) 1)))
	return 1;
      if ((ip->insn_mo->pinfo & INSN_READ_FPR_T)
	  && (((ip->insn_opcode >> OP_SH_FT) & OP_MASK_FT)
	      == (reg &~ (unsigned) 1)))
	return 1;
    }
  else
    {
      if ((ip->insn_mo->pinfo & INSN_READ_GPR_S)
	  && ((ip->insn_opcode >> OP_SH_RS) & OP_MASK_RS) == reg)
	return 1;
      if ((ip->insn_mo->pinfo & INSN_READ_GPR_T)
	  && ((ip->insn_opcode >> OP_SH_RT) & OP_MASK_RT) == reg)
	return 1;
    }

  return 0;
}

/* Output an instruction.  PLACE is where to put the instruction; if
   it is NULL, this uses frag_more to get room.  IP is the instruction
   information.  ADDRESS_EXPR is an operand of the instruction to be
   used with RELOC_TYPE.  */

static void
append_insn (place, ip, address_expr, reloc_type)
     char *place;
     struct mips_cl_insn *ip;
     expressionS *address_expr;
     bfd_reloc_code_real_type reloc_type;
{
  register unsigned long prev_pinfo, pinfo;
  char *f;
  fixS *fixp;
  int nops = 0;

  prev_pinfo = prev_insn.insn_mo->pinfo;
  pinfo = ip->insn_mo->pinfo;

  if (place == NULL && ! mips_noreorder)
    {
      /* If the previous insn required any delay slots, see if we need
	 to insert a NOP or two.  There are eight kinds of possible
	 hazards, of which an instruction can have at most one type.
	 (1) a load from memory delay
	 (2) a load from a coprocessor delay
	 (3) an unconditional branch delay
	 (4) a conditional branch delay
	 (5) a move to coprocessor register delay
	 (6) a load coprocessor register from memory delay
	 (7) a coprocessor condition code delay
	 (8) a HI/LO special register delay

	 There are a lot of optimizations we could do that we don't.
	 In particular, we do not, in general, reorder instructions.
	 If you use gcc with optimization, it will reorder
	 instructions and generally do much more optimization then we
	 do here; repeating all that work in the assembler would only
	 benefit hand written assembly code, and does not seem worth
	 it.  */

      /* This is how a NOP is emitted.  */
#define emit_nop() md_number_to_chars (frag_more (4), 0, 4)

      /* The previous insn might require a delay slot, depending upon
	 the contents of the current insn.  */
      if ((prev_pinfo & INSN_LOAD_COPROC_DELAY)
	  || (mips_isa < 2
	      && (prev_pinfo & INSN_LOAD_MEMORY_DELAY)))
	{
	  /* A load from a coprocessor or from memory.  All load
	     delays delay the use of general register rt for one
	     instruction on the r3000.  The r6000 and r4000 use
	     interlocks.  */
	  know (prev_pinfo & INSN_WRITE_GPR_T);
	  if (mips_optimize == 0
	      || insn_uses_reg (ip,
				((prev_insn.insn_opcode >> OP_SH_RT)
				 & OP_MASK_RT),
				0))
	    ++nops;
	}
      else if ((prev_pinfo & INSN_COPROC_MOVE_DELAY)
	       || (mips_isa < 2
		   && (prev_pinfo & INSN_COPROC_MEMORY_DELAY)))
	{
	  /* A generic coprocessor delay.  The previous instruction
	     modified a coprocessor general or control register.  If
	     it modified a control register, we need to avoid any
	     coprocessor instruction (this is probably not always
	     required, but it sometimes is).  If it modified a general
	     register, we avoid using that register.

	     On the r6000 and r4000 loading a coprocessor register
	     from memory is interlocked, and does not require a delay.

	     This case is not handled very well.  There is no special
	     knowledge of CP0 handling, and the coprocessors other
	     than the floating point unit are not distinguished at
	     all.  */
	  if (prev_pinfo & INSN_WRITE_FPR_T)
	    {
	      if (mips_optimize == 0
		  || insn_uses_reg (ip,
				    ((prev_insn.insn_opcode >> OP_SH_FT)
				     & OP_MASK_FT),
				    1))
		++nops;
	    }
	  else if (prev_pinfo & INSN_WRITE_FPR_S)
	    {
	      if (mips_optimize == 0
		  || insn_uses_reg (ip,
				    ((prev_insn.insn_opcode >> OP_SH_FS)
				     & OP_MASK_FS),
				    1))
		++nops;
	    }
	  else
	    {
	      /* We don't know exactly what the previous instruction
		 does.  If the current instruction uses a coprocessor
		 register, we must insert a NOP.  If previous
		 instruction may set the condition codes, and the
		 current instruction uses them, we must insert two
		 NOPS.  */
	      if (mips_optimize == 0
		  || ((prev_pinfo & INSN_WRITE_COND_CODE)
		      && (pinfo & INSN_READ_COND_CODE)))
		nops += 2;
	      else if (pinfo & INSN_COP)
		++nops;
	    }
	}
      else if (prev_pinfo & INSN_WRITE_COND_CODE)
	{
	  /* The previous instruction sets the coprocessor condition
	     codes, but does not require a general coprocessor delay
	     (this means it is a floating point comparison
	     instruction).  If this instruction uses the condition
	     codes, we need to insert a single NOP.  */
	  if (mips_optimize == 0
	      || (pinfo & INSN_READ_COND_CODE))
	    ++nops;
	}
      else if (prev_pinfo & INSN_READ_LO)
	{
	  /* The previous instruction reads the LO register; if the
	     current instruction writes to the LO register, we must
	     insert two NOPS.  */
	  if (mips_optimize == 0
	      || (pinfo & INSN_WRITE_LO))
	    nops += 2;
	}
      else if (prev_insn.insn_mo->pinfo & INSN_READ_HI)
	{
	  /* The previous instruction reads the HI register; if the
	     current instruction writes to the HI register, we must
	     insert a NOP.  */
	  if (mips_optimize == 0
	      || (pinfo & INSN_WRITE_HI))
	    nops += 2;
	}

      /* There are two cases which require two intervening
	 instructions: 1) setting the condition codes using a move to
	 coprocessor instruction which requires a general coprocessor
	 delay and then reading the condition codes 2) reading the HI
	 or LO register and then writing to it.  If we are not already
	 emitting a NOP instruction, we must check for these cases
	 compared to the instruction previous to the previous
	 instruction.  */
      if (nops == 0
	  && (((prev_prev_insn.insn_mo->pinfo & INSN_COPROC_MOVE_DELAY)
	       && (prev_prev_insn.insn_mo->pinfo & INSN_WRITE_COND_CODE)
	       && (pinfo & INSN_READ_COND_CODE))
	      || ((prev_prev_insn.insn_mo->pinfo & INSN_READ_LO)
		  && (pinfo & INSN_WRITE_LO))
	      || ((prev_prev_insn.insn_mo->pinfo & INSN_READ_HI)
		  && (pinfo & INSN_WRITE_HI))))
	++nops;

      /* If we are being given a nop instruction, don't bother with
	 one of the nops we would otherwise output.  This will only
	 happen when a nop instruction is used with mips_optimize set
	 to 0.  */
      if (nops > 0 && ip->insn_opcode == 0)
	--nops;

      /* Now emit the right number of NOP instructions.  */
      if (nops > 0)
	{
	  emit_nop ();
	  if (nops > 1)
	    emit_nop ();
	  if (insn_label != NULL)
	    {
	      assert (S_GET_SEGMENT (insn_label) == now_seg);
	      insn_label->sy_frag = frag_now;
	      S_SET_VALUE (insn_label, (valueT) frag_now_fix ());
	    }
	}
    }
  
  if (place == NULL)
    f = frag_more (4);
  else
    f = place;
  fixp = NULL;
  if (address_expr != NULL)
    {
      if (address_expr->X_op == O_constant)
	{
	  switch (reloc_type)
	    {
	    case BFD_RELOC_32:
	      ip->insn_opcode |= address_expr->X_add_number;
	      break;

	    case BFD_RELOC_LO16:
	      ip->insn_opcode |= address_expr->X_add_number & 0xffff;
	      break;

	    case BFD_RELOC_MIPS_JMP:
	    case BFD_RELOC_16_PCREL_S2:
	      goto need_reloc;

	    default:
	      internalError ();
	    }
	}
      else
	{
	  assert (reloc_type != BFD_RELOC_UNUSED);
	need_reloc:
	  /* Don't generate a reloc if we are writing into a variant
	     frag.  */
	  if (place == NULL)
	    fixp = fix_new_exp (frag_now, f - frag_now->fr_literal, 4,
				address_expr,
				reloc_type == BFD_RELOC_16_PCREL_S2,
				reloc_type);
	}
    }

  md_number_to_chars (f, ip->insn_opcode, 4);

  /* Update the register mask information.  */
  if (pinfo & INSN_WRITE_GPR_D)
    mips_gprmask |= 1 << ((ip->insn_opcode >> OP_SH_RD) & OP_MASK_RD);
  if ((pinfo & (INSN_WRITE_GPR_T | INSN_READ_GPR_T)) != 0)
    mips_gprmask |= 1 << ((ip->insn_opcode >> OP_SH_RT) & OP_MASK_RT);
  if (pinfo & INSN_READ_GPR_S)
    mips_gprmask |= 1 << ((ip->insn_opcode >> OP_SH_RS) & OP_MASK_RS);
  if (pinfo & INSN_WRITE_GPR_31)
    mips_gprmask |= 1 << 31;
  if (pinfo & INSN_WRITE_FPR_D)
    mips_cprmask[1] |= 1 << ((ip->insn_opcode >> OP_SH_FD) & OP_MASK_FD);
  if ((pinfo & (INSN_WRITE_FPR_S | INSN_READ_FPR_S)) != 0)
    mips_cprmask[1] |= 1 << ((ip->insn_opcode >> OP_SH_FS) & OP_MASK_FS);
  if ((pinfo & (INSN_WRITE_FPR_T | INSN_READ_FPR_T)) != 0)
    mips_cprmask[1] |= 1 << ((ip->insn_opcode >> OP_SH_FT) & OP_MASK_FT);
  if (pinfo & INSN_COP)
    {
      /* We don't keep enough information to sort these cases out.  */
    }
  /* Never set the bit for $0, which is always zero.  */
  mips_gprmask &=~ 1 << 0;

  if (place == NULL && ! mips_noreorder)
    {
      /* Filling the branch delay slot is more complex.  We try to
	 switch the branch with the previous instruction, which we can
	 do if the previous instruction does not set up a condition
	 that the branch tests and if the branch is not itself the
	 target of any branch.  */
      if ((pinfo & INSN_UNCOND_BRANCH_DELAY)
	  || (pinfo & INSN_COND_BRANCH_DELAY))
	{
	  if (mips_optimize < 2
	      /* If we have seen .set nobopt, don't optimize.  */
	      || mips_nobopt != 0
	      /* If we have seen .set volatile or .set nomove, don't
		 optimize.  */
	      || mips_nomove != 0
	      /* If we had to emit any NOP instructions, then we
		 already know we can not swap.  */
	      || nops != 0
	      /* If we don't even know the previous insn, we can not
		 swap. */
	      || ! prev_insn_valid
	      /* If the previous insn is already in a branch delay
		 slot, then we can not swap.  */
	      || prev_insn_is_delay_slot
	      /* If the previous previous insn was in a .set
		 noreorder, we can't swap.  Actually, the MIPS
		 assembler will swap in this situation.  However, gcc
		 configured -with-gnu-as will generate code like
		   .set noreorder
		   lw	$4,XXX
		   .set	reorder
		   INSN
		   bne	$4,$0,foo
		 in which we can not swap the bne and INSN.  If gcc is
		 not configured -with-gnu-as, it does not output the
		 .set pseudo-ops.  We don't have to check
		 prev_insn_unreordered, because prev_insn_valid will
		 be 0 in that case.  We don't want to use
		 prev_prev_insn_valid, because we do want to be able
		 to swap at the start of a function.  */
	      || prev_prev_insn_unreordered
	      /* If the branch is itself the target of a branch, we
		 can not swap.  We cheat on this; all we check for is
		 whether there is a label on this instruction.  If
		 there are any branches to anything other than a
		 label, users must use .set noreorder.  */
	      || insn_label != NULL
	      /* If the previous instruction is in a variant frag, we
		 can not do the swap.  */
	      || prev_insn_frag->fr_type == rs_machine_dependent
	      /* If the branch reads the condition codes, we don't
		 even try to swap, because in the sequence
		   ctc1 $X,$31
		   INSN
		   INSN
		   bc1t LABEL
		 we can not swap, and I don't feel like handling that
		 case.  */
	      || (pinfo & INSN_READ_COND_CODE)
	      /* We can not swap with an instruction that requires a
		 delay slot, becase the target of the branch might
		 interfere with that instruction.  */
	      || (prev_pinfo
		  & (INSN_LOAD_COPROC_DELAY
		     | INSN_COPROC_MOVE_DELAY
		     | INSN_WRITE_COND_CODE
		     | INSN_READ_LO
		     | INSN_READ_HI))
	      || (mips_isa < 2
		  && (prev_pinfo
		      & (INSN_LOAD_MEMORY_DELAY
			 | INSN_COPROC_MEMORY_DELAY)))
	      /* We can not swap with a branch instruction.  */
	      || (prev_pinfo
		  & (INSN_UNCOND_BRANCH_DELAY
		     | INSN_COND_BRANCH_DELAY
		     | INSN_COND_BRANCH_LIKELY))
	      /* We do not swap with a trap instruction, since it
		 complicates trap handlers to have the trap
		 instruction be in a delay slot.  */
	      || (prev_pinfo & INSN_TRAP)
	      /* If the branch reads a register that the previous
		 instruction sets, we can not swap.  */
	      || ((prev_pinfo & INSN_WRITE_GPR_T)
		  && insn_uses_reg (ip,
				    ((prev_insn.insn_opcode >> OP_SH_RT)
				     & OP_MASK_RT),
				    0))
	      || ((prev_pinfo & INSN_WRITE_GPR_D)
		  && insn_uses_reg (ip,
				    ((prev_insn.insn_opcode >> OP_SH_RD)
				     & OP_MASK_RD),
				    0))
	      /* If the branch writes a register that the previous
		 instruction sets, we can not swap (we know that
		 branches write only to RD or to $31).  */
	      || ((prev_pinfo & INSN_WRITE_GPR_T)
		  && (((pinfo & INSN_WRITE_GPR_D)
		       && (((prev_insn.insn_opcode >> OP_SH_RT) & OP_MASK_RT)
			   == ((ip->insn_opcode >> OP_SH_RD) & OP_MASK_RD)))
		      || ((pinfo & INSN_WRITE_GPR_31)
			  && (((prev_insn.insn_opcode >> OP_SH_RT)
			       & OP_MASK_RT)
			      == 31))))
	      || ((prev_pinfo & INSN_WRITE_GPR_D)
		  && (((pinfo & INSN_WRITE_GPR_D)
		       && (((prev_insn.insn_opcode >> OP_SH_RD) & OP_MASK_RD)
			   == ((ip->insn_opcode >> OP_SH_RD) & OP_MASK_RD)))
		      || ((pinfo & INSN_WRITE_GPR_31)
			  && (((prev_insn.insn_opcode >> OP_SH_RD)
			       & OP_MASK_RD)
			      == 31))))
	      /* If the branch writes a register that the previous
		 instruction reads, we can not swap (we know that
		 branches only write to RD or to $31).  */
	      || ((pinfo & INSN_WRITE_GPR_D)
		  && insn_uses_reg (&prev_insn,
				    ((ip->insn_opcode >> OP_SH_RD)
				     & OP_MASK_RD),
				    0))
	      || ((pinfo & INSN_WRITE_GPR_31)
		  && insn_uses_reg (&prev_insn, 31, 0))
	      /* If the previous previous instruction has a load
		 delay, and sets a register that the branch reads, we
		 can not swap.  */
	      || (((prev_prev_insn.insn_mo->pinfo & INSN_LOAD_COPROC_DELAY)
		   || (mips_isa < 2
		       && (prev_prev_insn.insn_mo->pinfo
			   & INSN_LOAD_MEMORY_DELAY)))
		  && insn_uses_reg (ip,
				    ((prev_prev_insn.insn_opcode >> OP_SH_RT)
				     & OP_MASK_RT),
				    0)))
	    {
	      /* We could do even better for unconditional branches to
		 portions of this object file; we could pick up the
		 instruction at the destination, put it in the delay
		 slot, and bump the destination address.  */
	      emit_nop ();
	      /* Update the previous insn information.  */
	      prev_prev_insn = *ip;
	      prev_insn.insn_mo = &dummy_opcode;
	    }
	  else
	    {
	      char *prev_f;
	      char temp[4];

	      /* It looks like we can actually do the swap.  */
	      prev_f = prev_insn_frag->fr_literal + prev_insn_where;
	      memcpy (temp, prev_f, 4);
	      memcpy (prev_f, f, 4);
	      memcpy (f, temp, 4);
	      if (prev_insn_fixp)
		{
		  prev_insn_fixp->fx_frag = frag_now;
		  prev_insn_fixp->fx_where = f - frag_now->fr_literal;
		}
	      if (fixp)
		{
		  fixp->fx_frag = prev_insn_frag;
		  fixp->fx_where = prev_insn_where;
		}
	      /* Update the previous insn information; leave prev_insn
		 unchanged.  */
	      prev_prev_insn = *ip;
	    }
	  prev_insn_is_delay_slot = 1;

	  /* If that was an unconditional branch, forget the previous
	     insn information.  */
	  if (pinfo & INSN_UNCOND_BRANCH_DELAY)
	    {
	      prev_prev_insn.insn_mo = &dummy_opcode;
	      prev_insn.insn_mo = &dummy_opcode;
	    }
	}
      else if (pinfo & INSN_COND_BRANCH_LIKELY)
	{
	  /* We don't yet optimize a branch likely.  What we should do
	     is look at the target, copy the instruction found there
	     into the delay slot, and increment the branch to jump to
	     the next instruction.  */
	  emit_nop ();
	  /* Update the previous insn information.  */
	  prev_prev_insn = *ip;
	  prev_insn.insn_mo = &dummy_opcode;
	}
      else
	{
	  /* Update the previous insn information.  */
	  if (nops > 0)
	    prev_prev_insn.insn_mo = &dummy_opcode;
	  else
	    prev_prev_insn = prev_insn;
	  prev_insn = *ip;

	  /* Any time we see a branch, we always fill the delay slot
	     immediately; since this insn is not a branch, we know it
	     is not in a delay slot.  */
	  prev_insn_is_delay_slot = 0;
	}

      prev_prev_insn_unreordered = prev_insn_unreordered;
      prev_insn_unreordered = 0;
      prev_insn_frag = frag_now;
      prev_insn_where = f - frag_now->fr_literal;
      prev_insn_fixp = fixp;
      prev_insn_valid = 1;
    }

  /* We just output an insn, so the next one doesn't have a label.  */
  insn_label = NULL;
}

/* This function forgets that there was any previous instruction or
   label.  */

static void
mips_no_prev_insn ()
{
  prev_insn.insn_mo = &dummy_opcode;
  prev_prev_insn.insn_mo = &dummy_opcode;
  prev_insn_valid = 0;
  prev_insn_is_delay_slot = 0;
  prev_insn_unreordered = 0;
  prev_prev_insn_unreordered = 0;
  insn_label = NULL;
}

/* This function must be called whenever we turn on noreorder or emit
   something other than instructions.  It inserts any NOPS which might
   be needed by the previous instruction, and clears the information
   kept for the previous instructions.  */

static void
mips_emit_delays ()
{
  if (! mips_noreorder)
    {
      int nop;

      nop = 0;
      if ((prev_insn.insn_mo->pinfo
	   & (INSN_LOAD_COPROC_DELAY
	      | INSN_COPROC_MOVE_DELAY
	      | INSN_WRITE_COND_CODE
	      | INSN_READ_LO
	      | INSN_READ_HI))
	  || (mips_isa < 2
	      && (prev_insn.insn_mo->pinfo
		  & (INSN_LOAD_MEMORY_DELAY
		     | INSN_COPROC_MEMORY_DELAY))))
	{
	  nop = 1;
	  if ((prev_insn.insn_mo->pinfo & INSN_WRITE_COND_CODE)
	      || (prev_insn.insn_mo->pinfo & INSN_READ_HI)
	      || (prev_insn.insn_mo->pinfo & INSN_READ_LO))
	    emit_nop ();
	}
      else if ((prev_prev_insn.insn_mo->pinfo & INSN_WRITE_COND_CODE)
	       || (prev_prev_insn.insn_mo->pinfo & INSN_READ_HI)
	       || (prev_prev_insn.insn_mo->pinfo & INSN_READ_LO))
	nop = 1;
      if (nop)
	{
	  emit_nop ();
	  if (insn_label != NULL)
	    {
	      assert (S_GET_SEGMENT (insn_label) == now_seg);
	      insn_label->sy_frag = frag_now;
	      S_SET_VALUE (insn_label, (valueT) frag_now_fix ());
	    }
	}
      mips_no_prev_insn ();
    }
}

/* Build an instruction created by a macro expansion.  This is passed
   a pointer to the count of instructions created so far, an
   expression, the name of the instruction to build, an operand format
   string, and corresponding arguments.  */

#ifndef NO_STDARG
static void
macro_build (char *place,
	     int *counter,
	     expressionS * ep,
	     const char *name,
	     const char *fmt,
	     ...)
#else /* ! defined (NO_STDARG) */
static void
macro_build (place, counter, ep, name, fmt, va_alist)
     char *place;
     int *counter;
     expressionS *ep;
     const char *name;
     const char *fmt;
     va_dcl
#endif /* ! defined (NO_STDARG) */
{
  struct mips_cl_insn insn;
  bfd_reloc_code_real_type r;
  va_list args;

#ifndef NO_STDARG
  va_start (args, fmt);
#else
  va_start (args);
#endif

  /*
   * If the macro is about to expand into a second instruction,
   * print a warning if needed. We need to pass ip as a parameter
   * to generate a better warning message here...
   */
  if (mips_warn_about_macros && place == NULL && *counter == 1)
    as_warn ("Macro instruction expanded into multiple instructions");

  if (place == NULL)
    *counter += 1;		/* bump instruction counter */

  r = BFD_RELOC_UNUSED;
  insn.insn_mo = (struct mips_opcode *) hash_find (op_hash, name);
  assert (insn.insn_mo);
  assert (strcmp (name, insn.insn_mo->name) == 0);

  while (strcmp (fmt, insn.insn_mo->args) != 0
	 || insn.insn_mo->pinfo == INSN_MACRO)
    {
      ++insn.insn_mo;
      assert (insn.insn_mo->name);
      assert (strcmp (name, insn.insn_mo->name) == 0);
    }
  insn.insn_opcode = insn.insn_mo->match;
  for (;;)
    {
      switch (*fmt++)
	{
	case '\0':
	  break;

	case ',':
	case '(':
	case ')':
	  continue;

	case 't':
	case 'w':
	case 'E':
	  insn.insn_opcode |= va_arg (args, int) << 16;
	  continue;

	case 'c':
	case 'T':
	case 'W':
	  insn.insn_opcode |= va_arg (args, int) << 16;
	  continue;

	case 'd':
	case 'G':
	  insn.insn_opcode |= va_arg (args, int) << 11;
	  continue;

	case 'V':
	case 'S':
	  insn.insn_opcode |= va_arg (args, int) << 11;
	  continue;

	case 'z':
	  continue;

	case '<':
	  insn.insn_opcode |= va_arg (args, int) << 6;
	  continue;

	case 'D':
	  insn.insn_opcode |= va_arg (args, int) << 6;
	  continue;

	case 'B':
	  insn.insn_opcode |= va_arg (args, int) << 6;
	  continue;

	case 'b':
	case 's':
	case 'r':
	case 'v':
	  insn.insn_opcode |= va_arg (args, int) << 21;
	  continue;

	case 'i':
	case 'j':
	case 'o':
	  r = (bfd_reloc_code_real_type) va_arg (args, int);
	  assert (r == BFD_RELOC_MIPS_GPREL
		  || r == BFD_RELOC_MIPS_LITERAL
		  || r == BFD_RELOC_LO16
		  || r == BFD_RELOC_MIPS_GOT16
		  || r== BFD_RELOC_MIPS_CALL16);
	  continue;

	case 'u':
	  assert (ep != NULL && ep->X_op == O_constant);
	  insn.insn_opcode |= (ep->X_add_number >> 16) & 0xffff;
	  ep = NULL;
	  continue;

	case 'p':
	  assert (ep != NULL);
	  /*
	   * This allows macro() to pass an immediate expression for
	   * creating short branches without creating a symbol.
	   * Note that the expression still might come from the assembly
	   * input, in which case the value is not checked for range nor
	   * is a relocation entry generated (yuck).
	   */
	  if (ep->X_op == O_constant)
	    {
	      insn.insn_opcode |= (ep->X_add_number >> 2) & 0xffff;
	      ep = NULL;
	    }
	  else
	    r = BFD_RELOC_16_PCREL_S2;
	  continue;

	case 'a':
	  assert (ep != NULL);
	  r = BFD_RELOC_MIPS_JMP;
	  continue;

	default:
	  internalError ();
	}
      break;
    }
  va_end (args);
  assert (r == BFD_RELOC_UNUSED ? ep == NULL : ep != NULL);

  append_insn (place, &insn, ep, r);
}

/*
 * Generate a "lui" instruction.
 */
static void
macro_build_lui (place, counter, ep, regnum)
     char *place;
     int *counter;
     expressionS *ep;
     int regnum;
{
  expressionS high_expr;
  struct mips_cl_insn insn;
  bfd_reloc_code_real_type r;
  CONST char *name = "lui";
  CONST char *fmt = "t,u";

  if (place == NULL)
    high_expr = *ep;
  else
    {
      high_expr.X_op = O_constant;
      high_expr.X_add_number = 0;
    }

  if (high_expr.X_op == O_constant)
    {
      /* we can compute the instruction now without a relocation entry */
      if (high_expr.X_add_number & 0x8000)
	high_expr.X_add_number += 0x10000;
      high_expr.X_add_number =
	((unsigned long) high_expr.X_add_number >> 16) & 0xffff;
      r = BFD_RELOC_UNUSED;
    }
  else
    {
      assert (ep->X_op == O_symbol);
      /* _gp_disp is a special case, used from s_cpload.  */
      assert (mips_pic == 0
	      || strcmp (S_GET_NAME (ep->X_add_symbol), "_gp_disp") == 0);
      r = BFD_RELOC_HI16_S;
    }

  /*
   * If the macro is about to expand into a second instruction,
   * print a warning if needed. We need to pass ip as a parameter
   * to generate a better warning message here...
   */
  if (mips_warn_about_macros && place == NULL && *counter == 1)
    as_warn ("Macro instruction expanded into multiple instructions");

  if (place == NULL)
    *counter += 1;		/* bump instruction counter */

  insn.insn_mo = (struct mips_opcode *) hash_find (op_hash, name);
  assert (insn.insn_mo);
  assert (strcmp (name, insn.insn_mo->name) == 0);
  assert (strcmp (fmt, insn.insn_mo->args) == 0);

  insn.insn_opcode = insn.insn_mo->match | (regnum << OP_SH_RT);
  if (r == BFD_RELOC_UNUSED)
    {
      insn.insn_opcode |= high_expr.X_add_number;
      append_insn (place, &insn, NULL, r);
    }
  else
    append_insn (place, &insn, &high_expr, r);
}

/*			set_at()
 * Generates code to set the $at register to true (one)
 * if reg is less than the immediate expression.
 */
static void
set_at (counter, reg, unsignedp)
     int *counter;
     int reg;
     int unsignedp;
{
  if (imm_expr.X_add_number >= -0x8000 && imm_expr.X_add_number < 0x8000)
    macro_build ((char *) NULL, counter, &imm_expr,
		 unsignedp ? "sltiu" : "slti",
		 "t,r,j", AT, reg, (int) BFD_RELOC_LO16);
  else
    {
      load_register (counter, AT, &imm_expr);
      macro_build ((char *) NULL, counter, NULL,
		   unsignedp ? "sltu" : "slt",
		   "d,v,t", AT, reg, AT);
    }
}

/* Warn if an expression is not a constant.  */

static void
check_absolute_expr (ip, ex)
     struct mips_cl_insn *ip;
     expressionS *ex;
{
  if (ex->X_op != O_constant)
    as_warn ("Instruction %s requires absolute expression", ip->insn_mo->name);
}

/*			load_register()
 *  This routine generates the least number of instructions neccessary to load
 *  an absolute expression value into a register.
 */
static void
load_register (counter, reg, ep)
     int *counter;
     int reg;
     expressionS *ep;
{
  assert (ep->X_op == O_constant);
  if (ep->X_add_number >= -0x8000 && ep->X_add_number < 0x8000)
    macro_build ((char *) NULL, counter, ep,
		 mips_isa < 3 ? "addiu" : "daddiu",
		 "t,r,j", reg, 0, (int) BFD_RELOC_LO16);
  else if (ep->X_add_number >= 0 && ep->X_add_number < 0x10000)
    macro_build ((char *) NULL, counter, ep, "ori", "t,r,i", reg, 0,
		 (int) BFD_RELOC_LO16);
  else if ((ep->X_add_number &~ (offsetT) 0x7fffffff) == 0
	   || ((ep->X_add_number &~ (offsetT) 0x7fffffff)
	       == ~ (offsetT) 0x7fffffff))
    {
      macro_build ((char *) NULL, counter, ep, "lui", "t,u", reg);
      if ((ep->X_add_number & 0xffff) != 0)
	macro_build ((char *) NULL, counter, ep, "ori", "t,r,i", reg, reg,
		     (int) BFD_RELOC_LO16);
    }
  else if (mips_isa < 3)
    {
      as_bad ("Number larger than 32 bits");
      macro_build ((char *) NULL, counter, ep, "addiu", "t,r,j", reg, 0,
		   (int) BFD_RELOC_LO16);
    }
  else
    {
      int shift;
      expressionS hi32, lo32;

      hi32 = *ep;
      shift = 32;
      hi32.X_add_number >>= shift;
      hi32.X_add_number &= 0xffffffff;
      if ((hi32.X_add_number & 0x80000000) != 0)
	hi32.X_add_number |= ~ (offsetT) 0xffffffff;
      load_register (counter, reg, &hi32);
      lo32 = *ep;
      lo32.X_add_number &= 0xffffffff;
      if ((lo32.X_add_number & 0xffff0000) == 0)
	macro_build ((char *) NULL, counter, NULL, "dsll32", "d,w,<", reg,
		     reg, 0);
      else
	{
	  expressionS mid16;

	  macro_build ((char *) NULL, counter, NULL, "dsll", "d,w,<", reg,
		       reg, 16);
	  mid16 = lo32;
	  mid16.X_add_number >>= 16;
	  macro_build ((char *) NULL, counter, &mid16, "ori", "t,r,i", reg,
		       reg, (int) BFD_RELOC_LO16);
	  macro_build ((char *) NULL, counter, NULL, "dsll", "d,w,<", reg,
		       reg, 16);
	}
      if ((lo32.X_add_number & 0xffff) != 0)
	macro_build ((char *) NULL, counter, &lo32, "ori", "t,r,i", reg, reg,
		     (int) BFD_RELOC_LO16);
    }
}

/* Load an address into a register.  */

static void
load_address (counter, reg, ep)
     int *counter;
     int reg;
     expressionS *ep;
{
  char *p;

  if (ep->X_op != O_constant
      && ep->X_op != O_symbol)
    {
      as_bad ("expression too complex");
      ep->X_op = O_constant;
    }

  if (ep->X_op == O_constant)
    load_register (counter, reg, ep);
  else if (mips_pic == 0)
    {
      /* If this is a reference to a GP relative symbol, we want
	   addiu	$reg,$gp,<sym>		(BFD_RELOC_MIPS_GPREL)
	 Otherwise we want
	   lui		$reg,$gp,<sym>		(BFD_RELOC_HI16_S)
	   addiu	$reg,$reg,<sym>		(BFD_RELOC_LO16)
	 If we have an addend, we always use the latter form.  */
      if (ep->X_add_number != 0)
	p = NULL;
      else
	{
	  frag_grow (12);
	  macro_build ((char *) NULL, counter, ep,
		       mips_isa < 3 ? "addiu" : "daddiu",
		       "t,r,j", reg, GP, (int) BFD_RELOC_MIPS_GPREL);
	  p = frag_var (rs_machine_dependent, 8, 0,
			RELAX_ENCODE (4, 8, -4, 0, 0, mips_warn_about_macros),
			ep->X_add_symbol, (long) 0, (char *) NULL);
	}
      macro_build_lui (p, counter, ep, reg);
      if (p != NULL)
	p += 4;
      macro_build (p, counter, ep,
		   mips_isa < 3 ? "addiu" : "daddiu",
		   "t,r,j", reg, reg, (int) BFD_RELOC_LO16);
    }
  else
    {
      expressionS ex;

      /* If this is a reference to an external symbol, we want
	   lw		$reg,<sym>($gp)		(BFD_RELOC_MIPS_GOT16)
	 Otherwise we want
	   lw		$reg,<sym>($gp)		(BFD_RELOC_MIPS_GOT16)
	   nop
	   addiu	$reg,$reg,<sym>		(BFD_RELOC_LO16)
	 If there is a constant, it must be added in afterward.  */
      ex.X_add_number = ep->X_add_number;
      ep->X_add_number = 0;
      frag_grow (12);
      macro_build ((char *) NULL, counter, ep,
		   mips_isa < 3 ? "lw" : "ld",
		   "t,o(b)", reg, (int) BFD_RELOC_MIPS_GOT16, GP);
      macro_build ((char *) NULL, counter, (expressionS *) NULL, "nop", "");
      p = frag_var (rs_machine_dependent, 4, 0,
		    RELAX_ENCODE (0, 4, -8, 0, 0, mips_warn_about_macros),
		    ep->X_add_symbol, (long) 0, (char *) NULL);
      macro_build (p, counter, ep,
		   mips_isa < 3 ? "addiu" : "daddiu",
		   "t,r,j", reg, reg, (int) BFD_RELOC_LO16);
      if (ex.X_add_number != 0)
	{
	  if (ex.X_add_number < -0x8000 || ex.X_add_number >= 0x8000)
	    as_bad ("PIC code offset overflow (max 16 signed bits)");
	  ex.X_op = O_constant;
	  macro_build (p, counter, &ex,
		       mips_isa < 3 ? "addiu" : "daddiu",
		       "t,r,j", reg, reg, (int) BFD_RELOC_LO16);
	}
    }		       
}

/*
 *			Build macros
 *   This routine implements the seemingly endless macro or synthesized
 * instructions and addressing modes in the mips assembly language. Many
 * of these macros are simple and are similar to each other. These could
 * probably be handled by some kind of table or grammer aproach instead of
 * this verbose method. Others are not simple macros but are more like
 * optimizing code generation.
 *   One interesting optimization is when several store macros appear
 * consecutivly that would load AT with the upper half of the same address.
 * The ensuing load upper instructions are ommited. This implies some kind
 * of global optimization. We currently only optimize within a single macro.
 *   For many of the load and store macros if the address is specified as a
 * constant expression in the first 64k of memory (ie ld $2,0x4000c) we
 * first load register 'at' with zero and use it as the base register. The
 * mips assembler simply uses register $zero. Just one tiny optimization
 * we're missing.
 */
static void
macro (ip)
     struct mips_cl_insn *ip;
{
  register int treg, sreg, dreg, breg;
  int tempreg;
  int mask;
  int icnt = 0;
  int used_at;
  expressionS expr1;
  const char *s;
  const char *s2;
  const char *fmt;
  int likely = 0;
  int dbl = 0;
  int coproc = 0;
  offsetT maxnum;
  bfd_reloc_code_real_type r;
  char *p;

  treg = (ip->insn_opcode >> 16) & 0x1f;
  dreg = (ip->insn_opcode >> 11) & 0x1f;
  sreg = breg = (ip->insn_opcode >> 21) & 0x1f;
  mask = ip->insn_mo->mask;

  expr1.X_op = O_constant;
  expr1.X_op_symbol = NULL;
  expr1.X_add_symbol = NULL;
  expr1.X_add_number = 1;

  switch (mask)
    {
    case M_DABS:
      dbl = 1;
    case M_ABS:
      /* bgez $a0,.+12
	 move v0,$a0
	 sub v0,$zero,$a0
	 */

      mips_emit_delays ();
      ++mips_noreorder;
      mips_any_noreorder = 1;

      expr1.X_add_number = 8;
      macro_build ((char *) NULL, &icnt, &expr1, "bgez", "s,p", sreg);
      if (dreg == sreg)
	macro_build ((char *) NULL, &icnt, NULL, "nop", "", 0);
      else
	macro_build ((char *) NULL, &icnt, NULL, "move", "d,s", dreg, sreg, 0);
      macro_build ((char *) NULL, &icnt, NULL,
		   dbl ? "dsub" : "sub",
		   "d,v,t", dreg, 0, sreg);

      --mips_noreorder;
      return;

    case M_ADD_I:
      s = "addi";
      s2 = "add";
      goto do_addi;
    case M_ADDU_I:
      s = "addiu";
      s2 = "addu";
      goto do_addi;
    case M_DADD_I:
      dbl = 1;
      s = "daddi";
      s2 = "dadd";
      goto do_addi;
    case M_DADDU_I:
      dbl = 1;
      s = "daddiu";
      s2 = "daddu";
    do_addi:
      if (imm_expr.X_add_number >= -0x8000 && imm_expr.X_add_number < 0x8000)
	{
	  macro_build ((char *) NULL, &icnt, &imm_expr, s, "t,r,j", treg, sreg,
		       (int) BFD_RELOC_LO16);
	  return;
	}
      load_register (&icnt, AT, &imm_expr);
      macro_build ((char *) NULL, &icnt, NULL, s2, "d,v,t", treg, sreg, AT);
      break;

    case M_AND_I:
      s = "andi";
      s2 = "and";
      goto do_bit;
    case M_OR_I:
      s = "ori";
      s2 = "or";
      goto do_bit;
    case M_NOR_I:
      s = "";
      s2 = "nor";
      goto do_bit;
    case M_XOR_I:
      s = "xori";
      s2 = "xor";
    do_bit:
      if (imm_expr.X_add_number >= 0 && imm_expr.X_add_number < 0x10000)
	{
	  if (mask != M_NOR_I)
	    macro_build ((char *) NULL, &icnt, &imm_expr, s, "t,r,i", treg,
			 sreg, (int) BFD_RELOC_LO16);
	  else
	    {
	      macro_build ((char *) NULL, &icnt, &imm_expr, "ori", "t,r,i",
			   treg, sreg, (int) BFD_RELOC_LO16);
	      macro_build ((char *) NULL, &icnt, &imm_expr, "nor", "d,v,t",
			   treg, treg, 0);
	    }
	  return;
	}

      load_register (&icnt, AT, &imm_expr);
      macro_build ((char *) NULL, &icnt, NULL, s2, "d,v,t", treg, sreg, AT);
      break;

    case M_BEQ_I:
      s = "beq";
      goto beq_i;
    case M_BEQL_I:
      s = "beql";
      likely = 1;
      goto beq_i;
    case M_BNE_I:
      s = "bne";
      goto beq_i;
    case M_BNEL_I:
      s = "bnel";
      likely = 1;
    beq_i:
      if (imm_expr.X_add_number == 0)
	{
	  macro_build ((char *) NULL, &icnt, &offset_expr, s, "s,t,p", sreg,
		       0);
	  return;
	}
      load_register (&icnt, AT, &imm_expr);
      macro_build ((char *) NULL, &icnt, &offset_expr, s, "s,t,p", sreg, AT);
      break;

    case M_BGEL:
      likely = 1;
    case M_BGE:
      if (treg == 0)
	{
	  macro_build ((char *) NULL, &icnt, &offset_expr,
		       likely ? "bgezl" : "bgez",
		       "s,p", sreg);
	  return;
	}
      if (sreg == 0)
	{
	  macro_build ((char *) NULL, &icnt, &offset_expr,
		       likely ? "blezl" : "blez",
		       "s,p", treg);
	  return;
	}
      macro_build ((char *) NULL, &icnt, NULL, "slt", "d,v,t", AT, sreg, treg);
      macro_build ((char *) NULL, &icnt, &offset_expr,
		   likely ? "beql" : "beq",
		   "s,t,p", AT, 0);
      break;

    case M_BGTL_I:
      likely = 1;
    case M_BGT_I:
      /* check for > max integer */
      maxnum = 0x7fffffff;
      if (mips_isa >= 3)
	{
	  maxnum <<= 16;
	  maxnum |= 0xffff;
	  maxnum <<= 16;
	  maxnum |= 0xffff;
	}
      if (imm_expr.X_add_number >= maxnum)
	{
	do_false:
	  /* result is always false */
	  if (! likely)
	    {
	      as_warn ("Branch %s is always false (nop)", ip->insn_mo->name);
	      macro_build ((char *) NULL, &icnt, NULL, "nop", "", 0);
	    }
	  else
	    {
	      as_warn ("Branch likely %s is always false", ip->insn_mo->name);
	      macro_build ((char *) NULL, &icnt, &offset_expr, "bnel",
			   "s,t,p", 0, 0);
	    }
	  return;
	}
      imm_expr.X_add_number++;
      /* FALLTHROUGH */
    case M_BGE_I:
    case M_BGEL_I:
      if (mask == M_BGEL_I)
	likely = 1;
      if (imm_expr.X_add_number == 0)
	{
	  macro_build ((char *) NULL, &icnt, &offset_expr,
		       likely ? "bgezl" : "bgez",
		       "s,p", sreg);
	  return;
	}
      if (imm_expr.X_add_number == 1)
	{
	  macro_build ((char *) NULL, &icnt, &offset_expr,
		       likely ? "bgtzl" : "bgtz",
		       "s,p", sreg);
	  return;
	}
      maxnum = 0x7fffffff;
      if (mips_isa >= 3)
	{
	  maxnum <<= 16;
	  maxnum |= 0xffff;
	  maxnum <<= 16;
	  maxnum |= 0xffff;
	}
      maxnum = - maxnum - 1;
      if (imm_expr.X_add_number <= maxnum)
	{
	do_true:
	  /* result is always true */
	  as_warn ("Branch %s is always true", ip->insn_mo->name);
	  macro_build ((char *) NULL, &icnt, &offset_expr, "b", "p");
	  return;
	}
      set_at (&icnt, sreg, 0);
      macro_build ((char *) NULL, &icnt, &offset_expr,
		   likely ? "beql" : "beq",
		   "s,t,p", AT, 0);
      break;

    case M_BGEUL:
      likely = 1;
    case M_BGEU:
      if (treg == 0)
	goto do_true;
      if (sreg == 0)
	{
	  macro_build ((char *) NULL, &icnt, &offset_expr,
		       likely ? "beql" : "beq",
		       "s,t,p", 0, treg);
	  return;
	}
      macro_build ((char *) NULL, &icnt, NULL, "sltu", "d,v,t", AT, sreg,
		   treg);
      macro_build ((char *) NULL, &icnt, &offset_expr,
		   likely ? "beql" : "beq",
		   "s,t,p", AT, 0);
      break;

    case M_BGTUL_I:
      likely = 1;
    case M_BGTU_I:
      if (sreg == 0 || imm_expr.X_add_number == -1)
	goto do_false;
      imm_expr.X_add_number++;
      /* FALLTHROUGH */
    case M_BGEU_I:
    case M_BGEUL_I:
      if (mask == M_BGEUL_I)
	likely = 1;
      if (imm_expr.X_add_number == 0)
	goto do_true;
      if (imm_expr.X_add_number == 1)
	{
	  macro_build ((char *) NULL, &icnt, &offset_expr,
		       likely ? "bnel" : "bne",
		       "s,t,p", sreg, 0);
	  return;
	}
      set_at (&icnt, sreg, 1);
      macro_build ((char *) NULL, &icnt, &offset_expr,
		   likely ? "beql" : "beq",
		   "s,t,p", AT, 0);
      break;

    case M_BGTL:
      likely = 1;
    case M_BGT:
      if (treg == 0)
	{
	  macro_build ((char *) NULL, &icnt, &offset_expr,
		       likely ? "bgtzl" : "bgtz",
		       "s,p", sreg);
	  return;
	}
      if (sreg == 0)
	{
	  macro_build ((char *) NULL, &icnt, &offset_expr,
		       likely ? "bltzl" : "bltz",
		       "s,p", treg);
	  return;
	}
      macro_build ((char *) NULL, &icnt, NULL, "slt", "d,v,t", AT, treg, sreg);
      macro_build ((char *) NULL, &icnt, &offset_expr,
		   likely ? "bnel" : "bne",
		   "s,t,p", AT, 0);
      break;

    case M_BGTUL:
      likely = 1;
    case M_BGTU:
      if (treg == 0)
	{
	  macro_build ((char *) NULL, &icnt, &offset_expr,
		       likely ? "bnel" : "bne",
		       "s,t,p", sreg, 0);
	  return;
	}
      if (sreg == 0)
	goto do_false;
      macro_build ((char *) NULL, &icnt, NULL, "sltu", "d,v,t", AT, treg,
		   sreg);
      macro_build ((char *) NULL, &icnt, &offset_expr,
		   likely ? "bnel" : "bne",
		   "s,t,p", AT, 0);
      break;

    case M_BLEL:
      likely = 1;
    case M_BLE:
      if (treg == 0)
	{
	  macro_build ((char *) NULL, &icnt, &offset_expr,
		       likely ? "blezl" : "blez",
		       "s,p", sreg);
	  return;
	}
      if (sreg == 0)
	{
	  macro_build ((char *) NULL, &icnt, &offset_expr,
		       likely ? "bgezl" : "bgez",
		       "s,p", treg);
	  return;
	}
      macro_build ((char *) NULL, &icnt, NULL, "slt", "d,v,t", AT, treg, sreg);
      macro_build ((char *) NULL, &icnt, &offset_expr,
		   likely ? "beql" : "beq",
		   "s,t,p", AT, 0);
      break;

    case M_BLEL_I:
      likely = 1;
    case M_BLE_I:
      maxnum = 0x7fffffff;
      if (mips_isa >= 3)
	{
	  maxnum <<= 16;
	  maxnum |= 0xffff;
	  maxnum <<= 16;
	  maxnum |= 0xffff;
	}
      if (imm_expr.X_add_number >= maxnum)
	goto do_true;
      imm_expr.X_add_number++;
      /* FALLTHROUGH */
    case M_BLT_I:
    case M_BLTL_I:
      if (mask == M_BLTL_I)
	likely = 1;
      if (imm_expr.X_add_number == 0)
	{
	  macro_build ((char *) NULL, &icnt, &offset_expr,
		       likely ? "bltzl" : "bltz",
		       "s,p", sreg);
	  return;
	}
      if (imm_expr.X_add_number == 1)
	{
	  macro_build ((char *) NULL, &icnt, &offset_expr,
		       likely ? "blezl" : "blez",
		       "s,p", sreg);
	  return;
	}
      set_at (&icnt, sreg, 0);
      macro_build ((char *) NULL, &icnt, &offset_expr,
		   likely ? "bnel" : "bne",
		   "s,t,p", AT, 0);
      break;

    case M_BLEUL:
      likely = 1;
    case M_BLEU:
      if (treg == 0)
	{
	  macro_build ((char *) NULL, &icnt, &offset_expr,
		       likely ? "beql" : "beq",
		       "s,t,p", sreg, 0);
	  return;
	}
      if (sreg == 0)
	goto do_true;
      macro_build ((char *) NULL, &icnt, NULL, "sltu", "d,v,t", AT, treg,
		   sreg);
      macro_build ((char *) NULL, &icnt, &offset_expr,
		   likely ? "beql" : "beq",
		   "s,t,p", AT, 0);
      break;

    case M_BLEUL_I:
      likely = 1;
    case M_BLEU_I:
      if (sreg == 0 || imm_expr.X_add_number == -1)
	goto do_true;
      imm_expr.X_add_number++;
      /* FALLTHROUGH */
    case M_BLTU_I:
    case M_BLTUL_I:
      if (mask == M_BLTUL_I)
	likely = 1;
      if (imm_expr.X_add_number == 0)
	goto do_false;
      if (imm_expr.X_add_number == 1)
	{
	  macro_build ((char *) NULL, &icnt, &offset_expr,
		       likely ? "beql" : "beq",
		       "s,t,p", sreg, 0);
	  return;
	}
      set_at (&icnt, sreg, 1);
      macro_build ((char *) NULL, &icnt, &offset_expr,
		   likely ? "bnel" : "bne",
		   "s,t,p", AT, 0);
      break;

    case M_BLTL:
      likely = 1;
    case M_BLT:
      if (treg == 0)
	{
	  macro_build ((char *) NULL, &icnt, &offset_expr,
		       likely ? "bltzl" : "bltz",
		       "s,p", sreg);
	  return;
	}
      if (sreg == 0)
	{
	  macro_build ((char *) NULL, &icnt, &offset_expr,
		       likely ? "bgtzl" : "bgtz",
		       "s,p", treg);
	  return;
	}
      macro_build ((char *) NULL, &icnt, NULL, "slt", "d,v,t", AT, sreg, treg);
      macro_build ((char *) NULL, &icnt, &offset_expr,
		   likely ? "bnel" : "bne",
		   "s,t,p", AT, 0);
      break;

    case M_BLTUL:
      likely = 1;
    case M_BLTU:
      if (treg == 0)
	goto do_false;
      if (sreg == 0)
	{
	  macro_build ((char *) NULL, &icnt, &offset_expr,
		       likely ? "bnel" : "bne",
		       "s,t,p", 0, treg);
	  return;
	}
      macro_build ((char *) NULL, &icnt, NULL, "sltu", "d,v,t", AT, sreg,
		   treg);
      macro_build ((char *) NULL, &icnt, &offset_expr,
		   likely ? "bnel" : "bne",
		   "s,t,p", AT, 0);
      break;

    case M_DDIV_3:
      dbl = 1;
    case M_DIV_3:
      s = "mflo";
      goto do_div3;
    case M_DREM_3:
      dbl = 1;
    case M_REM_3:
      s = "mfhi";
    do_div3:
      if (treg == 0)
	{
	  as_warn ("Divide by zero.");
	  macro_build ((char *) NULL, &icnt, NULL, "break", "c", 7);
	  return;
	}

      mips_emit_delays ();
      ++mips_noreorder;
      mips_any_noreorder = 1;
      macro_build ((char *) NULL, &icnt, NULL,
		   dbl ? "ddiv" : "div",
		   "z,s,t", sreg, treg);
      expr1.X_add_number = 8;
      macro_build ((char *) NULL, &icnt, &expr1, "bne", "s,t,p", treg, 0);
      macro_build ((char *) NULL, &icnt, NULL, "nop", "", 0);
      macro_build ((char *) NULL, &icnt, NULL, "break", "c", 7);
      expr1.X_add_number = -1;
      macro_build ((char *) NULL, &icnt, &expr1,
		   dbl ? "daddiu" : "addiu",
		   "t,r,j", AT, 0, (int) BFD_RELOC_LO16);
      expr1.X_add_number = dbl ? 20 : 16;
      macro_build ((char *) NULL, &icnt, &expr1, "bne", "s,t,p", treg, AT);
      if (dbl)
	{
	  expr1.X_add_number = 1;
	  macro_build ((char *) NULL, &icnt, &expr1, "daddiu", "t,r,j", AT, 0,
		       (int) BFD_RELOC_LO16);
	  macro_build ((char *) NULL, &icnt, NULL, "dsll32", "d,w,<", AT, AT,
		       31);
	}
      else
	{
	  expr1.X_add_number = 0x80000000;
	  macro_build ((char *) NULL, &icnt, &expr1, "lui", "t,u", AT);
	}
      expr1.X_add_number = 8;
      macro_build ((char *) NULL, &icnt, &expr1, "bne", "s,t,p", sreg, AT);
      macro_build ((char *) NULL, &icnt, NULL, "nop", "", 0);
      macro_build ((char *) NULL, &icnt, NULL, "break", "c", 6);
      --mips_noreorder;
      macro_build ((char *) NULL, &icnt, NULL, s, "d", dreg);
      break;

    case M_DIV_3I:
      s = "div";
      s2 = "mflo";
      goto do_divi;
    case M_DIVU_3I:
      s = "divu";
      s2 = "mflo";
      goto do_divi;
    case M_REM_3I:
      s = "div";
      s2 = "mfhi";
      goto do_divi;
    case M_REMU_3I:
      s = "divu";
      s2 = "mfhi";
      goto do_divi;
    case M_DDIV_3I:
      dbl = 1;
      s = "ddiv";
      s2 = "mflo";
      goto do_divi;
    case M_DDIVU_3I:
      dbl = 1;
      s = "ddivu";
      s2 = "mflo";
      goto do_divi;
    case M_DREM_3I:
      dbl = 1;
      s = "ddiv";
      s2 = "mfhi";
      goto do_divi;
    case M_DREMU_3I:
      dbl = 1;
      s = "ddivu";
      s2 = "mfhi";
    do_divi:
      if (imm_expr.X_add_number == 0)
	{
	  as_warn ("Divide by zero.");
	  macro_build ((char *) NULL, &icnt, NULL, "break", "c", 7);
	  return;
	}
      if (imm_expr.X_add_number == 1)
	{
	  if (strcmp (s2, "mflo") == 0)
	    macro_build ((char *) NULL, &icnt, NULL, "move", "d,s", dreg,
			 sreg);
	  else
	    macro_build ((char *) NULL, &icnt, NULL, "move", "d,s", dreg, 0);
	  return;
	}
      if (imm_expr.X_add_number == -1
	  && s[strlen (s) - 1] != 'u')
	{
	  if (strcmp (s2, "mflo") == 0)
	    {
	      if (dbl)
		macro_build ((char *) NULL, &icnt, NULL, "dneg", "d,w", dreg,
			     sreg);
	      else
		macro_build ((char *) NULL, &icnt, NULL, "neg", "d,w", dreg,
			     sreg);
	    }
	  else
	    macro_build ((char *) NULL, &icnt, NULL, "move", "d,s", dreg, 0);
	  return;
	}

      load_register (&icnt, AT, &imm_expr);
      macro_build ((char *) NULL, &icnt, NULL, s, "z,s,t", sreg, AT);
      macro_build ((char *) NULL, &icnt, NULL, s2, "d", dreg);
      break;

    case M_DIVU_3:
      s = "divu";
      s2 = "mflo";
      goto do_divu3;
    case M_REMU_3:
      s = "divu";
      s2 = "mfhi";
      goto do_divu3;
    case M_DDIVU_3:
      s = "ddivu";
      s2 = "mflo";
      goto do_divu3;
    case M_DREMU_3:
      s = "ddivu";
      s2 = "mfhi";
    do_divu3:
      mips_emit_delays ();
      ++mips_noreorder;
      mips_any_noreorder = 1;
      macro_build ((char *) NULL, &icnt, NULL, s, "z,s,t", sreg, treg);
      expr1.X_add_number = 8;
      macro_build ((char *) NULL, &icnt, &expr1, "bne", "s,t,p", treg, 0);
      macro_build ((char *) NULL, &icnt, NULL, "nop", "", 0);
      macro_build ((char *) NULL, &icnt, NULL, "break", "c", 7);
      --mips_noreorder;
      macro_build ((char *) NULL, &icnt, NULL, s2, "d", dreg);
      return;

    case M_LA_AB:
      /* Load the address of a symbol into a register.  If M_LA_AB, we
	 then add a base register to it.  */
      if (offset_expr.X_op != O_symbol
	  && offset_expr.X_op != O_constant)
	{
	  as_bad ("expression too complex");
	  offset_expr.X_op = O_constant;
	}

      if (treg == breg)
	{
	  tempreg = AT;
	  used_at = 1;
	}
      else
	{
	  tempreg = treg;
	  used_at = 0;
	}

      if (offset_expr.X_op == O_constant)
	load_register (&icnt, tempreg, &offset_expr);
      else if (mips_pic == 0)
	{
	  /* If this is a reference to an GP relative symbol, we want
	       addiu	$tempreg,$gp,<sym>	(BFD_RELOC_MIPS_GPREL)
	     Otherwise we want
	       lui	$tempreg,<sym>		(BFD_RELOC_HI16_S)
	       addiu	$tempreg,$tempreg,<sym>	(BFD_RELOC_LO16)
	     If we have a constant, we need two instructions anyhow,
	     so we may as well always use the latter form.  */
	  if (offset_expr.X_add_number != 0)
	    p = NULL;
	  else
	    {
	      frag_grow (12);
	      macro_build ((char *) NULL, &icnt, &offset_expr,
			   mips_isa < 3 ? "addiu" : "daddiu",
			   "t,r,j", tempreg, GP, (int) BFD_RELOC_MIPS_GPREL);
	      p = frag_var (rs_machine_dependent, 8, 0,
			    RELAX_ENCODE (4, 8, 0, 4, 0,
					  mips_warn_about_macros),
			    offset_expr.X_add_symbol, (long) 0,
			    (char *) NULL);
	    }
	  macro_build_lui (p, &icnt, &offset_expr, tempreg);
	  if (p != NULL)
	    p += 4;
	  macro_build (p, &icnt, &offset_expr,
		       mips_isa < 3 ? "addiu" : "daddiu",
		       "t,r,j", tempreg, tempreg, (int) BFD_RELOC_LO16);
	}
      else
	{
	  /* If this is a reference to an external symbol, and there
	     is no constant, we want
	       lw	$tempreg,<sym>($gp)	(BFD_RELOC_MIPS_GOT16)
	     For a local symbol, we want
	       lw	$tempreg,<sym>($gp)	(BFD_RELOC_MIPS_GOT16)
	       nop
	       addiu	$tempreg,$tempreg,<sym>	(BFD_RELOC_LO16)

	     If we have a small constant, and this is a reference to
	     an external symbol, we want
	       lw	$tempreg,<sym>($gp)	(BFD_RELOC_MIPS_GOT16)
	       nop
	       addiu	$tempreg,$tempreg,<constant>
	     For a local symbol, we want the same instruction
	     sequence, but we output a BFD_RELOC_LO16 reloc on the
	     addiu instruction.

	     If we have a large constant, and this is a reference to
	     an external symbol, we want
	       lw	$tempreg,<sym>($gp)	(BFD_RELOC_MIPS_GOT16)
	       lui	$at,<hiconstant>
	       addiu	$at,$at,<loconstant>
	       addu	$tempreg,$tempreg,$at
	     For a local symbol, we want the same instruction
	     sequence, but we output a BFD_RELOC_LO16 reloc on the
	     addiu instruction.  */
	  expr1.X_add_number = offset_expr.X_add_number;
	  offset_expr.X_add_number = 0;
	  frag_grow (24);
	  macro_build ((char *) NULL, &icnt, &offset_expr,
		       mips_isa < 3 ? "lw" : "ld",
		       "t,o(b)", tempreg, (int) BFD_RELOC_MIPS_GOT16, GP);
	  if (expr1.X_add_number == 0)
	    {
	      int off;

	      if (breg == 0)
		off = 0;
	      else
		{
		  /* We're going to put in an addu instruction using
		     tempreg, so we may as well insert the nop right
		     now.  */
		  macro_build ((char *) NULL, &icnt, (expressionS *) NULL,
			       "nop", "");
		  off = 4;
		}
	      p = frag_var (rs_machine_dependent, 8 - off, 0,
			    RELAX_ENCODE (0, 8 - off, -4 - off, 4 - off, 0,
					  (breg == 0
					   ? mips_warn_about_macros
					   : 0)),
			    offset_expr.X_add_symbol, (long) 0,
			    (char *) NULL);
	      if (breg == 0)
		{
		  macro_build (p, &icnt, (expressionS *) NULL, "nop", "");
		  p += 4;
		}
	      macro_build (p, &icnt, &expr1,
			   mips_isa < 3 ? "addiu" : "daddiu",
			   "t,r,j", tempreg, tempreg, (int) BFD_RELOC_LO16);
	      /* FIXME: If breg == 0, and the next instruction uses
		 $tempreg, then if this variant case is used an extra
		 nop will be generated.  */
	    }
	  else if (expr1.X_add_number >= -0x8000
		   && expr1.X_add_number < 0x8000)
	    {
	      macro_build ((char *) NULL, &icnt, (expressionS *) NULL,
			   "nop", "");
	      macro_build ((char *) NULL, &icnt, &expr1,
			   mips_isa < 3 ? "addiu" : "daddiu",
			   "t,r,j", tempreg, tempreg, (int) BFD_RELOC_LO16);
	      (void) frag_var (rs_machine_dependent, 0, 0,
			       RELAX_ENCODE (0, 0, -12, -4, 0, 0),
			       offset_expr.X_add_symbol, (long) 0,
			       (char *) NULL);
	    }
	  else
	    {
	      int off1;

	      /* If we are going to add in a base register, and the
		 target register and the base register are the same,
		 then we are using AT as a temporary register.  Since
		 we want to load the constant into AT, we add our
		 current AT (from the global offset table) and the
		 register into the register now, and pretend we were
		 not using a base register.  */
	      if (breg != treg)
		off1 = 0;
	      else
		{
		  macro_build ((char *) NULL, &icnt, (expressionS *) NULL,
			       "nop", "");
		  macro_build ((char *) NULL, &icnt, (expressionS *) NULL,
			       mips_isa < 3 ? "addu" : "daddu",
			       "d,v,t", treg, AT, breg);
		  breg = 0;
		  tempreg = treg;
		  off1 = -8;
		}

	      macro_build_lui ((char *) NULL, &icnt, &expr1, AT);
	      macro_build ((char *) NULL, &icnt, &expr1,
			   mips_isa < 3 ? "addiu" : "daddiu",
			   "t,r,j", AT, AT, (int) BFD_RELOC_LO16);
	      macro_build ((char *) NULL, &icnt, (expressionS *) NULL,
			   mips_isa < 3 ? "addu" : "daddu",
			   "d,v,t", tempreg, tempreg, AT);
	      (void) frag_var (rs_machine_dependent, 0, 0,
			       RELAX_ENCODE (0, 0, -16 + off1, -8, 0, 0),
			       offset_expr.X_add_symbol, (long) 0,
			       (char *) NULL);
	      used_at = 1;
	    }
	}

      if (breg != 0)
	macro_build ((char *) NULL, &icnt, (expressionS *) NULL,
		     mips_isa < 3 ? "addu" : "daddu",
		     "d,v,t", treg, tempreg, breg);

      if (! used_at)
	return;

      break;

    case M_J_A:
      /* The j instruction may not be used in PIC code, since it
	 requires an absolute address.  We convert it to a b
	 instruction.  */
      if (mips_pic == 0)
	macro_build ((char *) NULL, &icnt, &offset_expr, "j", "a");
      else
	macro_build ((char *) NULL, &icnt, &offset_expr, "b", "p");
      return;

      /* The jal instructions must be handled as macros because when
	 generating PIC code they expand to multi-instruction
	 sequences.  Normally they are simple instructions.  */
    case M_JAL_1:
      dreg = RA;
      /* Fall through.  */
    case M_JAL_2:
      if (mips_pic == 0)
	{
	  macro_build ((char *) NULL, &icnt, (expressionS *) NULL, "jalr",
		       "d,s", dreg, sreg);
	  return;
	}

      /* I only know how to handle pic2.  */
      assert (mips_pic == 2);

      if (sreg != PIC_CALL_REG)
	as_warn ("MIPS PIC call to register other than $25");
      
      macro_build ((char *) NULL, &icnt, (expressionS *) NULL, "jalr", "d,s",
		   dreg, sreg);
      if (mips_cprestore_offset < 0)
	as_warn ("No .cprestore pseudo-op used in PIC code");
      else
	{
	  expr1.X_add_number = mips_cprestore_offset;
	  macro_build ((char *) NULL, &icnt, &expr1,
		       mips_isa < 3 ? "lw" : "ld",
		       "t,o(b)", GP, (int) BFD_RELOC_LO16, mips_frame_reg);
	}
      return;

    case M_JAL_A:
      if (mips_pic == 0)
	{
	  macro_build ((char *) NULL, &icnt, &offset_expr, "jal", "a");
	  return;
	}

      /* I only know how to handle pic2.  */
      assert (mips_pic == 2);

      /* If this is a reference to an external symbol, we want
	   lw		$25,<sym>($gp)		(BFD_RELOC_MIPS_CALL16)
	   nop
	   jalr		$25
	   nop
	   lw		$gp,cprestore($sp)
	 The cprestore value is set using the .cprestore pseudo-op.
	 If the symbol is not external, we want
	   lw		$25,<sym>($gp)		(BFD_RELOC_MIPS_GOT16)
	   nop
	   addiu	$25,$25,<sym>		(BFD_RELOC_LO16)
	   jalr		$25
	   nop
	   lw		$gp,cprestore($sp)
	 */
      frag_grow (12);
      macro_build ((char *) NULL, &icnt, &offset_expr,
		   mips_isa < 3 ? "lw" : "ld",
		   "t,o(b)", PIC_CALL_REG, (int) BFD_RELOC_MIPS_CALL16, GP);
      macro_build ((char *) NULL, &icnt, (expressionS *) NULL, "nop", "");
      p = frag_var (rs_machine_dependent, 4, 0,
		    RELAX_ENCODE (0, 4, -8, 0, 0, 0),
		    offset_expr.X_add_symbol, (long) 0, (char *) NULL);
      macro_build (p, &icnt, &offset_expr,
		   mips_isa < 3 ? "addiu" : "daddiu",
		   "t,r,j", PIC_CALL_REG, PIC_CALL_REG,
		   (int) BFD_RELOC_LO16);
      macro_build ((char *) NULL, &icnt, (expressionS *) NULL, "jalr", "s",
		   PIC_CALL_REG);
      if (mips_cprestore_offset < 0)
	as_warn ("No .cprestore pseudo-op used in PIC code");
      else
	{
	  if (mips_noreorder)
	    macro_build ((char *) NULL, &icnt, (expressionS *) NULL,
			 "nop", "");
	  expr1.X_add_number = mips_cprestore_offset;
	  macro_build ((char *) NULL, &icnt, &expr1,
		       mips_isa < 3 ? "lw" : "ld",
		       "t,o(b)", GP, (int) BFD_RELOC_LO16, mips_frame_reg);
	}
      return;

    case M_LB_AB:
      s = "lb";
      goto ld;
    case M_LBU_AB:
      s = "lbu";
      goto ld;
    case M_LH_AB:
      s = "lh";
      goto ld;
    case M_LHU_AB:
      s = "lhu";
      goto ld;
    case M_LW_AB:
      s = "lw";
      goto ld;
    case M_LWC0_AB:
      s = "lwc0";
      coproc = 1;
      goto ld;
    case M_LWC1_AB:
      s = "lwc1";
      coproc = 1;
      goto ld;
    case M_LWC2_AB:
      s = "lwc2";
      coproc = 1;
      goto ld;
    case M_LWC3_AB:
      s = "lwc3";
      coproc = 1;
      goto ld;
    case M_LWL_AB:
      s = "lwl";
      goto ld;
    case M_LWR_AB:
      s = "lwr";
      goto ld;
    case M_LDC1_AB:
      s = "ldc1";
      coproc = 1;
      goto ld;
    case M_LDC2_AB:
      s = "ldc2";
      coproc = 1;
      goto ld;
    case M_LDC3_AB:
      s = "ldc3";
      coproc = 1;
      goto ld;
    case M_LDL_AB:
      s = "ldl";
      goto ld;
    case M_LDR_AB:
      s = "ldr";
      goto ld;
    case M_LL_AB:
      s = "ll";
      goto ld;
    case M_LLD_AB:
      s = "lld";
      goto ld;
    case M_LWU_AB:
      s = "lwu";
    ld:
      if (breg == treg || coproc)
	{
	  tempreg = AT;
	  used_at = 1;
	}
      else
	{
	  tempreg = treg;
	  used_at = 0;
	}
      goto ld_st;
    case M_SB_AB:
      s = "sb";
      goto st;
    case M_SH_AB:
      s = "sh";
      goto st;
    case M_SW_AB:
      s = "sw";
      goto st;
    case M_SWC0_AB:
      s = "swc0";
      coproc = 1;
      goto st;
    case M_SWC1_AB:
      s = "swc1";
      coproc = 1;
      goto st;
    case M_SWC2_AB:
      s = "swc2";
      coproc = 1;
      goto st;
    case M_SWC3_AB:
      s = "swc3";
      coproc = 1;
      goto st;
    case M_SWL_AB:
      s = "swl";
      goto st;
    case M_SWR_AB:
      s = "swr";
      goto st;
    case M_SC_AB:
      s = "sc";
      goto st;
    case M_SCD_AB:
      s = "scd";
      goto st;
    case M_SDC1_AB:
      s = "sdc1";
      coproc = 1;
      goto st;
    case M_SDC2_AB:
      s = "sdc2";
      coproc = 1;
      goto st;
    case M_SDC3_AB:
      s = "sdc3";
      coproc = 1;
      goto st;
    case M_SDL_AB:
      s = "sdl";
      goto st;
    case M_SDR_AB:
      s = "sdr";
    st:
      tempreg = AT;
      used_at = 1;
    ld_st:
      if (mask == M_LWC1_AB
	  || mask == M_SWC1_AB
	  || mask == M_LDC1_AB
	  || mask == M_SDC1_AB
	  || mask == M_L_DAB
	  || mask == M_S_DAB)
	fmt = "T,o(b)";
      else if (coproc)
	fmt = "E,o(b)";
      else
	fmt = "t,o(b)";

      if (offset_expr.X_op != O_constant
	  && offset_expr.X_op != O_symbol)
	{
	  as_bad ("expression too complex");
	  offset_expr.X_op = O_constant;
	}

      /* A constant expression in PIC code can be handled just as it
	 is in non PIC code.  */
      if (mips_pic == 0
	  || offset_expr.X_op == O_constant)
	{
	  /* If this is a reference to a GP relative symbol, and there
	     is no base register, we want
	       <op>	$treg,<sym>($gp)	(BFD_RELOC_MIPS_GPREL)
	     Otherwise we want
	       lui	$tempreg,<sym>		(BFD_RELOC_HI16_S)
	       <op>	$treg,<sym>($tempreg)	(BFD_RELOC_LO16)
	     If we have a constant, we need two instructions anyhow,
	     so we always use the latter form.

	     If we have a base register, and this is a reference to a
	     GP relative symbol, we want
	       addu	$tempreg,$breg,$gp
	       <op>	$treg,<sym>($tempreg)	(BFD_RELOC_MIPS_GPREL)
	     Otherwise we want
	       lui	$tempreg,<sym>		(BFD_RELOC_HI16_S)
	       addu	$tempreg,$tempreg,$breg
	       <op>	$treg,<sym>($tempreg)	(BFD_RELOC_LO16)
	     With a constant we always use the latter case.  */
	  if (breg == 0)
	    {
	      if (offset_expr.X_add_number != 0)
		p = NULL;
	      else
		{
		  frag_grow (12);
		  macro_build ((char *) NULL, &icnt, &offset_expr, s, fmt,
			       treg, (int) BFD_RELOC_MIPS_GPREL, GP);
		  p = frag_var (rs_machine_dependent, 8, 0,
				RELAX_ENCODE (4, 8, 0, 4, 0,
					      mips_warn_about_macros),
				offset_expr.X_add_symbol, (long) 0,
				(char *) NULL);
		}
	      macro_build_lui (p, &icnt, &offset_expr, tempreg);
	      if (p != NULL)
		p += 4;
	      macro_build (p, &icnt, &offset_expr, s, fmt, treg,
			   (int) BFD_RELOC_LO16, tempreg);
	    }
	  else
	    {
	      if (offset_expr.X_add_number != 0)
		p = NULL;
	      else
		{
		  frag_grow (20);
		  macro_build ((char *) NULL, &icnt, (expressionS *) NULL,
			       mips_isa < 3 ? "addu" : "daddu",
			       "d,v,t", tempreg, breg, GP);
		  macro_build ((char *) NULL, &icnt, &offset_expr, s, fmt,
			       treg, (int) BFD_RELOC_MIPS_GPREL, tempreg);
		  p = frag_var (rs_machine_dependent, 12, 0,
				RELAX_ENCODE (8, 12, 0, 8, 0, 0),
				offset_expr.X_add_symbol, (long) 0,
				(char *) NULL);
		}
	      macro_build_lui (p, &icnt, &offset_expr, tempreg);
	      if (p != NULL)
		p += 4;
	      macro_build (p, &icnt, (expressionS *) NULL,
			   mips_isa < 3 ? "addu" : "daddu",
			   "d,v,t", tempreg, tempreg, breg);
	      if (p != NULL)
		p += 4;
	      macro_build (p, &icnt, &offset_expr, s, fmt, treg,
			   (int) BFD_RELOC_LO16, tempreg);
	    }
	}
      else
	{
	  /* If this is a reference to an external symbol, we want
	       lw	$tempreg,<sym>($gp)	(BFD_RELOC_MIPS_GOT16)
	       nop
	       <op>	$treg,0($tempreg)
	     Otherwise we want
	       lw	$tempreg,<sym>($gp)	(BFD_RELOC_MIPS_GOT16)
	       nop
	       addiu	$tempreg,$tempreg,<sym>	(BFD_RELOC_LO16)
	       <op>	$treg,0($tempreg)
	     If there is a base register, we add it to $tempreg before
	     the <op>.  If there is a constant, we stick it in the
	     <op> instruction.  We don't handle constants larger than
	     16 bits, because we have no way to load the upper 16 bits
	     (actually, we could handle them for the subset of cases
	     in which we are not using $at).  */
	  assert (offset_expr.X_op == O_symbol);
	  expr1.X_add_number = offset_expr.X_add_number;
	  offset_expr.X_add_number = 0;
	  if (expr1.X_add_number < -0x8000
	      || expr1.X_add_number >= 0x8000)
	    as_bad ("PIC code offset overflow (max 16 signed bits)");
	  frag_grow (12);
	  macro_build ((char *) NULL, &icnt, &offset_expr,
		       mips_isa < 3 ? "lw" : "ld",
		       "t,o(b)", tempreg, (int) BFD_RELOC_MIPS_GOT16, GP);
	  macro_build ((char *) NULL, &icnt, (expressionS *) NULL, "nop", "");
	  p = frag_var (rs_machine_dependent, 4, 0, 
			RELAX_ENCODE (0, 4, -8, 0, 0, 0),
			offset_expr.X_add_symbol, (long) 0,
			(char *) NULL);
	  macro_build (p, &icnt, &offset_expr,
		       mips_isa < 3 ? "addiu" : "daddiu",
		       "t,r,j", tempreg, tempreg, (int) BFD_RELOC_LO16);
	  if (breg != 0)
	    macro_build ((char *) NULL, &icnt, (expressionS *) NULL,
			 mips_isa < 3 ? "addu" : "daddu",
			 "d,v,t", tempreg, tempreg, breg);
	  macro_build ((char *) NULL, &icnt, &expr1, s, fmt, treg,
		       (int) BFD_RELOC_LO16, tempreg);
	}

      if (! used_at)
	return;

      break;

    case M_LI:
    case M_LI_S:
      load_register (&icnt, treg, &imm_expr);
      return;

    case M_LI_SS:
      if (mips_pic == 0)
	{
	  assert (offset_expr.X_op == O_symbol
		  && strcmp (segment_name (S_GET_SEGMENT
					   (offset_expr.X_add_symbol)),
			     ".lit4") == 0
		  && offset_expr.X_add_number == 0);
	  macro_build ((char *) NULL, &icnt, &offset_expr, "lwc1", "T,o(b)",
		       treg, (int) BFD_RELOC_MIPS_LITERAL, GP);
	}
      else
	{
	  assert (imm_expr.X_op == O_constant);
	  load_register (&icnt, treg, &imm_expr);
	}
      return;

    case M_LI_D:
      /* We know that sym is in the .rdata instruction.  First we get
	 the upper 16 bits of the address.  */
      if (mips_pic == 0)
	{
	  /* FIXME: This won't work for a 64 bit address.  */
	  macro_build_lui ((char *) NULL, &icnt, &offset_expr, AT);
	}
      else
	{
	  macro_build ((char *) NULL, &icnt, &offset_expr,
		       mips_isa < 3 ? "lw" : "ld",
		       "t,o(b)", AT, (int) BFD_RELOC_MIPS_GOT16, GP);
	}
      /* Now we load the register(s).  */
      if (mips_isa >= 3)
	macro_build ((char *) NULL, &icnt, &offset_expr, "ld", "t,o(b)",
		     treg, (int) BFD_RELOC_LO16, AT);
      else
	{
	  macro_build ((char *) NULL, &icnt, &offset_expr, "lw", "t,o(b)",
		       treg, (int) BFD_RELOC_LO16, AT);
	  if (treg != 31)
	    {
	      /* FIXME: How in the world do we deal with the possible
		 overflow here?  */
	      offset_expr.X_add_number += 4;
	      macro_build ((char *) NULL, &icnt, &offset_expr, "lw", "t,o(b)",
			   treg + 1, (int) BFD_RELOC_LO16, AT);
	    }
	}
	       
      break;

    case M_LI_DD:
      if (mips_pic == 0)
	{
	  /* Load a floating point number from the .lit8 section.  */
	  assert (offset_expr.X_op == O_symbol
		  && strcmp (segment_name (S_GET_SEGMENT
					   (offset_expr.X_add_symbol)),
			     ".lit8") == 0
		  && offset_expr.X_add_number == 0);
	  if (mips_isa >= 2)
	    {
	      macro_build ((char *) NULL, &icnt, &offset_expr, "ldc1",
			   "T,o(b)", treg, (int) BFD_RELOC_MIPS_LITERAL, GP);
	      return;
	    }
	  breg = GP;
	  r = BFD_RELOC_MIPS_LITERAL;
	  goto dob;
	}
      else
	{
	  /* Load the double from the .rdata section.  */
	  macro_build ((char *) NULL, &icnt, &offset_expr,
		       mips_isa < 3 ? "lw" : "ld",
		       "t,o(b)", AT, (int) BFD_RELOC_MIPS_GOT16, GP);
	  if (mips_isa >= 2)
	    {
	      macro_build ((char *) NULL, &icnt, &offset_expr, "ldc1",
			   "T,o(b)", treg, (int) BFD_RELOC_LO16, GP);
	      break;
	    }
	  breg = AT;
	  r = BFD_RELOC_LO16;
	  goto dob;
	}

    case M_L_DOB:
      /* Even on a big endian machine $fn comes before $fn+1.  We have
	 to adjust when loading from memory.  */
      r = BFD_RELOC_LO16;
    dob:
      assert (mips_isa < 2);
      macro_build ((char *) NULL, &icnt, &offset_expr, "lwc1", "T,o(b)",
		   byte_order == LITTLE_ENDIAN ? treg : treg + 1,
		   (int) r, breg);
      /* FIXME: A possible overflow which I don't know how to deal
	 with.  */
      offset_expr.X_add_number += 4;
      macro_build ((char *) NULL, &icnt, &offset_expr, "lwc1", "T,o(b)",
		   byte_order == LITTLE_ENDIAN ? treg + 1 : treg,
		   (int) r, breg);
      if (breg != AT)
	return;
      break;

    case M_L_DAB:
      /*
       * The MIPS assembler seems to check for X_add_number not
       * being double aligned and generating:
       *	lui	at,%hi(foo+1)
       *	addu	at,at,v1
       *	addiu	at,at,%lo(foo+1)
       *	lwc1	f2,0(at)
       *	lwc1	f3,4(at)
       * But, the resulting address is the same after relocation so why
       * generate the extra instruction?
       */
      coproc = 1;
      if (mips_isa >= 2)
	{
	  s = "ldc1";
	  goto ld;
	}

      s = "lwc1";
      fmt = "T,o(b)";
      goto ldd_std;

    case M_S_DAB:
      if (mips_isa >= 2)
	{
	  s = "sdc1";
	  goto st;
	}

      s = "swc1";
      fmt = "T,o(b)";
      coproc = 1;
      goto ldd_std;

    case M_LD_AB:
      if (mips_isa >= 3)
	{
	  s = "ld";
	  goto ld;
	}

      s = "lw";
      fmt = "t,o(b)";
      goto ldd_std;

    case M_SD_AB:
      if (mips_isa >= 3)
	{
	  s = "sd";
	  goto st;
	}

      s = "sw";
      fmt = "t,o(b)";

    ldd_std:
      if (offset_expr.X_op != O_symbol
	  && offset_expr.X_op != O_constant)
	{
	  as_bad ("expression too complex");
	  offset_expr.X_op = O_constant;
	}

      /* Even on a big endian machine $fn comes before $fn+1.  We have
	 to adjust when loading from memory.  We set coproc if we must
	 load $fn+1 first.  */
      if (byte_order == LITTLE_ENDIAN)
	coproc = 0;

      if (mips_pic == 0
	  || offset_expr.X_op == O_constant)
	{
	  /* If this is a reference to a GP relative symbol, we want
	       <op>	$treg,<sym>($gp)	(BFD_RELOC_MIPS_GPREL)
	       <op>	$treg+1,<sym>+4($gp)	(BFD_RELOC_MIPS_GPREL)
	     If we have a base register, we use this
	       addu	$at,$breg,$gp
	       <op>	$treg,<sym>($at)	(BFD_RELOC_MIPS_GPREL)
	       <op>	$treg+1,<sym>+4($at)	(BFD_RELOC_MIPS_GPREL)
	     If this is not a GP relative symbol, we want
	       lui	$at,<sym>		(BFD_RELOC_HI16_S)
	       <op>	$treg,<sym>($at)	(BFD_RELOC_LO16)
	       <op>	$treg+1,<sym>+4($at)	(BFD_RELOC_LO16)
	     If there is a base register, we add it to $at after the
	     lui instruction.  If there is a constant, we always use
	     the last case.  */
	  if (offset_expr.X_add_number != 0)
	    {
	      p = NULL;
	      used_at = 1;
	    }
	  else
	    {
	      int off;

	      if (breg == 0)
		{
		  frag_grow (20);
		  tempreg = GP;
		  off = 0;
		  used_at = 0;
		}
	      else
		{
		  frag_grow (28);
		  macro_build ((char *) NULL, &icnt, (expressionS *) NULL,
			       mips_isa < 3 ? "addu" : "daddu",
			       "d,v,t", AT, breg, GP);
		  tempreg = AT;
		  off = 4;
		  used_at = 1;
		}

	      macro_build ((char *) NULL, &icnt, &offset_expr, s, fmt,
			   coproc ? treg + 1 : treg,
			   (int) BFD_RELOC_MIPS_GPREL, tempreg);
	      offset_expr.X_add_number += 4;
	      macro_build ((char *) NULL, &icnt, &offset_expr, s, fmt,
			   coproc ? treg : treg + 1,
			   (int) BFD_RELOC_MIPS_GPREL, tempreg);
	      p = frag_var (rs_machine_dependent, 12 + off, 0,
			    RELAX_ENCODE (8 + off, 12 + off, 0, 4 + off, 1,
					  ! used_at && mips_noat),
			    offset_expr.X_add_symbol, (long) 0,
			    (char *) NULL);

	      /* We just generated two relocs.  When tc_gen_reloc
		 handles this case, it will skip the first reloc and
		 handle the second.  The second reloc already has an
		 extra addend of 4, which we added above.  We must
		 subtract it out, and then subtract another 4 to make
		 the first reloc come out right.  The second reloc
		 will come out right because we are going to add 4 to
		 offset_expr when we build its instruction below.  */
	      offset_expr.X_add_number -= 8;
	      offset_expr.X_op = O_constant;
	    }
	  macro_build_lui (p, &icnt, &offset_expr, AT);
	  if (p != NULL)
	    p += 4;
	  if (breg != 0)
	    {
	      macro_build (p, &icnt, (expressionS *) NULL,
			   mips_isa < 3 ? "addu" : "daddu",
			   "d,v,t", AT, breg, AT);
	      if (p != NULL)
		p += 4;
	    }
	  macro_build (p, &icnt, &offset_expr, s, fmt,
		       coproc ? treg + 1 : treg,
		       (int) BFD_RELOC_LO16, AT);
	  if (p != NULL)
	    p += 4;
	  /* FIXME: How do we handle overflow here?  */
	  offset_expr.X_add_number += 4;
	  macro_build (p, &icnt, &offset_expr, s, fmt,
		       coproc ? treg : treg + 1,
		       (int) BFD_RELOC_LO16, AT);
	}	  
      else
	{
	  int off;

	  /* If this is a reference to an external symbol, we want
	       lw	$at,<sym>($gp)		(BFD_RELOC_MIPS_GOT16)
	       nop
	       <op>	$treg,0($at)
	       <op>	$treg+1,4($at)
	     Otherwise we want
	       lw	$at,<sym>($gp)		(BFD_RELOC_MIPS_GOT16)
	       nop
	       <op>	$treg,<sym>($at)	(BFD_RELOC_LO16)
	       <op>	$treg+1,<sym>+4($at)	(BFD_RELOC_LO16)
	     If there is a base register we add it to $at before the
	     lwc1 instructions.  If there is a constant we include it
	     in the lwc1 instructions.  */
	  used_at = 1;
	  expr1.X_add_number = offset_expr.X_add_number;
	  offset_expr.X_add_number = 0;
	  if (expr1.X_add_number < -0x8000
	      || expr1.X_add_number >= 0x8000 - 4)
	    as_bad ("PIC code offset overflow (max 16 signed bits)");
	  if (breg == 0)
	    off = 0;
	  else
	    off = 4;
	  frag_grow (16 + off);
	  macro_build ((char *) NULL, &icnt, &offset_expr,
		       mips_isa < 3 ? "lw" : "ld",
		       "t,o(b)", AT, (int) BFD_RELOC_MIPS_GOT16, GP);
	  macro_build ((char *) NULL, &icnt, (expressionS *) NULL, "nop", "");
	  if (breg != 0)
	    macro_build ((char *) NULL, &icnt, (expressionS *) NULL,
			 mips_isa < 3 ? "addu" : "daddu",
			 "d,v,t", AT, breg, AT);
	  macro_build ((char *) NULL, &icnt, &expr1, s, fmt,
		       coproc ? treg + 1 : treg,
		       (int) BFD_RELOC_LO16, AT);
	  expr1.X_add_number += 4;
	  macro_build ((char *) NULL, &icnt, &expr1, s, fmt,
		       coproc ? treg : treg + 1,
		       (int) BFD_RELOC_LO16, AT);
	  (void) frag_var (rs_machine_dependent, 0, 0,
			   RELAX_ENCODE (0, 0, -16 - off, -8, 1, 0),
			   offset_expr.X_add_symbol, (long) 0,
			   (char *) NULL);
	}

      if (! used_at)
	return;

      break;

    case M_LD_OB:
      s = "lw";
      goto sd_ob;
    case M_SD_OB:
      s = "sw";
    sd_ob:
      assert (mips_isa < 3);
      macro_build ((char *) NULL, &icnt, &offset_expr, s, "t,o(b)", treg,
		   (int) BFD_RELOC_LO16, breg);
      offset_expr.X_add_number += 4;
      macro_build ((char *) NULL, &icnt, &offset_expr, s, "t,o(b)", treg + 1,
		   (int) BFD_RELOC_LO16, breg);
      return;
#ifdef LOSING_COMPILER
    default:
      macro2 (ip);
      return;
    }
  if (mips_noat)
    as_warn ("Macro used $at after \".set noat\"");
}
          
static void
macro2 (ip)
     struct mips_cl_insn *ip;
{
  register int treg, sreg, dreg, breg;
  int tempreg;
  int mask;
  int icnt = 0;
  int used_at;
  expressionS expr1;
  const char *s;
  const char *s2;
  const char *fmt;
  int likely = 0;
  int dbl = 0;
  int coproc = 0;
  offsetT maxnum;
  bfd_reloc_code_real_type r;
  char *p;
          
  treg = (ip->insn_opcode >> 16) & 0x1f;
  dreg = (ip->insn_opcode >> 11) & 0x1f;
  sreg = breg = (ip->insn_opcode >> 21) & 0x1f;
  mask = ip->insn_mo->mask;
          
  expr1.X_op = O_constant;
  expr1.X_op_symbol = NULL;
  expr1.X_add_symbol = NULL;
  expr1.X_add_number = 1;
          
  switch (mask)
    {
#endif /* LOSING_COMPILER */

    case M_DMUL:
      dbl = 1;
    case M_MUL:
      macro_build ((char *) NULL, &icnt, NULL,
		   dbl ? "dmultu" : "multu",
		   "s,t", sreg, treg);
      macro_build ((char *) NULL, &icnt, NULL, "mflo", "d", dreg);
      return;

    case M_DMUL_I:
      dbl = 1;
    case M_MUL_I:
      /* The MIPS assembler some times generates shifts and adds.  I'm
	 not trying to be that fancy. GCC should do this for us
	 anyway.  */
      load_register (&icnt, AT, &imm_expr);
      macro_build ((char *) NULL, &icnt, NULL,
		   dbl ? "dmult" : "mult",
		   "s,t", sreg, AT);
      macro_build ((char *) NULL, &icnt, NULL, "mflo", "d", dreg);
      break;

    case M_DMULO:
      dbl = 1;
    case M_MULO:
      mips_emit_delays ();
      ++mips_noreorder;
      mips_any_noreorder = 1;
      macro_build ((char *) NULL, &icnt, NULL,
		   dbl ? "dmult" : "mult",
		   "s,t", sreg, treg);
      macro_build ((char *) NULL, &icnt, NULL, "mflo", "d", dreg);
      macro_build ((char *) NULL, &icnt, NULL,
		   dbl ? "dsra32" : "sra",
		   "d,w,<", dreg, dreg, 31);
      macro_build ((char *) NULL, &icnt, NULL, "mfhi", "d", AT);
      expr1.X_add_number = 8;
      macro_build ((char *) NULL, &icnt, &expr1, "beq", "s,t,p", dreg, AT);
      macro_build ((char *) NULL, &icnt, NULL, "nop", "", 0);
      macro_build ((char *) NULL, &icnt, NULL, "break", "c", 6);
      --mips_noreorder;
      macro_build ((char *) NULL, &icnt, NULL, "mflo", "d", dreg);
      break;

    case M_DMULOU:
      dbl = 1;
    case M_MULOU:
      mips_emit_delays ();
      ++mips_noreorder;
      mips_any_noreorder = 1;
      macro_build ((char *) NULL, &icnt, NULL,
		   dbl ? "dmultu" : "multu",
		   "s,t", sreg, treg);
      macro_build ((char *) NULL, &icnt, NULL, "mfhi", "d", AT);
      macro_build ((char *) NULL, &icnt, NULL, "mflo", "d", dreg);
      expr1.X_add_number = 8;
      macro_build ((char *) NULL, &icnt, &expr1, "beq", "s,t,p", AT, 0);
      macro_build ((char *) NULL, &icnt, NULL, "nop", "", 0);
      macro_build ((char *) NULL, &icnt, NULL, "break", "c", 6);
      --mips_noreorder;
      break;

    case M_ROL:
      macro_build ((char *) NULL, &icnt, NULL, "subu", "d,v,t", AT, 0, treg);
      macro_build ((char *) NULL, &icnt, NULL, "srlv", "d,t,s", AT, sreg, AT);
      macro_build ((char *) NULL, &icnt, NULL, "sllv", "d,t,s", dreg, sreg,
		   treg);
      macro_build ((char *) NULL, &icnt, NULL, "or", "d,v,t", dreg, dreg, AT);
      break;

    case M_ROL_I:
      macro_build ((char *) NULL, &icnt, NULL, "sll", "d,w,<", AT, sreg,
		   imm_expr.X_add_number & 0x1f);
      macro_build ((char *) NULL, &icnt, NULL, "srl", "d,w,<", dreg, sreg,
		   (0 - imm_expr.X_add_number) & 0x1f);
      macro_build ((char *) NULL, &icnt, NULL, "or", "d,v,t", dreg, dreg, AT);
      break;

    case M_ROR:
      macro_build ((char *) NULL, &icnt, NULL, "subu", "d,v,t", AT, 0, treg);
      macro_build ((char *) NULL, &icnt, NULL, "sllv", "d,t,s", AT, sreg, AT);
      macro_build ((char *) NULL, &icnt, NULL, "srlv", "d,t,s", dreg, sreg,
		   treg);
      macro_build ((char *) NULL, &icnt, NULL, "or", "d,v,t", dreg, dreg, AT);
      break;

    case M_ROR_I:
      macro_build ((char *) NULL, &icnt, NULL, "srl", "d,w,<", AT, sreg,
		   imm_expr.X_add_number & 0x1f);
      macro_build ((char *) NULL, &icnt, NULL, "sll", "d,w,<", dreg, sreg,
		   (0 - imm_expr.X_add_number) & 0x1f);
      macro_build ((char *) NULL, &icnt, NULL, "or", "d,v,t", dreg, dreg, AT);
      break;

    case M_S_DOB:
      assert (mips_isa < 2);
      /* Even on a big endian machine $fn comes before $fn+1.  We have
	 to adjust when storing to memory.  */
      macro_build ((char *) NULL, &icnt, &offset_expr, "swc1", "T,o(b)",
		   byte_order == LITTLE_ENDIAN ? treg : treg + 1,
		   (int) BFD_RELOC_LO16, breg);
      offset_expr.X_add_number += 4;
      macro_build ((char *) NULL, &icnt, &offset_expr, "swc1", "T,o(b)",
		   byte_order == LITTLE_ENDIAN ? treg + 1 : treg,
		   (int) BFD_RELOC_LO16, breg);
      return;

    case M_SEQ:
      if (sreg == 0)
	macro_build ((char *) NULL, &icnt, &expr1, "sltiu", "t,r,j", dreg,
		     treg, (int) BFD_RELOC_LO16);
      else if (treg == 0)
	macro_build ((char *) NULL, &icnt, &expr1, "sltiu", "t,r,j", dreg,
		     sreg, (int) BFD_RELOC_LO16);
      else
	{
	  macro_build ((char *) NULL, &icnt, NULL, "xor", "d,v,t", dreg,
		       sreg, treg);
	  macro_build ((char *) NULL, &icnt, &expr1, "sltiu", "t,r,j", dreg,
		       dreg, (int) BFD_RELOC_LO16);
	}
      return;

    case M_SEQ_I:
      if (imm_expr.X_add_number == 0)
	{
	  macro_build ((char *) NULL, &icnt, &expr1, "sltiu", "t,r,j", dreg,
		       sreg, (int) BFD_RELOC_LO16);
	  return;
	}
      if (sreg == 0)
	{
	  as_warn ("Instruction %s: result is always false",
		   ip->insn_mo->name);
	  macro_build ((char *) NULL, &icnt, NULL, "move", "d,s", dreg, 0);
	  return;
	}
      if (imm_expr.X_add_number >= 0 && imm_expr.X_add_number < 0x10000)
	{
	  macro_build ((char *) NULL, &icnt, &imm_expr, "xori", "t,r,i", dreg,
		       sreg, (int) BFD_RELOC_LO16);
	  used_at = 0;
	}
      else if (imm_expr.X_add_number > -0x8000 && imm_expr.X_add_number < 0)
	{
	  imm_expr.X_add_number = -imm_expr.X_add_number;
	  macro_build ((char *) NULL, &icnt, &imm_expr,
		       mips_isa < 3 ? "addiu" : "daddiu",
		       "t,r,j", dreg, sreg,
		       (int) BFD_RELOC_LO16);
	  used_at = 0;
	}
      else
	{
	  load_register (&icnt, AT, &imm_expr);
	  macro_build ((char *) NULL, &icnt, NULL, "xor", "d,v,t", dreg,
		       sreg, AT);
	  used_at = 1;
	}
      macro_build ((char *) NULL, &icnt, &expr1, "sltiu", "t,r,j", dreg, dreg,
		   (int) BFD_RELOC_LO16);
      if (used_at)
	break;
      return;

    case M_SGE:		/* sreg >= treg <==> not (sreg < treg) */
      s = "slt";
      goto sge;
    case M_SGEU:
      s = "sltu";
    sge:
      macro_build ((char *) NULL, &icnt, NULL, s, "d,v,t", dreg, sreg, treg);
      macro_build ((char *) NULL, &icnt, &expr1, "xori", "t,r,i", dreg, dreg,
		   (int) BFD_RELOC_LO16);
      return;

    case M_SGE_I:		/* sreg >= I <==> not (sreg < I) */
    case M_SGEU_I:
      if (imm_expr.X_add_number >= -0x8000 && imm_expr.X_add_number < 0x8000)
	{
	  macro_build ((char *) NULL, &icnt, &expr1,
		       mask == M_SGE_I ? "slti" : "sltiu",
		       "t,r,j", dreg, sreg, (int) BFD_RELOC_LO16);
	  used_at = 0;
	}
      else
	{
	  load_register (&icnt, AT, &imm_expr);
	  macro_build ((char *) NULL, &icnt, NULL,
		       mask == M_SGE_I ? "slt" : "sltu",
		       "d,v,t", dreg, sreg, AT);
	  used_at = 1;
	}
      macro_build ((char *) NULL, &icnt, &expr1, "xori", "t,r,i", dreg, dreg,
		   (int) BFD_RELOC_LO16);
      if (used_at)
	break;
      return;

    case M_SGT:		/* sreg > treg  <==>  treg < sreg */
      s = "slt";
      goto sgt;
    case M_SGTU:
      s = "sltu";
    sgt:
      macro_build ((char *) NULL, &icnt, NULL, s, "d,v,t", dreg, treg, sreg);
      return;

    case M_SGT_I:		/* sreg > I  <==>  I < sreg */
      s = "slt";
      goto sgti;
    case M_SGTU_I:
      s = "sltu";
    sgti:
      load_register (&icnt, AT, &imm_expr);
      macro_build ((char *) NULL, &icnt, NULL, s, "d,v,t", dreg, AT, sreg);
      break;

    case M_SLE:		/* sreg <= treg  <==>  treg >= sreg  <==>  not (treg < sreg) */
      s = "slt";
      goto sle;
    case M_SLEU:
      s = "sltu";
    sle:
      macro_build ((char *) NULL, &icnt, NULL, s, "d,v,t", dreg, treg, sreg);
      macro_build ((char *) NULL, &icnt, &expr1, "xori", "t,r,i", dreg, dreg,
		   (int) BFD_RELOC_LO16);
      return;

    case M_SLE_I:		/* sreg <= I <==> I >= sreg <==> not (I < sreg) */
      s = "slt";
      goto slei;
    case M_SLEU_I:
      s = "sltu";
    slei:
      load_register (&icnt, AT, &imm_expr);
      macro_build ((char *) NULL, &icnt, NULL, s, "d,v,t", dreg, AT, sreg);
      macro_build ((char *) NULL, &icnt, &expr1, "xori", "t,r,i", dreg, dreg,
		   (int) BFD_RELOC_LO16);
      break;

    case M_SLT_I:
      if (imm_expr.X_add_number >= -0x8000 && imm_expr.X_add_number < 0x8000)
	{
	  macro_build ((char *) NULL, &icnt, &imm_expr, "slti", "t,r,j",
		       dreg, sreg, (int) BFD_RELOC_LO16);
	  return;
	}
      load_register (&icnt, AT, &imm_expr);
      macro_build ((char *) NULL, &icnt, NULL, "slt", "d,v,t", dreg, sreg, AT);
      break;

    case M_SLTU_I:
      if (imm_expr.X_add_number >= -0x8000 && imm_expr.X_add_number < 0x8000)
	{
	  macro_build ((char *) NULL, &icnt, &imm_expr, "sltiu", "t,r,j",
		       dreg, sreg, (int) BFD_RELOC_LO16);
	  return;
	}
      load_register (&icnt, AT, &imm_expr);
      macro_build ((char *) NULL, &icnt, NULL, "sltu", "d,v,t", dreg, sreg,
		   AT);
      break;

    case M_SNE:
      if (sreg == 0)
	macro_build ((char *) NULL, &icnt, NULL, "sltu", "d,v,t", dreg, 0,
		     treg);
      else if (treg == 0)
	macro_build ((char *) NULL, &icnt, NULL, "sltu", "d,v,t", dreg, 0,
		     sreg);
      else
	{
	  macro_build ((char *) NULL, &icnt, NULL, "xor", "d,v,t", dreg,
		       sreg, treg);
	  macro_build ((char *) NULL, &icnt, NULL, "sltu", "d,v,t", dreg, 0,
		       dreg);
	}
      return;

    case M_SNE_I:
      if (imm_expr.X_add_number == 0)
	{
	  macro_build ((char *) NULL, &icnt, NULL, "sltu", "d,v,t", dreg, 0,
		       sreg);
	  return;
	}
      if (sreg == 0)
	{
	  as_warn ("Instruction %s: result is always true",
		   ip->insn_mo->name);
	  macro_build ((char *) NULL, &icnt, &expr1,
		       mips_isa < 3 ? "addiu" : "daddiu",
		       "t,r,j", dreg, 0, (int) BFD_RELOC_LO16);
	  return;
	}
      if (imm_expr.X_add_number >= 0 && imm_expr.X_add_number < 0x10000)
	{
	  macro_build ((char *) NULL, &icnt, &imm_expr, "xori", "t,r,i",
		       dreg, sreg, (int) BFD_RELOC_LO16);
	  used_at = 0;
	}
      else if (imm_expr.X_add_number > -0x8000 && imm_expr.X_add_number < 0)
	{
	  imm_expr.X_add_number = -imm_expr.X_add_number;
	  macro_build ((char *) NULL, &icnt, &imm_expr,
		       mips_isa < 3 ? "addiu" : "daddiu",
		       "t,r,j", dreg, sreg, (int) BFD_RELOC_LO16);
	  used_at = 0;
	}
      else
	{
	  load_register (&icnt, AT, &imm_expr);
	  macro_build ((char *) NULL, &icnt, NULL, "xor", "d,v,t", dreg,
		       sreg, AT);
	  used_at = 1;
	}
      macro_build ((char *) NULL, &icnt, NULL, "sltu", "d,v,t", dreg, 0, dreg);
      if (used_at)
	break;
      return;

    case M_DSUB_I:
      dbl = 1;
    case M_SUB_I:
      if (imm_expr.X_add_number > -0x8000 && imm_expr.X_add_number <= 0x8000)
	{
	  imm_expr.X_add_number = -imm_expr.X_add_number;
	  macro_build ((char *) NULL, &icnt, &imm_expr,
		       dbl ? "daddi" : "addi",
		       "t,r,j", dreg, sreg, (int) BFD_RELOC_LO16);
	  return;
	}
      load_register (&icnt, AT, &imm_expr);
      macro_build ((char *) NULL, &icnt, NULL,
		   dbl ? "dsub" : "sub",
		   "d,v,t", dreg, sreg, AT);
      break;

    case M_DSUBU_I:
      dbl = 1;
    case M_SUBU_I:
      if (imm_expr.X_add_number > -0x8000 && imm_expr.X_add_number <= 0x8000)
	{
	  imm_expr.X_add_number = -imm_expr.X_add_number;
	  macro_build ((char *) NULL, &icnt, &imm_expr,
		       dbl ? "daddiu" : "addiu",
		       "t,r,j", dreg, sreg, (int) BFD_RELOC_LO16);
	  return;
	}
      load_register (&icnt, AT, &imm_expr);
      macro_build ((char *) NULL, &icnt, NULL,
		   dbl ? "dsubu" : "subu",
		   "d,v,t", dreg, sreg, AT);
      break;

    case M_TEQ_I:
      s = "teq";
      goto trap;
    case M_TGE_I:
      s = "tge";
      goto trap;
    case M_TGEU_I:
      s = "tgeu";
      goto trap;
    case M_TLT_I:
      s = "tlt";
      goto trap;
    case M_TLTU_I:
      s = "tltu";
      goto trap;
    case M_TNE_I:
      s = "tne";
    trap:
      load_register (&icnt, AT, &imm_expr);
      macro_build ((char *) NULL, &icnt, NULL, s, "s,t", sreg, AT);
      break;

    case M_TRUNCWD:
    case M_TRUNCWS:
      assert (mips_isa < 2);
      sreg = (ip->insn_opcode >> 11) & 0x1f;	/* floating reg */
      dreg = (ip->insn_opcode >> 06) & 0x1f;	/* floating reg */

      /*
       * Is the double cfc1 instruction a bug in the mips assembler;
       * or is there a reason for it?
       */
      mips_emit_delays ();
      ++mips_noreorder;
      mips_any_noreorder = 1;
      macro_build ((char *) NULL, &icnt, NULL, "cfc1", "t,G", treg, 31);
      macro_build ((char *) NULL, &icnt, NULL, "cfc1", "t,G", treg, 31);
      macro_build ((char *) NULL, &icnt, NULL, "nop", "");
      expr1.X_add_number = 3;
      macro_build ((char *) NULL, &icnt, &expr1, "ori", "t,r,i", AT, treg,
		   (int) BFD_RELOC_LO16);
      expr1.X_add_number = 2;
      macro_build ((char *) NULL, &icnt, &expr1, "xori", "t,r,i", AT, AT,
		     (int) BFD_RELOC_LO16);
      macro_build ((char *) NULL, &icnt, NULL, "ctc1", "t,G", AT, 31);
      macro_build ((char *) NULL, &icnt, NULL, "nop", "");
      macro_build ((char *) NULL, &icnt, NULL,
	      mask == M_TRUNCWD ? "cvt.w.d" : "cvt.w.s", "D,S", dreg, sreg);
      macro_build ((char *) NULL, &icnt, NULL, "ctc1", "t,G", treg, 31);
      macro_build ((char *) NULL, &icnt, NULL, "nop", "");
      --mips_noreorder;
      break;

    case M_ULH:
      s = "lb";
      goto ulh;
    case M_ULHU:
      s = "lbu";
    ulh:
      /* avoid load delay */
      offset_expr.X_add_number += 1;
      macro_build ((char *) NULL, &icnt, &offset_expr, s, "t,o(b)", treg,
		   (int) BFD_RELOC_LO16, breg);
      offset_expr.X_add_number -= 1;
      macro_build ((char *) NULL, &icnt, &offset_expr, "lbu", "t,o(b)", AT,
		   (int) BFD_RELOC_LO16, breg);
      macro_build ((char *) NULL, &icnt, NULL, "sll", "d,w,<", treg, treg, 8);
      macro_build ((char *) NULL, &icnt, NULL, "or", "d,v,t", treg, treg, AT);
      break;

    case M_ULW:
      /* does this work on a big endian machine? */
      offset_expr.X_add_number += 3;
      macro_build ((char *) NULL, &icnt, &offset_expr, "lwl", "t,o(b)", treg,
		   (int) BFD_RELOC_LO16, breg);
      offset_expr.X_add_number -= 3;
      macro_build ((char *) NULL, &icnt, &offset_expr, "lwr", "t,o(b)", treg,
		   (int) BFD_RELOC_LO16, breg);
      return;

    case M_ULH_A:
    case M_ULHU_A:
    case M_ULW_A:
      load_address (&icnt, AT, &offset_expr);
      if (mask == M_ULW_A)
	{
	  expr1.X_add_number = 3;
	  macro_build ((char *) NULL, &icnt, &expr1, "lwl", "t,o(b)", treg,
		       (int) BFD_RELOC_LO16, AT);
	  expr1.X_add_number = 0;
	  macro_build ((char *) NULL, &icnt, &expr1, "lwr", "t,o(b)", treg,
		       (int) BFD_RELOC_LO16, AT);
	}
      else
	{
	  macro_build ((char *) NULL, &icnt, &expr1,
		       mask == M_ULH_A ? "lb" : "lbu", "t,o(b)", treg,
		       (int) BFD_RELOC_LO16, AT);
	  expr1.X_add_number = 0;
	  macro_build ((char *) NULL, &icnt, &expr1, "lbu", "t,o(b)", AT,
		       (int) BFD_RELOC_LO16, AT);
	  macro_build ((char *) NULL, &icnt, NULL, "sll", "d,w,<", treg,
		       treg, 8);
	  macro_build ((char *) NULL, &icnt, NULL, "or", "d,v,t", treg,
		       treg, AT);
	}
      break;

    case M_USH:
      macro_build ((char *) NULL, &icnt, &offset_expr, "sb", "t,o(b)", treg,
		   (int) BFD_RELOC_LO16, breg);
      macro_build ((char *) NULL, &icnt, NULL, "srl", "d,w,<", AT, treg, 8);
      offset_expr.X_add_number += 1;
      macro_build ((char *) NULL, &icnt, &offset_expr, "sb", "t,o(b)", AT,
		   (int) BFD_RELOC_LO16, breg);
      break;

    case M_USW:
      offset_expr.X_add_number += 3;
      macro_build ((char *) NULL, &icnt, &offset_expr, "swl", "t,o(b)", treg,
		   (int) BFD_RELOC_LO16, breg);
      offset_expr.X_add_number -= 3;
      macro_build ((char *) NULL, &icnt, &offset_expr, "swr", "t,o(b)", treg,
		   (int) BFD_RELOC_LO16, breg);
      return;

    case M_USH_A:
    case M_USW_A:
      load_address (&icnt, AT, &offset_expr);
      if (mask == M_USW_A)
	{
	  expr1.X_add_number = 3;
	  macro_build ((char *) NULL, &icnt, &expr1, "swl", "t,o(b)", treg,
		       (int) BFD_RELOC_LO16, AT);
	  expr1.X_add_number = 0;
	  macro_build ((char *) NULL, &icnt, &expr1, "swr", "t,o(b)", treg,
		       (int) BFD_RELOC_LO16, AT);
	}
      else
	{
	  expr1.X_add_number = 0;
	  macro_build ((char *) NULL, &icnt, &expr1, "sb", "t,o(b)", treg,
		       (int) BFD_RELOC_LO16, AT);
	  macro_build ((char *) NULL, &icnt, NULL, "srl", "d,w,<", treg,
		       treg, 8);
	  expr1.X_add_number = 1;
	  macro_build ((char *) NULL, &icnt, &expr1, "sb", "t,o(b)", treg,
		       (int) BFD_RELOC_LO16, AT);
	  expr1.X_add_number = 0;
	  macro_build ((char *) NULL, &icnt, &expr1, "lbu", "t,o(b)", AT,
		       (int) BFD_RELOC_LO16, AT);
	  macro_build ((char *) NULL, &icnt, NULL, "sll", "d,w,<", treg,
		       treg, 8);
	  macro_build ((char *) NULL, &icnt, NULL, "or", "d,v,t", treg,
		       treg, AT);
	}
      break;

    default:
      as_bad ("Macro %s not implemented yet", ip->insn_mo->name);
      break;
    }
  if (mips_noat)
    as_warn ("Macro used $at after \".set noat\"");
}


/*
This routine assembles an instruction into its binary format.  As a side
effect it sets one of the global variables imm_reloc or offset_reloc to the
type of relocation to do if one of the operands is an address expression.
*/
static void
mips_ip (str, ip)
     char *str;
     struct mips_cl_insn *ip;
{
  char *s;
  const char *args;
  char c;
  struct mips_opcode *insn;
  char *argsStart;
  unsigned int regno;
  unsigned int lastregno = 0;
  char *s_reset;

  insn_error = NULL;

  for (s = str; islower (*s) || (*s >= '0' && *s <= '3') || *s == '.'; ++s)
    continue;
  switch (*s)
    {
    case '\0':
      break;

    case ' ':
      *s++ = '\0';
      break;

    default:
      as_warn ("Unknown opcode: `%s'", str);
      exit (1);
    }
  if ((insn = (struct mips_opcode *) hash_find (op_hash, str)) == NULL)
    {
      as_warn ("`%s' not in hash table.", str);
      insn_error = "ERROR: Unrecognized opcode";
      return;
    }
  argsStart = s;
  for (;;)
    {
      int insn_isa;

      assert (strcmp (insn->name, str) == 0);

      if (insn->pinfo == INSN_MACRO)
	insn_isa = insn->match;
      else if (insn->pinfo & INSN_ISA2)
	insn_isa = 2;
      else if (insn->pinfo & INSN_ISA3)
	insn_isa = 3;
      else
	insn_isa = 1;

      if (insn_isa > mips_isa)
	{
	  if (insn + 1 < &mips_opcodes[NUMOPCODES]
	      && strcmp (insn->name, insn[1].name) == 0)
	    {
	      ++insn;
	      continue;
	    }
	  insn_error = "ERROR: instruction not supported on this processor";
	  return;
	}

      ip->insn_mo = insn;
      ip->insn_opcode = insn->match;
      for (args = insn->args;; ++args)
	{
	  if (*s == ' ')
	    ++s;
	  switch (*args)
	    {
	    case '\0':		/* end of args */
	      if (*s == '\0')
		return;
	      break;

	    case ',':
	      if (*s++ == *args)
		continue;
	      s--;
	      switch (*++args)
		{
		case 'r':
		case 'v':
		  ip->insn_opcode |= lastregno << 21;
		  continue;

		case 'w':
		case 'W':
		  ip->insn_opcode |= lastregno << 16;
		  continue;

		case 'V':
		  ip->insn_opcode |= lastregno << 11;
		  continue;
		}
	      break;

	    case '(':
	      /* handle optional base register.
		 Either the base register is omitted or
		 we must have a left paren. */
	      /* this is dependent on the next operand specifier
		 is a 'b' for base register */
	      assert (args[1] == 'b');
	      if (*s == '\0')
		return;

	    case ')':		/* these must match exactly */
	      if (*s++ == *args)
		continue;
	      break;

	    case '<':		/* must be at least one digit */
	      /*
	       * According to the manual, if the shift amount is greater
	       * than 31 or less than 0 the the shift amount should be
	       * mod 32. In reality the mips assembler issues an error.
	       * We issue a warning and mask out all but the low 5 bits.
	       */
	      my_getExpression (&imm_expr, s);
	      check_absolute_expr (ip, &imm_expr);
	      if ((unsigned long) imm_expr.X_add_number > 31)
		{
		  as_warn ("Improper shift amount (%ld)",
			   (long) imm_expr.X_add_number);
		  imm_expr.X_add_number = imm_expr.X_add_number & 0x1f;
		}
	      ip->insn_opcode |= imm_expr.X_add_number << 6;
	      imm_expr.X_op = O_absent;
	      s = expr_end;
	      continue;

	    case '>':		/* shift amount minus 32 */
	      my_getExpression (&imm_expr, s);
	      check_absolute_expr (ip, &imm_expr);
	      if ((unsigned long) imm_expr.X_add_number < 32
		  || (unsigned long) imm_expr.X_add_number > 63)
		break;
	      ip->insn_opcode |= (imm_expr.X_add_number - 32) << 6;
	      imm_expr.X_op = O_absent;
	      s = expr_end;
	      continue;

	    case 'k':		/* cache code */
	      my_getExpression (&imm_expr, s);
	      check_absolute_expr (ip, &imm_expr);
	      if ((unsigned long) imm_expr.X_add_number > 31)
		{
		  as_warn ("Invalid cahce opcode (%lu)",
			   (unsigned long) imm_expr.X_add_number);
		  imm_expr.X_add_number &= 0x1f;
		}
	      ip->insn_opcode |= imm_expr.X_add_number << OP_SH_CACHE;
	      imm_expr.X_op = O_absent;
	      s = expr_end;
	      continue;

	    case 'c':		/* break code */
	      my_getExpression (&imm_expr, s);
	      check_absolute_expr (ip, &imm_expr);
	      if ((unsigned) imm_expr.X_add_number > 1023)
		as_warn ("Illegal break code (%ld)",
			 (long) imm_expr.X_add_number);
	      ip->insn_opcode |= imm_expr.X_add_number << 16;
	      imm_expr.X_op = O_absent;
	      s = expr_end;
	      continue;

	    case 'B':		/* syscall code */
	      my_getExpression (&imm_expr, s);
	      check_absolute_expr (ip, &imm_expr);
	      if ((unsigned) imm_expr.X_add_number > 0xfffff)
		as_warn ("Illegal syscall code (%ld)",
			 (long) imm_expr.X_add_number);
	      ip->insn_opcode |= imm_expr.X_add_number << 6;
	      imm_expr.X_op = O_absent;
	      s = expr_end;
	      continue;

            case 'C':           /* Coprocessor code */
              my_getExpression (&imm_expr, s);
	      check_absolute_expr (ip, &imm_expr);
              if ((unsigned long) imm_expr.X_add_number >= (1<<25))
		{
                  as_warn ("Coproccesor code > 25 bits (%ld)",
			   (long) imm_expr.X_add_number);
                  imm_expr.X_add_number &= ((1<<25) - 1);
		}
              ip->insn_opcode |= imm_expr.X_add_number;
              imm_expr.X_op = O_absent;
              s = expr_end;
              continue;

	    case 'b':		/* base register */
	    case 'd':		/* destination register */
	    case 's':		/* source register */
	    case 't':		/* target register */
	    case 'r':		/* both target and source */
	    case 'v':		/* both dest and source */
	    case 'w':		/* both dest and target */
	    case 'E':		/* coprocessor target register */
	    case 'G':		/* coprocessor destination register */
	    case 'x':		/* ignore register name */
	    case 'z':		/* must be zero register */
	      s_reset = s;
	      if (s[0] == '$')
		{
		  if (isdigit (s[1]))
		    {
		      ++s;
		      regno = 0;
		      do
			{
			  regno *= 10;
			  regno += *s - '0';
			  ++s;
			}
		      while (isdigit (*s));
		      if (regno > 31)
			as_bad ("Invalid register number (%d)", regno);
		    }
		  else if (*args == 'E' || *args == 'G')
		    goto notreg;
		  else
		    {
		      if (s[1] == 'f' && s[2] == 'p')
			{
			  s += 3;
			  regno = FP;
			}
		      else if (s[1] == 's' && s[2] == 'p')
			{
			  s += 3;
			  regno = SP;
			}
		      else if (s[1] == 'g' && s[2] == 'p')
			{
			  s += 3;
			  regno = GP;
			}
		      else if (s[1] == 'a' && s[2] == 't')
			{
			  s += 3;
			  regno = AT;
			}
		      else
			goto notreg;
		    }
		  if (regno == AT && ! mips_noat)
		    as_warn ("Used $at without \".set noat\"");
		  c = *args;
		  if (*s == ' ')
		    s++;
		  if (args[1] != *s)
		    {
		      if (c == 'r' || c == 'v' || c == 'w')
			{
			  regno = lastregno;
			  s = s_reset;
			  args++;
			}
		    }
		  /* 'z' only matches $0.  */
		  if (c == 'z' && regno != 0)
		    break;
		  switch (c)
		    {
		    case 'r':
		    case 's':
		    case 'v':
		    case 'b':
		      ip->insn_opcode |= regno << 21;
		      break;
		    case 'd':
		    case 'G':
		      ip->insn_opcode |= regno << 11;
		      break;
		    case 'w':
		    case 't':
		    case 'E':
		      ip->insn_opcode |= regno << 16;
		      break;
		    case 'x':
		      /* This case exists because on the r3000 trunc
			 expands into a macro which requires a gp
			 register.  On the r6000 or r4000 it is
			 assembled into a single instruction which
			 ignores the register.  Thus the insn version
			 is MIPS_ISA2 and uses 'x', and the macro
			 version is MIPS_ISA1 and uses 't'.  */
		      break;
		    case 'z':
		      /* This case is for the div instruction, which
			 acts differently if the destination argument
			 is $0.  This only matches $0, and is checked
			 outside the switch.  */
		      break;
		    }
		  lastregno = regno;
		  continue;
		}
	    notreg:
	      switch (*args++)
		{
		case 'r':
		case 'v':
		  ip->insn_opcode |= lastregno << 21;
		  continue;
		case 'w':
		  ip->insn_opcode |= lastregno << 16;
		  continue;
		}
	      break;

	    case 'D':		/* floating point destination register */
	    case 'S':		/* floating point source register */
	    case 'T':		/* floating point target register */
	    case 'V':
	    case 'W':
	      s_reset = s;
	      if (s[0] == '$' && s[1] == 'f' && isdigit (s[2]))
		{
		  s += 2;
		  regno = 0;
		  do
		    {
		      regno *= 10;
		      regno += *s - '0';
		      ++s;
		    }
		  while (isdigit (*s));

		  if (regno > 31)
		    as_bad ("Invalid float register number (%d)", regno);

		  if ((regno & 1) != 0
		      && mips_isa < 3
		      && ! (strcmp (str, "mtc1") == 0 ||
			    strcmp (str, "mfc1") == 0 ||
			    strcmp (str, "lwc1") == 0 ||
			    strcmp (str, "swc1") == 0))
		    as_warn ("Float register should be even, was %d",
			     regno);

		  c = *args;
		  if (*s == ' ')
		    s++;
		  if (args[1] != *s)
		    {
		      if (c == 'V' || c == 'W')
			{
			  regno = lastregno;
			  s = s_reset;
			  args++;
			}
		    }
		  switch (c)
		    {
		    case 'D':
		      ip->insn_opcode |= regno << 6;
		      break;
		    case 'V':
		    case 'S':
		      ip->insn_opcode |= regno << 11;
		      break;
		    case 'W':
		    case 'T':
		      ip->insn_opcode |= regno << 16;
		    }
		  lastregno = regno;
		  continue;
		}
	      switch (*args++)
		{
		case 'V':
		  ip->insn_opcode |= lastregno << 11;
		  continue;
		case 'W':
		  ip->insn_opcode |= lastregno << 16;
		  continue;
		}
	      break;

	    case 'I':
	      my_getExpression (&imm_expr, s);
	      check_absolute_expr (ip, &imm_expr);
	      s = expr_end;
	      continue;

	    case 'A':
	      my_getExpression (&offset_expr, s);
	      imm_reloc = BFD_RELOC_32;
	      s = expr_end;
	      continue;

	    case 'F':
	    case 'L':
	    case 'f':
	    case 'l':
	      {
		int f64;
		char *save_in;
		char *err;
		unsigned char temp[8];
		int len;
		unsigned int length;
		segT seg;
		subsegT subseg;
		char *p;

		/* These only appear as the last operand in an
		   instruction, and every instruction that accepts
		   them in any variant accepts them in all variants.
		   This means we don't have to worry about backing out
		   any changes if the instruction does not match.

		   The difference between them is the size of the
		   floating point constant and where it goes.  For 'F'
		   and 'L' the constant is 64 bits; for 'f' and 'l' it
		   is 32 bits.  Where the constant is placed is based
		   on how the MIPS assembler does things:
		    F -- .rdata
		    L -- .lit8
		    f -- immediate value
		    l -- .lit4

		    When generating PIC code, we do not use the .lit8
		    or .lit4 sections at all, in order to reserve the
		    entire global offset table.  */

		f64 = *args == 'F' || *args == 'L';

		save_in = input_line_pointer;
		input_line_pointer = s;
		err = md_atof (f64 ? 'd' : 'f', (char *) temp, &len);
		length = len;
		s = input_line_pointer;
		input_line_pointer = save_in;
		if (err != NULL && *err != '\0')
		  {
		    as_bad ("Bad floating point constant: %s", err);
		    memset (temp, '\0', sizeof temp);
		    length = f64 ? 8 : 4;
		  }

		assert (length == (f64 ? 8 : 4));

		if (*args == 'f'
		    || (mips_pic != 0 && *args == 'l'))
		  {
		    imm_expr.X_op = O_constant;
		    if (byte_order == LITTLE_ENDIAN)
		      imm_expr.X_add_number =
			(((((((int) temp[3] << 8)
			     | temp[2]) << 8)
			   | temp[1]) << 8)
			 | temp[0]);
		    else
		      imm_expr.X_add_number =
			(((((((int) temp[0] << 8)
			     | temp[1]) << 8)
			   | temp[2]) << 8)
			 | temp[3]);
		  }
		else
		  {
		    const char *newname;
		    segT new_seg;

		    /* Switch to the right section.  */
		    seg = now_seg;
		    subseg = now_subseg;
		    switch (*args)
		      {
		      default: /* unused default case avoids warnings.  */
		      case 'L':
			newname = (mips_pic == 0 ? ".lit8" : ".rdata");
			break;
		      case 'F':
			newname = ".rdata";
			break;
		      case 'l':
			assert (mips_pic == 0);
			newname = ".lit4";
			break;
		      }
		    new_seg = subseg_new (newname, (subsegT) 0);
#ifdef OBJ_ELF
		    bfd_set_section_alignment (stdoutput, new_seg, 4);
#endif
		    if (seg == now_seg)
		      as_bad ("Can't use floating point insn in this section");

		    /* Set the argument to the current address in the
		       section.  */
		    offset_expr.X_op = O_symbol;
		    offset_expr.X_add_symbol =
		      symbol_new ("L0\001", now_seg,
				  (valueT) frag_now_fix (), frag_now);
		    offset_expr.X_add_number = 0;

		    /* Put the floating point number into the section.  */
		    p = frag_more ((int) length);
		    memcpy (p, temp, length);

		    /* Switch back to the original section.  */
		    subseg_set (seg, subseg);
		  }
	      }
	      continue;

	    case 'i':		/* 16 bit unsigned immediate */
	    case 'j':		/* 16 bit signed immediate */
	      imm_reloc = BFD_RELOC_LO16;
	      c = my_getSmallExpression (&imm_expr, s);
	      if (c)
		{
		  if (c != 'l')
		    {
		      if (imm_expr.X_op == O_constant)
			imm_expr.X_add_number =
			  (imm_expr.X_add_number >> 16) & 0xffff;
		      else if (c == 'h')
			imm_reloc = BFD_RELOC_HI16_S;
		      else
			imm_reloc = BFD_RELOC_HI16;
		    }
		}
	      else
		check_absolute_expr (ip, &imm_expr);
	      if (*args == 'i')
		{
		  if (imm_expr.X_add_number < 0
		      || imm_expr.X_add_number >= 0x10000)
		    {
		      if (insn + 1 < &mips_opcodes[NUMOPCODES] &&
			  !strcmp (insn->name, insn[1].name))
			break;
		      as_bad ("16 bit expression not in range 0..65535");
		    }
		}
	      else
		{
		  if (imm_expr.X_add_number < -0x8000 ||
		      imm_expr.X_add_number >= 0x8000)
		    {
		      if (insn + 1 < &mips_opcodes[NUMOPCODES] &&
			  !strcmp (insn->name, insn[1].name))
			break;
		      as_bad ("16 bit expression not in range -32768..32767");
		    }
		}
	      s = expr_end;
	      continue;

	    case 'o':		/* 16 bit offset */
	      c = my_getSmallExpression (&offset_expr, s);
	      /*
	       * If this value won't fit into a 16 bit offset, then
	       * go find a macro that will generate the 32 bit offset
	       * code pattern.
	       */
	      if (offset_expr.X_op != O_constant
		  || offset_expr.X_add_number >= 0x8000
		  || offset_expr.X_add_number < -0x8000)
		break;

	      offset_reloc = BFD_RELOC_LO16;
	      if (c == 'h' || c == 'H')
		{
		  assert (offset_expr.X_op == O_constant);
		  offset_expr.X_add_number =
		    (offset_expr.X_add_number >> 16) & 0xffff;
		}
	      s = expr_end;
	      continue;

	    case 'p':		/* pc relative offset */
	      offset_reloc = BFD_RELOC_16_PCREL_S2;
	      my_getExpression (&offset_expr, s);
	      s = expr_end;
	      continue;

	    case 'u':		/* upper 16 bits */
	      c = my_getSmallExpression (&imm_expr, s);
	      if (imm_expr.X_op != O_constant
		  || imm_expr.X_add_number < 0
		  || imm_expr.X_add_number >= 0x10000)
		as_bad ("lui expression not in range 0..65535");
	      imm_reloc = BFD_RELOC_LO16;
	      if (c)
		{
		  if (c != 'l')
		    {
		      if (imm_expr.X_op == O_constant)
			imm_expr.X_add_number =
			  (imm_expr.X_add_number >> 16) & 0xffff;
		      else if (c == 'h')
			imm_reloc = BFD_RELOC_HI16_S;
		      else
			imm_reloc = BFD_RELOC_HI16;
		    }
		}
	      s = expr_end;
	      continue;

	    case 'a':		/* 26 bit address */
	      my_getExpression (&offset_expr, s);
	      s = expr_end;
	      offset_reloc = BFD_RELOC_MIPS_JMP;
	      continue;

	    default:
	      fprintf (stderr, "bad char = '%c'\n", *args);
	      internalError ();
	    }
	  break;
	}
      /* Args don't match.  */
      if (insn + 1 < &mips_opcodes[NUMOPCODES] &&
	  !strcmp (insn->name, insn[1].name))
	{
	  ++insn;
	  s = argsStart;
	  continue;
	}
      insn_error = "ERROR: Illegal operands";
      return;
    }
}

#define LP '('
#define RP ')'

static int
my_getSmallExpression (ep, str)
     expressionS *ep;
     char *str;
{
  char *sp;
  int c = 0;

  if (*str == ' ')
    str++;
  if (*str == LP
      || (*str == '%' &&
	  ((str[1] == 'h' && str[2] == 'i')
	   || (str[1] == 'H' && str[2] == 'I')
	   || (str[1] == 'l' && str[2] == 'o'))
	  && str[3] == LP))
    {
      if (*str == LP)
	c = 0;
      else
	{
	  c = str[1];
	  str += 3;
	}

      /*
       * A small expression may be followed by a base register.
       * Scan to the end of this operand, and then back over a possible
       * base register.  Then scan the small expression up to that
       * point.  (Based on code in sparc.c...)
       */
      for (sp = str; *sp && *sp != ','; sp++)
	;
      if (sp - 4 >= str && sp[-1] == RP)
	{
	  if (isdigit (sp[-2]))
	    {
	      for (sp -= 3; sp >= str && isdigit (*sp); sp--)
		;
	      if (*sp == '$' && sp > str && sp[-1] == LP)
		{
		  sp--;
		  goto do_it;
		}
	    }
	  else if (sp - 5 >= str
		   && sp[-5] == LP
		   && sp[-4] == '$'
		   && ((sp[-3] == 'f' && sp[-2] == 'p')
		       || (sp[-3] == 's' && sp[-2] == 'p')
		       || (sp[-3] == 'g' && sp[-2] == 'p')
		       || (sp[-3] == 'a' && sp[-2] == 't')))
	    {
	      sp -= 5;
	    do_it:
	      if (sp == str)
		{
		  /* no expression means zero offset */
		  if (c)
		    {
		      /* %xx(reg) is an error */
		      ep->X_op = O_absent;
		      expr_end = str - 3;
		    }
		  else
		    {
		      ep->X_op = O_constant;
		      expr_end = sp;
		    }
		  ep->X_add_symbol = NULL;
		  ep->X_op_symbol = NULL;
		  ep->X_add_number = 0;
		}
	      else
		{
		  *sp = '\0';
		  my_getExpression (ep, str);
		  *sp = LP;
		}
	      return c;
	    }
	}
    }
  my_getExpression (ep, str);
  return c;			/* => %hi or %lo encountered */
}

static void
my_getExpression (ep, str)
     expressionS *ep;
     char *str;
{
  char *save_in;

  save_in = input_line_pointer;
  input_line_pointer = str;
  expression (ep);
  expr_end = input_line_pointer;
  input_line_pointer = save_in;
}

/* Turn a string in input_line_pointer into a floating point constant
   of type type, and store the appropriate bytes in *litP.  The number
   of LITTLENUMS emitted is stored in *sizeP .  An error message is
   returned, or NULL on OK.  */

char *
md_atof (type, litP, sizeP)
     int type;
     char *litP;
     int *sizeP;
{
  int prec;
  LITTLENUM_TYPE words[4];
  char *t;
  int i;

  switch (type)
    {
    case 'f':
      prec = 2;
      break;

    case 'd':
      prec = 4;
      break;

    default:
      *sizeP = 0;
      return "bad call to md_atof";
    }

  t = atof_ieee (input_line_pointer, type, words);
  if (t)
    input_line_pointer = t;

  *sizeP = prec * 2;

  if (byte_order == LITTLE_ENDIAN)
    {
      for (i = prec - 1; i >= 0; i--)
	{
	  md_number_to_chars (litP, (valueT) words[i], 2);
	  litP += 2;
	}
    }
  else
    {
      for (i = 0; i < prec; i++)
	{
	  md_number_to_chars (litP, (valueT) words[i], 2);
	  litP += 2;
	}
    }
     
  return NULL;
}

void
md_number_to_chars (buf, val, n)
     char *buf;
     valueT val;
     int n;
{
  switch (byte_order)
    {
    case LITTLE_ENDIAN:
      number_to_chars_littleendian (buf, val, n);
      break;

    case BIG_ENDIAN:
      number_to_chars_bigendian (buf, val, n);
      break;

    default:
      internalError ();
    }
}

int
md_parse_option (argP, cntP, vecP)
     char **argP;
     int *cntP;
     char ***vecP;
{
  /* Accept -nocpp but ignore it. */
  if (strcmp (*argP, "nocpp") == 0)
    {
      *argP += 5;
      return 1;
    }

  if (strcmp (*argP, "EL") == 0
      || strcmp (*argP, "EB") == 0)
    {
      /* FIXME: This breaks -L -EL.  */
      flagseen['L'] = 0;
      *argP = "";
      return 1;
    }

  if (**argP == 'O')
    {
      if ((*argP)[1] == '0')
	mips_optimize = 1;
      else
	mips_optimize = 2;
      return 1;
    }

  if (**argP == 'g')
    {
      if ((*argP)[1] == '\0' || (*argP)[1] == '2')
	mips_optimize = 0;
      return 1;
    }

  if (strncmp (*argP, "mips", 4) == 0)
    {
      mips_isa = atol (*argP + 4);
      if (mips_isa == 0)
	mips_isa = 1;
      else if (mips_isa < 1 || mips_isa > 3)
	{
	  as_bad ("-mips%d not supported", mips_isa);
	  mips_isa = 1;
	}
      *argP = "";
      return 1;
    }

  if (strncmp (*argP, "mcpu=", 5) == 0)
    {
      char *p;

      /* Identify the processor type */
      p = *argP + 5;
      if (strcmp (p, "default") == 0
	  || strcmp (p, "DEFAULT") == 0)
	mips_isa = -1;
      else
	{
	  if (*p == 'r' || *p == 'R')
	    p++;

	  mips_isa = -1;
	  switch (*p)
	    {
	    case '2':
	      if (strcmp (p, "2000") == 0
		  || strcmp (p, "2k") == 0
		  || strcmp (p, "2K") == 0)
		mips_isa = 1;
	      break;

	    case '3':
	      if (strcmp (p, "3000") == 0
		  || strcmp (p, "3k") == 0
		  || strcmp (p, "3K") == 0)
		mips_isa = 1;
	      break;

	    case '4':
	      if (strcmp (p, "4000") == 0
		  || strcmp (p, "4k") == 0
		  || strcmp (p, "4K") == 0)
		mips_isa = 3;
	      break;

	    case '6':
	      if (strcmp (p, "6000") == 0
		  || strcmp (p, "6k") == 0
		  || strcmp (p, "6K") == 0)
		mips_isa = 2;
	      break;
	    }

	  if (mips_isa == -1)
	    {
	      as_bad ("bad value (%s) for -mcpu= switch", *argP + 5);
	      mips_isa = 1;
	    }
	}

      *argP = "";
      return 1;
    }


#ifdef GPOPT
  if (**argP == 'G')
    {
      if ((*argP)[1] != '\0')
	g_switch_value = atoi (*argP + 1);
      else if (*cntP)
	{
	  **vecP = (char *) NULL;
	  (*cntP)--;
	  (*vecP)++;
	  g_switch_value = atoi (**vecP);
	}
      else
	as_warn ("Number expected after -G");
      *argP = "";
      return 1;
    }
#endif

  return 1;			/* pretend you parsed the character */
}

long
md_pcrel_from (fixP)
     fixS *fixP;
{
  /* return the address of the delay slot */
  return fixP->fx_size + fixP->fx_where + fixP->fx_frag->fr_address;
}

/* This is called by emit_expr via TC_CONS_FIX_NEW when creating a
   reloc for a cons.  We could use the definition there, except that
   we want to handle 64 bit relocs specially.  */

void
cons_fix_new_mips (frag, where, nbytes, exp)
     fragS *frag;
     int where;
     unsigned int nbytes;
     expressionS *exp;
{
  /* If we are assembling in 32 bit mode, turn an 8 byte reloc into a
     4 byte reloc.  
     FIXME: There is no way to select anything but 32 bit mode right
     now.  */
  if (nbytes == 8)
    {
      if (byte_order == BIG_ENDIAN)
	where += 4;
      nbytes = 4;
    }

  if (nbytes != 2 && nbytes != 4)
    as_bad ("Unsupported reloc size %d", nbytes);

  fix_new_exp (frag_now, where, (int) nbytes, exp, 0,
	       nbytes == 2 ? BFD_RELOC_16 : BFD_RELOC_32);
}

int
md_apply_fix (fixP, valueP)
     fixS *fixP;
     valueT *valueP;
{
  unsigned char *buf;
  long insn, value;

  assert (fixP->fx_size == 4);

  value = *valueP;
  fixP->fx_addnumber = value;	/* Remember value for tc_gen_reloc */

  switch (fixP->fx_r_type)
    {
    case BFD_RELOC_32:
    case BFD_RELOC_MIPS_JMP:
    case BFD_RELOC_HI16:
    case BFD_RELOC_HI16_S:
    case BFD_RELOC_LO16:
    case BFD_RELOC_MIPS_GPREL:
    case BFD_RELOC_MIPS_LITERAL:
    case BFD_RELOC_MIPS_CALL16:
    case BFD_RELOC_MIPS_GOT16:
    case BFD_RELOC_MIPS_GPREL32:
      /* Nothing needed to do. The value comes from the reloc entry */
      return 1;

    case BFD_RELOC_16_PCREL_S2:
      /*
       * We need to save the bits in the instruction since fixup_segment()
       * might be deleting the relocation entry (i.e., a branch within
       * the current segment).
       */
      if (value & 0x3)
	as_warn ("Branch to odd address (%lx)", value);
      value >>= 2;
      if ((value & ~0xFFFF) && (value & ~0xFFFF) != (-1 & ~0xFFFF))
	as_bad ("Relocation overflow");

      /* update old instruction data */
      buf = (unsigned char *) (fixP->fx_where + fixP->fx_frag->fr_literal);
      switch (byte_order)
	{
	case LITTLE_ENDIAN:
	  insn = (buf[3] << 24) | (buf[2] << 16) | (buf[1] << 8) | buf[0];
	  break;

	case BIG_ENDIAN:
	  insn = (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
	  break;

	default:
	  internalError ();
	  return 0;
	}
      insn |= value & 0xFFFF;
      md_number_to_chars ((char *) buf, (valueT) insn, 4);
      break;

    default:
      internalError ();
    }
  return 1;
}

#if 0
void
printInsn (oc)
     unsigned long oc;
{
  const struct mips_opcode *p;
  int treg, sreg, dreg, shamt;
  short imm;
  const char *args;
  int i;

  for (i = 0; i < NUMOPCODES; ++i)
    {
      p = &mips_opcodes[i];
      if (((oc & p->mask) == p->match) && (p->pinfo != INSN_MACRO))
	{
	  printf ("%08lx %s\t", oc, p->name);
	  treg = (oc >> 16) & 0x1f;
	  sreg = (oc >> 21) & 0x1f;
	  dreg = (oc >> 11) & 0x1f;
	  shamt = (oc >> 6) & 0x1f;
	  imm = oc;
	  for (args = p->args;; ++args)
	    {
	      switch (*args)
		{
		case '\0':
		  printf ("\n");
		  break;

		case ',':
		case '(':
		case ')':
		  printf ("%c", *args);
		  continue;

		case 'r':
		  assert (treg == sreg);
		  printf ("$%d,$%d", treg, sreg);
		  continue;

		case 'd':
		case 'G':
		  printf ("$%d", dreg);
		  continue;

		case 't':
		case 'E':
		  printf ("$%d", treg);
		  continue;

		case 'k':
		  printf ("0x%x", treg);
		  continue;

		case 'b':
		case 's':
		  printf ("$%d", sreg);
		  continue;

		case 'a':
		  printf ("0x%08lx", oc & 0x1ffffff);
		  continue;

		case 'i':
		case 'j':
		case 'o':
		case 'u':
		  printf ("%d", imm);
		  continue;

		case '<':
		case '>':
		  printf ("$%d", shamt);
		  continue;

		default:
		  internalError ();
		}
	      break;
	    }
	  return;
	}
    }
  printf ("%08lx  UNDEFINED\n", oc);
}
#endif

static symbolS *
get_symbol ()
{
  int c;
  char *name;
  symbolS *p;

  name = input_line_pointer;
  c = get_symbol_end ();
  p = (symbolS *) symbol_find_or_make (name);
  *input_line_pointer = c;
  return p;
}

/* Align the current frag to a given power of two.  The MIPS assembler
   also automatically adjusts any preceding label.  */

static void
mips_align (to, fill)
     int to;
     int fill;
{
  mips_emit_delays ();
  frag_align (to, fill);
  record_alignment (now_seg, to);
  if (insn_label != NULL)
    {
      assert (S_GET_SEGMENT (insn_label) == now_seg);
      insn_label->sy_frag = frag_now;
      S_SET_VALUE (insn_label, (valueT) frag_now_fix ());
      insn_label = NULL;
    }
}

/* Align to a given power of two.  .align 0 turns off the automatic
   alignment used by the data creating pseudo-ops.  */

static void
s_align (x)
     int x;
{
  register int temp;
  register long temp_fill;
  long max_alignment = 15;

  /*

    o  Note that the assembler pulls down any immediately preceeding label
       to the aligned address.
    o  It's not documented but auto alignment is reinstated by
       a .align pseudo instruction.
    o  Note also that after auto alignment is turned off the mips assembler
       issues an error on attempt to assemble an improperly aligned data item.
       We don't.

    */

  temp = get_absolute_expression ();
  if (temp > max_alignment)
    as_bad ("Alignment too large: %d. assumed.", temp = max_alignment);
  else if (temp < 0)
    {
      as_warn ("Alignment negative: 0 assumed.");
      temp = 0;
    }
  if (*input_line_pointer == ',')
    {
      input_line_pointer++;
      temp_fill = get_absolute_expression ();
    }
  else
    temp_fill = 0;
  if (temp)
    {
      auto_align = 1;
      mips_align (temp, (int) temp_fill);
    }
  else
    {
      auto_align = 0;
    }

  demand_empty_rest_of_line ();
}

/* Handle .ascii and .asciiz.  This just calls stringer and forgets
   that there was a previous instruction.  */

static void
s_stringer (append_zero)
     int append_zero;
{
  mips_emit_delays ();
  insn_label = NULL;
  stringer (append_zero);
}

static void
s_change_sec (sec)
     int sec;
{
#ifdef GPOPT
  segT seg;
#endif

  mips_emit_delays ();
  switch (sec)
    {
    case 't':
      s_text (0);
      break;
    case 'd':
      s_data (0);
      break;
    case 'b':
      subseg_set (bss_section, (subsegT) get_absolute_expression ());
      demand_empty_rest_of_line ();
      break;

    case 'r':
#ifdef OBJ_ECOFF
      subseg_new (".rdata", (subsegT) get_absolute_expression ());
      demand_empty_rest_of_line ();
      break;
#else /* ! defined (OBJ_ECOFF) */
#ifdef OBJ_ELF
      seg = subseg_new (".rodata", (subsegT) get_absolute_expression ());
      bfd_set_section_flags (stdoutput, seg,
			     (SEC_ALLOC
			      | SEC_LOAD
			      | SEC_READONLY
			      | SEC_RELOC
			      | SEC_DATA));
      bfd_set_section_alignment (stdoutput, seg, 4);
      demand_empty_rest_of_line ();
      break;
#else /* ! defined (OBJ_ELF) */
      s_data (0);
      break;
#endif /* ! defined (OBJ_ELF) */
#endif /* ! defined (OBJ_ECOFF) */

    case 's':
#ifdef GPOPT
      seg = subseg_new (".sdata", (subsegT) get_absolute_expression ());
#ifdef OBJ_ELF
      bfd_set_section_flags (stdoutput, seg,
			     SEC_ALLOC | SEC_LOAD | SEC_RELOC | SEC_DATA);
      bfd_set_section_alignment (stdoutput, seg, 4);
#endif
      demand_empty_rest_of_line ();
      break;
#else /* ! defined (GPOPT) */
      as_bad ("Global pointers not supported; recompile -G 0");
      demand_empty_rest_of_line ();
      return;
#endif /* ! defined (GPOPT) */
    }

  auto_align = 1;
}

static void
s_cons (log_size)
     int log_size;
{
  mips_emit_delays ();
  if (log_size > 0 && auto_align)
    mips_align (log_size, 0);
  insn_label = NULL;
  cons (1 << log_size);
}

static void
s_err (x)
     int x;
{
  as_fatal ("Encountered `.err', aborting assembly");
}

static void
s_extern (x)
     int x;
{
  valueT size;
  symbolS *symbolP;

  symbolP = get_symbol ();
  if (*input_line_pointer == ',')
    input_line_pointer++;
  size = get_absolute_expression ();
  S_SET_VALUE (symbolP, size);
  S_SET_EXTERNAL (symbolP);

#ifdef ECOFF_DEBUGGING
  /* ECOFF needs to distinguish a .comm symbol from a .extern symbol,
     so we use an additional ECOFF specific field.  */
  symbolP->ecoff_undefined = 1;
#endif
}

static void
s_float_cons (type)
     int type;
{
  mips_emit_delays ();

  if (auto_align)
    if (type == 'd')
      mips_align (3, 0);
    else
      mips_align (2, 0);

  insn_label = NULL;

  float_cons (type);
}

static void
s_option (x)
     int x;
{
  char *opt;
  char c;

  opt = input_line_pointer;
  c = get_symbol_end ();

  if (*opt == 'O')
    {
      /* FIXME: What does this mean?  */
    }
  else if (strncmp (opt, "pic", 3) == 0)
    {
      mips_pic = atoi (opt + 3);
      /* Supposedly no other values are used.  */
      assert (mips_pic == 0 || mips_pic == 2);
    }
  else
    as_warn ("Unrecognized option \"%s\"", opt);

  *input_line_pointer = c;
  demand_empty_rest_of_line ();
}

static void
s_mipsset (x)
     int x;
{
  char *name = input_line_pointer, ch;

  while (!is_end_of_line[(unsigned char) *input_line_pointer])
    input_line_pointer++;
  ch = *input_line_pointer;
  *input_line_pointer = '\0';

  if (strcmp (name, "reorder") == 0)
    {
      if (mips_noreorder)
	{
	  prev_insn_unreordered = 1;
	  prev_prev_insn_unreordered = 1;
	}
      mips_noreorder = 0;
    }
  else if (strcmp (name, "noreorder") == 0)
    {
      mips_emit_delays ();
      mips_noreorder = 1;
      mips_any_noreorder = 1;
    }
  else if (strcmp (name, "at") == 0)
    {
      mips_noat = 0;
    }
  else if (strcmp (name, "noat") == 0)
    {
      mips_noat = 1;
    }
  else if (strcmp (name, "macro") == 0)
    {
      mips_warn_about_macros = 0;
    }
  else if (strcmp (name, "nomacro") == 0)
    {
      if (mips_noreorder == 0)
	as_bad ("`noreorder' must be set before `nomacro'");
      mips_warn_about_macros = 1;
    }
  else if (strcmp (name, "move") == 0 || strcmp (name, "novolatile") == 0)
    {
      mips_nomove = 0;
    }
  else if (strcmp (name, "nomove") == 0 || strcmp (name, "volatile") == 0)
    {
      mips_nomove = 1;
    }
  else if (strcmp (name, "bopt") == 0)
    {
      mips_nobopt = 0;
    }
  else if (strcmp (name, "nobopt") == 0)
    {
      mips_nobopt = 1;
    }
  else
    {
      as_warn ("Tried to set unrecognized symbol: %s\n", name);
    }
  *input_line_pointer = ch;
  demand_empty_rest_of_line ();
}

/* The same as the usual .space directive, except that we have to
   forget about any previous instruction.  */

static void
s_mips_space (param)
     int param;
{
  mips_emit_delays ();
  insn_label = NULL;
  s_space (param);
}

/* Handle the .abicalls pseudo-op.  I believe this is equivalent to
   .option pic2.  It means to generate SVR4 PIC calls.  */

static void
s_abicalls (ignore)
     int ignore;
{
  mips_pic = 2;
  demand_empty_rest_of_line ();
}

/* Handle the .cpload pseudo-op.  This is used when generating SVR4
   PIC code.  It sets the $gp register for the function based on the
   function address, which is in the register named in the argument.
   This uses a relocation against _gp_disp, which is handled specially
   by the linker.  The result is:
	lui	$gp,%hi(_gp_disp)
	addiu	$gp,$gp,%lo(_gp_disp)
	addu	$gp,$gp,.cpload argument
   The .cpload argument is normally $25 == $t9.  */

static void
s_cpload (ignore)
     int ignore;
{
  expressionS ex;
  int icnt = 0;

  /* If we are not generating PIC code, .cpload is ignored.  */
  if (mips_pic == 0)
    {
      s_ignore (0);
      return;
    }

  /* .cpload should be a in .set noreorder section.  */
  if (mips_noreorder == 0)
    as_warn (".cpload not in noreorder section");

  ex.X_op = O_symbol;
  ex.X_add_symbol = symbol_find_or_make ("_gp_disp");
  ex.X_op_symbol = NULL;
  ex.X_add_number = 0;

  macro_build_lui ((char *) NULL, &icnt, &ex, GP);
  macro_build ((char *) NULL, &icnt, &ex, "addiu", "t,r,j", GP, GP,
	       (int) BFD_RELOC_LO16);

  macro_build ((char *) NULL, &icnt, (expressionS *) NULL, "addu", "d,v,t",
	       GP, GP, tc_get_register (0));

  demand_empty_rest_of_line ();
}

/* Handle the .cprestore pseudo-op.  This stores $gp into a given
   offset from $sp.  The offset is remembered, and after making a PIC
   call $gp is restored from that location.  */

static void
s_cprestore (ignore)
     int ignore;
{
  expressionS ex;
  int icnt = 0;

  /* If we are not generating PIC code, .cprestore is ignored.  */
  if (mips_pic == 0)
    {
      s_ignore (0);
      return;
    }

  mips_cprestore_offset = get_absolute_expression ();

  ex.X_op = O_constant;
  ex.X_add_symbol = NULL;
  ex.X_op_symbol = NULL;
  ex.X_add_number = mips_cprestore_offset;

  macro_build ((char *) NULL, &icnt, &ex,
	       mips_isa < 3 ? "sw" : "sd",
	       "t,o(b)", GP, (int) BFD_RELOC_LO16, SP);

  demand_empty_rest_of_line ();
}

/* Handle the .gpword pseudo-op.  This is used when generating PIC
   code.  It generates a 32 bit GP relative reloc.  */

static void
s_gpword (ignore)
     int ignore;
{
  expressionS ex;
  char *p;

  /* When not generating PIC code, this is treated as .word.  */
  if (mips_pic == 0)
    {
      s_cons (2);
      return;
    }

  mips_emit_delays ();
  if (auto_align)
    mips_align (2, 0);
  insn_label = NULL;

  expression (&ex);

  if (ex.X_op != O_symbol || ex.X_add_number != 0)
    {
      as_bad ("Unsupported use of .gpword");
      ignore_rest_of_line ();
    }

  p = frag_more (4);
  md_number_to_chars (p, (valueT) 0, 4);
  fix_new_exp (frag_now, p - frag_now->fr_literal, 4, &ex, 0,
	       BFD_RELOC_MIPS_GPREL32);

  demand_empty_rest_of_line ();
}

/* Handle the .cpadd pseudo-op.  This is used when dealing with switch
   tables in SVR4 PIC code.  */

static void
s_cpadd (ignore)
     int ignore;
{
  int icnt = 0;
  int reg;

  /* This is ignored when not generating SVR4 PIC code.  */
  if (mips_pic == 0)
    {
      s_ignore (0);
      return;
    }

  /* Add $gp to the register named as an argument.  */
  reg = tc_get_register (0);
  macro_build ((char *) NULL, &icnt, (expressionS *) NULL,
	       mips_isa < 3 ? "addu" : "daddu",
	       "d,v,t", reg, reg, GP);

  demand_empty_rest_of_line ();  
}

/* Parse a register string into a number.  Called from the ECOFF code
   to parse .frame.  The argument is non-zero if this is the frame
   register, so that we can record it in mips_frame_reg.  */

int
tc_get_register (frame)
     int frame;
{
  int reg;

  SKIP_WHITESPACE ();
  if (*input_line_pointer++ != '$')
    {
      as_warn ("expected `$'");
      reg = 0;
    }
  else if (isdigit ((unsigned char) *input_line_pointer))
    {
      reg = get_absolute_expression ();
      if (reg < 0 || reg >= 32)
	{
	  as_warn ("Bad register number");
	  reg = 0;
	}
    }
  else
    {
      if (strncmp (input_line_pointer, "fp", 2) == 0)
	reg = FP;
      else if (strncmp (input_line_pointer, "sp", 2) == 0)
	reg = SP;
      else if (strncmp (input_line_pointer, "gp", 2) == 0)
	reg = GP;
      else if (strncmp (input_line_pointer, "at", 2) == 0)
	reg = AT;
      else
	{
	  as_warn ("Unrecognized register name");
	  reg = 0;
	}
      input_line_pointer += 2;
    }
  if (frame)
    mips_frame_reg = reg != 0 ? reg : SP;
  return reg;
}

valueT
md_section_align (seg, addr)
     asection *seg;
     valueT addr;
{
  int align = bfd_get_section_alignment (stdoutput, seg);

  return ((addr + (1 << align) - 1) & (-1 << align));
}

/* Estimate the size of a frag before relaxing.  We are not really
   relaxing here, and the final size is encoded in the subtype
   information.  */

/*ARGSUSED*/
int
md_estimate_size_before_relax (fragp, segtype)
     fragS *fragp;
     asection *segtype;
{
  int change;

  if (mips_pic == 0)
    {
#ifdef GPOPT
      const char *symname;

      /* Find out whether this symbol can be referenced off the GP
	 register.  It can be if it is smaller than the -G size or if
	 it is in the .sdata or .sbss section.  Certain symbols can
	 not be referenced off the GP, although it appears as though
	 they can.  */
      symname = S_GET_NAME (fragp->fr_symbol);
      if (symname != (const char *) NULL
	  && (strcmp (symname, "eprol") == 0
	      || strcmp (symname, "etext") == 0
	      || strcmp (symname, "_gp") == 0
	      || strcmp (symname, "edata") == 0
	      || strcmp (symname, "_fbss") == 0
	      || strcmp (symname, "_fdata") == 0
	      || strcmp (symname, "_ftext") == 0
	      || strcmp (symname, "end") == 0
	      || strcmp (symname, "_gp_disp") == 0))
	change = 1;
      else if (! S_IS_DEFINED (fragp->fr_symbol)
	       && S_GET_VALUE (fragp->fr_symbol) != 0
	       && S_GET_VALUE (fragp->fr_symbol) <= g_switch_value)
	change = 0;
      else
	{
	  const char *segname;

	  segname = segment_name (S_GET_SEGMENT (fragp->fr_symbol));
	  assert (strcmp (segname, ".lit8") != 0
		  && strcmp (segname, ".lit4") != 0);
	  change = (strcmp (segname, ".sdata") != 0
		    && strcmp (segname, ".sbss") != 0);
	}
#else /* ! defined (GPOPT) */
      /* We are not optimizing for the GP register.  */
      change = 1;
#endif /* ! defined (GPOPT) */  
    }
  else
    {
      asection *symsec = fragp->fr_symbol->bsym->section;

      /* This must duplicate the test in adjust_reloc_syms.  */
      change = (symsec != &bfd_und_section
		&& symsec != &bfd_abs_section
		&& ! bfd_is_com_section (symsec));
    }

  if (change)
    {
      /* Record the offset to the first reloc in the fr_opcode field.
	 This lets md_convert_frag and tc_gen_reloc know that the code
	 must be expanded.  */
      fragp->fr_opcode = (fragp->fr_literal
			  + fragp->fr_fix
			  - RELAX_OLD (fragp->fr_subtype)
			  + RELAX_RELOC1 (fragp->fr_subtype));
      /* FIXME: This really needs as_warn_where.  */
      if (RELAX_WARN (fragp->fr_subtype))
	as_warn ("AT used after \".set noat\" or macro used after \".set nomacro\"");
    }

  if (! change)
    return 0;
  else
    return RELAX_NEW (fragp->fr_subtype) - RELAX_OLD (fragp->fr_subtype);
}

/* Translate internal representation of relocation info to BFD target
   format.  */

arelent **
tc_gen_reloc (section, fixp)
     asection *section;
     fixS *fixp;
{
  static arelent *retval[4];
  arelent *reloc;

  reloc = retval[0] = (arelent *) xmalloc (sizeof (arelent));
  retval[1] = NULL;

  reloc->sym_ptr_ptr = &fixp->fx_addsy->bsym;
  reloc->address = fixp->fx_frag->fr_address + fixp->fx_where;
  if (fixp->fx_pcrel == 0)
    reloc->addend = fixp->fx_addnumber;
  else
#ifdef OBJ_ELF
    reloc->addend = 0;
#else
    reloc->addend = -reloc->address;
#endif

  /* If this is a variant frag, we may need to adjust the existing
     reloc and generate a new one.  */
  if (fixp->fx_frag->fr_opcode != NULL
      && (fixp->fx_r_type == BFD_RELOC_MIPS_GPREL
	  || fixp->fx_r_type == BFD_RELOC_MIPS_GOT16
	  || fixp->fx_r_type == BFD_RELOC_MIPS_CALL16))
    {
      arelent *reloc2;

      /* If this is not the last reloc in this frag, then we have two
	 GPREL relocs, both of which are being replaced.  Let the
	 second one handle all of them.  */
      if (fixp->fx_next != NULL
	  && fixp->fx_frag == fixp->fx_next->fx_frag)
	{
	  assert (fixp->fx_r_type == BFD_RELOC_MIPS_GPREL
		  && fixp->fx_next->fx_r_type == BFD_RELOC_MIPS_GPREL);
	  retval[0] = NULL;
	  return retval;
	}

      fixp->fx_where = fixp->fx_frag->fr_opcode - fixp->fx_frag->fr_literal;
      reloc->address = fixp->fx_frag->fr_address + fixp->fx_where;
      reloc2 = retval[1] = (arelent *) xmalloc (sizeof (arelent));
      retval[2] = NULL;
      reloc2->sym_ptr_ptr = &fixp->fx_addsy->bsym;
      reloc2->address = (reloc->address
			 + (RELAX_RELOC2 (fixp->fx_frag->fr_subtype)
			    - RELAX_RELOC1 (fixp->fx_frag->fr_subtype)));
      reloc2->addend = fixp->fx_addnumber;
      reloc2->howto = bfd_reloc_type_lookup (stdoutput, BFD_RELOC_LO16);
      assert (reloc2->howto != NULL);

      if (RELAX_RELOC3 (fixp->fx_frag->fr_subtype))
	{
	  arelent *reloc3;

	  reloc3 = retval[2] = (arelent *) xmalloc (sizeof (arelent));
	  retval[3] = NULL;
	  *reloc3 = *reloc2;
	  reloc3->address += 4;
	}

      if (mips_pic == 0)
	{
	  assert (fixp->fx_r_type == BFD_RELOC_MIPS_GPREL);
	  fixp->fx_r_type = BFD_RELOC_HI16_S;
	}
      else
	{
	  if (fixp->fx_r_type != BFD_RELOC_MIPS_GOT16)
	    {
	      assert (fixp->fx_r_type == BFD_RELOC_MIPS_CALL16);
	      fixp->fx_r_type = BFD_RELOC_MIPS_GOT16;
	    }
	}
    }

  reloc->howto = bfd_reloc_type_lookup (stdoutput, fixp->fx_r_type);

  if (reloc->howto == NULL)
    {
      as_bad_where (fixp->fx_file, fixp->fx_line,
		    "Can not represent relocation in this object file format");
      retval[0] = NULL;
    }

  return retval;
}

/* Convert a machine dependent frag.  */

void
md_convert_frag (abfd, asec, fragp)
     bfd *abfd;
     segT asec;
     fragS *fragp;
{
  int old, new;
  char *fixptr;

  if (fragp->fr_opcode == NULL)
    return;

  old = RELAX_OLD (fragp->fr_subtype);
  new = RELAX_NEW (fragp->fr_subtype);
  fixptr = fragp->fr_literal + fragp->fr_fix;

  if (new > 0)
    memcpy (fixptr - old, fixptr, new);

  fragp->fr_fix += new - old;
}

/* This function is called whenever a label is defined.  It is used
   when handling branch delays; if a branch has a label, we assume we
   can not move it.  */

void
mips_define_label (sym)
     symbolS *sym;
{
  insn_label = sym;
}

#ifdef OBJ_ELF

/* Some special processing for a MIPS ELF file.  */

void
mips_elf_final_processing ()
{
  Elf32_RegInfo s;

  /* Write out the .reginfo section.  */
  s.ri_gprmask = mips_gprmask;
  s.ri_cprmask[0] = mips_cprmask[0];
  s.ri_cprmask[1] = mips_cprmask[1];
  s.ri_cprmask[2] = mips_cprmask[2];
  s.ri_cprmask[3] = mips_cprmask[3];
  /* The gp_value field is set by the MIPS ELF backend.  */

  bfd_mips_elf32_swap_reginfo_out (stdoutput, &s,
				   ((Elf32_External_RegInfo *)
				    mips_regmask_frag));

  /* Set the MIPS ELF flag bits.  FIXME: There should probably be some
     sort of BFD interface for this.  */
  if (mips_any_noreorder)
    elf_elfheader (stdoutput)->e_flags |= EF_MIPS_NOREORDER;
  if (mips_pic != 0)
    elf_elfheader (stdoutput)->e_flags |= EF_MIPS_PIC;
}

#endif /* OBJ_ELF */

#ifndef ECOFF_DEBUGGING

/* These functions should really be defined by the object file format,
   since they are related to debugging information.  However, this
   code has to work for the a.out format, which does not define them,
   so we provide simple versions here.  These don't actually generate
   any debugging information, but they do simple checking and someday
   somebody may make them useful.  */

typedef struct loc
{
  struct loc *loc_next;
  unsigned long loc_fileno;
  unsigned long loc_lineno;
  unsigned long loc_offset;
  unsigned short loc_delta;
  unsigned short loc_count;
#if 0
  fragS *loc_frag;
#endif
}
locS;

typedef struct proc
  {
    struct proc *proc_next;
    struct symbol *proc_isym;
    struct symbol *proc_end;
    unsigned long proc_reg_mask;
    unsigned long proc_reg_offset;
    unsigned long proc_fpreg_mask;
    unsigned long proc_fpreg_offset;
    unsigned long proc_frameoffset;
    unsigned long proc_framereg;
    unsigned long proc_pcreg;
    locS *proc_iline;
    struct file *proc_file;
    int proc_index;
  }
procS;

typedef struct file
  {
    struct file *file_next;
    unsigned long file_fileno;
    struct symbol *file_symbol;
    struct symbol *file_end;
    struct proc *file_proc;
    int file_numprocs;
  }
fileS;

static struct obstack proc_frags;
static procS *proc_lastP;
static procS *proc_rootP;
static int numprocs;

static void
md_obj_begin ()
{
  obstack_begin (&proc_frags, 0x2000);
}

static void
md_obj_end ()
{
  /* check for premature end, nesting errors, etc */
  if (proc_lastP && proc_lastP->proc_end == NULL)
    as_warn ("missing `.end' at end of assembly");
}

extern char hex_value[];

static long
get_number ()
{
  int negative = 0;
  long val = 0;

  if (*input_line_pointer == '-')
    {
      ++input_line_pointer;
      negative = 1;
    }
  if (!isdigit (*input_line_pointer))
    as_bad ("Expected simple number.");
  if (input_line_pointer[0] == '0')
    {
      if (input_line_pointer[1] == 'x')
	{
	  input_line_pointer += 2;
	  while (isxdigit (*input_line_pointer))
	    {
	      val <<= 4;
	      val |= hex_value[(int) *input_line_pointer++];
	    }
	  return negative ? -val : val;
	}
      else
	{
	  ++input_line_pointer;
	  while (isdigit (*input_line_pointer))
	    {
	      val <<= 3;
	      val |= *input_line_pointer++ - '0';
	    }
	  return negative ? -val : val;
	}
    }
  if (!isdigit (*input_line_pointer))
    {
      printf (" *input_line_pointer == '%c' 0x%02x\n",
	      *input_line_pointer, *input_line_pointer);
      as_warn ("Invalid number");
      return -1;
    }
  while (isdigit (*input_line_pointer))
    {
      val *= 10;
      val += *input_line_pointer++ - '0';
    }
  return negative ? -val : val;
}

/* The .file directive; just like the usual .file directive, but there
   is an initial number which is the ECOFF file index.  */

static void
s_file (x)
     int x;
{
  int line;

  line = get_number ();
  s_app_file (0);
}


/* The .end directive.  */

static void
s_mipsend (x)
     int x;
{
  symbolS *p;

  if (!is_end_of_line[(unsigned char) *input_line_pointer])
    {
      p = get_symbol ();
      demand_empty_rest_of_line ();
    }
  else
    p = NULL;
  if (now_seg != text_section)
    as_warn (".end not in text section");
  if (!proc_lastP)
    {
      as_warn (".end and no .ent seen yet.");
      return;
    }

  if (p != NULL)
    {
      assert (S_GET_NAME (p));
      if (strcmp (S_GET_NAME (p), S_GET_NAME (proc_lastP->proc_isym)))
	as_warn (".end symbol does not match .ent symbol.");
    }

  proc_lastP->proc_end = (symbolS *) 1;
}

/* The .aent and .ent directives.  */

static void
s_ent (aent)
     int aent;
{
  int number = 0;
  procS *procP;
  symbolS *symbolP;

  symbolP = get_symbol ();
  if (*input_line_pointer == ',')
    input_line_pointer++;
  SKIP_WHITESPACE ();
  if (isdigit (*input_line_pointer) || *input_line_pointer == '-')
    number = get_number ();
  if (now_seg != text_section)
    as_warn (".ent or .aent not in text section.");

  if (!aent && proc_lastP && proc_lastP->proc_end == NULL)
    as_warn ("missing `.end'");

  if (!aent)
    {
      procP = (procS *) obstack_alloc (&proc_frags, sizeof (*procP));
      procP->proc_isym = symbolP;
      procP->proc_reg_mask = 0;
      procP->proc_reg_offset = 0;
      procP->proc_fpreg_mask = 0;
      procP->proc_fpreg_offset = 0;
      procP->proc_frameoffset = 0;
      procP->proc_framereg = 0;
      procP->proc_pcreg = 0;
      procP->proc_end = NULL;
      procP->proc_next = NULL;
      if (proc_lastP)
	proc_lastP->proc_next = procP;
      else
	proc_rootP = procP;
      proc_lastP = procP;
      numprocs++;
    }
  demand_empty_rest_of_line ();
}

/* The .frame directive.  */

#if 0
static void
s_frame (x)
     int x;
{
  char str[100];
  symbolS *symP;
  int frame_reg;
  int frame_off;
  int pcreg;

  frame_reg = tc_get_register (1);
  if (*input_line_pointer == ',')
    input_line_pointer++;
  frame_off = get_absolute_expression ();
  if (*input_line_pointer == ',')
    input_line_pointer++;
  pcreg = tc_get_register (0);

  /* bob third eye */
  assert (proc_rootP);
  proc_rootP->proc_framereg = frame_reg;
  proc_rootP->proc_frameoffset = frame_off;
  proc_rootP->proc_pcreg = pcreg;
  /* bob macho .frame */

  /* We don't have to write out a frame stab for unoptimized code. */
  if (!(frame_reg == FP && frame_off == 0))
    {
      if (!proc_lastP)
	as_warn ("No .ent for .frame to use.");
      (void) sprintf (str, "R%d;%d", frame_reg, frame_off);
      symP = symbol_new (str, N_VFP, 0, frag_now);
      S_SET_TYPE (symP, N_RMASK);
      S_SET_OTHER (symP, 0);
      S_SET_DESC (symP, 0);
      symP->sy_forward = proc_lastP->proc_isym;
      /* bob perhaps I should have used pseudo set */
    }
  demand_empty_rest_of_line ();
}
#endif

/* The .fmask and .mask directives.  */

#if 0
static void
s_mask (reg_type)
     char reg_type;
{
  char str[100], *strP;
  symbolS *symP;
  int i;
  unsigned int mask;
  int off;

  mask = get_number ();
  if (*input_line_pointer == ',')
    input_line_pointer++;
  off = get_absolute_expression ();

  /* bob only for coff */
  assert (proc_rootP);
  if (reg_type == 'F')
    {
      proc_rootP->proc_fpreg_mask = mask;
      proc_rootP->proc_fpreg_offset = off;
    }
  else
    {
      proc_rootP->proc_reg_mask = mask;
      proc_rootP->proc_reg_offset = off;
    }

  /* bob macho .mask + .fmask */

  /* We don't have to write out a mask stab if no saved regs. */
  if (!(mask == 0))
    {
      if (!proc_lastP)
	as_warn ("No .ent for .mask to use.");
      strP = str;
      for (i = 0; i < 32; i++)
	{
	  if (mask % 2)
	    {
	      sprintf (strP, "%c%d,", reg_type, i);
	      strP += strlen (strP);
	    }
	  mask /= 2;
	}
      sprintf (strP, ";%d,", off);
      symP = symbol_new (str, N_RMASK, 0, frag_now);
      S_SET_TYPE (symP, N_RMASK);
      S_SET_OTHER (symP, 0);
      S_SET_DESC (symP, 0);
      symP->sy_forward = proc_lastP->proc_isym;
      /* bob perhaps I should have used pseudo set */
    }
}
#endif

/* The .loc directive.  */

#if 0
static void
s_loc (x)
     int x;
{
  symbolS *symbolP;
  int lineno;
  int addroff;

  assert (now_seg == text_section);

  lineno = get_number ();
  addroff = obstack_next_free (&frags) - frag_now->fr_literal;

  symbolP = symbol_new ("", N_SLINE, addroff, frag_now);
  S_SET_TYPE (symbolP, N_SLINE);
  S_SET_OTHER (symbolP, 0);
  S_SET_DESC (symbolP, lineno);
  symbolP->sy_segment = now_seg;
}
#endif

#endif /* ! defined (ECOFF_DEBUGGING) */
