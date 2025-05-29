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

#define BOOTNDS_SP9_SVC  0x023FFD80     /* From 0x023FFD00 to 0x023FFD80 */
#define BOOTNDS_SP9_IRQ  0x023FFD00     /* From 0x023FFC80 to 0x023FFD00 */
#define BOOTNDS_SP9_SYS  0x023FFC80     /* From 0x023FFC00 to 0x023FFC80 */

#define REG_WRAMCNT      0x04000247
#define REG_EXMEMCNT     0x04000204

#define REG_DISPCNT_A    0x04000000
#define REG_DISPCNT_B    0x04001000
#define REG_IPCSYNC      0x04000180
#define REG_IME          0x04000208
#define REG_MEMCTRL      0x04000800
#define REG_VRAMCNT_CD   0x04000242

.section .text
.thumb


#define WAIT_SYNC(value, wcnt)  \
  1:                            \
    ldr r0, =(wcnt);            \
    swi 0x30000;                \
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


// This code starts an already loaded NDS rom. The memory map at this point:
// Main RAM:
//  - [0x02000000, 0x023BFE00)   - Loaded ARM9 payload
//  - [0x023C0000, 0x023FF000)   - Current superFW ewram payload (this code, 252KiB)
//  - [0x023FF000, 0x023FFC00)   - Bootloader reloc mirror (max 3KiB)
//  - [0x023FFC00, 0x023FFD80)   - ARM9 stack (only during boot, 3*128 bytes)
//  - [0x023FFD80, 0x023FFE00)   - ARM7 stack (in bootloader, max 128 bytes)
//  - [0x023FFE00, 0x02400000)   - NDS header (already loaded)
.global nds_launch
nds_launch:
  mov r0, pc; bx r0            // Go into arm mode
  .arm

  // Setup IRQ, SVC and System stack pointers, we are about to remap.
  // No return to the menu firmware beyond this point.
  msr cpsr, #0x13          @ SVC Mode
  ldr sp, =(BOOTNDS_SP9_SVC)
  msr cpsr, #0x12          @ IRQ Mode
  ldr sp, =(BOOTNDS_SP9_IRQ)
  msr cpsr, #0x1F          @ System Mode
  ldr sp, =(BOOTNDS_SP9_SYS)

  // Remap the shared WRAM to ARM7, this is the default mapping it seems.
  ldr r0, =REG_WRAMCNT
  mov r1, $0x3
  strb r1, [r0]

  // Also remap the VRAM-D as WRAM/VRAM, it's used as tmp storage for the ARM7 payload
  ldr r0, =REG_VRAMCNT_CD
  mov r1, $0x8200
  strh r1, [r0]

  // Signal the ARM7 that we are about to launch/reset.
  SET_SYNC(0x5)

  WAIT_SYNC(0x6, 512)   // Wait until the ARM7 responds

  SET_SYNC(0x7)         // Request get-ready (ie. copy data if needed)

  // Wait until the ARM7 is done copying and is ready to reset.
  WAIT_SYNC(0xF, 512)

  // The ARM7 finished copying any data, we disable VRAM_D
  ldr r0, =REG_VRAMCNT_CD
  mov r1, $0
  strh r1, [r0]

  // The ARM7 is ready, and waiting for reset signal.
  // Initialize any state to a well known-state for the NDS rom.

  mov r1, $0x80                 // Disable displays
  ldr r0, =REG_DISPCNT_A
  str r1, [r0]
  ldr r0, =REG_DISPCNT_B
  str r1, [r0]

  // Default value (or sensible?)
  ldr r0, =REG_EXMEMCNT
  ldr r1, =0xE880
  str r1, [r0]

  // TODO: Wait for vcount exactly zero on the edge?
  // while(REG_VCOUNT!=191);
  // while(REG_VCOUNT==191);

  // TODO: What else does require initialization?

  // Signal the ARM7 to go reset/launch now!
  SET_SYNC(0xF)

  // Wait a bit, ensure the ARM7 receives it (in case the .NDS clears IPCSYNC)
  mov r0, $(16*1024)
  swi 0x30000

  // Go reset the ARM9 as well! The payload should be properly loaded and ready
  swi 0x0

.pool


.section .ewram
.balign 4
ewram_test_zone:
  .word 0x0, 0x0, 0x0, 0x0


.section .iwram.text, "ax", %progbits
.balign 4
.arm

// Slower copy to ROM routine. Performs checks on the written data.
.global rom_copy_checked32
rom_copy_checked32:
  push {r4-r11}

  // Copy 16 bytes and check whether they got copied correctly.
1:
  ldmia r1!, {r4-r7}
  stmia r0,  {r4-r7}
  ldmia r0!, {r8-r11}
  cmp   r4, r8
  cmpne r5, r9
  cmpne r6, r10
  cmpne r7, r11
  bne 2f
3:
  subs r2, $16
  bhi 1b

  pop {r4-r11}
  bx lr

2:
  // Copy failed, retry at a slower pace.
  strh r4, [r0, #-16]
  lsr r3, r4, #16; nop; nop;
  strh r3, [r0, #-14]

  str r5, [r0, #-12]
  lsr r3, r5, #16; nop; nop;
  strh r3, [r0, #-10]

  str r6, [r0, # -8]
  lsr r3, r6, #16; nop; nop;
  strh r3, [r0, #-6]

  str r7, [r0, # -4]
  lsr r3, r7, #16; nop; nop;
  strh r3, [r0, #-2]

  ldmdb r0, {r8-r11}

  cmp   r4, r8
  cmpne r5, r9
  cmpne r6, r10
  cmpne r7, r11
  bne 2b
  b 3b


.global test_fast_ewram
test_fast_ewram:
  push {r4-r9}

  // Test EWRAM reads/writes when overclocked
  ldr r1, =REG_MEMCTRL
  ldr r2, =ewram_test_zone

  ldr r0, =0x0E000020          // EWRAM enabled and 1 wait state
  str r0, [r1]

  adr r0, .dummy_data
  ldm r0, {r0, r3, r4, r5}

  stm r2, {r0, r3, r4, r5}     // Write data to EWRAM
  nop; nop; nop;
  ldm r2, {r6, r7, r8, r9}     // Read the data back

  cmp r0, r6
  cmpeq r3, r7
  cmpeq r4, r8
  cmpeq r5, r9
  moveq r0, $1                // r0 is one if the tests passed
  movne r0, $0

  ldr r2, =0x0D000020         // Go back to two wait states
  str r2, [r1]

  pop {r4-r9}
  bx lr

.dummy_data:
  .word 0xDEADBEEF, 0x0BADC0DE, 0xABAD1DEA, 0xDAB1D6F0


.global launch_reset
launch_reset:
  // Check arg[1], if non-zero, overclock EWRAM
  cmp r1, $0
  ldr r1, =REG_MEMCTRL
  ldrne r2, =0x0E000020
  ldreq r2, =0x0D000020
  str r2, [r1]

  // Check arg[0], if set, do a hard_reset
  cmp r0, $0
  bne hard_reset

  // fallback to soft_reset

soft_reset:
  ldr r0, =0x03007FFA
  mov r1, $0
  strb r1, [r0]            // Reset flag clear (means reset to ROM)
  ldr r0, =REG_IME
  strb r1, [r0]            // Disable interrupts

  // Reset stack values
  mov r0, #0xD3
  msr cpsr_cf, r0
  ldr sp, =0x03007FE0    // Default SVC BIOS mode stack value

  mov r0, #0x92
  msr cpsr_cf, r0
  ldr sp, =0x03007FA0    // Default IRQ BIOS mode stack value

  mov r0, #0x9F
  msr cpsr_cf, r0        // Restore system mode.
  ldr sp, =0x03007F00    // Default BIOS value

  ldr r7, =0x04000204
  ldr r8, [r7]           // Preserve WAITCNT

  // Copy the final clear + reset routine to the safe area
  ldr r0, =0x03007E00
  adr r1, 1f

  ldmia r1, {r2-r5}
  stmia r0, {r2-r5}

  bx r0

  .pool

  // Clears IWRAM (but the last 200 bytes of IWRAM)
  // and performs the soft reset.
  // Preserves WAITCNT while doing so!
1:
  mov r0, $0xFF
  swi 0x010000     // Clear all memory! Also I/O memory :D
  str r8, [r7]     // Restore WAITCNT
  swi 0x000000

hard_reset:
  ldr r0, =REG_IME         // Disable interrupts
  mov r1, $0
  strh r1, [r0]

  swi 0x260000             // Resets through the BIOS opening animation


.global running_on_nds
running_on_nds:
  // Overwrites a couple of words in RAM and checks whether they are mirrored
  // (would mean GBA) or not (it must be an NDS with more memory).
  // Restores the data to its previous state before returning.
  push {r4, r5}
  mov r0, $0x02000000
  add r1, r0, $(256*1024)
  ldr r2, =0xDEADBEEF
  ldr r3, =0x600DF00D

  ldr r4, [r0]     // Backup data
  ldr r5, [r1]

  str r2, [r0]     // Write two funny values
  str r3, [r1]

  ldr r2, [r0]     // Read the values back
  ldr r3, [r1]

  str r4, [r0]     // Restore original values
  str r5, [r1]

  mov r0, $0       // Assuming GBA for now
  cmp r2, r3
  movne r0, $1     // Return one if values are different (NDS)

  pop {r4, r5}
  bx lr


// It should be fine for NDS since it runs from WRAM and it has 4 cycles/access.
.global wait_ms
wait_ms:
  mov r1, $2096    // 2^24 cycles / 1000 / 8
  mul r0, r1, r0
1:
  nop; nop
  nop; nop
  subs r0, $1
  bne 1b
  bx lr

.global set_irq_enable
set_irq_enable:
  cmp r0, $0
  mrs r1, cpsr
  bicne r1, #0x80   // IRQ bit is number 7
  orreq r1, #0x80   // Represents "Disable IRQ" bit
  msr cpsr, r1
  bx lr

.global gba_irq_handler
gba_irq_handler:
  ldr r0, =irq_handler_fn
  bx r0

#define _APPACK_HWORD_VERSION
#include "unpack.S"

