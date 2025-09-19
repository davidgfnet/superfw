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

// Place the routines in IWRAM, they are perf critical for fast SD operations.
.section .iwram.text, "ax", %progbits
.align 4
.arm

// In general we use the 0x8-9 mapping, but A-B for data reads.
#define SC_MIRROR_BASE_8         0x08000000
#define SC_MIRROR_BASE_A         0x0A000000

// Used to perform writes (but also reads?) to the bus
#define SC_WRITE_REGISTER_8     (SC_MIRROR_BASE_8 + 0x01000000)
#define SC_WRITE_REGISTER_A     (SC_MIRROR_BASE_A + 0x01000000)
// Used to poll for reads (can be slow)
#define SC_READ_REGISTER_16     (SC_MIRROR_BASE_8 + 0x01100000)
// Used to read data from SD card.
#define SC_READ_REGISTER_8      (SC_MIRROR_BASE_8 + 0x01100000)
#define SC_READ_REGISTER_A      (SC_MIRROR_BASE_A + 0x01100000)
#define SCLITE_DATA_REGISTER32  (SC_MIRROR_BASE_8 + 0x01200000)

// Used to send/received commands.
#define SC_RDWR_COMMAND         (SC_MIRROR_BASE_8 + 0x01800000)
// ^^ CMD interface (bit 0 is an input pin, bit 7 is the output bit)

#define SD_DATA0                0x0100

#define SD_ERR_READTIMEOUT             10
#define SD_ERR_WRITETIMEOUT            11
#define CMD_WAIT_DATA            0x400000     // Aprox ~1s as data timeout


.global sc_read_sectors_w0   // Version that uses 0x08000000-0x09FFFFFF addrs
.global sc_read_sectors_w1   // Version that uses 0x0A000000-0x0BFFFFFF addrs

#ifndef SUPERCARD_LITE_READ

// Reads N 512 byte sectors mapped via ROM mappings into the provided buffer
// as fast as possible. Assumes that the read operation was issued and that
// it needs to wait for the block (by detecting the first zero bit).
// Took inspiration from SCFW and we use swp instructions to perform 2x32 bit
// accesses, with a rotated reg (unaligned addr) to avoid one rotr.

// r0: output byte buffer (output)
// r1: number of blocks to read

.type sc_read_sectors_w0,function
sc_read_sectors_w0:
  push {r4-r12}

  ldr r5, =SC_READ_REGISTER_8
  b 6f

.type sc_read_sectors_w1,function
sc_read_sectors_w1:
  push {r4-r12}

  ldr r5, =SC_READ_REGISTER_A

6:
  tst r0, $3                 // Check if the buffer address is aligned
  bne 5f                     // Branch to the slow routine.

  ldr r12, =0x0000FFFF

1:
  // Perform a wait on the data bus, until the bits are pulled zero once.
  mov r4, $(CMD_WAIT_DATA)
  mov r3, $(SC_READ_REGISTER_16)
  2:
    subs r4, r4, $1
    moveq r0, $1      // Non zero retvalue on timeout error
    beq 3f            // Function return.
    ldrh r2, [r3]
    tst r2, $(SD_DATA0)
  bne 2b

  // Go ahead and read a 512 byte sector
  mov r4, $(512 / 16 / 8)    // Performs 4 iterations per 512 byte block

  2:
    .rept 16
      // Reads 64 bits (even regs are mostly garbage)
      ldmia r5, {r2, r3, r6, r7, r8, r9, r10, r11}
      // Regs r2, r6, r8, r10 are garbage
      bic  r7, r12
      bic r11, r12
      // Merge half words (after clearing the LSB)
      orr  r7,  r7, r3, lsr #16
      orr r11, r11, r9, lsr #16
      // Write 8 bytes to the output buffer
      stmia r0!, {r7, r11}
    .endr

    subs r4, r4, $1
    bne 2b

  // Reads 64 bits (8 bytes of checksum) in one go.
  ldmia r5, {r2, r3, r6, r7, r8, r9, r10, r11}

  ldrh r2, [r5]       // Final clock: should go all high (0xF)

  subs r1, r1, $1
  bne 1b

  mov r0, $0            // Return value: success
3:
  pop {r4-r12}
  bx lr

  // Unaligned code! It should be slower (but also smaller)
5:
  // Perform a wait on the data bus, until the bits are pulled zero once.
  mov r4, $(CMD_WAIT_DATA)
  mov r3, $(SC_READ_REGISTER_16)
  2:
    subs r4, r4, $1
    moveq r0, $1      // Non zero retvalue on timeout error
    beq 3f            // Function return.
    ldrh r2, [r3]
    tst r2, $(SD_DATA0)
  bne 2b

  // Go ahead and read a 512 byte sector
  mov r4, $(512 / 2)     // Performs two bytes per iteration
  ldr r5, =SC_READ_REGISTER_8

  2:
    ldmia r5, {r2, r3}   // Reads two bytes in one go.
    lsr r2, r2, #24      // Places in the top 8 bits of the register
    lsr r3, r3, #24
    strb r2, [r0], #1    // Stores the received byte
    strb r3, [r0], #1
    subs r4, r4, $1
    bne 2b

  // Reads 64 bits (8 bytes of checksum) in one go.
  ldmia r5, {r2, r3, r6, r7, r8, r9, r10, r11}

  ldrh r2, [r5]       // Final clock: should go all high (0xF)

  subs r1, r1, $1
  bne 5b

  mov r0, $0            // Return value: success
3:
  pop {r4-r12}
  bx lr

#else

// This is the same as above, but using the Supercard Lite faster I/O interface.
// The cart clocks data for us and we can read it in 32 bit blocks (we assume
// that it clocks the SD card 8 times under the hood). We can't use the faster
// waitstates otherwise it doesn't have enough time to do so.

// r0: output byte buffer (output)
// r1: number of blocks to read

.type sc_read_sectors_w0,function
sc_read_sectors_w0:
.type sc_read_sectors_w1,function
sc_read_sectors_w1:

  push {r4-r11}
  ldr r5, =SCLITE_DATA_REGISTER32

6:
  tst r0, $3                 // Check if the buffer address is aligned
  bne 5f                     // Branch to the slow routine.

1:
  // Perform a wait on the data bus, until the bits are pulled zero once.
  mov r3, $(CMD_WAIT_DATA)
  mov r4, $(SC_READ_REGISTER_16)
  2:
    subs r3, r3, $1
    moveq r0, $1      // Non zero retvalue on timeout error
    beq 3f            // Function return.
    ldrh r2, [r4]
    tst r2, $(SD_DATA0)
  bne 2b

  ldrh r2, [r5]       // Discard the first 32 bits

  // Go ahead and read a 512 byte sector
  // Repeat 16 * 32 bytes = 512 bytes.
  .rept 16
    // Reads 256 bits
    ldmia r5, {r2, r3, r6, r7, r8, r9, r10, r11}
    // Write the data back to the buffer
    stmia r0!, {r2, r3, r6, r7, r8, r9, r10, r11}
  .endr

  // Reads 64 bits (8 bytes of checksum) in one go.
  ldmia r5, {r2, r3}

  ldrh r2, [r4]       // Final clock: should go all high (0xF)

  subs r1, r1, $1
  bne 1b

  mov r0, $0            // Return value: success
3:
  pop {r4-r11}
  bx lr

  // Unaligned code! It should be slower (but also smaller)
5:
  // Perform a wait on the data bus, until the bits are pulled zero once.
  mov r3, $(CMD_WAIT_DATA)
  mov r4, $(SC_READ_REGISTER_16)
  2:
    subs r3, r3, $1
    moveq r0, $1      // Non zero retvalue on timeout error
    beq 3f            // Function return.
    ldrh r2, [r4]
    tst r2, $(SD_DATA0)
  bne 2b

  ldrh r2, [r5]       // Discard the first 32 bits

  // Go ahead and read a 512 byte sector
  mov r6, $(512 / 8)     // Performs eight bytes per iteration

  2:
    ldmia r5, {r2, r3}   // Reads 8 bytes in one go.
    strb r2, [r0], #1    // Stores the received byte
    lsr r2, #8
    strb r2, [r0], #1
    lsr r2, #8
    strb r2, [r0], #1
    lsr r2, #8
    strb r2, [r0], #1
    strb r3, [r0], #1
    lsr r3, #8
    strb r3, [r0], #1
    lsr r3, #8
    strb r3, [r0], #1
    lsr r3, #8
    strb r3, [r0], #1

    subs r6, r6, $1
    bne 2b

  // Reads 64 bits (8 bytes of checksum) in one go.
  ldmia r5, {r2, r3}

  ldrh r2, [r4]       // Final clock: should go all high (0xF)

  subs r1, r1, $1
  bne 5b

  mov r0, $0            // Return value: success
3:
  pop {r4-r11}
  bx lr

#endif


// Writes a bunch of 512 byte sectors, calculating checksums for them as well
// as checking any error codes returned (and waiting between blocks).

// r0: data byte buffer (input)
// r1: number of 512byte blocks to send/write
// returns non zero on timeout or data rejected (ie. CRC error, write error...)
.global sc_write_sectors_w0   // Version that uses 0x08000000-0x09FFFFFF addrs
.global sc_write_sectors_w1   // Version that uses 0x0A000000-0x0BFFFFFF addrs

#ifndef SUPERCARD_LITE_IO

.type sc_write_sectors_w0,function
sc_write_sectors_w0:
  push {r4-r6, lr}
  mov r6, $(SC_WRITE_REGISTER_8)
  b 9f

.type sc_write_sectors_w1,function
sc_write_sectors_w1:
  push {r4-r6, lr}
  mov r6, $(SC_WRITE_REGISTER_A)

9:
  sub sp, $8    // Some space for the CRC buffer
  mov r4, r0    // Input buffer
  mov r5, r1    // Block count

  // Calculate crc for the first block.
  mov r1, sp
  bl crc16_nibble_512_nolut

  1: // Loop r5 times. Try to perform checksum while waiting for write to finish.

    // Go ahead send the data! Send the first byte token (0xFE? should be 0xFC!)
    mov r1, $0
    mov r2, $0xFFFFFFFF
    str r2, [r6]
    str r2, [r6]
    str r2, [r6]
    strh r2, [r6]
    strh r1, [r6]

    mov r1, $(512 / 2 / 8)   // Pushing 512 bytes in halfwords (unrolled x8)
    2:
      .rept 16
        ldrb r2, [r4], #1    // Load byte from buffer
        str r2, [r6]
      .endr
      subs r1, r1, $1
      bne 2b

    .set i, 0
    .rept 8                  // Send 16x4 bits of checksum
      ldrb r2, [sp, #(i)]
      str r2, [r6]
      .set i, i+1
    .endr

    // Response token is 8 bits, only transmitted via DAT0.
    // Write one to the bus, read a couple more times (ignore 3 MSB)
    mov  r2, $(~0)            // All data lines high after last bit
    strh r2, [r6]
    strh r2, [r6]
    strh r2, [r6]
    strh r2, [r6]

    // Get status code (3 bits) + dummy bit
    mov r0, $0
    .rept 4
      lsl r0, $1
      ldrh r2, [r6]
      tst r2, $SD_DATA0
      orrne r0, $0x1
    .endr

    // Pipe one byte, some cards do not start actually writing (ie. go busy?)
    // until we pipe an extra clock? Perhaps not, but does no harm.
    .rept 4
      ldr r2, [r6]
    .endr

    // Check status code, bail on error
    cmp r0, $0x5              // 0.010.1 means "data accepted" (0.status.1)
    bne 3f                    // Wait and exit with error

    // Break out if we are on the last block!
    subs r5, $1
    beq 5f

    // Calculate the block checksum for the next block to send.
    // (helps hiding busy latency!)
    mov r0, r4
    mov r1, sp
    bl crc16_nibble_512_nolut   // Must be in IWRAM as well!

    // Perform a wait on the data bus, DAT0 goes high when ready.
    mov r1, $(CMD_WAIT_DATA)
    2:
      subs r1, r1, $1
      beq 4f
      ldrh r2, [r6]
      tst r2, $(SD_DATA0)
    beq 2b

    b 1b  // Continue loop on the next block.

5:
  // Perform a final wait, ensure we leave with no pending operations.
  mov r0, $(CMD_WAIT_DATA)
  2:
    subs r0, r0, $1
    beq 4f
    ldrh r2, [r6]
    tst r2, $(SD_DATA0)
  beq 2b

  add sp, $8
  pop {r4-r6, lr}
  mov r0, $0            // Return OK
  bx lr

3:  // Wait-and-exit on error
  mov r0, $(CMD_WAIT_DATA)
  2:
    subs r0, r0, $1
    beq 4f
    ldrh r2, [r6]
    tst r2, $(SD_DATA0)
  beq 2b

4:  // Return non zero on timeout/response error
  add sp, $8
  pop {r4-r6, lr}
  mov r0, $1
  bx lr

#else

.type sc_write_sectors_w0,function
sc_write_sectors_w0:
.type sc_write_sectors_w1,function
sc_write_sectors_w1:

  push {r4-r11, lr}
  mov r6, $(SC_WRITE_REGISTER_8)
  mov r7, $(SCLITE_DATA_REGISTER32)

  sub sp, $8    // Some space for the CRC buffer
  mov r4, r0    // Input buffer
  mov r5, r1    // Block count

  // Calculate crc for the first block.
  mov r1, sp
  bl crc16_nibble_512_nolut

  // See if the input buffer is aligned for more performance.
  tst r4, $3
  bne 9f

  1: // Loop r5 times. Try to perform checksum while waiting for write to finish.

    // Go ahead send the data! Send the first byte token (0xFE? should be 0xFC!)
    mov r1, $0
    mov r2, $0xFFFFFFFF
    str r2, [r6]
    str r2, [r6]
    str r2, [r6]
    strh r2, [r6]
    strh r1, [r6]

    .rept 16
      ldm r4!, {r0, r1, r2, r3, r8, r9, r10, r11}
      stm r7, {r0, r1, r2, r3, r8, r9, r10, r11}
    .endr

    // Send checksum in one go
    ldr r2, [sp, #0]
    ldr r3, [sp, #4]
    stm r7, {r2, r3}

    // Response token is 8 bits, only transmitted via DAT0.
    // Write one to the bus, read a couple more times (ignore 3 MSB)
    mov  r2, $(~0)            // All data lines high after last bit
    strh r2, [r6]
    strh r2, [r6]
    strh r2, [r6]
    strh r2, [r6]

    // Get status code (3 bits) + dummy bit
    mov r0, $0
    .rept 4
      lsl r0, $1
      ldrh r2, [r6]
      tst r2, $SD_DATA0
      orrne r0, $0x1
    .endr

    // Pipe one byte, some cards do not start actually writing (ie. go busy?)
    // until we pipe an extra clock? Perhaps not, but does no harm.
    .rept 4
      ldr r2, [r6]
    .endr

    // Check status code, bail on error
    cmp r0, $0x5              // 0.010.1 means "data accepted" (0.status.1)
    bne 3f                    // Wait and exit with error

    // Break out if we are on the last block!
    subs r5, $1
    beq 5f

    // Calculate the block checksum for the next block to send.
    // (helps hiding busy latency!)
    mov r0, r4
    mov r1, sp
    bl crc16_nibble_512_nolut   // Must be in IWRAM as well!

    // Perform a wait on the data bus, DAT0 goes high when ready.
    mov r1, $(CMD_WAIT_DATA)
    2:
      subs r1, r1, $1
      beq 4f
      ldrh r2, [r6]
      tst r2, $(SD_DATA0)
    beq 2b

    b 1b  // Continue loop on the next block.


  // SLOW path. Uses byte-level reads.
9:
  1: // Loop r5 times. Try to perform checksum while waiting for write to finish.

    // Go ahead send the data! Send the first byte token (0xFE? should be 0xFC!)
    mov r1, $0
    mov r2, $0xFFFFFFFF
    str r2, [r6]
    str r2, [r6]
    str r2, [r6]
    strh r2, [r6]
    strh r1, [r6]

    mov r1, $(512 / 4 / 8)   // Pushing 512 bytes in halfwords (unrolled x8)
    2:
      .rept 8
        ldrb r2, [r4], #1    // Load bytes from buffer
        ldrb r3, [r4], #1
        orr r2, r2, r3, lsl #8
        ldrb r3, [r4], #1
        orr r2, r2, r3, lsl #16
        ldrb r3, [r4], #1
        orr r2, r2, r3, lsl #24

        str r2, [r7]         // Write using the 32 bit interface
      .endr
      subs r1, r1, $1
      bne 2b

    // Send checksum in one go
    ldr r2, [sp, #0]
    ldr r3, [sp, #4]
    stm r7, {r2, r3}

    // Response token is 8 bits, only transmitted via DAT0.
    // Write one to the bus, read a couple more times (ignore 3 MSB)
    mov  r2, $(~0)            // All data lines high after last bit
    strh r2, [r6]
    strh r2, [r6]
    strh r2, [r6]
    strh r2, [r6]

    // Get status code (3 bits) + dummy bit
    mov r0, $0
    .rept 4
      lsl r0, $1
      ldrh r2, [r6]
      tst r2, $SD_DATA0
      orrne r0, $0x1
    .endr

    // Pipe one byte, some cards do not start actually writing (ie. go busy?)
    // until we pipe an extra clock? Perhaps not, but does no harm.
    .rept 4
      ldr r2, [r6]
    .endr

    // Check status code, bail on error
    cmp r0, $0x5              // 0.010.1 means "data accepted" (0.status.1)
    bne 3f                    // Wait and exit with error

    // Break out if we are on the last block!
    subs r5, $1
    beq 5f

    // Calculate the block checksum for the next block to send.
    // (helps hiding busy latency!)
    mov r0, r4
    mov r1, sp
    bl crc16_nibble_512_nolut   // Must be in IWRAM as well!

    // Perform a wait on the data bus, DAT0 goes high when ready.
    mov r1, $(CMD_WAIT_DATA)
    2:
      subs r1, r1, $1
      beq 4f
      ldrh r2, [r6]
      tst r2, $(SD_DATA0)
    beq 2b

    b 1b  // Continue loop on the next block.


5:
  // Perform a final wait, ensure we leave with no pending operations.
  mov r0, $(CMD_WAIT_DATA)
  2:
    subs r0, r0, $1
    beq 4f
    ldrh r2, [r6]
    tst r2, $(SD_DATA0)
  beq 2b

  add sp, $8
  pop {r4-r11, lr}
  mov r0, $0            // Return OK
  bx lr

3:  // Wait-and-exit on error
  mov r0, $(CMD_WAIT_DATA)
  2:
    subs r0, r0, $1
    beq 4f
    ldrh r2, [r6]
    tst r2, $(SD_DATA0)
  beq 2b

4:  // Return non zero on timeout/response error
  add sp, $8
  pop {r4-r11, lr}
  mov r0, $1
  bx lr

#endif

.pool

// Command-related routines
// These are placed here so they can be reimplemented on other modes.

.text
.align 4
.thumb

.global send_empty_clocks
.thumb_func
// Sends empty clocks (just read the CMD register to force a clock)
// r0: number of clocks to send
send_empty_clocks:
  ldr r1, =SC_RDWR_COMMAND
  1:
    ldrh r2, [r1]
    sub r0, $1
    bne 1b
  bx lr

.global wait_sdcard_idle
.thumb_func
// Waits until the CMD line (bit0) goes high, indicating IDLE.
// r0: number of iterations before giving up
wait_sdcard_idle:
  ldr r1, =SC_RDWR_COMMAND
  mov r3, r0
  mov r0, $1
  1:
    ldrh r2, [r1]
    lsl r2, #31        // Move LSB to MSB
    bne 2f             // If LSB is set, return true (Idle reached)
    sub r3, $1         // Decrement timeout.
    bne 1b

  mov r0, $0
2:bx lr

.global wait_dat0_idle
.thumb_func
// Waits until the DAT0 line goes high, indicating not busy.
// r0: number of iterations before giving up
wait_dat0_idle:
  ldr r1, =SC_WRITE_REGISTER_8
  ldr r3, =SD_DATA0
  1:
    ldrh r2, [r1]
    tst r2, r3         // Check the right bit (SD_DATA0)
    bne 2f             // If bit set, it is idlea again, return true
    sub r0, $1         // Decrement timeout.
    bne 1b

  mov r0, $0
  bx lr
2:
  mov r0, $1
  bx lr


.global receive_sdcard_response
.thumb_func
// Waits for a response (or returns error) and receives a response to a buffer.
// r0: buffer pointer where we write the response (can be null!)
// r1: response length in bytes
// r2: number of iterations before giving up
receive_sdcard_response:
  push {r4-r6}
  ldr r4, =SC_RDWR_COMMAND

  mov r6, $7           // First bit belongs to first byte (always zero)
  mov r5, $0

  1:
    ldrh r3, [r4]
    lsr r3, #1         // Extract LSB to Carry Flag
    bcc 4f             // If LSB is clear, we can receive the next bits!
    sub r2, $1         // Decrement timeout.
    bne 1b

  mov r0, $0           // Timeout, return false
  pop {r4-r6}
  bx lr

  3:

    // Read 8 bits (MSB to LSB)
    mov r6, $8
    4:
      ldrh r3, [r4]
      lsr r3, #1       // Extract LSB into Carry Flag
      adc r5, r5       // Shift register left by 1, add CF to LSB
      sub r6, $1
      bne 4b

    cmp r0, $0
    beq 5f

    strb r5, [r0]      // Store it in the buffer, if ptr!=null
    add r0, $1

    5:
    sub r1, $1
    bne 3b

  mov r0, $1           // Return true
  pop {r4-r6}
  bx lr


.global send_sdcard_commandbuf
.thumb_func
// Waits for a response (or returns error) and receives a response to a buffer.
// r0: buffer pointer to send
// r1: command length in bytes
send_sdcard_commandbuf:
  ldr r3, =SC_RDWR_COMMAND

  1:
    ldrb r2, [r0]
    add r0, $1
    .rept 8
      strh r2, [r3]
      lsl r2, #1
    .endr

    sub r1, $1
    bne 1b

  bx lr

.pool

