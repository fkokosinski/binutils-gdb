#include "sysdep.h"
#include <stdio.h>
#define STATIC_TABLE
#define DEFINE_TABLE

#include "opcode/pdp1.h"
#include "dis-asm.h"

extern const pdp1_opc_info_t pdp1_opc_info[128];

static fprintf_ftype fpr;
static void *stream;

int 
print_insn_pdp1 (bfd_vma addr, struct disassemble_info *info)
{
  int status;
  int word;
  int i;
  int operand;
  pdp1_opc_info_t pdp1_opc;
  bfd_byte buffer[3];
  stream = info->stream;
  fpr = info->fprintf_func;

  if ((status = info->read_memory_func (addr, buffer, 3, info)))
    goto fail;

  word = bfd_getb24 (buffer);

  for (i = 0; i < 128; i++)
    {
      pdp1_opc = pdp1_opc_info[i];

      if ((word & pdp1_opc.mask) == pdp1_opc.opcode)
        {
	  operand = word & ~pdp1_opc.mask;

	  switch (pdp1_opc.type)
	    {
              case PDP1_INSTR_MEMREF:
              case PDP1_INSTR_SKIP_FLAG:
              case PDP1_INSTR_OP_FLAG:
              case PDP1_INSTR_SKIP_GENERIC:
	        fpr (stream, "%s %d", pdp1_opc.name, operand);
		break;
	      case PDP1_INSTR_IMM:
		if (operand & 0010000)
		  operand = -(operand & 0007777);
	        fpr (stream, "%s %d", pdp1_opc.name, operand);
		break;
	      case PDP1_INSTR_SHIFT:
		int n;
		operand = operand + 1;
		for (n = 0; operand % 2 == 0; operand >>= 1, n++)
		  ;
	        fpr (stream, "%s %d", pdp1_opc.name, n);
		break;
              case PDP1_INSTR_SKIP:
              case PDP1_INSTR_OP:
              case PDP1_INSTR_IO:
		fpr (stream, "%s", pdp1_opc.name);
		break;
	      case PDP1_INSTR_SKIP_SWITCH:
	      case PDP1_INSTR_OP_SWITCH:
	        fpr (stream, "%s %d", pdp1_opc.name, operand >> 3);
		break;
	      default:
		goto fail;
	    }
	  break;
        }
    }

  return 3;

 fail:
  info->memory_error_func (status, addr, info);
  return -1;
}
