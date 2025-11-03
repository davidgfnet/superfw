/*
 * SPDX-License-Identifier: CC0-1.0
 *
 * SPDX-FileContributor: Antonio Niño Díaz, 2022
 * SPDX-FileContributor: David Guillen Fandos, 2025
 */

OUTPUT_FORMAT("elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(startup_entrypoint)

/* The IGM uses EWRAM for rodata/text and IWRAM for everything else
 * including some code. Stack is also placed in IWRAM. */
MEMORY {
    SDRAM     : ORIGIN = 0x00000000, LENGTH = 1K
}

SECTIONS
{
    /* just a tiny payload for trampolining! */

    .text : ALIGN(4)
    {
        *(.text)
    } > SDRAM
}

ASSERT(startup_entrypoint          ==    0, "startup_entrypoint should be at address 0x0")
ASSERT(trampoline_nocheats_handler ==  8*4, "trampoline_nocheats_handler should be at address 32")
ASSERT(trampoline_cheats_handler   == 32*4, "trampoline_cheats_handler should be at address 0x80")
ASSERT(trampoline_code_end         == 56*4, "trampoline_code_end should be at address 0xE0")


