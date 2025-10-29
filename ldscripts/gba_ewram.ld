/*
 * SPDX-License-Identifier: CC0-1.0
 *
 * SPDX-FileContributor: Antonio Niño Díaz, 2022
 * SPDX-FileContributor: David Guillen Fandos, 2024
 */

/*
 * Some links with information about linker scripts:
 *
 * - https://refspecs.linuxbase.org/LSB_3.1.1/LSB-Core-generic/LSB-Core-generic/specialsections.html
 *
 * - https://blog.thea.codes/the-most-thoroughly-commented-linker-script/
 *
 * - http://beefchunk.com/documentation/sys-programming/binary_formats/elf/elf_from_the_programmers_perspective/node4.html
 *
 * Adapted for EWRAM-only usage (as a bootloader / loader)
 * The assumption is that a small bootstrap/loader code will copy this into
 * EWRAM and execute it from there (so no header is really needed).
 *
 */

OUTPUT_FORMAT("elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(entrypoint)

MEMORY {
    /* Run from a EWRAM mirror (for better NDS support!) */
    IWRAM   : ORIGIN = 0x03000000, LENGTH = 32K
    /* Limit fw to 251KB, so we can fit NDS mode */
    EWRAM   : ORIGIN = 0x027C0000, LENGTH = 251K
}

/* Set stack pointers the same way as the BIOS */

__STACK_SVC_SIZE__      = 0x40;
__STACK_IRQ_SIZE__      = 0xA0;
__STACK_USR_SIZE_MIN__  = 0x4000; /* Minimum size, the real size may be bigger */

__STACK_END__       = ORIGIN(IWRAM) + LENGTH(IWRAM) - 0x20; /* Used by BIOS */
__STACK_SVC_END__   = __STACK_END__;
__STACK_SVC_START__ = __STACK_SVC_END__ - __STACK_SVC_SIZE__;
__STACK_IRQ_END__   = __STACK_SVC_START__;
__STACK_IRQ_START__ = __STACK_IRQ_END__ - __STACK_IRQ_SIZE__;
__STACK_USR_END__   = __STACK_IRQ_START__;
__STACK_USR_START__ = __STACK_USR_END__ - __STACK_USR_SIZE_MIN__;
__STACK_START__     = __STACK_USR_START__;

SECTIONS
{
    /*
     * ROM sections (placed in EWRAM)
     * ==============================
     */

    /* Header and crt0 */

    .gba_ewram_crt0 : ALIGN(4)
    {
        KEEP (*(.gba_ewram_crt0))
    } > EWRAM

    /* Code */

    .text : ALIGN(4)
    {
        *(.text)
        *(.text*)
        *(.gnu.linkonce.t.*) /* Used for vague linking */

        /* ARM/Thumb interworking code */
        *(.glue_7)  /* glue arm to thumb code */
        *(.glue_7t) /* glue thumb to arm code */

        /* Array of functions to be called at the start of the program */
        KEEP(*(.init))
        /* Array of functions to be called at the end of the program */
        KEEP(*(.fini))
    } > EWRAM

    /* Read-only data */

    .rodata : ALIGN(4)
    {
        *(.ewram)
        *(.rodata)
        *(.rodata*)
        *(.gnu.linkonce.r.*) /* Used for vague linking */
    } > EWRAM

   /*
    * Required for C++ and for C programs that try to examine backtraces. Each
    * function that can throw exceptions has entries in exidx and extab.
    *
    * - exidx is used to contain index entries for stack unwinding.
    * - extab names sections containing exception unwinding information.
    */

    .ARM.extab : ALIGN(4)
    {
        *(.ARM.extab* .gnu.linkonce.armextab.*)
    } > EWRAM

    .ARM.exidx : ALIGN(4)
    {
        PROVIDE_HIDDEN(__exidx_start = .);
        *(.ARM.exidx* .gnu.linkonce.armexidx.*)
        PROVIDE_HIDDEN(__exidx_end = .);
    } > EWRAM

    /* Array of functions to be called during initialization of the program */

    .preinit_array : ALIGN(4)
    {
        __preinit_array_start = .;
        KEEP(*(.preinit_array*))
        __preinit_array_end = .;
    } > EWRAM

    /*
     * Array of functions to be called during initialization of the program.
     * They are called after calling everything in .preinit_array
     */

    .init_array : ALIGN(4)
    {
        __init_array_start = .;
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array*))
        __init_array_end = .;
    } > EWRAM

    /* Array of functions called when the program ends. */

    .fini_array : ALIGN(4)
    {
        __fini_array_start = .;
        KEEP(*(SORT(.fini_array.*)))
        KEEP(*(.fini_array*))
        __fini_array_end = .;
    } > EWRAM

    /*
     * IWRAM sections
     * ==============
     */

    /* Uninitialized variables */

    .bss (NOLOAD) : ALIGN(4)
    {
        *(.bss)
        *(.bss*)
        *(.gnu.linkonce.b.*) /* Used for vague linking */
        *(COMMON) /* In case -fcommon is used in gcc (not used by default) */
        . = ALIGN(4);
    } > IWRAM

    __IWRAM_BSS_START__ = ADDR(.bss);
    __IWRAM_BSS_SIZE__ = SIZEOF(.bss);
    __IWRAM_BSS_END__ = __IWRAM_BSS_START__ + __IWRAM_BSS_SIZE__;

    /* Read-write data goes into IWRAM */

    .data : ALIGN(4)
    {
        __DATA_START__ = .;
        *(.data)
        *(.data*)
        *(.gnu.linkonce.d.*) /* Used for vague linking */
        . = ALIGN(4);
        __DATA_END__ = .;
    } > IWRAM AT > EWRAM

    __DATA_SIZE__ = __DATA_END__ - __DATA_START__;
    __DATA_LMA__ = LOADADDR(.data);

    /* Sections that the user requests to add to IWRAM */

    .iwram : ALIGN(4)
    {
        __IWRAM_START__ = .;
        *(.iwram)
        *(.iwram*)
        *iwram.*(.text*)
        *iwram.*(.data*)
        . = ALIGN(4);
        __IWRAM_END__ = .;
    } > IWRAM AT > EWRAM

    __IWRAM_SIZE__ = __IWRAM_END__ - __IWRAM_START__;
    __IWRAM_LMA__ = LOADADDR(.iwram);

    /* The stack goes afterwards, check that there aren't overflows */

    ASSERT(__IWRAM_END__ <= __STACK_START__,
           "Not enough free IWRAM for stack")

    /* Calculate real size of the user stack */

    __STACK_USR_SIZE__ = __STACK_USR_END__ - __IWRAM_END__;

    /*
     * EWRAM sections
     * ==============
     */

    /*
     * Uninitialized data explicitly placed in EWRAM. This section must be
     * called ".sbss" for compatibility with devkitARM.
     */

    .sbss (NOLOAD) : ALIGN(4)
    {
        *(.sbss)
        *(.sbss*)
        . = ALIGN(4);
    } > EWRAM

    /* No heap defined nor used here */
}

#include "validate.inc"

