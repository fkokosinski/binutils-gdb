#define PDP1_INSTR_MEMREF	0
#define PDP1_INSTR_IMM		1
#define PDP1_INSTR_SHIFT	2
#define PDP1_INSTR_SKIP		3
#define PDP1_INSTR_SKIP_FLAG	4
#define PDP1_INSTR_SKIP_SWITCH	5
#define PDP1_INSTR_OP		6
#define PDP1_INSTR_OP_FLAG	7
#define PDP1_INSTR_OP_SWITCH	8
#define PDP1_INSTR_IO		9

typedef struct pdp1_opc_info_t
{
  const char *name;
  int opcode;
  int mask;
  int type;
} pdp1_opc_info_t;
