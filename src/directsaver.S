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

// Direct Saver payload.
// Provides handlers to read and write save data blocks.
// Requires low-level access to the save file (allocated contiguously).
//
// Handlers assume a valid stack with some reasonable size (~64 bytes)
// and certain tolerance for disabling interrupts (for a few cycles at a time).

#include "gba_regs.h"
#include "directsave.h"

#define SAFE_OP_ROUTINE_SIZE    12   // Number of insts (32 bit insts)
#define SAFE_OP_INST_OFF         5   // Instruction number (from 0) in the routine

#define STACK_IRESV     (4*SAFE_OP_ROUTINE_SIZE + 4) // Extra slot for saving LR

// Jump table in ARM mode to simplify instruction sizes.
.section    .directsave, "ax"
.align 4
.arm

// It starts directly at the entry point routine.
// Receives a function call, the call number is stored in r7
ds_entrypoint:
  push {lr}

  // Set mutex.
  mov r3, $1
  bl set_clear_mutex

  adr r3, ds_table
  mov lr, pc
  add pc, r3, r7, lsl #2

  // Clear mutex.
  mov r3, $0
  bl set_clear_mutex

  pop {lr}
  bx lr

ds_table:
  b ds_read_eeprom
  b ds_write_eeprom
  b ds_read_flash
  b ds_write_sector_flash
  b ds_erase_chip_flash
  b ds_erase_sector_flash


// r3: value to write to the mutex register
set_clear_mutex:
  push {r0, lr}

  mov r0, $0x0F000000
  strb r3, [r0, #(-DIRSAV_CFG_SIZE + DIRSAV_CFG_MUTEX_OFF)]

  pop {r0, pc}


// Validate that the SRAM configuration is correct (not corrupted).
.global validate_config
validate_config:
  sub sp, $DIRSAV_CFG_SIZE

  // Copy the SRAM config to the stack
  ldr r0, =(0x0F000000 - DIRSAV_CFG_SIZE)
  mov r1, sp
  mov r3, $DIRSAV_CFG_SIZE
1:
  ldrb r2, [r0], #1
  strb r2, [r1], #1
  subs r3, $1
  bne 1b

  // Check the magic header
  ldr r0, [sp, #DIRSAV_CFG_MAGIC_OFF]
  ldr r1, =DIRSAV_CFG_MAGIC
  cmp r0, r1
  bne 9f

  // Clear the mutex variable for checksum calculation
  mov r0, $0
  strb r0, [sp, #DIRSAV_CFG_MUTEX_OFF]
  // Calculate checksum
  mov r1, sp
  mov r3, $DIRSAV_CFG_SIZE
1:
  ldr r2, [r1], #4
  eor r0, r2, r0
  subs r3, $4
  bne 1b

  cmp r0, $0
  bne 9f

  add sp, $DIRSAV_CFG_SIZE
  mov r0, $1
  bx lr

9:  // Bad config!
  add sp, $DIRSAV_CFG_SIZE
  mov r0, $0
  bx lr


// Necessary handlers for the SC driver, reads info from SRAM.
.global sc_issdhc
sc_issdhc:
  mov r0, $0x0F000000
  ldrb r0, [r0, #(-DIRSAV_CFG_SIZE + DIRSAV_CFG_ISSDHC_OFF)]
  bx lr

.global sc_rca
sc_rca:
  mov r0, $0x0F000000
  ldrb r1, [r0, #(-DIRSAV_CFG_SIZE + DIRSAV_CFG_RCA_OFF)]
  ldrb r0, [r0, #(-DIRSAV_CFG_SIZE + DIRSAV_CFG_RCA_OFF + 1)]
  orr r0, r1, r0, lsl #8
  bx lr

.global base_sector
base_sector:
  mov r3, $0x0F000000
  ldrb r0, [r3, #(-DIRSAV_CFG_SIZE + DIRSAV_CFG_BSECT_OFF)]
  ldrb r1, [r3, #(-DIRSAV_CFG_SIZE + DIRSAV_CFG_BSECT_OFF + 1)]
  ldrb r2, [r3, #(-DIRSAV_CFG_SIZE + DIRSAV_CFG_BSECT_OFF + 2)]
  ldrb r3, [r3, #(-DIRSAV_CFG_SIZE + DIRSAV_CFG_BSECT_OFF + 3)]
  orr r0, r0, r1, lsl #8
  orr r0, r0, r2, lsl #16
  orr r0, r0, r3, lsl #23
  bx lr

.global get_memory_size
get_memory_size:
  mov r3, $0x0F000000
  ldrb r0, [r3, #(-DIRSAV_CFG_SIZE + DIRSAV_CFG_MEMSIZE_OFF)]
  ldrb r1, [r3, #(-DIRSAV_CFG_SIZE + DIRSAV_CFG_MEMSIZE_OFF + 1)]
  ldrb r2, [r3, #(-DIRSAV_CFG_SIZE + DIRSAV_CFG_MEMSIZE_OFF + 2)]
  ldrb r3, [r3, #(-DIRSAV_CFG_SIZE + DIRSAV_CFG_MEMSIZE_OFF + 3)]
  orr r0, r0, r1, lsl #8
  orr r0, r0, r2, lsl #16
  orr r0, r0, r3, lsl #23
  bx lr


// Reimplementation of supercard_io.S routines, with the following caveats:
// Run code from the stack, disable interrupts during SD reg access and
// remap ROM when interrupts are enabled.

#define SC_MIRROR_BASE_8         0x08000000

// Used to perform writes (but also reads?) to the bus
#define SC_WRITE_REGISTER_8     (SC_MIRROR_BASE_8 + 0x01000000)
// Used to poll for reads (can be slow)
#define SC_READ_REGISTER_16     (SC_MIRROR_BASE_8 + 0x01100000)
// Command interface (bit 7 and 0)
#define SC_RDWR_COMMAND         (SC_MIRROR_BASE_8 + 0x01800000)

#define SD_DATA0                   0x0100


// Some registers are fixed values, set on function entry for speed.
// r7 must contain REG_IMG address
// r8 must contain 0x09FFFFFE address
// r9 must contain 0xA55A constant
// r10-r11 are scratch registers!


// r0: output byte buffer (output)
// r1: number of blocks to read
// r2: number of timeout iterations
.global sc_read_sectors_w0
.type sc_read_sectors_w0,function
sc_read_sectors_w0:
  push {r2, r4-r12, lr}
  ldr r7, =(0x04000000 + REG_IME)
  ldr r8, =0x09FFFFFE
  ldr r9, =0xA55A

  sub sp, $STACK_IRESV      // Some space for the code

  // Copy the template routine to the stack
  .set i, 0
  .rept SAFE_OP_ROUTINE_SIZE
    ldr r2, ((safe_sd_operation) + i*4)
    str r2, [sp, #(i*4)]
    .set i, i+1
  .endr

  ldr r5, =SC_READ_REGISTER_16

  1:
    // This is the slower unaligned version (a bit more friendly for this use case)
    // Perform a wait on the data bus, until the bits are pulled zero once.
    ldr r4, [sp, #STACK_IRESV]   // Read initial timeout value
    2:
      subs r4, r4, $1
      moveq r0, $1      // Non zero retvalue on timeout error
      beq 3f            // Function return.
      bl sd_ldrh
      tst r2, $(SD_DATA0)
    bne 2b

    // Go ahead and read a 512 byte sector
    mov r4, $(512 / 4)     // Performs four bytes per iteration

    2:
      bl sd_ldm4            // Reads four bytes in one go.
      lsr  r2,  r2, #24     // Placed in the top 8 bits of the register
      lsr  r3,  r3, #24
      lsr  r6,  r6, #24
      lsr r12, r12, #24
      strb r2, [r0], #1     // Stores the received byte
      strb r3, [r0], #1
      strb r6, [r0], #1
      strb r12, [r0], #1
      subs r4, r4, $1
      bne 2b

    // Skip 8 bytes
    bl sd_ldm4
    bl sd_ldm4

    bl sd_ldrh            // Final clock: should go all high (0xF)

    subs r1, r1, $1
  bne 1b

  mov r0, $0            // Return value: success
3:
  add sp, $(STACK_IRESV + 4)     // Skip r2 pushed on the stack
  pop {r4-r12, lr}
  bx lr



// Writes a bunch of 512 byte sectors, calculating checksums for them as well
// as checking any error codes returned (and waiting between blocks).

// r0: data byte buffer (input)
// r1: number of 512byte blocks to send/write
// r2: number of timeout iterations
// returns non zero on timeout or data rejected (ie. CRC error, write error...)
.global sc_write_sectors_w0
.type sc_write_sectors_w0,function
sc_write_sectors_w0:
  push {r2, r4-r12, lr}
  mov r5, $(SC_WRITE_REGISTER_8)
  ldr r7, =(0x04000000 + REG_IME)
  ldr r8, =0x09FFFFFE
  ldr r9, =0xA55A

  sub sp, $(STACK_IRESV + 8)    // Some space for the CRC buffer, and the RAM routine.
  mov r4, r0                    // Input buffer
  mov r6, r1                    // Block count

  // Copy the template routine to the stack
  .set i, 0
  .rept SAFE_OP_ROUTINE_SIZE
    ldr r2, ((safe_sd_operation) + i*4)
    str r2, [sp, #(i*4)]
    .set i, i+1
  .endr

  // Calculate crc for the first block.
  add r1, sp, $STACK_IRESV
  bl crc16_nibble_512_nolut8bit

  1: // Loop r6 times. Try to perform checksum while waiting for write to finish.

    // Go ahead send the data! Send the first byte token (0xFE? should be 0xFC!)
    mov r2, $0xFFFFFFFF
    bl sd_str
    bl sd_str
    bl sd_str
    bl sd_strh
    mov r2, $0
    bl sd_strh

    #ifdef SUPERCARD_LITE_IO
    mov r1, $(512 / 2)       // Pushing 512 bytes in half-words
    2:
      ldrb r2, [r4], #1      // Load byte from buffer
      ldrb r3, [r4], #1
      orr r2, r2, r3, lsl #8
      orr r2, r2, lsl #20
      mov r3, r2, lsr #8

      bl sd_stm2
      subs r1, r1, $1
      bne 2b
    #else
    mov r1, $(512)           // Pushing 512 bytes in bytes
     2:
      ldrb r2, [r4], #1      // Load byte from buffer
      bl sd_str
      subs r1, r1, $1
      bne 2b
    #endif

    .set i, 0
    #ifdef SUPERCARD_LITE_IO
    .rept 4                  // Send 16x4 bits of checksum
      ldrh r2, [sp, #(i*2 + STACK_IRESV)]
      orr r2, r2, lsl #20
      mov r3, r2, lsr #8
      bl sd_stm2
      .set i, i+1
    .endr
    #else
    .rept 8                  // Send 16x4 bits of checksum
      ldrb r2, [sp, #(i + STACK_IRESV)]
      bl sd_str
      .set i, i+1
    .endr
    #endif

    // Response token is 8 bits, only transmitted via DAT0.
    // Write one to the bus, read a couple more times (ignore 3 MSB)
    mov  r2, $(~0)            // All data lines high after last bit
    bl sd_strh
    bl sd_strh
    bl sd_strh
    bl sd_strh

    // Get status code (3 bits) + dummy bit
    mov r0, $0
    .rept 4
      lsl r0, $1
      bl sd_ldrh
      tst r2, $SD_DATA0
      orrne r0, $0x1
    .endr

    // Pipe one byte, some cards do not start actually writing (ie. go busy?)
    // until we pipe an extra clock? Perhaps not, but does no harm.
    .rept 4
      bl sd_ldr
    .endr

    // Check status code, bail on error
    cmp r0, $0x5              // 0.010.1 means "data accepted" (0.status.1)
    bne 3f                    // Wait and exit with error

    // Break out if we are on the last block!
    subs r6, $1
    beq 5f

    // Calculate the block checksum for the next block to send.
    // (helps hiding busy latency!)
    mov r0, r4
    add r1, sp, $STACK_IRESV
    bl crc16_nibble_512_nolut8bit

    // Perform a wait on the data bus, DAT0 goes high when ready.
    ldr r1, [sp, #(STACK_IRESV + 8)]
    2:
      subs r1, r1, $1
      beq 4f
      bl sd_ldrh
      tst r2, $(SD_DATA0)
    beq 2b

    b 1b  // Continue loop on the next block.

5:
  // Perform a final wait, ensure we leave with no pending operations.
  ldr r0, [sp, #(STACK_IRESV + 8)]
  2:
    subs r0, r0, $1
    beq 4f
    bl sd_ldrh
    tst r2, $(SD_DATA0)
  beq 2b

  add sp, $(8 + STACK_IRESV)
  pop {r2, r4-r12, lr}
  mov r0, $0            // Return OK
  bx lr

3:  // Wait-and-exit on error
  ldr r0, [sp, #(STACK_IRESV + 8)]
  2:
    subs r0, r0, $1
    beq 4f
    bl sd_ldrh
    tst r2, $(SD_DATA0)
  beq 2b

4:  // Return non zero on timeout/response error
  add sp, $(8 + STACK_IRESV)
  pop {r2, r4-r12, lr}
  mov r0, $1
  bx lr


// Command-related routines


.global send_empty_clocks
// Sends empty clocks (just read the CMD register to force a clock)
// r0: number of clocks to send
send_empty_clocks:
  push {r5-r12, lr}
  sub sp, $STACK_IRESV      // Some space for the code
  ldr r7, =(0x04000000 + REG_IME)
  ldr r8, =0x09FFFFFE
  ldr r9, =0xA55A

  // Copy the template routine to the stack
  .set i, 0
  .rept SAFE_OP_ROUTINE_SIZE
    ldr r2, ((safe_sd_operation) + i*4)
    str r2, [sp, #(i*4)]
    .set i, i+1
  .endr

  ldr r5, =SC_RDWR_COMMAND
  1:
    bl sd_ldm4
    subs r0, $8
    bgt 1b
  add sp, $STACK_IRESV
  pop {r5-r12, lr}
  bx lr


.global wait_sdcard_idle
// Waits until the CMD line (bit0) goes high, indicating IDLE.
// r0: number of iterations before giving up
wait_sdcard_idle:
  push {r5-r12, lr}
  sub sp, $STACK_IRESV      // Some space for the code
  ldr r7, =(0x04000000 + REG_IME)
  ldr r8, =0x09FFFFFE
  ldr r9, =0xA55A

  // Copy the template routine to the stack
  .set i, 0
  .rept SAFE_OP_ROUTINE_SIZE
    ldr r2, ((safe_sd_operation) + i*4)
    str r2, [sp, #(i*4)]
    .set i, i+1
  .endr

  ldr r5, =SC_RDWR_COMMAND
  mov r3, r0
  mov r0, $1
  1:
    bl sd_ldrh
    lsls r2, #31       // Extract LSB to Carry Flag
    bne 2f             // If LSB is set, return true (Idle reached)
    subs r3, $1        // Decrement timeout.
    bne 1b

  mov r0, $0
2:
  add sp, $STACK_IRESV
  pop {r5-r12, lr}
  bx lr


.global wait_dat0_idle
// Waits until the DAT0 line goes high, indicating not busy.
// r0: number of iterations before giving up
wait_dat0_idle:
  push {r5-r12, lr}
  sub sp, $STACK_IRESV
  ldr r7, =(0x04000000 + REG_IME)
  ldr r8, =0x09FFFFFE
  ldr r9, =0xA55A

  // Copy the template routine to the stack
  .set i, 0
  .rept SAFE_OP_ROUTINE_SIZE
    ldr r2, ((safe_sd_operation) + i*4)
    str r2, [sp, #(i*4)]
    .set i, i+1
  .endr

  ldr r5, =SC_WRITE_REGISTER_8

  ldr r3, =SD_DATA0
  1:
    bl sd_ldrh
    tst r2, $(SD_DATA0)      // Check the right bit (SD_DATA0)
    bne 2f                   // If bit set, it is idlea again, return true
    subs r0, $1              // Decrement timeout.
    bne 1b

  add sp, $STACK_IRESV
  pop {r5-r12, lr}
  mov r0, $0
  bx lr
2:
  add sp, $STACK_IRESV
  pop {r5-r12, lr}
  mov r0, $1
  bx lr


.global receive_sdcard_response
// Waits for a response (or returns error) and receives a response to a buffer.
// r0: buffer pointer where we write the response (can be null!)
// r1: response length in bytes
// r2: number of iterations before giving up
receive_sdcard_response:
  push {r4-r12, lr}
  sub sp, $STACK_IRESV
  ldr r7, =(0x04000000 + REG_IME)
  ldr r8, =0x09FFFFFE
  ldr r9, =0xA55A

  // Copy the template routine to the stack
  .set i, 0
  .rept SAFE_OP_ROUTINE_SIZE
    ldr r3, ((safe_sd_operation) + i*4)
    str r3, [sp, #(i*4)]
    .set i, i+1
  .endr

  ldr r5, =SC_RDWR_COMMAND

  mov r3, r2
  mov r6, $7           // First bit belongs to first byte (always zero)
  mov r4, $0

  1:
    bl sd_ldrh
    lsrs r2, #1        // Extract LSB to Carry Flag
    bcc 4f             // If LSB is clear, we can receive the next bits!
    subs r3, $1        // Decrement timeout.
    bne 1b

  mov r0, $0           // Timeout, return false
  add sp, $STACK_IRESV
  pop {r4-r12, lr}
  bx lr

  3:

    // Read 8 bits (MSB to LSB)
    mov r6, $8
    4:
      bl sd_ldrh
      lsrs r2, #1      // Extract LSB into Carry Flag
      adc r4, r4       // Shift register left by 1, add CF to LSB
      subs r6, $1
      bne 4b

    cmp r0, $0
    strneb r4, [r0]      // Store it in the buffer, if ptr!=null
    addne r0, $1

    subs r1, $1
    bne 3b

  mov r0, $1           // Return true
  add sp, $STACK_IRESV
  pop {r4-r12, lr}
  bx lr



.global send_sdcard_commandbuf
// Waits for a response (or returns error) and receives a response to a buffer.
// r0: buffer pointer to send
// r1: command length in bytes
send_sdcard_commandbuf:
  push {r5-r12, lr}
  sub sp, $STACK_IRESV
  ldr r7, =(0x04000000 + REG_IME)
  ldr r8, =0x09FFFFFE
  ldr r9, =0xA55A

  // Copy the template routine to the stack
  .set i, 0
  .rept SAFE_OP_ROUTINE_SIZE
    ldr r2, ((safe_sd_operation) + i*4)
    str r2, [sp, #(i*4)]
    .set i, i+1
  .endr

  ldr r5, =SC_RDWR_COMMAND

  1:
    ldrb r2, [r0], #1
    orr r2, r2, r2, lsl #17
    lsl r3, r2, #2
    lsl r6, r2, #4
    lsl r12, r2, #6
    bl sd_stm4

    subs r1, $1
    bne 1b

  add sp, $STACK_IRESV
  pop {r5-r12, lr}
  bx lr



// Supercard SD interface "safe" read/write operation
#define SD_OPERATION(fn_name, ...)                                            \
  fn_name:                                                                    \
    str lr, [sp, #(SAFE_OP_ROUTINE_SIZE*4)];   /* Preserve LR */              \
                                                                              \
    /* Insert operation instruction */                                        \
    ldr r11, 1f;                                                              \
    str r11, [sp, #(SAFE_OP_INST_OFF*4)];                                     \
                                                                              \
    /* Disable IRQs */                                                        \
    mov r11, $0;                                                              \
    swp r10, r11, [r7]; /* Writes zero to REG_IME, preserve previous value */ \
                                                                              \
    /* Actually call the routine */                                           \
    mov lr, pc;                                                               \
    mov pc, sp;                                                               \
                                                                              \
    /* (maybe) re-enable IRQs */                                              \
    str r10, [r7];                                                            \
                                                                              \
    ldr pc, [sp, #(SAFE_OP_ROUTINE_SIZE*4)];   /* Restore LR directly */      \
  1:                                                                          \
    __VA_ARGS__;                                                              \


SD_OPERATION(sd_ldr,  ldr  r2, [r5]);
SD_OPERATION(sd_ldrh, ldrh r2, [r5]);
SD_OPERATION(sd_str,  str  r2, [r5]);
SD_OPERATION(sd_strh, strh r2, [r5]);

SD_OPERATION(sd_stm2, stmia r5, {r2, r3});
SD_OPERATION(sd_ldm4, ldmia r5, {r2, r3, r6, r12});
SD_OPERATION(sd_stm4, stmia r5, {r2, r3, r6, r12});


// Actual implementation template. (12 insts, 48 bytes, see STACK_IRESV)
safe_sd_operation:
  // Map SD card driver into the high address space (5 insts)
  mov r11, $(0xD7)       // SDRAM + RW + SD iface
  strh r9,  [r8]
  strh r9,  [r8]
  strh r11, [r8]
  strh r11, [r8]

  nop   // Actual operation to perform! To be patched on demand!

  mov r11, $(0xD1)       // SDRAM + RO
  strh r9,  [r8]
  strh r9,  [r8]
  strh r11, [r8]
  strh r11, [r8]

  bx lr

// Constant pool goes here.
.pool


