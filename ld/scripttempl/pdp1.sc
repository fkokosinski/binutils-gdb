TORS=".tors :
  {
    ___ctors = . ;
    *(.ctors)
    ___ctors_end = . ;
    ___dtors = . ;
    *(.dtors)
    ___dtors_end = . ;
  } > ram"

cat <<EOF
OUTPUT_FORMAT("${OUTPUT_FORMAT}")
OUTPUT_ARCH(${ARCH})

MEMORY
{
  ram (rwx) : ORIGIN = 0x0, LENGTH = 64K
}

SECTIONS
{
  .text :
  {
    *(.text)
    ${RELOCATING+KEEP (*(SORT_NONE(.init)))
    KEEP (*(SORT_NONE(.fini)))
    *(.strings)
    _etext = . ; }
  } ${RELOCATING+ > ram}
  ${CONSTRUCTING+${TORS}}
  .data :
  {
    *(.data)
    ${RELOCATING+ _edata = . ; }
  } ${RELOCATING+ > ram}
  .bss :
  {
    ${RELOCATING+ _bss_start = . ; }
    *(.bss)
    *(COMMON)
    ${RELOCATING+ _end = . ;  }
  } ${RELOCATING+ > ram}
  .stack ${RELOCATING+ 0x300 }  :
  {
    ${RELOCATING+ _stack = . ; }
    *(.stack)
  } ${RELOCATING+ > ram}
  .stab 0 ${RELOCATING+(NOLOAD)} :
  {
    *(.stab)
  }
  .stabstr 0 ${RELOCATING+(NOLOAD)} :
  {
    *(.stabstr)
  }
}
EOF
