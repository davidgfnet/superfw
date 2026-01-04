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

#define START_FUNC(name)  \
  .global name;           \
  .type name, %function;  \
  .balign 4;              \
  name:

#define END_FUNC(name)          \
  .balign 4;                    \
  .pool;                        \
  .balign 4;                    \
  name##_size:                  \
    .word (name##_size - name); \
  name##_end:                   \
  .global name##_size;          \
  .global name##_end;

// When running superchis, write the bank bit only (3rd)
#ifdef SUPERCHIS_IO
  #define SRAM_BANK(bank)   (0x0070 | ((bank) << 3))
#else
  #define SRAM_BANK(bank)   (0x0001 | ((bank) << 2))
#endif

// EEPROM read/write patch routines.

// R0: Block number (0-63 or 0-1023) to read from.
// R1: Byte buffer where the byte will be written (as bits)
// Returns 0 on success? (Space: 168 bytes)
START_FUNC(patch_eeprom_read_sram64k)
  lsl r0, r0, #22            // Cap address to 0x3FF
  lsr r0, r0, #19            // and mult by 8 (8 byte blocks)
  ldr r2, =0x0E000000        // Load the SRAM base (0x0E000000)
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
END_FUNC(patch_eeprom_read_sram64k)

// Similarly, this function gets an 8 byte buffer (8 bits) and stores it
// in the SRAM memory (at the specified address).

// R0: Block number (0-63 or 0-1023) to write to.
// R1: Byte buffer where the byte will be read (as bits)
// Returns 0 on success?  (Space: 204 bytes)
START_FUNC(patch_eeprom_write_sram64k)
  lsl r0, r0, #22            // Cap address to 0x3FF
  lsr r0, r0, #19            // and mult by 8 (8 byte blocks)
  ldr r2, =0x0E000000        // Load the SRAM base (0x0E000000)
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
END_FUNC(patch_eeprom_write_sram64k)


// FLASH read/write/erase patch routines.


// FLASH -> SRAM (64KB) conversion routines.
// These convert flash routines into SRAM routines.

// Reads data/sector to the user buffer
// R0: Sector to read (4KiB sector size)
// R1: byte offset withing the sector
// R2: buffer pointer (destination)
// R3: amount of bytes to read
// Has 118 bytes of space.
START_FUNC(patch_flash_read_sram64k)
  lsl r0, #28; lsr r0, #16   // Limit sector number to [0...15] + lsl #12
  add r0, r1                 // Address to read in the backup memory
  ldr r1, =0x0E000000
  add r0, r1                 // Add SRAM base to it
1:
  ldrb r1, [r0]              // Copy one byte at a time
  strb r1, [r2]
  add r0, #1
  add r2, #1
  sub r3, #1
  bne 1b

  mov r0, #0                 // Return zero
  bx lr
END_FUNC(patch_flash_read_sram64k)

// Clears the full flash device (fills it with 0xFF)
// Has 90 bytes of space.
START_FUNC(patch_flash_erase_device_sram64k)
  mov r0, #64                // Clear 64KiB
  lsl r0, #10
  ldr r2, =0x0E000000        // Load the SRAM base (0x0E000000)
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
END_FUNC(patch_flash_erase_device_sram64k)


// Clears a sector in the flash device (fills it with 0xFF)
// R0: Sector number to wipe
// Has 110 bytes of space.
START_FUNC(patch_flash_erase_sector_sram64k)
  lsl r0, #28; lsr r0, #16   // Limit sector number to [0...15] + lsl #12 (sectors are 4KB)
  ldr r2, =0x0E000000        // Load the SRAM base (0x0E000000)
  add r2, r2, r0             // r2 points to the base of the sector

  mov r1, #0xFF              // Clear with all-set-bits
  ldr r0, =1024              // Clear 1024 (*4) bytes
1:
  strb r1, [r2, #0]          // Write byte-by-byte to SRAM
  strb r1, [r2, #1]
  strb r1, [r2, #2]
  strb r1, [r2, #3]
  add r2, #4
  sub r0, #1
  bne 1b

  bx lr                      // Return, r0 is zero here.
END_FUNC(patch_flash_erase_sector_sram64k)


// Writes a sector with a user-provided buffer.
// R0: Sector number to write
// R1: User provided pointer
START_FUNC(patch_flash_write_sector_sram64k)
  lsl r0, #28; lsr r0, #16   // Limit sector number to [0...15] + lsl#12
  ldr r2, =0x0E000000        // Load the SRAM base (0x0E000000)
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
END_FUNC(patch_flash_write_sector_sram64k)


// Writes a single byte to a specifc sector and offset.
// R0: Sector number to write
// R1: Offset within the sector
// R2: Data (byte) to write
START_FUNC(patch_flash_write_byte_sram64k)
  lsl r0, #28; lsr r0, #16   // Limit sector number to [0...15] + lsl#12

  add r0, r0, r1             // Full offset from flash start
  ldr r1, =0x0E000000        // Load the SRAM base (0x0E000000)
  add r0, r0, r1             // Full address of the byte to program

  strb r2, [r0]              // Write the byte

  mov r0, #0
  bx lr
END_FUNC(patch_flash_write_byte_sram64k)


// FLASH -> SRAM (128KB) conversion routines.

// Reads data/sector to the user buffer
// R0: Sector to read (4KiB sector size)
// R1: byte offset withing the sector
// R2: buffer pointer (destination)
// R3: amount of bytes to read
// Around 118 bytes are available for this patch.
// Has 118 bytes of space.
START_FUNC(patch_flash_read_sram128k)
  push {r4, r5, r6}
  ldr r5, =0xA55A
  ldr r4, =0x9FFFFFE

  lsl r0, #28                // Limit sector number to [0...15] + lsl #12
  bcc 2f                     // Sector num MSB is in carry flag
    mov r6, $SRAM_BANK(1)    // Switch to bank 1, since the sector num is 16..31
    strh r5, [r4]
    strh r5, [r4]
    strh r6, [r4]
    strh r6, [r4]

2:
  lsr r0, #16                // Finish limit + lsl12

  add r0, r1                 // Address to read in the backup memory
  ldr r1, =0x0E000000        // Add SRAM base to it
  add r0, r1
1:
  ldrb r1, [r0]              // Copy one byte at a time
  strb r1, [r2]
  add r0, #1
  add r2, #1
  sub r3, #1
  bne 1b

  mov r6, $SRAM_BANK(0)      // Switch to bank 0
  strh r5, [r4]
  strh r5, [r4]
  strh r6, [r4]
  strh r6, [r4]

  mov r0, #0                 // Return zero
  pop {r4, r5, r6}
  bx lr
END_FUNC(patch_flash_read_sram128k)


// Clears the full flash device (fills it with 0xFF), max ~44 instr.
// Has 90 bytes of space.
START_FUNC(patch_flash_erase_device_sram128k)
  push {r4, r5}
  ldr r5, =0xA55A
  ldr r4, =0x9FFFFFE

  ldr r2, =0x0E000000        // Load the SRAM base (0x0E000000)
  mov r1, #0xFF              // Clear with all-set-bits

  // Clear first bank.
  mov r0, #32                // Clear 64KiB
  lsl r0, #10
1:
  strb r1, [r2, #0]          // Write byte-by-byte to SRAM
  strb r1, [r2, #1]
  add r2, #2
  sub r0, #1
  bne 1b

  mov r3, $SRAM_BANK(1)      // Switch to bank 1, might enable SDRAM writes (dangerous!)
  strh r5, [r4]
  strh r5, [r4]
  strh r3, [r4]
  strh r3, [r4]

  mov r0, #32                // Clear 64KiB
  lsl r0, #10
1:
  strb r1, [r2, #0]          // Write byte-by-byte to SRAM
  strb r1, [r2, #1]
  add r2, #2
  sub r0, #1
  bne 1b

  mov r3, $SRAM_BANK(0)      // Switch to bank 0
  strh r5, [r4]
  strh r5, [r4]
  strh r3, [r4]
  strh r3, [r4]

  pop {r4, r5}
  bx lr                      // Return, r0 is zero here.
END_FUNC(patch_flash_erase_device_sram128k)


// Clears a sector in the flash device (fills it with 0xFF)
// R0: Sector number to wipe
// Has 110 bytes of space.
START_FUNC(patch_flash_erase_sector_sram128k)
  push {r4, r5}
  ldr r5, =0xA55A
  ldr r4, =0x9FFFFFE

  lsl r0, #28                // Limit sector number to [0...15], MSB into Carry
  bcc 2f
    mov r3, $SRAM_BANK(1)    // Switch to bank 1, since the sector num is 16..31
    strh r5, [r4]
    strh r5, [r4]
    strh r3, [r4]
    strh r3, [r4]

2:
  lsr r0, #16                // Complete the clamp plus shift

  ldr r2, =0x0E000000        // Load the SRAM base (0x0E000000)
  add r2, r2, r0             // r2 points to the base of the sector

  mov r1, #0xFF              // Clear with all-set-bits
  ldr r0, =1024              // Clear 4KiB
1:
  strb r1, [r2, #0]          // Write byte-by-byte to SRAM
  strb r1, [r2, #1]
  strb r1, [r2, #2]
  strb r1, [r2, #3]
  add r2, #4
  sub r0, #1
  bne 1b

  mov r3, $SRAM_BANK(0)      // Unconditionally switch to bank 0
  strh r5, [r4]
  strh r5, [r4]
  strh r3, [r4]
  strh r3, [r4]

  pop {r4, r5}
  bx lr                      // Return, r0 is zero here.
END_FUNC(patch_flash_erase_sector_sram128k)


// Writes a sector with a user-provided buffer.
// R0: Sector number to write
// R1: User provided pointer
START_FUNC(patch_flash_write_sector_sram128k)
  push {r4, r5}
  ldr r5, =0xA55A
  ldr r4, =0x9FFFFFE

  lsl r0, #28                // Limit sector number to [0...15] + lsl#12
  bcc 2f
    mov r3, $SRAM_BANK(1)    // Switch to bank 1, since the sector num is 16..31
    strh r5, [r4]
    strh r5, [r4]
    strh r3, [r4]
    strh r3, [r4]

2:
  lsr r0, #16                // Finish lsl#12

  ldr r2, =0x0E000000        // Load the SRAM base (0x0E000000)
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

  mov r3, $SRAM_BANK(0)      // Unconditionally switch to bank 0
  strh r5, [r4]
  strh r5, [r4]
  strh r3, [r4]
  strh r3, [r4]

  pop {r4, r5}
  bx lr                      // Return, r0 is zero here.
END_FUNC(patch_flash_write_sector_sram128k)


// Writes a single byte to a specifc sector and offset.
// R0: Sector number to write
// R1: Offset within the sector
// R2: Data (byte) to write
START_FUNC(patch_flash_write_byte_sram128k)
  push {r4, r5}
  ldr r5, =0xA55A
  ldr r4, =0x9FFFFFE

  lsl r0, #28                // Limit sector number to [0...15] + lsl#12
  bcc 2f
    mov r3, $SRAM_BANK(1)    // Switch to bank 1, since the sector num is 16..31
    strh r5, [r4]
    strh r5, [r4]
    strh r3, [r4]
    strh r3, [r4]

2:
  lsr r0, #16                // Finish lsl#12

  add r0, r0, r1             // Full offset from flash start
  ldr r1, =0x0E000000        // Load the SRAM base (0x0E000000)
  add r0, r0, r1             // Full address of the byte to program

  strb r2, [r0]              // Write the byte

  mov r3, $SRAM_BANK(0)      // Unconditionally switch to bank 0
  strh r5, [r4]
  strh r5, [r4]
  strh r3, [r4]
  strh r3, [r4]

  mov r0, #0
  pop {r4, r5}
  bx lr
END_FUNC(patch_flash_write_byte_sram128k)


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
33:                                        \
  bx r3; /* r3 has target addr */

// This routine is similar to patch_eeprom_read_sram64k since the date is loaded
// from SRAM. However, it honors memory_size when it comes to parameter testing.

// R0: Block number (0-63 or 0-1023) to read from.
// R1: Byte buffer where the byte will be written (as bits)
// Returns 0 on success? (Space: 168 bytes)
START_FUNC(patch_eeprom_read_directsave)
  push {r7, lr}

  mov r7, $READ_EEPROM_FN
  ldr r3, patch_eeprom_read_directsave_size
  bl 33f

  pop {r7}
  pop {r1}
  bx r1
  trampoline_arm_r3()
END_FUNC(patch_eeprom_read_directsave)


// R0: Block number (0-63 or 0-1023) to write to.
// R1: Byte buffer where the byte will be read (as bits)
// Returns 0 on success?  (Space: 204 bytes)
START_FUNC(patch_eeprom_write_directsave)
  push {r7, lr}

  mov r7, $WRITE_EEPROM_FN
  ldr r3, patch_eeprom_write_directsave_size
  bl 33f

  // The handler does actually return an error code in r0
  pop {r7}
  pop {r1}
  bx r1
  trampoline_arm_r3()
END_FUNC(patch_eeprom_write_directsave)


// Reads data/sector to the user buffer
// R0: Sector to read (4KiB sector size)
// R1: byte offset withing the sector
// R2: buffer pointer (destination)
// R3: amount of bytes to read
// Has 118 bytes of space.
START_FUNC(patch_flash_read_directsave)
  push {r7, lr}

  lsl r0, #27; lsr r0, #15   // Limit sector number to [0...31] + lsl #12
  add r1, r0                 // Data offset to read at.

  mov r0, r2                 // User buffer (goes in r0)
  mov r2, r3                 // Byte count (into r2)

  // The address of the DirectSave payload is written right after the patch.
  mov r7, $READ_FLASH_FN
  ldr r3, patch_flash_read_directsave_size
  bl 33f

  pop {r7}
  pop {r1}
  bx r1
  trampoline_arm_r3()
END_FUNC(patch_flash_read_directsave)

// Clears the full flash device (fills it with 0xFF)
// Has 90 bytes of space.
START_FUNC(patch_flash_erase_device_directsave)
  push {r7, lr}

  mov r7, $ERASE_CHIP_FLASH_FN
  ldr r3, patch_flash_erase_device_directsave_size
  bl 33f

  pop {r7}
  pop {r1}
  bx r1
  trampoline_arm_r3()
END_FUNC(patch_flash_erase_device_directsave)


// Clears a sector in the flash device (fills it with 0xFF)
// R0: Sector number to wipe
// Has 110 bytes of space.
START_FUNC(patch_flash_erase_sector_directsave)
  push {r7, lr}

  lsl r0, #27; lsr r0, #27   // Limit sector number to [0...31]

  mov r7, $ERASE_SECTOR_FLASH_FN
  ldr r3, patch_flash_erase_sector_directsave_size
  bl 33f

  pop {r7}
  pop {r1}
  bx r1
  trampoline_arm_r3()
END_FUNC(patch_flash_erase_sector_directsave)

// Writes a sector with a user-provided buffer.
// R0: Sector number to write
// R1: User provided pointer
START_FUNC(patch_flash_write_sector_directsave)
  push {r7, lr}

  lsl r0, #27; lsr r0, #27   // Limit sector number to [0...31]

  mov r2, r0                 // Swap args
  mov r0, r1
  mov r1, r2

  mov r7, $WRITE_FLASH_FN
  ldr r3, patch_flash_write_sector_directsave_size
  bl 33f

  pop {r7}
  pop {r1}
  bx r1
  trampoline_arm_r3()
END_FUNC(patch_flash_write_sector_directsave)

// Writes a single byte to a specifc sector and offset.
// R0: Sector number to write
// R1: Offset within the sector
// R2: Data (byte) to write
START_FUNC(patch_flash_write_byte_directsave)
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
  ldr r3, patch_flash_write_byte_directsave_size
  bl 33f

  // Data was read, proceed to patch the sector byte
  pop {r0, r1, r2}
  mov r3, sp
  strb r2, [r3, r1]

  mov r1, r0
  mov r0, sp

  mov r7, $WRITE_FLASH_FN
  ldr r3, patch_flash_write_byte_directsave_size
  bl 33f

  add sp, $256
  add sp, $256

  mov r0, #0
  pop {r7}
  pop {r1}
  bx r1
  trampoline_arm_r3()
END_FUNC(patch_flash_write_byte_directsave)


// RTC emulation implementation.
// We (ab)use the CPU unused modes (UNDEF/ABORT) to store data in their SP/LR regs.
//
// The register contains: yyyyyyyy.MMMMMMdd.ddddmmmm.mmhhhhhh
// The routines convert these values to BCD. Year is encoded as 00-99.
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

// Have 214 bytes of code size (~100 insts)
START_FUNC(patch_rtc_probe)
  mov r0, $1                 // Just return 1 (working, no errors)
  bx lr
END_FUNC(patch_rtc_probe)

// Have around ~200 bytes of code size (~100 insts), modern versions ~26 bytes
START_FUNC(patch_rtc_getstatus)
  mov r1, $0x40
  add r0, r0, $7             // Overwrite status only
  strb r1, [r0]
  mov r0, $1                 // Return true
  bx lr
END_FUNC(patch_rtc_getstatus)

// Max size should be 176 bytes (88 insts)
START_FUNC(patch_rtc_gettimedate)
  push {r4, r5, lr}

  // Switch to ARM mode.
  bx pc

.balign 4
.arm
1:
  mrs r3, CPSR                 // Save the current CPSR
  msr CPSR_c, $(0x1B | 0x80)   // Move to undefined mode, no interrupts
  add lr, lr, sp, lsl #3       // UNDEF[SP] contains the RTC speed (0 to 5 -> 0,8,16,24,32,40)
  mov r5, lr                   // Read UNDEF[LR]
  msr CPSR_c, r3               // Restore original model and CPSR

  add r2, pc, $1
  bx r2

.thumb

  mov r4, r0           // Save buffer pointer

  mov r1, $60                  // Get seconds
  bl div_ts_bcd
  strb r0, [r4, #6]

  mov r1, $60                  // Get minutes
  bl div_ts_bcd
  strb r0, [r4, #5]

  mov r1, $24                  // Get hours
  bl div_ts_bcd
  strb r0, [r4, #4]

  // r5 contains the number of days since 2000-01-01
  // it's a bit trickier to get year/month/day now.

  mov r0, $0         // Start on year 2000 (0)
  1:
    mov r1, $180     // Assume non-leap year
    add r1, $185
    adr r2, months
    lsl r3, r0, $30
    bne 2f
      add r1, $1     // Leap year!
      add r2, $12    // Jump 12 months in the table
    2:
    sub r5, r1       // Decrement 365 or 366
    bcs 1b           // Keep looping if the result was 0+

  add r5, r1  // r5 underflowed, re-add r1 to go back one iteration

  // r0 is the year, r5 contains the day offset (in the year), r2 points to the month table.

  bl bcdconv         // Calculate year and store it
  strb r0, [r4, #0]

  mov r0, $0         // Start on month 0
  1:
    ldrb r1, [r2, r0]
    add r0, $1
    sub r5, r1       // Subtract the day count for that month
    bcs 1b           // Keep iterating if the result was 0+

  add r5, r1         // Go back 1 iteration.

  // r0 contains month number (1-12), r5 the day in the month (0-30)

  bl bcdconv                   // Get month
  strb r0, [r4, #1]

  mov r0, r5
  bl bcdconv                   // Get day
  strb r0, [r4, #2]

  pop {r4, r5}
  pop {r0}                     // Return non-zero (true)
  bx r0

// Division by N routine, divides r5 by r1.
// Quotient stored back to r5, the remainder r0 is BCD converted.
// r1-r3 are clobbered
div_ts_bcd:
  mov r0, r5
  swi 0x6
  mov r5, r0
  mov r0, r1
  // fallback to bcdconv

// Potential BCD conversion routine r0 number
bcdconv:
  mov r1, $10
  swi 0x6              // Divide, r0 is result, r1 residue
  lsl r0, $4
  orr r0, r1
  bx lr

.balign 4
months:
  .byte 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
  .byte 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31

END_FUNC(patch_rtc_gettimedate)


// Resets current time and date to Jan 1st 2000, at midnight.
// Max size should be ~130 bytes (65 insts). Newer builds are ~28bytes!
START_FUNC(patch_rtc_reset)
  mov r0, pc
  bx r0

.arm
  mrs r3, CPSR                 // Save the current CPSR
  msr CPSR_c, $(0x1B | 0x80)   // Move to undefined mode, no interrupts
  mov lr, $0                   // Write UNDEF[LR] with zero
  msr CPSR_c, r3               // Restore original model and CPSR

  mov r0, $1               // Return true
  bx lr
END_FUNC(patch_rtc_reset)


