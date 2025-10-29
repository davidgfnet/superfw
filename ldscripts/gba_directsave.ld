/*
 * SPDX-License-Identifier: CC0-1.0
 *
 * SPDX-FileContributor: Antonio Niño Díaz, 2022
 * SPDX-FileContributor: David Guillen Fandos, 2024
 */

OUTPUT_FORMAT("elf32-littlearm")
OUTPUT_ARCH(arm)

/* Link at 0x0 for easy relocation (if needed) */

MEMORY {
    ROM     : ORIGIN = 0x00000000, LENGTH = 7K
}

ASSERT(__bss_start - __bss_end == 0, "bss section should be empty")
/* No GOT section should exist (LTO should optimize it all away). */
ASSERT(__got_start - __got_end == 0, "got section should be empty")

SECTIONS
{
    .directsave : ALIGN(4)
    {
        __BINARY_START__ = .;
        KEEP (*(.directsave))
    } > ROM

    /* Actual Code */

    .text : ALIGN(4)
    {
        *(.text)
        *(.text*)
        *(.iwram)
        *(.iwram*)
        *iwram.*(.text*)
        *(.gnu.linkonce.t.*) /* Used for vague linking */
    } > ROM

    /* Read-only Data */

    .rodata : ALIGN(4)
    {
        *(.rodata)
        *(.rodata*)
    } > ROM

    /* We should not generate any GOT data, since is is linked with LTO */
    __got_start = .;
    .got :
    {
        *(.got.plt)
        *(.got)
        *(.rel.got)
    } > ROM
    __got_end = .;

    /* Data */
    .data : ALIGN(4)
    {
        *iwram.*(.data*)
        *(.data)
        *(.data*)
        . = ALIGN(4);
    } > ROM

    /* BSS should also be empty when using LTO */
    .bss : ALIGN(4)
    {
        __bss_start = .;
        *(.bss)
        *(.bss*)
        __bss_end = .;
    } > ROM

    __BINARY_END__ = .;
    __BINARY_SIZE__ = __BINARY_END__ - __BINARY_START__;
}
