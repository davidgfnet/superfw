/*
 * SPDX-License-Identifier: CC0-1.0
 *
 * SPDX-FileContributor: Antonio Niño Díaz, 2022
 * SPDX-FileContributor: David Guillen Fandos, 2024
 */

OUTPUT_FORMAT("elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(ingame_menu)

/* The IGM uses EWRAM for rodata/text and IWRAM for everything else
 * including some code. Stack is also placed in IWRAM. */
MEMORY {
    IWRAM     : ORIGIN = 0x03000000, LENGTH = 4K    /* Extra 12KB are used for stack */
    EWRAM     : ORIGIN = 0x02000000, LENGTH = 60K
}

SECTIONS
{
    /* in-game menu, entry point at the top, followed by the assembly code */

    .ingame_menu_start : ALIGN(4)
    {
        __BINARY_START__ = .;
        __EWRAM_START__ = .;
        KEEP (*(.ingame_menu_start))
    } > EWRAM

    /* C Code */

    .text : ALIGN(4)
    {
        *(.text)
        *(.text*)
        *(.gnu.linkonce.t.*) /* Used for vague linking */
    } > EWRAM

    /* Read-only Data */

    .rodata : ALIGN(4)
    {
        *(.rodata)
        *(.rodata*)
        . = ALIGN(4);
        __EWRAM_END__ = .;
    } > EWRAM

    /* Code on IWRAM */
    .iwram : ALIGN(4)
    {
        __IWRAM_START__ = .;
        *(.iwram)
        *(.iwram*)
        *iwram.*(.text*)
        . = ALIGN(4);
    } > IWRAM AT > EWRAM

    /* Data */
    .data : ALIGN(4)
    {
        __DATAS_START__ = .;
        *iwram.*(.data*)
        *(.data)
        *(.data*)
        *(.bss)
        *(.bss*)
        . = ALIGN(4);
        __DATAS_END__ = .;
        __IWRAM_END__ = .;
    } > IWRAM AT > EWRAM

    .end : ALIGN(4)
    {
    } > IWRAM AT > EWRAM

    /* Whole binary size */
    __BINARY_SIZE__ = LOADADDR(.end) - (__BINARY_START__);

    /* Useful variables for the crt0-like loader */
    __EWRAM_SIZE__ = __EWRAM_END__ - __EWRAM_START__;
    __IWRAM_SIZE__ = __IWRAM_END__ - __IWRAM_START__;
}

ASSERT(ingame_menu             == 0x02000000, "ingame_menu should be at address 0x02000000")
ASSERT(igm_trampoline_nocheats == 0x02000020, "igm_trampoline_nocheats should be at address 0x02000020")
ASSERT(igm_trampoline_cheats   == 0x02000030, "igm_trampoline_nocheats should be at address 0x02000030")

