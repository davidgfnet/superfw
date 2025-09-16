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
#ifndef SUPERCARD_LITE_ADDR_SORT
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

// Returns manufacturer code in the higher bits, device id in the lower bits.
uint32_t flash_identify() {
  // Internal flash in write mode.
  FLASH_WE_MODE();

  // Reset any previous command that might be ongoing.
  for (unsigned i = 0; i < 32; i++)
    SLOT2_BASE_U16[0] = 0x00F0;

  SLOT2_BASE_U16[addr_perm(0x555)] = 0x00AA;
  SLOT2_BASE_U16[addr_perm(0x2AA)] = 0x0055;
  SLOT2_BASE_U16[addr_perm(0x555)] = 0x0090;

  uint32_t ret = (SLOT2_BASE_U16[addr_perm(0x000)] << 16) |
                  SLOT2_BASE_U16[addr_perm(0x001)];

  for (unsigned i = 0; i < 32; i++)
    SLOT2_BASE_U16[0] = 0x00F0;

  // Go back to R/W SDRAM.
  set_supercard_mode(MAPPED_SDRAM, true, true);

  return ret;
}

#ifndef SUPERCHIS
// Performs a flash full-chip erase.
bool flash_erase() {
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

// Programs the built-in flash memory.
// Uses a temporary buffer, since the buffer can (and usually is) on SDRAM.
bool flash_program(const uint8_t *buf, unsigned size) {
  // Reset any previous command that might be ongoing.
  FLASH_WE_MODE();
  SLOT2_BASE_U16[0] = 0x00F0;

  for (unsigned i = 0; i < size; i += 512) {
    uint8_t tmp[512];
    set_supercard_mode(MAPPED_SDRAM, true, true);
    memcpy(tmp, &buf[i], 512);

    FLASH_WE_MODE();
    for (unsigned off = 0; off < 512 && i+off < size; off += 2) {
      const uint32_t addr = i + off;
      const uint16_t value = tmp[off] | (tmp[off+1] << 8);

      SLOT2_BASE_U16[addr_perm(0x555)] = 0x00AA;
      SLOT2_BASE_U16[addr_perm(0x2AA)] = 0x0055;
      SLOT2_BASE_U16[addr_perm(0x555)] = 0x00A0; // Program command

      // Perform the actual write operation
      SLOT2_BASE_U16[addr / 2] = value;

      // It should take less than 1ms usually (in the order of us).
      for (unsigned j = 0; j < 8*1024; j++) {
        if (SLOT2_BASE_U16[0] == SLOT2_BASE_U16[0])
          break;
      }
      bool notfinished = (SLOT2_BASE_U16[0] != SLOT2_BASE_U16[0]);
      SLOT2_BASE_U16[0] = 0x00F0;   // Finish operation.

      // Timed out or the value programmed is wrong
      if (notfinished || SLOT2_BASE_U16[addr / 2] != value) {
        set_supercard_mode(MAPPED_SDRAM, true, true);
        return false;
      }
    }
  }

  set_supercard_mode(MAPPED_SDRAM, true, true);
  return true;
}

#else
// Performs a flash sector erase for first 4MByte

void flash_erase_sector(uint32_t sa) {
    SLOT2_BASE_U16[addr_perm(sa/2)] = 0xF0;
    SLOT2_BASE_U16[addr_perm(0xAAA/2)] = 0xAA;
    SLOT2_BASE_U16[addr_perm(0x555/2)] = 0x55;
    SLOT2_BASE_U16[addr_perm(0xAAA/2)] = 0x80;
    SLOT2_BASE_U16[addr_perm(0xAAA/2)] = 0xAA;
    SLOT2_BASE_U16[addr_perm(0x555/2)] = 0x55;
    SLOT2_BASE_U16[addr_perm(sa/2)] = 0x30;
}

bool flash_polling_wait(uint32_t addr, uint16_t expected_data)
{
    while (1) {
        uint16_t status1 = SLOT2_BASE_U16[addr_perm(addr/2)];
        // Check Erase
        if ((status1 & 0x80) == (expected_data & 0x80)) {
            // DQ7 = 1
            uint16_t status2 = SLOT2_BASE_U16[addr_perm(addr/2)];
            if ((status2 & 0x80) == (expected_data & 0x80)) {
                break;
            }
        }
        // Check Timeout
        if (status1 & 0x20) {
            // Timeout Check DQ7
            uint16_t status2 = SLOT2_BASE_U16[addr_perm(addr/2)];
            if ((status2 & 0x80) == (expected_data & 0x80)) {
                break;
            } else {
                SLOT2_BASE_U16[addr_perm(0)] = 0xF0;
                return false;
            }
        }
        __asm("nop");
    }
    SLOT2_BASE_U16[addr_perm(0)] = 0xF0;
    return true;
}

static bool is_pre_erased = false;
bool flash_program_auto(uint32_t address, const uint8_t *buffer, unsigned buffer_size, bool pre_erase) {
    if (address == 0 || pre_erase == false) {
        is_pre_erased = false;
    }
    uint32_t c = 0; // 计算偏移
    while (c < buffer_size) {
        uint32_t pa = address + c;
        uint8_t tmp[WRITE_BUF_SIZE];
        set_supercard_mode(MAPPED_SDRAM, true, true);
        memcpy(tmp, &buffer[pa], WRITE_BUF_SIZE);
        FLASH_WE_MODE();
        // Reset any previous command that might be ongoing.
        for (unsigned i = 0; i < 32; i++)
          SLOT2_BASE_U16[0] = 0x00F0;
        // is sector boundary
        if ((pa % SECTOR_SIZE) == 0) {
            if (is_pre_erased) {
                if (!flash_polling_wait(pa, 0xFF)) {
                  is_pre_erased = false;
                  set_supercard_mode(MAPPED_SDRAM, true, true);
                  return false;
                }
                is_pre_erased = false;
            } else {
                flash_erase_sector(pa);
                if (!flash_polling_wait(pa, 0xFF)) {
                  set_supercard_mode(MAPPED_SDRAM, true, true);
                  return false;
                }
            }
        }

        SLOT2_BASE_U16[addr_perm(0xAAA/2)] = 0xAA;
        SLOT2_BASE_U16[addr_perm(0x555/2)] = 0x55;
        SLOT2_BASE_U16[addr_perm(pa/2)] = 0x25;
        SLOT2_BASE_U16[addr_perm(pa/2)] = (WRITE_BUF_SIZE>>1)-1;
        for (uint32_t i=0; i<WRITE_BUF_SIZE; i+=2) {
            SLOT2_BASE_U16[addr_perm((pa+i)/2)] = (*((uint8_t*)tmp+i+1)) << 8 | (*((uint8_t*)tmp+i));
        }
        SLOT2_BASE_U16[addr_perm(pa/2)] = 0x29;
        uint32_t last_addr = pa + WRITE_BUF_SIZE - 2;
        uint16_t expected_data = (*((uint8_t*)tmp+WRITE_BUF_SIZE-1)) << 8 | (*((uint8_t*)tmp+WRITE_BUF_SIZE-2));
        if (!flash_polling_wait(last_addr, expected_data)) {
          set_supercard_mode(MAPPED_SDRAM, true, true);
          return false;
        }
        c += WRITE_BUF_SIZE;
    }
    // pre erase
    uint32_t next_addr = address + buffer_size;
    if (pre_erase && ((next_addr % SECTOR_SIZE) == 0) && next_addr < 32*1024*1024) {
        is_pre_erased = true;
        flash_erase_sector(next_addr);
    } else {
        is_pre_erased = false;
    }
    set_supercard_mode(MAPPED_SDRAM, true, true);
    return true;
}
bool flash_program(const uint8_t *buf, unsigned size) {
  return flash_program_auto(0, buf, size, false);
}
bool flash_erase() {
  FLASH_WE_MODE();
  // Reset any previous command that might be ongoing.
  for (unsigned i = 0; i < 32; i++)
    SLOT2_BASE_U16[0] = 0x00F0;
  for (uint32_t addr = 0; addr < 4 * 1024 * 1024; addr += SECTOR_SIZE) {
    flash_erase_sector(addr);
    if (!flash_polling_wait(addr, 0xFF)) {
      set_supercard_mode(MAPPED_SDRAM, true, true);
      return false;
    }
  }
  set_supercard_mode(MAPPED_SDRAM, true, true);
  return true;
}
#endif
// Programs the built-in flash memory. 
bool flash_verify(const uint8_t *buf, unsigned size) {
  for (unsigned i = 0; i < size; i += 512) {
    uint8_t tmp[512];
    FLASH_WE_MODE();
    for (unsigned j = 0; j < 512; j++)
      tmp[j] = SLOT2_BASE_U8[i + j];

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

