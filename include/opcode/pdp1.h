#define PDP1_INSTR_MEMREF	0
#define PDP1_INSTR_IMM		1
#define PDP1_INSTR_SHIFT	2
#define PDP1_INSTR_SKIP_GENERIC 3
#define PDP1_INSTR_SKIP		4
#define PDP1_INSTR_SKIP_FLAG	5
#define PDP1_INSTR_SKIP_SWITCH	6
#define PDP1_INSTR_OP		7
#define PDP1_INSTR_OP_FLAG	8
#define PDP1_INSTR_OP_SWITCH	9
#define PDP1_INSTR_IO		10

typedef struct pdp1_opc_info_t
{
  const char *name;
  int opcode;
  int mask;
  int type;
} pdp1_opc_info_t;
