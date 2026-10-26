/*
 * Copyright (C) 2025 David Guillen Fandos <david@davidgf.net>
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

// Executes Code Breaker codes.

.thumb
.balign 4

// r0: points to the cheat buffer.
cheat_exec:
  ldrb r3, [r0]              // Load opcode (times 2)
  ldrh r2, [r0, #2]          // Read value
  ldr  r1, [r0, #4]          // Read address
  add r0, #8                 // Advance pointer
  add pc, r3                 // Jump to table, no need for mode change
  nop
  // Cheat handlers branch to cheat_exec on exit

// These handlers are called to execute a cheat code.

cheat_hndlr_table:
  b cheat_cc_opc0
  b cheat_cc_opc1
  b cheat_cc_opc2
  b cheat_cc_opc3
  b cheat_cc_opc4
  b cheat_cc_opc5
  b cheat_cc_opc6
  b cheat_cc_opc7
  b cheat_cc_opc8
  b cheat_cc_opc9
  b cheat_cc_opcA
  b cheat_cc_opcB
  b cheat_cc_opcC
  b cheat_cc_opcD
  b cheat_cc_opcE
  b cheat_cc_opcF

// Calling convention is as follows:

// r0: points to the cheat buffer.
// r1: cheat address (28 bits)
// r2: cheat value (16 bits)

cheat_cc_opc0:               // Used as end mark, just return via LR
  bx lr

cheat_cc_opc1:               // Not supported, any cheats must be V-Blank based :(
cheat_cc_opc9:               // Not supported, cheats must be decrypted!
  b cheat_exec

cheat_cc_opc3:               // 8 bit immediate write
  strb r2, [r1]
  b cheat_exec

cheat_cc_opc8:               // 16 bit immediate write
  strh r2, [r1]
  b cheat_exec

cheat_cc_opc2:               // 16 bit immediate OR
  ldrh r3, [r1]
  orr r3, r2
  strh r3, [r1]
  b cheat_exec

cheat_cc_opc6:               // 16 bit immediate AND
  ldrh r3, [r1]
  and r3, r2
  strh r3, [r1]
  b cheat_exec

cheat_cc_opcE:               // Address increment value
  lsr r3, r1, #1             // Check address LSB
  bcs 1f
    // Bit zero -> 16 bit mem access
    ldrh r3, [r1]
    add r3, r2
    strh r3, [r1]
    b cheat_exec

  1:
    // Bit one -> 32 bit mem access
    lsl r1, r3, #1           // Aligned address in r1
    lsl r2, #16
    asr r2, #16              // Sign extend the 16 bit immediate
    ldr r3, [r1]
    add r3, r2
    str r3, [r1]
    b cheat_exec

cheat_cc_opc7:               // Compare EQ mem value or skip cheat
  ldrh r3, [r1]
  cmp r3, r2
  beq 1f
  ldrb r3, [r0, #1]          // Read next cheat size.
  add r0, r3                 // Skips the next cheat
1:
  b cheat_exec

cheat_cc_opcA:               // Compare NE mem value or skip cheat
  ldrh r3, [r1]
  cmp r3, r2
  bne 1f
  ldrb r3, [r0, #1]          // Read next cheat size.
  add r0, r3                 // Skips the next cheat
1:
  b cheat_exec

cheat_cc_opcB:               // Compare GT(signed) mem value or skip cheat
  ldrh r3, [r1]
  cmp r2, r3                 // Compare LT (reversed operands)
  blt 1f
  ldrb r3, [r0, #1]          // Read next cheat size.
  add r0, r3                 // Skips the next cheat
1:
  b cheat_exec

cheat_cc_opcC:               // Compare LT(signed) mem value or skip cheat
  ldrh r3, [r1]
  cmp r3, r2                 // Compare LT
  blt 1f
  ldrb r3, [r0, #1]          // Read next cheat size.
  add r0, r3                 // Skips the next cheat
1:
  b cheat_exec

cheat_cc_opcF:               // Compare and immediate with zero or skip cheat
  ldrh r3, [r1]
  and r3, r2
  bne 1f
  ldrb r3, [r0, #1]          // Read next cheat size.
  add r0, r3                 // Skips the next cheat
1:
  b cheat_exec

cheat_cc_opc4:               // Slide code, fills a buffer with some data pattern
  push {r4-r6}
  ldrh r4, [r0, #0]          // Read iteration count
  ldrh r5, [r0, #2]          // Read value increment
  ldrh r6, [r0, #4]          // Read address increment
  add r0, #8                 // Skip the extra args
1:
  strh r2, [r1]              // Actual memory write
  add r1, r6
  add r2, r5
  sub r4, $1
  bne 1b

  pop {r4-r6}
  b cheat_exec

cheat_cc_opc5:               // Super code
  push {r4-r6}
1:                           // Assuming itertion count is not zero
  ldr  r5, [r0]
  ldrh r6, [r0, #4]
  add r0, #8                 // Skip the next code (it's a payload) already

  strh r5, [r1]
  add r1, $2
  sub r2, $1                 // Decrement count
  beq 2f

  lsr r5, #16
  strh r5, [r1]
  add r1, $2
  sub r2, $1                 // Decrement count
  beq 2f

  strh r6, [r1]
  add r1, $2
  sub r2, $1                 // Decrement count
  bne 1b

2:
  pop {r4-r6}
  b cheat_exec

cheat_cc_opcD:
  ldr r3, =0x04000130        // Read REG_P1
  ldrh r3, [r3]

  lsl r1, #26
  lsr r1, #30
  lsl r1, #4      // Each block is 8 insts (16 bytes)
  add pc, r1
  nop

  // case 0: pad == value, skip code
  cmp r3, r2
  bne 3f
  ldrb r3, [r0, #1]          // Read next cheat size.
  add r0, r3                 // Skips the next cheat
  b cheat_exec
  nop; nop; nop;

  // case 1: ~pad & value == value, skip code
  eor r3, r3
  and r3, r2
  cmp r3, r2
  bne 3f
  ldrb r3, [r0, #1]          // Read next cheat size.
  add r0, r3                 // Skips the next cheat
  b cheat_exec
  nop

  // case 2: ~pad & value == 0, skip code
  eor r3, r3
  and r3, r2
  cmp r3, $0
  bne 3f
  ldrb r3, [r0, #1]          // Read next cheat size.
  add r0, r3                 // Skips the next cheat
  nop; nop;

3:
  b cheat_exec

.pool

