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

// Here we have the EEPROM/FLASH -> SRAM patches
// These routines are patched over existing EEPROM/FLASH routines to convert
// the ROMs into an SRAM-based storage type.
// There are two routines to patch for EEPROM storage: read and program.

// TODO: Emulate bad arguments/inputs are return proper error codes?
// Could be that some game relies on them or something.

.text
.balign 4
.thumb

// RTC patches/hooks
.global patch_rtc_probe
.global patch_rtc_probe_size
.global patch_rtc_getstatus
.global patch_rtc_getstatus_size
.global patch_rtc_gettimedate
.global patch_rtc_gettimedate_size
.global patch_rtc_reset
.global patch_rtc_reset_size

// EEPROM/FLASH patches for SRAM conversion
.global patch_eeprom_read_sram64k
.global patch_eeprom_read_sram64k_size
.global patch_eeprom_write_sram64k
.global patch_eeprom_write_sram64k_size

.global patch_flash_read_sram64k
.global patch_flash_read_sram64k_size
.global patch_flash_erase_device_sram64k
.global patch_flash_erase_device_sram64k_size
.global patch_flash_erase_sector_sram64k
.global patch_flash_erase_sector_sram64k_size
.global patch_flash_write_sector_sram64k
.global patch_flash_write_sector_sram64k_size
.global patch_flash_write_byte_sram64k
.global patch_flash_write_byte_sram64k_size

.global patch_flash_read_sram128k
.global patch_flash_read_sram128k_size
.global patch_flash_erase_device_sram128k
.global patch_flash_erase_device_sram128k_size
.global patch_flash_erase_sector_sram128k
.global patch_flash_erase_sector_sram128k_size
.global patch_flash_write_sector_sram128k
.global patch_flash_write_sector_sram128k_size
.global patch_flash_write_byte_sram128k
.global patch_flash_write_byte_sram128k_size

// EEPROM/FLASH patches for direct save mode.
.global patch_eeprom_read_directsave
.global patch_eeprom_read_directsave_size
.global patch_eeprom_write_directsave
.global patch_eeprom_write_directsave_size

.global patch_flash_read_directsave
.global patch_flash_read_directsave_size
.global patch_flash_erase_device_directsave
.global patch_flash_erase_device_directsave_size
.global patch_flash_erase_sector_directsave
.global patch_flash_erase_sector_directsave_size
.global patch_flash_write_sector_directsave
.global patch_flash_write_sector_directsave_size
.global patch_flash_write_byte_directsave
.global patch_flash_write_byte_directsave_size


#ifdef SUPERCHIS_IO
  #define SRAM_BANK(reg, bank)   ldr reg, =(0x0100 | (bank))
#else
  #define SRAM_BANK(reg, bank)   mov reg, $(0x0001 | ((bank) << 2))
#endif



// This function reads a byte from the emulated EEPROM
// The output format is a bit odd, instead of a single byte we output 8 bytes
// each of them being 0x00 or 0x01 (due to how the EEPROM works). The bit order
// is MSB to LSB.

// R0: Block number (0-63 or 0-1023) to read from.
// R1: Byte buffer where the byte will be written (as bits)
// Returns 0 on success?
patch_eeprom_read_sram64k:
  lsl r0, r0, #22            // Cap address to 0x3FF
  lsr r0, r0, #19            // and mult by 8 (8 byte blocks)
  mov r2, #0x0E              // Load the SRAM base (0x0E000000)
  lsl r2, #24
  add r2, r2, r0             // Address of the desired byte
  add r2, r2, #8             // Read from the MSB
  mov r0, #8                 // Bit counter
1:
  sub r2, #1                 // Decrement read pointer
  ldrb r3, [r2]              // Read the byte
  strb r3, [r1]              // Write byte to buffer
  add r1, #1                 // Advance write pointer
  sub r0, #1                 // Decrement counter
  bne 1b
  bx lr                      // Return, r0 is zero here.
  .balign 4
patch_eeprom_read_sram64k_size:
  .word (patch_eeprom_read_sram64k_size - patch_eeprom_read_sram64k)

// Similarly, this function gets an 8 byte buffer (8 bits) and stores it
// in the SRAM memory (at the specified address).

// R0: Block number (0-63 or 0-1023) to write to.
// R1: Byte buffer where the byte will be read (as bits)
// Returns 0 on success?
patch_eeprom_write_sram64k:
  lsl r0, r0, #22            // Cap address to 0x3FF
  lsr r0, r0, #19            // and mult by 8 (8 byte blocks)
  mov r2, #0x0E              // Load the SRAM base (0x0E000000)
  lsl r2, #24
  add r2, r2, r0             // Address of the desired byte
  add r2, r2, #8             // Write in inverse order (MSB first)
  mov r0, #8                 // Bit counter
1:
  ldrb r3, [r1]              // Read byte from buffer
  add r1, #1                 // Advance read pointer
  sub r2, #1                 // Decrement write pointer
  strb r3, [r2]              // Write the byte
  sub r0, #1                 // Decrement counter
  bne 1b
  bx lr                      // Return, r0 is zero here.
  .balign 4
patch_eeprom_write_sram64k_size:
  .word (patch_eeprom_write_sram64k_size - patch_eeprom_write_sram64k)


// FLASH read/write/erase patch routines.


// FLASH -> SRAM (64KB) conversion routines.
// These convert flash routines into SRAM routines.

// Reads data/sector to the user buffer
// R0: Sector to read (4KiB sector size)
// R1: byte offset withing the sector
// R2: buffer pointer (destination)
// R3: amount of bytes to read
patch_flash_read_sram64k:
  lsl r0, #28; lsr r0, #16   // Limit sector number to [0...15] + lsl #12
  add r0, r1                 // Address to read in the backup memory
  mov r1, #0x0E              // Add SRAM base to it
  lsl r1, #24
  add r0, r1
1:
  ldrb r1, [r0]              // Copy one byte at a time
  strb r1, [r2]
  add r0, #1
  add r2, #1
  sub r3, #1
  bne 1b

  mov r0, #0                 // Return zero
  bx lr
  .balign 4
patch_flash_read_sram64k_size:
  .word (patch_flash_read_sram64k_size - patch_flash_read_sram64k)


// Clears the full flash device (fills it with 0xFF)
patch_flash_erase_device_sram64k:
  mov r0, #64                // Clear 64KiB
  lsl r0, #10
  mov r2, #0x0E              // Load the SRAM base (0x0E000000)
  lsl r2, #24
  mov r1, #0xFF              // Clear with all-set-bits
1:
  strb r1, [r2, #0]          // Write byte-by-byte to SRAM
  strb r1, [r2, #1]
  strb r1, [r2, #2]
  strb r1, [r2, #3]

  add r2, #4
  sub r0, #1
  bne 1b

  bx lr                      // Return, r0 is zero here.
  .balign 4
patch_flash_erase_device_sram64k_size:
  .word (patch_flash_erase_device_sram64k_size - patch_flash_erase_device_sram64k)


// Clears a sector in the flash device (fills it with 0xFF)
// R0: Sector number to wipe
patch_flash_erase_sector_sram64k:
  lsl r0, #28; lsr r0, #16   // Limit sector number to [0...15] + lsl #12 (sectors are 4KB)
  mov r2, #0x0E              // Load the SRAM base (0x0E000000)
  lsl r2, #24
  add r2, r2, r0             // r2 points to the base of the sector

  mov r1, #0xFF              // Clear with all-set-bits
  mov r0, #4                 // Clear 4KiB
  lsl r0, #10
1:
  strb r1, [r2, #0]          // Write byte-by-byte to SRAM
  strb r1, [r2, #1]
  strb r1, [r2, #2]
  strb r1, [r2, #3]

  add r2, #4
  sub r0, #1
  bne 1b

  bx lr                      // Return, r0 is zero here.
  .balign 4
patch_flash_erase_sector_sram64k_size:
  .word (patch_flash_erase_sector_sram64k_size - patch_flash_erase_sector_sram64k)


// Writes a sector with a user-provided buffer.
// R0: Sector number to write
// R1: User provided pointer
patch_flash_write_sector_sram64k:
  lsl r0, #28; lsr r0, #16   // Limit sector number to [0...15] + lsl#12
  mov r2, #0x0E              // Load the SRAM base (0x0E000000)
  lsl r2, #24
  add r2, r2, r0             // r2 points to the base of the sector

  mov r0, #4                 // Copy 4KiB
  lsl r0, #10
1:
  ldrb r3, [r1, #0]          // Copy byte by byte into SRAM
  strb r3, [r2, #0]
  ldrb r3, [r1, #1]
  strb r3, [r2, #1]
  ldrb r3, [r1, #2]
  strb r3, [r2, #2]
  ldrb r3, [r1, #3]
  strb r3, [r2, #3]

  add r1, #4
  add r2, #4
  sub r0, #4
  bne 1b

  bx lr                      // Return, r0 is zero here.
  .balign 4
patch_flash_write_sector_sram64k_size:
  .word (patch_flash_write_sector_sram64k_size - patch_flash_write_sector_sram64k)


// Writes a single byte to a specifc sector and offset.
// R0: Sector number to write
// R1: Offset within the sector
// R2: Data (byte) to write
patch_flash_write_byte_sram64k:
  lsl r0, #28; lsr r0, #16   // Limit sector number to [0...15] + lsl#12

  add r0, r0, r1             // Full offset from flash start
  mov r1, #0x0E              // Load the SRAM base (0x0E000000)
  lsl r1, #24
  add r0, r0, r1             // Full address of the byte to program

  strb r2, [r0]              // Write the byte

  mov r0, #0
  bx lr
  .balign 4
patch_flash_write_byte_sram64k_size:
  .word (patch_flash_write_byte_sram64k_size - patch_flash_write_byte_sram64k)



// FLASH -> SRAM (128KB) conversion routines.

// Reads data/sector to the user buffer
// R0: Sector to read (4KiB sector size)
// R1: byte offset withing the sector
// R2: buffer pointer (destination)
// R3: amount of bytes to read
// Around 118 bytes are available for this patch.
patch_flash_read_sram128k:
  push {r4, r5, r6}
  ldr r5, =0xA55A
  ldr r4, =0x9FFFFFE

  lsl r0, #28                // Limit sector number to [0...15] + lsl #12
  bcc 2f                     // Sector num MSB is in carry flag
    SRAM_BANK(r6, 1)         // Switch to bank 1, since the sector num is 16..31
    strh r5, [r4]
    strh r5, [r4]
    strh r6, [r4]
    strh r6, [r4]

2:
  lsr r0, #16                // Finish limit + lsl12

  add r0, r1                 // Address to read in the backup memory
  mov r1, #0x0E              // Add SRAM base to it
  lsl r1, #24
  add r0, r1
1:
  ldrb r1, [r0]              // Copy one byte at a time
  strb r1, [r2]
  add r0, #1
  add r2, #1
  sub r3, #1
  bne 1b

  SRAM_BANK(r6, 0)           // Switch to bank 0
  strh r5, [r4]
  strh r5, [r4]
  strh r6, [r4]
  strh r6, [r4]

  mov r0, #0                 // Return zero
  pop {r4, r5, r6}
  bx lr

  .balign 4
  .pool
patch_flash_read_sram128k_size:
  .word (patch_flash_read_sram128k_size - patch_flash_read_sram128k)


// Clears the full flash device (fills it with 0xFF), max ~44 instr.
patch_flash_erase_device_sram128k:
  push {r4, r5}
  ldr r5, =0xA55A
  ldr r4, =0x9FFFFFE

  mov r2, #0x0E              // Load the SRAM base (0x0E000000)
  lsl r2, #24
  mov r1, #0xFF              // Clear with all-set-bits

  // Clear first bank.
  mov r0, #64                // Clear 64KiB
  lsl r0, #10
1:
  strb r1, [r2, #0]          // Write byte-by-byte to SRAM
  strb r1, [r2, #1]
  strb r1, [r2, #2]
  strb r1, [r2, #3]
  add r2, #4
  sub r0, #1
  bne 1b

  SRAM_BANK(r3, 1)           // Switch to bank 1, might enable SDRAM writes (dangerous!)
  strh r5, [r4]
  strh r5, [r4]
  strh r3, [r4]
  strh r3, [r4]

  mov r0, #64                // Clear 64KiB
  lsl r0, #10
1:
  strb r1, [r2, #0]          // Write byte-by-byte to SRAM
  strb r1, [r2, #1]
  strb r1, [r2, #2]
  strb r1, [r2, #3]

  add r2, #4
  sub r0, #1
  bne 1b

  SRAM_BANK(r3, 0)           // Switch to bank 0
  strh r5, [r4]
  strh r5, [r4]
  strh r3, [r4]
  strh r3, [r4]

  pop {r4, r5}
  bx lr                      // Return, r0 is zero here.

  .balign 4
  .pool
patch_flash_erase_device_sram128k_size:
  .word (patch_flash_erase_device_sram128k_size - patch_flash_erase_device_sram128k)


// Clears a sector in the flash device (fills it with 0xFF)
// R0: Sector number to wipe
patch_flash_erase_sector_sram128k:
  push {r4, r5}
  ldr r5, =0xA55A
  ldr r4, =0x9FFFFFE

  lsl r0, #28                // Limit sector number to [0...15], MSB into Carry
  bcc 2f
    SRAM_BANK(r3, 1)         // Switch to bank 1, since the sector num is 16..31
    strh r5, [r4]
    strh r5, [r4]
    strh r3, [r4]
    strh r3, [r4]

2:
  lsr r0, #16                // Complete the clamp plus shift

  mov r2, #0x0E              // Load the SRAM base (0x0E000000)
  lsl r2, #24
  add r2, r2, r0             // r2 points to the base of the sector

  mov r1, #0xFF              // Clear with all-set-bits
  mov r0, #4                 // Clear 4KiB
  lsl r0, #10
1:
  strb r1, [r2, #0]          // Write byte-by-byte to SRAM
  strb r1, [r2, #1]
  strb r1, [r2, #2]
  strb r1, [r2, #3]

  add r2, #4
  sub r0, #1
  bne 1b

  SRAM_BANK(r3, 0)           // Unconditionally switch to bank 0
  strh r5, [r4]
  strh r5, [r4]
  strh r3, [r4]
  strh r3, [r4]

  pop {r4, r5}
  bx lr                      // Return, r0 is zero here.
  .balign 4
  .pool
patch_flash_erase_sector_sram128k_size:
  .word (patch_flash_erase_sector_sram128k_size - patch_flash_erase_sector_sram128k)


// Writes a sector with a user-provided buffer.
// R0: Sector number to write
// R1: User provided pointer
patch_flash_write_sector_sram128k:
  push {r4, r5}
  ldr r5, =0xA55A
  ldr r4, =0x9FFFFFE

  lsl r0, #28                // Limit sector number to [0...15] + lsl#12
  bcc 2f
    SRAM_BANK(r3, 1)         // Switch to bank 1, since the sector num is 16..31
    strh r5, [r4]
    strh r5, [r4]
    strh r3, [r4]
    strh r3, [r4]

2:
  lsr r0, #16                // Finish lsl#12

  mov r2, #0x0E              // Load the SRAM base (0x0E000000)
  lsl r2, #24
  add r2, r2, r0             // r2 points to the base of the sector

  mov r0, #4                 // Copy 4KiB
  lsl r0, #10
1:
  ldrb r3, [r1, #0]          // Copy byte by byte into SRAM
  strb r3, [r2, #0]
  ldrb r3, [r1, #1]
  strb r3, [r2, #1]
  ldrb r3, [r1, #2]
  strb r3, [r2, #2]
  ldrb r3, [r1, #3]
  strb r3, [r2, #3]

  add r1, #4
  add r2, #4
  sub r0, #4
  bne 1b

  SRAM_BANK(r3, 0)           // Unconditionally switch to bank 0
  strh r5, [r4]
  strh r5, [r4]
  strh r3, [r4]
  strh r3, [r4]

  pop {r4, r5}
  bx lr                      // Return, r0 is zero here.
  .balign 4
  .pool
patch_flash_write_sector_sram128k_size:
  .word (patch_flash_write_sector_sram128k_size - patch_flash_write_sector_sram128k)


// Writes a single byte to a specifc sector and offset.
// R0: Sector number to write
// R1: Offset within the sector
// R2: Data (byte) to write
patch_flash_write_byte_sram128k:
  push {r4, r5}
  ldr r5, =0xA55A
  ldr r4, =0x9FFFFFE

  lsl r0, #28                // Limit sector number to [0...15] + lsl#12
  bcc 2f
    SRAM_BANK(r3, 1)         // Switch to bank 1, since the sector num is 16..31
    strh r5, [r4]
    strh r5, [r4]
    strh r3, [r4]
    strh r3, [r4]

2:
  lsr r0, #16                // Finish lsl#12

  add r0, r0, r1             // Full offset from flash start
  mov r1, #0x0E              // Load the SRAM base (0x0E000000)
  lsl r1, #24
  add r0, r0, r1             // Full address of the byte to program

  strb r2, [r0]              // Write the byte

  SRAM_BANK(r3, 0)           // Unconditionally switch to bank 0
  strh r5, [r4]
  strh r5, [r4]
  strh r3, [r4]
  strh r3, [r4]

  mov r0, #0
  pop {r4, r5}
  bx lr
  .balign 4
  .pool
patch_flash_write_byte_sram128k_size:
  .word (patch_flash_write_byte_sram128k_size - patch_flash_write_byte_sram128k)



// Direct Saving patch routines.
// These convert EEPROM/FLASH save routines into SD saving routines.
// Data is read and written from/to SD card directly.

// Keep these in sync with directsaver.S!
#define READ_EEPROM_FN                (0)
#define WRITE_EEPROM_FN               (1)
#define READ_FLASH_FN                 (2)
#define WRITE_FLASH_FN                (3)
#define ERASE_CHIP_FLASH_FN           (4)
#define ERASE_SECTOR_FLASH_FN         (5)


#define trampoline_arm_r3()                \
  .balign 4;                               \
  /* r3 contains the jumping addr */;      \
33:;                                       \
  .thumb;                                  \
  bx pc; nop;                              \
  .arm;                                    \
  bx r3;                                   \
  .thumb;                                  \
  .balign 4;

// This routine is similar to patch_eeprom_read_sram64k since the date is loaded
// from SRAM. However, it honors memory_size when it comes to parameter testing.

// R0: Block number (0-63 or 0-1023) to read from.
// R1: Byte buffer where the byte will be written (as bits)
patch_eeprom_read_directsave:
  push {r7, lr}

  mov r2, r0                 // Swap args
  mov r0, r1
  mov r1, r2

  mov r7, $READ_EEPROM_FN
  adr r3, patch_eeprom_read_directsave_size
  ldr r3, [r3]
  bl 33f

  pop {r7}
  pop {r1}
  bx r1
  trampoline_arm_r3()

  .balign 4
patch_eeprom_read_directsave_size:
  .word (patch_eeprom_read_directsave_size - patch_eeprom_read_directsave)


// R0: Block number (0-63 or 0-1023) to write to.
// R1: Byte buffer where the byte will be read (as bits)
patch_eeprom_write_directsave:
  push {r7, lr}

  mov r2, r0                 // Swap args
  mov r0, r1
  mov r1, r2

  mov r7, $WRITE_EEPROM_FN
  adr r3, patch_eeprom_write_directsave_size
  ldr r3, [r3]
  bl 33f

  // The handler does actually return an error code in r0
  pop {r7}
  pop {r1}
  bx r1
  trampoline_arm_r3()
  .balign 4
patch_eeprom_write_directsave_size:
  .word (patch_eeprom_write_directsave_size - patch_eeprom_write_directsave)


// Reads data/sector to the user buffer
// R0: Sector to read (4KiB sector size)
// R1: byte offset withing the sector
// R2: buffer pointer (destination)
// R3: amount of bytes to read
.balign 4
patch_flash_read_directsave:
  push {r7, lr}
  lsl r0, #27; lsr r0, #15   // Limit sector number to [0...31] + lsl #12
  add r1, r0                 // Data offset to read at.

  mov r0, r2                 // User buffer (goes in r0)
  mov r2, r3                 // Byte count (into r2)

  // The address of the DirectSave payload is written right after the patch.
  mov r7, $READ_FLASH_FN
  adr r3, patch_flash_read_directsave_size
  ldr r3, [r3]
  bl 33f

  mov r0, #0
  pop {r7}
  pop {r1}
  bx r1
  trampoline_arm_r3()

  .balign 4
patch_flash_read_directsave_size:
  .word (patch_flash_read_directsave_size - patch_flash_read_directsave)

// Clears the full flash device (fills it with 0xFF)
.balign 4
patch_flash_erase_device_directsave:
  push {r7, lr}

  mov r7, $ERASE_CHIP_FLASH_FN
  adr r3, patch_flash_erase_device_directsave_size
  ldr r3, [r3]
  bl 33f

  mov r0, #0
  pop {r7}
  pop {r1}
  bx r1
  trampoline_arm_r3()

.balign 4
patch_flash_erase_device_directsave_size:
  .word (patch_flash_erase_device_directsave_size - patch_flash_erase_device_directsave)


// Clears a sector in the flash device (fills it with 0xFF)
// R0: Sector number to wipe
.balign 4
patch_flash_erase_sector_directsave:
  push {r7, lr}
  lsl r0, #27; lsr r0, #27   // Limit sector number to [0...31]

  mov r7, $ERASE_SECTOR_FLASH_FN
  adr r3, patch_flash_erase_sector_directsave_size
  ldr r3, [r3]

  bl 33f

  mov r0, #0
  pop {r7}
  pop {r1}
  bx r1
  trampoline_arm_r3()

.balign 4
patch_flash_erase_sector_directsave_size:
  .word (patch_flash_erase_sector_directsave_size - patch_flash_erase_sector_directsave)

// Writes a sector with a user-provided buffer.
// R0: Sector number to write
// R1: User provided pointer
.balign 4
patch_flash_write_sector_directsave:
  push {r7, lr}
  lsl r0, #27; lsr r0, #27   // Limit sector number to [0...31]

  mov r2, r0                 // Swap args
  mov r0, r1
  mov r1, r2

  mov r7, $WRITE_FLASH_FN
  adr r3, patch_flash_write_sector_directsave_size
  ldr r3, [r3]

  bl 33f

  mov r0, #0
  pop {r7}
  pop {r1}
  bx r1
  trampoline_arm_r3()

.balign 4
patch_flash_write_sector_directsave_size:
  .word (patch_flash_write_sector_directsave_size - patch_flash_write_sector_directsave)

// Writes a single byte to a specifc sector and offset.
// R0: Sector number to write
// R1: Offset within the sector
// R2: Data (byte) to write
patch_flash_write_byte_directsave:
  push {r7, lr}
  sub sp, $256               // Reserve a sector worth of data
  sub sp, $256

  push {r0, r1, r2}          // Save write information

  // We need to read a sector, replace a byte, and write it back.
  // This function sucks but it should be used rarely.
  lsl r0, #27                // Limit sector number to [0...31] + lsl #12
  lsr r1, r0, #15

  mov r0, sp
  mov r2, $128; lsl r2, #2;  // Load 512 bytes

  mov r7, $READ_FLASH_FN
  adr r3, patch_flash_write_byte_directsave_size
  ldr r3, [r3]
  bl 33f

  // Data was read, proceed to patch the sector byte
  pop {r0, r1, r2}
  mov r3, sp
  strb r2, [r3, r1]

  mov r1, r0
  mov r0, sp

  mov r7, $WRITE_FLASH_FN
  adr r3, patch_flash_write_byte_directsave_size
  ldr r3, [r3]
  bl 33f

  add sp, $256
  add sp, $256

  mov r0, #0
  pop {r7}
  pop {r1}
  bx r1
  trampoline_arm_r3()

.balign 4
patch_flash_write_byte_directsave_size:
  .word (patch_flash_write_byte_directsave_size - patch_flash_write_byte_directsave)


// RTC emulation implementation.
// We (ab)use the GPIO port memory range [0xC4...0xC9] that contains 6 bytes.
// We store the current time and date in this memory block as follows:
//
// 0xC4: TBD
// 0xC5: Hours (in 24h format)
// 0xC6: Minutes
// 0xC7: Day
// 0xC8: Month
// 0xC9: Year    (0x00 means to 2000, so byte + 2000)
//
// We store values there, so that the ingame menu can also tweak them, but this
// works regardless of whether the ingame menu is enabled/patched-in.
//
// struct SiiRtcInfo {
//  u8 year;
//  u8 month;
//  u8 day;
//  u8 dayOfWeek;
//  u8 hour;
//  u8 minute;
//  u8 second;
//  u8 status;               // Return STATUS_24HOUR (0x40)
//  u8 alarmHour;
//  u8 alarmMinute;

// Have around ~200 bytes of code size (~100 insts)
patch_rtc_probe:
  mov r0, $1                 // Just return 1 (working, no errors)
  bx lr
.balign 4
patch_rtc_probe_size:
  .word (patch_rtc_probe_size - patch_rtc_probe)

// Have around ~200 bytes of code size (~100 insts)
patch_rtc_getstatus:
  mov r1, $0x40
  add r0, r0, $7             // Overwrite status only
  strb r1, [r0]
  mov r0, $1                 // Return true
  bx lr
.balign 4
patch_rtc_getstatus_size:
  .word (patch_rtc_getstatus_size - patch_rtc_getstatus)

// Max size should be ~170 bytes (85 insts)
.balign 4
patch_rtc_gettimedate:
  ldr r3, =0x080000C4

  // TODO: Advance RTC?

  mov r2, $(1f - . - 4 - 2)
  add r2, pc

  // Copy date/time in to the struct format.
  mov r1, $0           // Seconds always zero
  strb r1, [r0, #6]

  ldrb r1, [r3, #1]    // Hours
  ldrb r1, [r2, r1]
  strb r1, [r0, #4]

  ldrb r1, [r3, #2]    // Minutes
  ldrb r1, [r2, r1]
  strb r1, [r0, #5]

  ldrb r1, [r3, #3]    // Day
  ldrb r1, [r2, r1]
  strb r1, [r0, #2]

  ldrb r1, [r3, #4]    // Month
  add r1, $1           // 0-11 -> 1-12
  ldrb r1, [r2, r1]
  strb r1, [r0, #1]

  ldrb r1, [r3, #5]    // Year
  ldrb r1, [r2, r1]
  strb r1, [r0, #0]

  mov r0, $1                 // Return true
  bx lr

1:
  .byte 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09
  .byte 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19
  .byte 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29
  .byte 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39
  .byte 0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49
  .byte 0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59
  .byte 0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69
  .byte 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79

  .pool
.balign 4
patch_rtc_gettimedate_size:
  .word (patch_rtc_gettimedate_size - patch_rtc_gettimedate)


// Resets current time and date to Jan 1st 2000, at midnight.
// Max size should be ~130 bytes (65 insts)
.balign 4
patch_rtc_reset:
  ldr r0, =0xA55A            // Enable ROM writes
  ldr r2, =0x09FFFFFE
  mov r3, $0x5
  strh r0, [r2]
  strh r0, [r2]
  strh r3, [r2]
  strh r3, [r2]

  mov r1, $0                 // Clear GPIO data
  str r3, [r1]
  strh r3, [r1, #4]

  mov r3, $0x1               // Disable ROM writes.
  strh r0, [r2]
  strh r0, [r2]
  strh r3, [r2]
  strh r3, [r2]

  mov r0, $1                 // Return true
  bx lr
  .pool
.balign 4
patch_rtc_reset_size:
  .word (patch_rtc_reset_size - patch_rtc_reset)


