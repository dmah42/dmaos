ENTRY(start)

SECTIONS {
  . = 0x1000000;

  .text :{
    KEEP(*(.text.start));
    *(.text .text.*);
  }

  .rodata : ALIGN(8) {
    *(.rodata .rodata.*);
  }

  .data : ALIGN(8) {
    *(.data .data.*);
  }

  .bss : ALIGN(8) {
    __bss = .;
    *(.bss .bss.* .sbss .sbss.*);
    __bss_end = .;
    . = ALIGN(16);
    . += 64 * 1024;  /* 64KB */
    __stack_top = .;

    ASSERT(. < 0x1800000, "executable too large");
  }
}