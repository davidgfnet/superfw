/*
 * Copyright (C) 2024 David Guillen Fandos <david@davidgf.net>
 *
 * This program is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see
 * <http://www.gnu.org/licenses/>.
 */

@ Wrapper/Bootloader for firmware.ewram
@ Wraps the EWRAM payload around a ROM (linked at 0x08000000) and boots it by
@ copying the payload into the EWRAM and executing it from there.

@ The layout is as follows:
@
@ The EWRAM payload is copied at 0x027C0000 (abusing ewram mirrors!).
@ Assets are copied at 0x09E00000 (last 1MB of ROM/SDRAM), compressed
@ Built-in patch DB is copied at 0x09D00000, uncompressed.
@ No IWRAM is initialized (that is the ewram-payload responsibility).

@ Incorporates NDS support, which similarly boots firmware.ewram but using
@ the ARM9 CPU. The ARM7 is left behind waiting for instructions to boot an
@ NDS file. In this mode only basic NDS boot functionality is used.

#define GBA_SWIDTH    240
#define GBA_SHEIGHT   160
#define NDS_SWIDTH    256
#define NDS_SHEIGHT   192

#define NDS_WRAM_MIRROR  0x06000000     /* Temp WRAM storage area (using VRAM-D) */
#define BOOTMIRROR_ADDR  0x023FEC00     /* Bootloader relocation (NDS mode) */

#define BOOTNDS_SP7_SVC  0x023FFE00     /* From 0x023FFD80 to 0x023FFE00 */
#define BOOTNDS_SP9_SVC  0x023FFD80     /* From 0x023FFD00 to 0x023FFD80 */
#define BOOTNDS_SP9_IRQ  0x023FFD00     /* From 0x023FFC80 to 0x023FFD00 */
#define BOOTNDS_SP9_SYS  0x023FFC80     /* From 0x023FF800 to 0x023FFC80 */

#define FWPAYLOAD_ADDR   0x027C0000

#define NDS_HEADER_ADDR  0x027FFE00
#define WRAM_BOOTM_ADDR  0x03800000     /* WRAM were boot code is copied */
#define GAMEPAK_BASEADDR 0x08000000

#define FONT_BASE_ADDR   0x08F00000     @ Unpacked font glyph database
#define HISCRATCH_ADDR   0x09000000     @ Place here any temporary data
#define PATCH_DB_ADDR    0x09D00000     @ Where patch db lives
#define ASSETS_ADDR      0x09E00000     @ Where assets (emu/menu) live

#define REG_DISPCNT      0x04000000
#define REG_BG2CNT       0x0400000C
#define REG_BG2PA        0x04000020
#define REG_BG2PB        0x04000022
#define REG_BG2PC        0x04000024
#define REG_BG2PD        0x04000026
#define REG_BG2X         0x04000028
#define REG_BG2Y         0x0400002C

#define REG_WRAMCNT      0x04000247
#define REG_EXMEMCNT     0x04000204
#define REG_DISPCNT_A    0x04000000
#define REG_DISPCNT_B    0x04001000
#define REG_POWERCNT     0x04000304
#define REG_VRAMCNT_ABCD 0x04000240

#define REG_IPCSYNC      0x04000180

@ Waits on IPCSYNC and sleeps a few microseconds every time before checking.
#define WAIT_SYNC(value, wcnt)  \
  1:                            \
    mov r0, $(wcnt);            \
    swi 0x00030000;             \
                                \
    ldr r0, =REG_IPCSYNC;       \
    ldr r1, [r0];               \
    and r1, $0xF;               \
    cmp r1, $(value);           \
    bne 1b

#define SET_SYNC(value)         \
  ldr r0, =REG_IPCSYNC;         \
  mov r1, $(value << 8);        \
  str r1, [r0];


@ This is mainly to allow for easy ROM manipulation without having to worry
@ about mappings having custom handlers in IWRAM.

.section    .text, "ax"
.globl _start
.globl _main_end
.globl _end_bootloader
.cpu        arm7tdmi

.arm

_start:
  b entry_point

  @ Nintendo Logo Character Data
  .byte 0x24,0xFF,0xAE,0x51,0x69,0x9A,0xA2,0x21,0x3D,0x84,0x82,0x0A,0x84,0xE4,0x09,0xAD
  .byte 0x11,0x24,0x8B,0x98,0xC0,0x81,0x7F,0x21,0xA3,0x52,0xBE,0x19,0x93,0x09,0xCE,0x20
  .byte 0x10,0x46,0x4A,0x4A,0xF8,0x27,0x31,0xEC,0x58,0xC7,0xE8,0x33,0x82,0xE3,0xCE,0xBF
  .byte 0x85,0xF4,0xDF,0x94,0xCE,0x4B,0x09,0xC1,0x94,0x56,0x8A,0xC0,0x13,0x72,0xA7,0xFC
  .byte 0x9F,0x84,0x4D,0x73,0xA3,0xCA,0x9A,0x61,0x58,0x97,0xA3,0x27,0xFC,0x03,0x98,0x76
  .byte 0x23,0x1D,0xC7,0x61,0x03,0x04,0xAE,0x56,0xBF,0x38,0x84,0x00,0x40,0xA7,0x0E,0xFD
  .byte 0xFF,0x52,0xFE,0x03,0x6F,0x95,0x30,0xF1,0x97,0xFB,0xC0,0x85,0x60,0xD6,0x80,0x25
  .byte 0xA9,0x63,0xBE,0x03,0x01,0x4E,0x38,0xE2,0xF9,0xA2,0x34,0xFF,0xBB,0x3E,0x03,0x44
  .byte 0x78,0x00,0x90,0xCB,0x88,0x11,0x3A,0x94,0x65,0xC0,0x7C,0x63,0x87,0xF0,0x3C,0xAF
  .byte 0xD6,0x25,0xE4,0x8B,0x38,0x0A,0xAC,0x72,0x21,0xD4,0xF8,0x07

  @ Game Title (80000A0h): SUPERFW
  .byte   0x53,0x55,0x50,0x45,0x52,0x46,0x57,0x00,0x00,0x00,0x00,0x00

  @ Game Code (80000ACh)
  .byte   0x5A,0x53,0x46,0x57

  @ Maker Code (80000B0h)
  .byte   0x44,0x44

  @ Fixed Value (80000B2h)
  .byte   0x96

  @ Main Unit Code (80000B3h)
  .byte   0x00

  @ Device Type (80000B4h)
  .byte   0x00

  @ Unused Data (7Byte) (80000B5h)
  .byte   0x00,0x00,0x00,0x00,0x00,0x00,0x00

  @ Software Version No (80000BCh)
  .byte   0x00

  @ Complement Check (80000BDh)
  .byte   0x00

  @ Checksum (80000BEh)
  .byte   0x00,0x00

entry_point:
  b main                       @ Must be valid code (for passme...)
  .word VERSION_WORD           @ Version (as a 32 bit word)
  .word VERSION_SLUG_WORD      @ Commit ID as 32 bits
  .word 0                      @ 32 bit firmware file size (patched)
  .fill 32, 1, 0               @ 32 byte hash (patched)
  .asciz "SUPERFW~DAVIDGF"     @ Name/author

main:
  @ Basic GBA+NDS common HW initialization
  mov r1, $0x04000000
  strb r1, [r1, #0x208]        @ Disable interrupts!

  @ Copy the boot code to IWRAM so we can properly page flash and sdram as required.
  @ We copy 1KB first (since the mapping can be borked after that).
  @ On NDS it is copied to the ARM7-WRAM (on GBA we just abuse mirroring)
  @ Do not use pool loads in this first KiB!
  mov r0, $GAMEPAK_BASEADDR
  mov r1, $WRAM_BOOTM_ADDR
  mov r2, $(1024 / 4)
  swi 0x0C0000

  @ Resume execution at IWRAM, do the re-offset trick.
  mov r1, $(GAMEPAK_BASEADDR - WRAM_BOOTM_ADDR)
  sub pc, pc, r1
  nop; nop;

  @ Figure out whether this payload is running on FW flash or on SDRAM.
  @ Place the mode (0x4 for flash or 0x5 for sdram) in r12
  mov r0, $GAMEPAK_BASEADDR
  mov r1, $63
  mov r12, $0x4          @ Assume booting from flash
  ldmia r0, {r4-r7}

  1:
    add r0, $(512*1024)
    ldmia r0, {r8-r11}   @ Compare the first 16 bytes of each 512KB block.
    cmp   r4, r8
    cmpeq r5, r9
    cmpeq r6, r10
    cmpeq r7, r11
    movne r12, $0x5      @ Running from SDRAM, set flag, break loop.
    bne 2f

    subs r1, $1
    bne 1b
  2:

  @ Unconditionally set the guessed mode, to ensure the map is not bonkers
  @ (like in the Flash case when WE=0, I suspect a CPLD bug)
  mov r0, $0x0A000000
  mov r1, $0x5A
  orr r1, $0xA500
  strh r1, [r0, #-2]
  strh r1, [r0, #-2]
  strh r12, [r0, #-2]
  strh r12, [r0, #-2]

  @ Now proceed to copy the full bootloader into IWRAM, not just the first KB.
  mov r0, $GAMEPAK_BASEADDR
  mov r1, $WRAM_BOOTM_ADDR
  mov r2, $(((_end_bootloader - _start + 15) / 16) * 4)   @ Round it up to fit
  swi 0x0C0000
  nop; nop; nop; nop

_main_end:

  @ Check if we are running on GBA or NDS, since the startup changes significantly.
  @ EWRAM (at 0x02000000) is 256KiB on GBA but 4MB on DS, (ab)use mirroring.
  @ Check if mem[0x02000000] == mem[0x02040000].
  mov r0, $0x02000000
  add r1, r0, $(256*1024)
  ldr r2, =0xDEADBEEF
  ldr r3, =0xBADC0DE1
  str r2, [r0]           @ these two positions should be the same in GBA
  str r3, [r1]           @ on NDS these are two different memory positions tho.

  ldr r2, [r0]
  ldr r3, [r1]
  cmp r2, r3
  bne nds_boot_detected  @ They are different, therefore we have an NDS device


  @ Initialize the GBA screen and display the logo while we boot/unpack

  @ Disable GBA screen for now
  mov r8, $REG_DISPCNT
  mov r3, $0x80
  strh r3, [r8]
  @ Clear the screen to zero (background color)
  mov r0, $0x8
  swi 0x10000
  @ Copy palette
  adr r0, boot_logo_pal
  mov r1, $0x05000000
  ldm r0, {r2-r5}
  stm r1, {r2-r5}
  @ Render the logo, scaled by 4 byte-word.
  adr r0, boot_logo
  ldr r1, =(0x06000000 + (GBA_SWIDTH-31*4)/2 + ((GBA_SHEIGHT-7*4)/2) * GBA_SWIDTH)
  mov r3, $7
  1:
    mov r2, $16
    2:
      ldrb r4, [r0], #1    @ Copy to 4 columns
      mov r5, r4, lsr #4
      orr r5, r5, lsl #8
      orr r5, r5, lsl #16
      str r5, [r1, #720]   @ Copy to four rows
      str r5, [r1, #480]
      str r5, [r1, #240]
      str r5, [r1], #4
      and r5, r4, $0xF
      orr r5, r5, lsl #8
      orr r5, r5, lsl #16
      str r5, [r1, #720]   @ Copy to four rows
      str r5, [r1, #480]
      str r5, [r1, #240]
      str r5, [r1], #4
      subs r2, $1
      bne 2b
    add r1, $(GBA_SWIDTH-32*4 + GBA_SWIDTH*3)
    subs r3, $1
    bne 1b

  @ Setup BG2 regs, ensure it has some sensible state
  ldr r0, =REG_BG2CNT
  mov r1, $0      @ Base block, palette mode, etc
  strh r1, [r0]

  @ Setup bitmap offset regs
  mov r1, $0
  mov r2, $0x0100
  ldr r0, =REG_BG2PA; strh r2, [r0]
  ldr r0, =REG_BG2PB; strh r1, [r0]
  ldr r0, =REG_BG2PC; strh r1, [r0]
  ldr r0, =REG_BG2PD; strh r2, [r0]
  ldr r0, =REG_BG2X ; str r1, [r0]
  ldr r0, =REG_BG2Y ; str r1, [r0]

  @ Enable screen again. Bitmap 8bit mode
  ldr r1, =0x0404    // Mode 4, BG2 enabled
  strh r1, [r8]

  @ Uncompress the font glyphs. This supports any size.
  ldr r0, =font_start
  mov r1, $HISCRATCH_ADDR
  mov r2, $((font_end - font_start + 2047) / 2048)
  bl sdram_copy                        @ Copy compressed fonts to scratch area.
  mov r0, $0x5
  bl set_sd_mode                       @ Map SDRAM so we can unpack it.
  mov r0, $HISCRATCH_ADDR
  mov r1, $FONT_BASE_ADDR
  bl apunpack16                        @ Unpack font assets

  @ Uncompress the patch database. The uncompressed payload must be < 256KiB!
  mov r0, r12
  bl set_sd_mode                       @ Switch to source map
  ldr r1, =patches_start
  mov r0, $0x02000000
  bl upkr_unpack                       @ Decompress to EWRAM
  add r4, r0, $3
  mov r0, $0x5
  bl set_sd_mode                       @ Switch to SDRAM map
  mov r0, $0x02000000
  mov r1, $PATCH_DB_ADDR
  lsr r2, r4, $2                       @ Number of words to copy
  swi 0x0C0000                         @ Copy decompressed data to SDRAM

  @ Go ahead and copy assets to SDRAM locations.
  ldr r0, =assets_start
  mov r1, $ASSETS_ADDR
  mov r2, $((assets_end - assets_start + 2047) / 2048)
  bl sdram_copy

  @ Now we can proceed to unpack the firmware to EWRAM
  mov r0, r12
  bl set_sd_mode                       @ Switch to source map

  ldr r0, =FWPAYLOAD_ADDR
  ldr r1, =payload_ewram
  bl upkr_unpack

  @ Proceed to launch/boot the actual firmware!
  @ Reset to EWRAM using SoftReset
  mov r0, $0x04000000
  mov r1, $1
  strb r1, [r0, #-6]   @ Flag stored at 0x03007FFA
  swi 0x0              @ Reset to RAM

boot_logo:
  .byte 0x33,0x33,0x33,0x33,0x33,0x33,0x33,0x33,0x33,0x33,0x22,0x22,0x22,0x22,0x22,0x20
  .byte 0x33,0x11,0x31,0x31,0x31,0x13,0x31,0x13,0x11,0x33,0x21,0x11,0x21,0x22,0x21,0x20
  .byte 0x31,0x33,0x31,0x31,0x31,0x31,0x31,0x33,0x13,0x13,0x21,0x22,0x21,0x22,0x21,0x20
  .byte 0x31,0x11,0x31,0x31,0x31,0x31,0x31,0x13,0x13,0x13,0x21,0x12,0x21,0x21,0x21,0x20
  .byte 0x33,0x31,0x31,0x31,0x31,0x13,0x31,0x33,0x11,0x33,0x21,0x22,0x21,0x21,0x21,0x20
  .byte 0x31,0x13,0x33,0x11,0x31,0x33,0x31,0x13,0x13,0x13,0x21,0x22,0x22,0x12,0x12,0x20
  .byte 0x33,0x33,0x33,0x33,0x33,0x33,0x33,0x33,0x33,0x33,0x22,0x22,0x22,0x22,0x22,0x20
  .balign 4

boot_logo_pal:
  .hword 0xffff,0x7fff,0x0013,0x0000,0x0000,0x0000
  .balign 4

@ Copies data from the SDRAM/Flash to the desired SDRAM address
@ Uses IWRAM as a buffer (2KiB chunks)
@ r0: source address
@ r1: dest address
@ r2: number of 2KiB blocks to copy
sdram_copy:
  @ Go ahead and copy assets to SDRAM locations. We use IWRAM as tmp buffer.
  mov r4, r1
  mov r5, r2
  mov r6, r0
  ldr r8, =0x03004000   @ Use offset 16KiB-18KiB
  mov r9, lr
  1:
    mov r0, r12
    bl set_sd_mode           @ Map source (FW/SDRAM)

    @ Copy 2KiB to IWRAM.
    mov r0, r6               @ Source addr
    mov r1, r8               @ Dest addr
    mov r2, $(2048 / 4)      @ Word count
    swi 0x0C0000
    add r6, $2048

    mov r0, $0x5
    bl set_sd_mode           @ Map SDRAM to copy data to.

    @ Copy 2KiB from IWRAM.
    mov r0, r8               @ Source addr
    mov r1, r4               @ Dest addr
    mov r2, $(2048 / 4)      @ Word count
    swi 0x0C0000
    add r4, $2048

    subs r5, $1
    bne 1b

  @ Map source again (FW/SDRAM)
  mov r0, r12
  bl set_sd_mode
  bx r9

@ Sets the Supercard map mode
@ r0: mode
set_sd_mode:
  mov r1, $0x0A000000
  ldr r2, =0xA55A
  strh r2, [r1, #-2]
  strh r2, [r1, #-2]
  strh r0, [r1, #-2]
  strh r0, [r1, #-2]
  bx lr


@ NDS boot codepath
nds_boot_detected:
  ldr r0, =REG_IPCSYNC       @ Clear SYNC reg
  mov r1, $0
  str r1, [r0]

  @ We do not use the stack in this codepath, except for the BIOS calls
  msr cpsr, #0x13          @ SVC Mode
  ldr sp, =(BOOTNDS_SP7_SVC)
  msr cpsr, #0x1F          @ System Mode

  @ We are currently running on the ARM7 on ARM7-WRAM, prepare to enable ARM9.
  @ Copy the boot rom to main ram (only place where ARM9 can boot).
  mov r0, $GAMEPAK_BASEADDR
  ldr r1, =BOOTMIRROR_ADDR
  mov r2, $(((_end_bootloader - _start + 15) / 16) * 4)   @ Round it up to fit
  swi 0x0C0000

  @ Move ARM7 execution back to main RAM as well, since the payload will use WRAM.
  adr r0, arm7_bootaddr
  ldr r1, =(WRAM_BOOTM_ADDR - BOOTMIRROR_ADDR)
  sub r0, r0, r1
  bx r0

arm7_bootaddr:
  @ Unblock the ARM9 by writing the main-ram mapped address for arm9_bootaddr
  adr r0, arm9_bootaddr
  ldr r1, =0x027FFE24        @ Boot address, overwrite with any value
  str r0, [r1]

  mov r0, $0x4000            @ Wait a few ms before checking on the ARM9
  swi 0x00030000

  @ The arm9 should be up and running, we block here until we hear from it.
  WAIT_SYNC(0x5, 4096)       @ Slow wait, do not starve the ARM9

  SET_SYNC(0x6)              @ Reply with flag 0x6

  WAIT_SYNC(0x7, 512)        @ Waits for ARM9 to be mostly ready to launch

  @ Get ready to launch binary. All WRAM was mapped back to ARM7 at this point.

  @ Check the NDS header, see if we need to copy data to WRAM
  ldr r0, =NDS_HEADER_ADDR
  ldr r1, [r0, #56]            @ Load addr
  lsr r2, r1, $24              @ Get top address byte
  cmp r2, $3                   @ Check if it's in WRAM
  bne 1f

    ldr r2, [r0, #60]            @ Load size
    ldr r3, =NDS_WRAM_MIRROR     @ ARM9 placed the data here!
    2:
      ldm r3!, {r4-r11}
      stm r1!, {r4-r11}          @ Copy the data into the final addr
      subs r2, $32
      bgt 2b
  1:

  @ We are ready with the setup, signal so
  SET_SYNC(0xF)              @ Reply we are ready.

  @ Final wait before reset. Make it a tight loop to avoid missing it.
  WAIT_SYNC(0xF, 4)

  @ Reset now!
  swi 0x0


arm9_bootaddr:
  @ Here is where the arm9 execution begins! Running from main RAM.

  ldr r0, =REG_IPCSYNC     @ Clear SYNC reg
  mov r1, $0
  str r1, [r0]

  // Setup IRQ, SVC and System stack pointers
  // We use stack, and the passme setup is quite inconsistent.
  msr cpsr, #0x13          @ SVC Mode
  ldr sp, =(BOOTNDS_SP9_SVC)
  msr cpsr, #0x12          @ IRQ Mode
  ldr sp, =(BOOTNDS_SP9_IRQ)
  msr cpsr, #0x1F          @ System Mode
  ldr sp, =(BOOTNDS_SP9_SYS)

  ldr r0, =REG_WRAMCNT     @ Ensure we map all the shared-WRAM to ARM9
  mov r1, $0
  strb r1, [r0]
  ldr r0, =REG_EXMEMCNT    @ Map slot2 to ARM9
  strh r1, [r0]

  ldr r0, =REG_DISPCNT_B   @ Disable screen B
  str r1, [r0]
  ldr r0, =REG_POWERCNT    @ Enable screens (power bits)
  ldr r1, =0x8003          @ Move splash to upper screen
  strh r1, [r0]

  @ Enable top LCD with the right bits/config to display the logo
  ldr r0, =REG_DISPCNT_A   @ Enable top screen (engine A)
  ldr r1, =0x00010405      @ Enable mode 5, enable BG2
  str r1, [r0]

  @ Setup BG2 mode, 8-bit indexed color mode
  ldr r0, =REG_BG2CNT
  ldr r1, =0x4080
  strh r1, [r0]

  @ Setup bitmap offset regs
  mov r1, $0
  mov r2, $0x0100
  ldr r0, =REG_BG2PA; strh r2, [r0]
  ldr r0, =REG_BG2PB; strh r1, [r0]
  ldr r0, =REG_BG2PC; strh r1, [r0]
  ldr r0, =REG_BG2PD; strh r2, [r0]
  ldr r0, =REG_BG2X ; str r1, [r0]
  ldr r0, =REG_BG2Y ; str r1, [r0]

  @ Enable VRAM-A and allow CPU access
  @ Enable VRAM-D and map it to the ARM9
  @ Disable other VRAM banks
  ldr r0, =REG_VRAMCNT_ABCD
  ldr r1, =0x80000081
  str r1, [r0]

  @ Init LCD logo with the logo on the top screen, using 8 bit bitmap mode.
  mov r0, $0x06000000
  mov r1, $(NDS_SWIDTH * NDS_SHEIGHT)    @ Clear the first 48KB or so.
  mov r2, $0
  1:
    str r2, [r0], #4; str r2, [r0], #4;
    str r2, [r0], #4; str r2, [r0], #4;
    subs r1, $16
    bne 1b
  @ Copy palette
  adr r0, boot_logo_pal
  mov r1, $0x05000000
  ldm r0, {r2-r5}
  stm r1, {r2-r5}
  @ Draw logo
  adr r0, boot_logo
  ldr r1, =(0x06000000 + (NDS_SWIDTH-31*4)/2 + ((NDS_SHEIGHT-7*4)/2) * NDS_SWIDTH)
  mov r3, $7
  1:
    mov r2, $16
    2:
      ldrb r4, [r0], #1    @ Copy to 4 columns
      mov r5, r4, lsr #4
      orr r5, r5, lsl #8
      orr r5, r5, lsl #16
      str r5, [r1, #768]   @ Copy to four rows
      str r5, [r1, #512]
      str r5, [r1, #256]
      str r5, [r1], #4
      and r5, r4, $0xF
      orr r5, r5, lsl #8
      orr r5, r5, lsl #16
      str r5, [r1, #768]   @ Copy to four rows
      str r5, [r1, #512]
      str r5, [r1, #256]
      str r5, [r1], #4
      subs r2, $1
      bne 2b
    add r1, $(NDS_SWIDTH-32*4 + NDS_SWIDTH*3)
    subs r3, $1
    bne 1b

  @ In NDS mode, we only need the actual payload (includes DLDI).
  @ Only the base ASCII font is ever used (on error)

  @ Proceed to unpack the EWRAM payload to main-ram.
  ldr r0, =FWPAYLOAD_ADDR
  ldr r1, =payload_ewram
  bl upkr_unpack

  @ NDS soft-reset to run EWRAM payload
  ldr r0, =FWPAYLOAD_ADDR
  ldr r1, =0x027FFE24        @ Boot address, start of main ram
  str r0, [r1]
  swi 0x0                    @ Reset to RAM (ewram payload)


@ Include decompression routines here
#define _APPACK_HWORD_VERSION
#include "src/unpack.S"

#include "src/upkrunpack.S"

@ Place constants here.
.pool

_end_bootloader:

@ apack compressed assets!
payload_ewram:
  .incbin "firmware.ewram.gba.comp"
  .balign 4

patches_start:
  .incbin "res/patches.db.comp"
  .balign 4
patches_end:

font_start:
  .incbin "res/fonts.pack.comp"
  .balign 4
font_end:

assets_start:
  #ifdef BUNDLE_GBC_EMULATOR
  .ascii "GCEM"                                        @ GB/C emulator (GoombaColor)
  .word (comp_goomba_emu_end - comp_goomba_emu_start)  @ Payload size
  comp_goomba_emu_start:
    .incbin "emu/jagoombacolor_v0.5.gba.comp"
  comp_goomba_emu_end:
  .balign 4
  #endif

  .word 0               @ End of asset list
  .word 0
assets_end:


