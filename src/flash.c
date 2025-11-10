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

#include <stdint.h>
#include <string.h>

#include "common.h"
#include "util.h"
#include "sha256.h"
#include "supercard_driver.h"

// Supercard internal flash routines
// Assumes the code runs from IW/EWRAM!

// Supercard's internal flash is a regular 512KiB flash, mapped to
// 0x08000000 (whenever the CPLD is not mapping the SDRAM of course).
// The address bus is not wired in a straightforward manner though, there's
// some sort of address permutation (for some unknown reason).
// In general this address mangling is not problematic since it is bijective
// transformation, however for certain specific operations (such as erase or
// write, specific addresses must be sent, ie. 0x555 or 0x2AA).

// Gamepak interface side
// A17 A16 A15 A14 A13 A12 A11 A10  A9  A8  A7  A6  A5  A4  A3  A2  A1  A0
//  |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |
//  |   |   |   |   |   |   |   |   |   |   |   \---|---|---|---|---|---|---;
//  |   |   |   |   |   |   |   |   |   |   |       |   |   |   |   |   |   |
//  |   |   |   |   |   |   |   |   |   \---|---\   |   |   |   |   |   |   |
//  |   |   |   |   |   |   |   |   |       |   |   |   |   |   |   |   |   |
//  |   |   |   |   |   |   |   |   |   /---|---|---|---|---/   |   |   |   |
//  |   |   |   |   |   |   |   |   |   |   |   |   |   |       |   |   |   |
//  |   |   |   |   |   |   |   |   |   |   \---|---|---|---\   |   |   |   |
//  |   |   |   |   |   |   |   |   |   |       |   |   |   |   |   |   |   |
//  |   |   |   |   |   |   |   |   |   |   /---|---|---/   |   |   |   |   |
//  |   |   |   |   |   |   |   |   |   |   |   |   |       |   |   |   |   |
//  |   |   |   |   |   |   |   |   |   |   |   |   |   /---|---|---|---/   |
//  |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |       |
//  |   |   |   |   |   |   |   |   |   |   |   |   |   |   \---|---|---\   |
//  |   |   |   |   |   |   |   |   |   |   |   |   |   |       |   |   |   |
//  |   |   |   |   |   |   |   |   |   |   |   |   \---|---\   |   |   |   |
//  |   |   |   |   |   |   |   |   |   |   |   |       |   |   |   |   |   |
//  |   |   |   |   |   |   |   |   |   |   |   |   /---|---|---/   |   |   |
//  |   |   |   |   |   |   |   |   |   |   |   |   |   |   |       |   |   |
//  |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   /---|---|---/
//  |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |
// A17 A16 A15 A14 A13 A12 A11 A10  A9  A8  A7  A6  A5  A4  A3  A2  A1  A0
// Flash IC interface side

#define SLOT2_BASE_U8  ((volatile  uint8_t*)(0x08000000))
#define SLOT2_BASE_U16 ((volatile uint16_t*)(0x08000000))

// Given a desired flash address, it generates the gamepak address necessary
// to access it, taking into consideration the address permutation described.
#ifdef SUPERCARD_FLASH_ADDRPERM
  static uint32_t addr_perm(uint32_t addr) {
    return (addr & 0xFFFFFE02) |
           ((addr & 0x001) << 7) |
           ((addr & 0x004) << 4) |
           ((addr & 0x008) << 2) |
           ((addr & 0x010) >> 4) |
           ((addr & 0x020) >> 3) |
           ((addr & 0x040) << 2) |
           ((addr & 0x080) >> 3) |
           ((addr & 0x100) >> 5);
  }
#else
  static uint32_t addr_perm(uint32_t addr) {
    return addr;
  }
#endif

// Mapping the Flash with WriteEnable set is a bit tricky on Lite
#ifndef SUPERCARD_LITE_IO
  #define FLASH_WE_MODE() set_supercard_mode(MAPPED_FIRMWARE, true, false)
#else
  #define FLASH_WE_MODE() write_supercard_mode(0x1510)
#endif

// Checks flash device and extracts info about it
bool flash_identify(t_flash_info *info) {
  memset(info, 0, sizeof(*info));

  // Internal flash in write mode.
  FLASH_WE_MODE();

  // Reset any previous command that might be ongoing.
  for (unsigned i = 0; i < 32; i++)
    SLOT2_BASE_U16[0] = 0x00F0;

  SLOT2_BASE_U16[addr_perm(0x555)] = 0x00AA;
  SLOT2_BASE_U16[addr_perm(0x2AA)] = 0x0055;
  SLOT2_BASE_U16[addr_perm(0x555)] = 0x0090;

  info->deviceid = (SLOT2_BASE_U16[addr_perm(0x000)] << 16) |
                    SLOT2_BASE_U16[addr_perm(0x001)];

  for (unsigned i = 0; i < 32; i++)
    SLOT2_BASE_U16[0] = 0x00F0;

  // Enter CFI mode and extract flash information
  SLOT2_BASE_U16[addr_perm(0x555)] = 0x0098;
  uint8_t qs[3] = {
    SLOT2_BASE_U16[addr_perm(0x010)],
    SLOT2_BASE_U16[addr_perm(0x011)],
    SLOT2_BASE_U16[addr_perm(0x012)],
  };
  if (qs[0] == 'Q' && qs[1] == 'R' && qs[2] == 'Y') {
    info->size = 1 << SLOT2_BASE_U16[addr_perm(0x027)];

    info->regioncnt = SLOT2_BASE_U16[addr_perm(0x02C)] & 0xFF;
    info->blkcount = ((SLOT2_BASE_U16[addr_perm(0x02D)] & 0xFF) |
                     ((SLOT2_BASE_U16[addr_perm(0x02E)] & 0xFF) << 8)) + 1;

    info->blksize = ((SLOT2_BASE_U16[addr_perm(0x02F)] & 0xFF) |
                    ((SLOT2_BASE_U16[addr_perm(0x030)] & 0xFF) << 8)) << 8;
    info->blksize = (info->blksize ?: 128);

    info->blkwrite = (SLOT2_BASE_U16[addr_perm(0x02A)] & 0xFF);
    info->blkwrite = (info->blkwrite ? (1 << info->blkwrite) : 0);
  }

  for (unsigned i = 0; i < 32; i++)
    SLOT2_BASE_U16[0] = 0x00F0;

  // Go back to R/W SDRAM.
  set_supercard_mode(MAPPED_SDRAM, true, true);

  return true;
}

// Performs a flash full-chip erase.
bool flash_erase_chip() {
  FLASH_WE_MODE();

  // Reset any previous command that might be ongoing.
  for (unsigned i = 0; i < 32; i++)
    SLOT2_BASE_U16[0] = 0x00F0;

  SLOT2_BASE_U16[addr_perm(0x555)] = 0x00AA;
  SLOT2_BASE_U16[addr_perm(0x2AA)] = 0x0055;
  SLOT2_BASE_U16[addr_perm(0x555)] = 0x0080; // Erase command
  SLOT2_BASE_U16[addr_perm(0x555)] = 0x00AA;
  SLOT2_BASE_U16[addr_perm(0x2AA)] = 0x0055;
  SLOT2_BASE_U16[addr_perm(0x555)] = 0x0010; // Full chip erase!

  // Wait for the erase operation to finish. We rely on Q6 toggling:
  for (unsigned i = 0; i < 60*100; i++) {
    wait_ms(10);    // Wait for a bit, erase can take a while.
    if (SLOT2_BASE_U16[0] == SLOT2_BASE_U16[0])
      break;
  }
  bool retok = (SLOT2_BASE_U16[0] == SLOT2_BASE_U16[0]);

  for (unsigned i = 0; i < 32; i++)
    SLOT2_BASE_U16[0] = 0x00F0;            // Reset for a few cycles

  set_supercard_mode(MAPPED_SDRAM, true, true);
  return retok;
}

// Validate that the sector is clear.
bool flash_check_erased(uintptr_t addr, unsigned size) {
  FLASH_WE_MODE();

  // Checks for all ones!
  for (unsigned i = 0; i < size; i += 2) {
    if (*(volatile uint16_t*)(addr + i) != 0xffff) {
      set_supercard_mode(MAPPED_SDRAM, true, true);
      return false;
    }
  }

  set_supercard_mode(MAPPED_SDRAM, true, true);
  return true;
}

// Starts a flash sector erase operation without waiting for completion.
void flash_erase_sector_start(uintptr_t addr) {
  FLASH_WE_MODE();

  // Reset any previous command that might be ongoing.
  for (unsigned i = 0; i < 32; i++)
    SLOT2_BASE_U16[0] = 0x00F0;

  SLOT2_BASE_U16[addr_perm(0x555)] = 0x00AA;
  SLOT2_BASE_U16[addr_perm(0x2AA)] = 0x0055;
  SLOT2_BASE_U16[addr_perm(0x555)] = 0x0080; // Erase command
  SLOT2_BASE_U16[addr_perm(0x555)] = 0x00AA;
  SLOT2_BASE_U16[addr_perm(0x2AA)] = 0x0055;

  *(volatile uint16_t*)(addr) = 0x0030; // Erase sector
  
  // Don't wait, just return to allow background erase
  set_supercard_mode(MAPPED_SDRAM, true, true);
}

// Checks if a flash erase/program operation is complete.
// Returns true if complete, false if still in progress.
bool flash_operation_complete() {
  FLASH_WE_MODE();
  // Check Q6 toggling - if it's stable, operation is complete
  uint16_t a = SLOT2_BASE_U16[0];
  uint16_t b = SLOT2_BASE_U16[0];
  set_supercard_mode(MAPPED_SDRAM, true, true);
  return (a == b);
}

// Waits for flash operation to complete and finalizes it.
// Returns true on success, false on timeout.
bool flash_operation_wait() {
  FLASH_WE_MODE();
  // Wait for the erase operation to finish. We rely on Q6 toggling:
  for (unsigned i = 0; i < 60*100; i++) {
    if (SLOT2_BASE_U16[0] == SLOT2_BASE_U16[0])
      break;
    wait_ms(10);    // Wait for a bit, erase can take a while.
  }
  bool retok = (SLOT2_BASE_U16[0] == SLOT2_BASE_U16[0]);

  for (unsigned i = 0; i < 32; i++)
    SLOT2_BASE_U16[0] = 0x00F0;            // Reset for a few cycles

  set_supercard_mode(MAPPED_SDRAM, true, true);
  return retok;
}

// Performs a flash sector erase (blocking version).
bool flash_erase_sector(uintptr_t addr) {
  flash_erase_sector_start(addr);
  return flash_operation_wait();
}

// Deletes a bunch of sectors of a given size.
bool flash_erase_sectors(uint32_t baseaddr, unsigned sectsize, unsigned sectcount) {
  for (unsigned i = 0; i < sectcount; i++) {
    if (!flash_erase_sector(baseaddr + i * sectsize))
      return false;
  }
  return true;
}

// Programs the built-in flash memory, assumes memory was cleared.
// Also uses temporary buffers to allow for SDRAM buffers too.
bool flash_program(uint32_t baseaddr, const uint8_t *buf, unsigned size) {
  // Reset any previous command that might be ongoing.
  FLASH_WE_MODE();
  SLOT2_BASE_U16[0] = 0x00F0;

  for (unsigned i = 0; i < size; i += 512) {
    uint16_t tmp[256];
    set_supercard_mode(MAPPED_SDRAM, true, true);
    memcpy(tmp, &buf[i], sizeof(tmp));

    FLASH_WE_MODE();
    for (unsigned off = 0; off < 512 && i+off < size; off += 2) {
      const uint32_t addr = i + off;

      SLOT2_BASE_U16[addr_perm(0x555)] = 0x00AA;
      SLOT2_BASE_U16[addr_perm(0x2AA)] = 0x0055;
      SLOT2_BASE_U16[addr_perm(0x555)] = 0x00A0; // Program command

      // Perform the actual write operation
      volatile uint16_t *ptr = (uint16_t*)(baseaddr + addr);
      *ptr = tmp[off/2];

      // It should take less than 1ms usually (in the order of us).
      for (unsigned j = 0; j < 8*1024; j++) {
        if (SLOT2_BASE_U16[0] == SLOT2_BASE_U16[0])
          break;
      }
      bool notfinished = (SLOT2_BASE_U16[0] != SLOT2_BASE_U16[0]);
      SLOT2_BASE_U16[0] = 0x00F0;   // Finish operation.

      // Timed out or the value programmed is wrong
      if (notfinished || *ptr != tmp[off/2]) {
        set_supercard_mode(MAPPED_SDRAM, true, true);
        return false;
      }
    }
  }

  set_supercard_mode(MAPPED_SDRAM, true, true);
  return true;
}

// Programs the built-in flash memory using the internal write buffer.
bool flash_program_buffered(uint32_t baseaddr, const uint8_t *buf, unsigned size, unsigned bufsize) {
  // Reset any previous command that might be ongoing.
  FLASH_WE_MODE();
  SLOT2_BASE_U16[0] = 0x00F0;
  const unsigned wrsize = MIN(bufsize, 512);

  for (unsigned i = 0; i < size; i += 512) {
    uint16_t tmp[256];
    set_supercard_mode(MAPPED_SDRAM, true, true);
    memcpy(tmp, &buf[i], sizeof(tmp));

    FLASH_WE_MODE();
    for (unsigned off = 0; off < 512 && i+off < size; off += wrsize) {
      const uint32_t toff = (i + off);
      const uint16_t bcnt = MIN(wrsize, size - toff);
      volatile uint16_t *ptr = (uint16_t*)(baseaddr + toff);

      SLOT2_BASE_U16[addr_perm(0x555)]  = 0x00AA;
      SLOT2_BASE_U16[addr_perm(0x2AA)]  = 0x0055;
      *ptr = 0x0025;        // Write buffer command
      *ptr = bcnt / 2 - 1;  // Word count

      for (unsigned j = 0; j < bcnt / 2; j++)
        *ptr++ = tmp[off/2+j];

      *(ptr-1) = 0x29;     // Confirm write buffer operation.

      // Wait a bit for the operation to finish.
      for (unsigned j = 0; j < 32*1024; j++) {
        if (SLOT2_BASE_U16[0] == SLOT2_BASE_U16[0])
          break;
      }
      bool notfinished = (SLOT2_BASE_U16[0] != SLOT2_BASE_U16[0]);
      SLOT2_BASE_U16[0] = 0x00F0;   // Finish operation.

      // Timed out or the value programmed is wrong
      if (notfinished) {
        set_supercard_mode(MAPPED_SDRAM, true, true);
        return false;
      }
    }
  }

  set_supercard_mode(MAPPED_SDRAM, true, true);
  return true;
}

// Reads data into a buffer, even if it's on SDRAM.
// Size must be multiple of 4 bytes!
void flash_read(uint32_t baseaddr, uint8_t *buf, unsigned size) {
  // Reset any previous command that might be ongoing.
  FLASH_WE_MODE();
  SLOT2_BASE_U16[0] = 0x00F0;

  for (unsigned i = 0; i < size; i += 512) {
    unsigned tocpy = MIN(512U, size - i);
    uint16_t tmp[256];
    const uint8_t *ptr = (uint8_t*)(baseaddr + i);
    memcpy32(tmp, ptr, 512);

    set_supercard_mode(MAPPED_SDRAM, true, true);
    memcpy32(&buf[i], tmp, tocpy);
    FLASH_WE_MODE();
  }

  set_supercard_mode(MAPPED_SDRAM, true, true);
}

// Programs the built-in flash memory. 
bool flash_verify(uint32_t baseaddr, const uint8_t *buf, unsigned size) {
  volatile uint8_t *ptr = (uint8_t*)(baseaddr);
  for (unsigned i = 0; i < size; i += 512) {
    uint8_t tmp[512];
    FLASH_WE_MODE();
    for (unsigned j = 0; j < 512; j++)
      tmp[j] = ptr[i + j];

    set_supercard_mode(MAPPED_SDRAM, true, true);
    unsigned tocmp = MIN(512, size - i);
    if (memcmp(tmp, &buf[i], tocmp))
      return false;
  }

  return true;
}


#define FW_VERSION_OFFSET       0xC4
#define FW_GITVERS_OFFSET       0xC8
#define FW_IMGSIZE_OFFSET       0xCC
#define FW_IMGHASH_OFFSET       0xD0
#define FW_MAGICSG_OFFSET       0xF0

#define FW_IMGHASH_SIZE         32

// Validates a superFW image header
bool check_superfw(const uint8_t *h, uint32_t *ver) {
  if (memcmp(&h[FW_MAGICSG_OFFSET], "SUPERFW~DAVIDGF", 16))
    return false;
  if (ver)
    *ver = parse32le(&h[FW_VERSION_OFFSET]);
  return true;
}

bool validate_superfw_checksum(const uint8_t *fw, unsigned fwsize) {
  // Check that the file size matches the advertized size in the header
  uint32_t hsize = parse32le(&fw[FW_IMGSIZE_OFFSET]);
  if (hsize != fwsize || fwsize < 256)
    return false;

  // Calculate the SH256 checksum on a zero-ed checksum field.
  uint8_t hash[FW_IMGHASH_SIZE] = {0};
  SHA256_State st;
  sha256_init(&st);
  sha256_transform(&st, &fw[0], FW_IMGHASH_OFFSET);
  sha256_transform(&st, hash, sizeof(hash));
  sha256_transform(&st, &fw[FW_MAGICSG_OFFSET], fwsize - FW_MAGICSG_OFFSET);
  sha256_finalize(&st, hash);

  if (memcmp(&fw[FW_IMGHASH_OFFSET], hash, sizeof(hash)))
    return false;

  return true;
}

