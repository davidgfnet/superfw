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

// DLDI driver for Supercard.
// This is used in NDS mode but can also be used in GBA mode to directly
// access the SDcard (ie. Direct-Save mode).

.section    .dldi_header, "ax"

.text
.arm
.align 4

.global dldi_startup_80

#define FEATURE_MEDIUM_CANREAD   0x00000001     // Read function available
#define FEATURE_MEDIUM_CANWRITE  0x00000002     // Write function available
#define FEATURE_SLOT_GBA         0x00000010     // This is a Slot2 driver
#define FEATURE_SLOT_NDS         0x00000020     // This is a Slot1 driver
#define FEATURE_ARM7_CAPABLE     0x00000100     // This is a BlocksDS extension

dldi_payload:
  .word 0xBF8DA5ED                          // Magic signature
  .asciz " Chishm"                          // Magic string
  .byte 0x01                                // Version
  .byte __log_payload_size                  // 2^N (should fit in 8KB comfortably)
  .byte __fix_flags                         // FIX_* flags (only use FIX_GOT and/or FIX_BSS)
  .byte 0x00                                // Allocated app space size (avail_size, ignore)

  #if defined(SUPERCHIS_IO)
    .asciz "SuperFW SuperChis DLDI driver by davidgf"
  #elif defined(SUPERCARD_LITE_IO)
    .asciz "SuperFW Supercard Lite DLDI driver by davidgf"
  #else
    .asciz "SuperFW Supercard DLDI driver by davidgf"
  #endif
  .align 6

  .word __BINARY_START__    // start address (including the header?)
  .word __BINARY_END__      // end of the binary
  .word 0x00000000          // glue start (unused)
  .word 0x00000000          // glue end
  .word __got_start
  .word __got_end
  .word __bss_start
  .word __bss_end

  .ascii "SFSC"        // ioType (SuperFW's Supecard)
  .word (FEATURE_MEDIUM_CANREAD | FEATURE_MEDIUM_CANWRITE | FEATURE_SLOT_GBA | FEATURE_ARM7_CAPABLE)

  .word dldi_startup_80
  .word dldi_inserted
  .word dldi_readsectors
  .word dldi_writesectors
  .word dldi_clearstatus
  .word dldi_shutdown

dldi_startup_80:
  // Switch to Thumb mode to call the startup() function
  add r0, pc, $1
  bx r0
  .thumb
  b dldi_startup
  nop

