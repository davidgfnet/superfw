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
 * This is just for rom_boot.S, only text is really supported
 *
 */

OUTPUT_FORMAT("elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(_start)

MEMORY {
    ROM   : ORIGIN = 0x08000000, LENGTH = MAX_FLASH_SIZE
}

/* The first stage must be under 1KiB, so it is properly mapped. */
ASSERT(_main_end - _start <= 1024, "Bootloader 1st stage exceeds 1KiB")
/* The bootloader should use less than 3KiB. */
ASSERT(_end_bootloader - _start <= 3*1024, "Bootloader exceeds 3KiB")

SECTIONS
{
    .text : ALIGN(4)
    {
        *(.text)
    } > ROM
}

