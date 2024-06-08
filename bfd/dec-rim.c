#include "sysdep.h"
#include "bfd.h"
#include "libbfd.h"
#include "libiberty.h"
#include "safe-ctype.h"

#define DEC_RIM_LEADER_LENGTH 240
#define DEC_RIM_PADDING_LENGTH 5

struct dec_rim_data_list_struct
{
  struct dec_rim_data_list_struct *next;
  bfd_byte *data;
  bfd_vma where;
  bfd_size_type size;
};

typedef struct dec_rim_data_list_struct dec_rim_data_list_type;

typedef struct dec_rim_data_struct
{
  dec_rim_data_list_type *head;
  dec_rim_data_list_type *tail;
}
tdata_type;

static bool
dec_rim_set_section_contents (bfd *abfd,
			      asection *sec,
			      const void * where ATTRIBUTE_UNUSED,
			      file_ptr offset,
			      bfd_size_type size)
{
  dec_rim_data_list_type *entry;
  tdata_type *tdata = abfd->tdata.dec_rim_data;
  bfd_byte *data;

  if (size == 0
      || (sec->flags & SEC_ALLOC) == 0
      || (sec->flags & SEC_LOAD) == 0)
    return true;

  entry = (dec_rim_data_list_type *) bfd_alloc (abfd, sizeof (* entry));
  if (entry == NULL)
    return false;

  data = (bfd_byte *) bfd_alloc (abfd, size * 2); /* DIO/JMP + INSTR for every line */
  if (data == NULL)
    return false;

  bfd_byte *saved_data = data;
  for (bfd_size_type i = 0; i < size; i++)
    {
      /* write DIO + addr */
      int instr = 0320000 + sec->lma + offset + i;
      *(bfd_byte *)(data++) = ((instr >> 12) & 0000077) | 0x80;
      *(bfd_byte *)(data++) = ((instr >> 6) & 0000077) | 0x80;
      *(bfd_byte *)(data++) = ((instr >> 0) & 0000077) | 0x80;


      /* write instr */
      instr = 0;
      instr |= (*(bfd_byte *)(where++) << 16) & 0x0f0000;
      instr |= (*(bfd_byte *)(where++) << 8) & 0x00ff00;
      instr |= (*(bfd_byte *)(where++) << 0) & 0x0000ff;
      *(bfd_byte *)(data++) = ((instr >> 12) & 0000077) | 0x80;
      *(bfd_byte *)(data++) = ((instr >> 6) & 0000077) | 0x80;
      *(bfd_byte *)(data++) = ((instr >> 0) & 0000077) | 0x80;
    }

  entry->data = saved_data;
  entry->where = sec->lma + offset;
  entry->size = size * 2;

  /* sort records by address */
  tdata = abfd->tdata.dec_rim_data;
  if (tdata->tail != NULL
      && entry->where >= tdata->tail->where)
    {
      tdata->tail->next = entry;
      entry->next = NULL;
      tdata->tail = entry;
    }
  else
    {
      dec_rim_data_list_type **look;
      
      for (look = &tdata->head;
           *look != NULL && (*look)->where < entry->where;
           look = &(*look)->next)
        ;
      entry->next = *look;
      *look = entry;
      if (entry->next == NULL)
        tdata->tail = entry;
    }

  return true;
}

static bool
dec_rim_write_object_contents (bfd *abfd)
{
  char buffer[256];

  tdata_type *tdata = abfd->tdata.dec_rim_data;
  dec_rim_data_list_type *list;

  /* write leader - 240 zeroes, which on the physical tape should roughly equal
   * to 2 ft of tape (10 zeroes per inch) */
  memset(buffer, 0, DEC_RIM_LEADER_LENGTH);
  if (!bfd_write (buffer, DEC_RIM_LEADER_LENGTH, abfd))
      return false;

  /* start from the 2nd entry; rim is already handled */
  for (list = tdata->head; list != NULL; list = list->next)
    {
      if (!bfd_write (list->data, list->size, abfd))
        return false;
    }

  /* write JMP 300 */
  /* TODO: which addr should we jump to? */
  int instr = 0600000 + 300;
  buffer[0] = ((instr >> 12) & 0000077) | 0x80;
  buffer[1] = ((instr >> 6) & 0000077) | 0x80;
  buffer[2] = ((instr >> 0) & 0000077) | 0x80;
  if (!bfd_write (buffer, 3, abfd))
      return false;

  /* write padding - 5 lines of zeroes */
  memset(buffer, 0, DEC_RIM_PADDING_LENGTH);
  if (!bfd_write (buffer, DEC_RIM_PADDING_LENGTH, abfd))
      return false;

  return true;
}

/* Set up the dec_rim tdata information.  */

static bool
dec_rim_mkobject (bfd *abfd)
{
  tdata_type *tdata;

  tdata = (tdata_type *) bfd_alloc (abfd, sizeof (tdata_type));
  if (tdata == NULL)
    return false;

  abfd->tdata.dec_rim_data = tdata;
  tdata->head = NULL;
  tdata->tail = NULL;
  
  return true;
}

#define dec_rim_close_and_cleanup		     _bfd_generic_close_and_cleanup
#define dec_rim_bfd_free_cached_info		     _bfd_generic_bfd_free_cached_info
#define dec_rim_new_section_hook		     _bfd_generic_new_section_hook
#define dec_rim_bfd_is_target_special_symbol	     _bfd_bool_bfd_asymbol_false
#define dec_rim_bfd_is_local_label_name		     bfd_generic_is_local_label_name
#define dec_rim_get_lineno			     _bfd_nosymbols_get_lineno
#define dec_rim_find_nearest_line		     _bfd_nosymbols_find_nearest_line
#define dec_rim_find_nearest_line_with_alt	     _bfd_nosymbols_find_nearest_line_with_alt
#define dec_rim_find_inliner_info		     _bfd_nosymbols_find_inliner_info
#define dec_rim_make_empty_symbol		     _bfd_generic_make_empty_symbol
#define dec_rim_bfd_make_debug_symbol		     _bfd_nosymbols_bfd_make_debug_symbol
#define dec_rim_read_minisymbols		     _bfd_generic_read_minisymbols
#define dec_rim_minisymbol_to_symbol		     _bfd_generic_minisymbol_to_symbol
#define dec_rim_get_section_contents_in_window	     _bfd_generic_get_section_contents_in_window
#define dec_rim_bfd_get_relocated_section_contents   bfd_generic_get_relocated_section_contents
#define dec_rim_bfd_relax_section		     bfd_generic_relax_section
#define dec_rim_bfd_gc_sections			     bfd_generic_gc_sections
#define dec_rim_bfd_merge_sections		     bfd_generic_merge_sections
#define dec_rim_bfd_is_group_section		     bfd_generic_is_group_section
#define dec_rim_bfd_group_name			     bfd_generic_group_name
#define dec_rim_bfd_discard_group		     bfd_generic_discard_group
#define dec_rim_section_already_linked		     _bfd_generic_section_already_linked
#define dec_rim_bfd_link_hash_table_create	     _bfd_generic_link_hash_table_create
#define dec_rim_bfd_link_add_symbols		     _bfd_generic_link_add_symbols
#define dec_rim_bfd_link_just_syms		     _bfd_generic_link_just_syms
#define dec_rim_bfd_final_link			     _bfd_generic_final_link
#define dec_rim_bfd_link_split_section		     _bfd_generic_link_split_section
#define dec_rim_set_arch_mach			     _bfd_generic_set_arch_mach

const bfd_target dec_rim_vec =
{
  "dec_rim",			/* Name.  */
  bfd_target_dec_rim_flavour,
  BFD_ENDIAN_UNKNOWN,		/* Target byte order.  */
  BFD_ENDIAN_UNKNOWN,		/* Target headers byte order.  */
  (HAS_RELOC | EXEC_P |		/* Object flags.  */
   HAS_LINENO | HAS_DEBUG |
   HAS_SYMS | HAS_LOCALS | WP_TEXT | D_PAGED),
  (SEC_CODE | SEC_DATA | SEC_ROM | SEC_HAS_CONTENTS
   | SEC_ALLOC | SEC_LOAD | SEC_RELOC),	/* Section flags.  */
  0,				/* Leading underscore.  */
  ' ',				/* AR_pad_char.  */
  16,				/* AR_max_namelen.  */
  0,				/* match priority.  */
  TARGET_KEEP_UNUSED_SECTION_SYMBOLS, /* keep unused section symbols.  */
  bfd_getb64, bfd_getb_signed_64, bfd_putb64,
  bfd_getb32, bfd_getb_signed_32, bfd_putb32,
  bfd_getb16, bfd_getb_signed_16, bfd_putb16,	/* Data.  */
  bfd_getb64, bfd_getb_signed_64, bfd_putb64,
  bfd_getb32, bfd_getb_signed_32, bfd_putb32,
  bfd_getb16, bfd_getb_signed_16, bfd_putb16,	/* Hdrs.  */

  {
    _bfd_dummy_target,
    _bfd_dummy_target,
    _bfd_dummy_target,
    _bfd_dummy_target,
  },
  {
    _bfd_bool_bfd_false_error,
    dec_rim_mkobject,
    _bfd_bool_bfd_false_error,
    _bfd_bool_bfd_false_error,
  },
  {				/* bfd_write_contents.  */
    _bfd_bool_bfd_false_error,
    dec_rim_write_object_contents,
    _bfd_bool_bfd_false_error,
    _bfd_bool_bfd_false_error,
  },

  BFD_JUMP_TABLE_GENERIC (_bfd_generic),
  BFD_JUMP_TABLE_COPY (_bfd_generic),
  BFD_JUMP_TABLE_CORE (_bfd_nocore),
  BFD_JUMP_TABLE_ARCHIVE (_bfd_noarchive),
  BFD_JUMP_TABLE_SYMBOLS (_bfd_nosymbols),
  BFD_JUMP_TABLE_RELOCS (_bfd_norelocs),
  BFD_JUMP_TABLE_WRITE (dec_rim),
  BFD_JUMP_TABLE_LINK (_bfd_nolink),
  BFD_JUMP_TABLE_DYNAMIC (_bfd_nodynamic),

  NULL,

  NULL
};
