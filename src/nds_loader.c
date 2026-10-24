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

// Minimal NDS loader functionality.

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "util.h"
#include "crc.h"
#include "dldi_patcher.h"
#include "fatfs/ff.h"

#pragma GCC optimize ("Os")

// This is used to validate the NDS header and ensure we do not overwrite
// random places in RAM and cause some funny business.

#define NDS_HEADER_ADDR        0x027FFE00
#define MAINRAM_TMP_WRAM7_ADDR 0x02000000   // Use main RAM (top)
#define MAINRAM_TMP_VRAM_ADDR  0x06860000   // Use VRAM bank D, already enabled

#define MAINRAM_MAX_PAYLOAD    0x003BFE00   // (That's 4MB minus 256KB and 512bytes)
#define MAINRAM_MIN_ADDR       0x02000000
#define MAINRAM_MAX_ADDR       (MAINRAM_MIN_ADDR + MAINRAM_MAX_PAYLOAD)

#define WRAM_MAX_PAYLOAD       0x00018000   // (We *could* have up to 64+32KiB)
#define WRAM_MIN_ADDR          0x037F8000   // (That's 32KiB before the WRAM-ARM7)
#define WRAM_MAX_ADDR          0x03810000   // (WRAM-ARM7 is 64KiB big)

typedef struct {
  char gtitle[12];             // Game title (ASCII)
  char gcode[4];               // Game code (usually ASCII)
  char gmaker[2];              // Game maker (usually ASCII)
  uint8_t unit_code;           // 0 for NDS, 2 for NDS&DSi, 3 for DSi
  uint8_t enc_seed;            // Encryption seed selector
  uint8_t devsize;             // Device size (cart capacity)
  uint8_t pad[8];              // Usually zero filled
  uint8_t region;              // Region code
  uint8_t version;             // Game version?
  uint8_t autostart;           // Skips some boot screen

  uint32_t arm9_rom_offset;    // Offset (expressed in bytes) into the ROM file.
  uint32_t arm9_entrypoint;    // Address (Main RAM) to start the ARM9 CPU.
  uint32_t arm9_load_addr;     // Address (Main RAM) where the payload is loaded.
  uint32_t arm9_load_size;     // Size to load from ROM to RAM.

  uint32_t arm7_rom_offset;    // Offset (expressed in bytes) into the ROM file.
  uint32_t arm7_entrypoint;    // Address (Main RAM/WRAM) to start the ARM7 CPU.
  uint32_t arm7_load_addr;     // Address (Main RAM/WRAM) where the payload is loaded.
  uint32_t arm7_load_size;     // Size to load from ROM to RAM.

  uint32_t fnt_offset, fnt_size;
  uint32_t fat_offset, fat_size;

  uint32_t arm9_overlay_offset, arm9_overlay_size;
  uint32_t arm7_overlay_offset, arm7_overlay_size;

  uint32_t port_a, port_b;

  uint32_t icon_offset;
  uint16_t secure_area_checksum, secure_area_delay;

  uint32_t arm9_load_hook, arm7_load_hook;
  uint32_t secure_area_disable[2];

  uint32_t total_rom_size, header_size;
  uint32_t unknown[3];
  uint16_t nand_eorom, nand_startrw;

  uint8_t reserved[40];

  uint8_t logo[156];
  uint16_t logo_checksum, header_checksum;

  uint32_t debug[3];

  uint8_t tail[148];
} t_nds_header;

_Static_assert(sizeof(t_nds_header) == 512, "NDS header is 512 bytes long");

bool validate_nds_header(const t_nds_header *header) {
  if (header->logo_checksum != 0xCF56)
    return false;

  // Calculate header CRC16.
  uint16_t ccrc = ds_crc16((uint8_t*)header, 0x15E);
  return (ccrc == header->header_checksum);
}

// Loads an NDS file by:
//  - Loading its header (to main RAM) from disk and parsing it.
//  - Loading ARM9 & ARM7 code sections to main RAM
//  - Patching the provided DLDI driver (if any)
// Returns error if the NDS file doesn't exist, looks invalid in any way, etc.

unsigned load_nds(const char *filename, const void *dldi_driver) {
  FIL fd;
  FRESULT res = f_open(&fd, filename, FA_READ);
  if (res != FR_OK)
    return ERR_FILE_ACCESS;

  // Read header directly to its RAM destination.
  t_nds_header *hdr = (t_nds_header*)NDS_HEADER_ADDR;
  UINT rdbytes;
  if (FR_OK != f_read(&fd, hdr, sizeof(*hdr), &rdbytes) || rdbytes != sizeof(*hdr))
    return ERR_FILE_ACCESS;

  // Disable header check, many homebrew do not follow the header format.
  // if (!validate_nds_header(hdr))
  //   return ERR_NDS_BADHEADER;

  // Proceed to sanity check the header code fields.
  bool arm7_on_wram = (hdr->arm7_entrypoint >> 24) == 3;

  const t_dldi_driver *driver = (t_dldi_driver*)dldi_driver;
  const unsigned driver_size = driver ? (1 << driver->h.req_size) : 0;

  // ARM9 size, addresses and entrypoint
  if (hdr->arm9_load_size > MAINRAM_MAX_PAYLOAD)
    return ERR_NDS_TOO_BIG;
  if (hdr->arm9_load_addr < MAINRAM_MIN_ADDR ||
      hdr->arm9_load_addr + hdr->arm9_load_size >= MAINRAM_MAX_ADDR)
    return ERR_NDS_BAD_ADDRS;
  if (hdr->arm9_entrypoint < hdr->arm9_load_addr ||
      hdr->arm9_entrypoint > hdr->arm9_load_addr + hdr->arm9_load_size)
    return ERR_NDS_BAD_ENTRYP;

  // ARM7 size, addresses and entrypoint
  if (arm7_on_wram) {
    if (hdr->arm7_load_size > WRAM_MAX_PAYLOAD)
      return ERR_NDS_TOO_BIG;
    if (hdr->arm7_load_addr < WRAM_MIN_ADDR ||
        hdr->arm7_load_addr + hdr->arm7_load_size >= WRAM_MAX_ADDR)
      return ERR_NDS_BAD_ADDRS;
  } else {
    if (hdr->arm7_load_size > MAINRAM_MAX_PAYLOAD)
      return ERR_NDS_TOO_BIG;
    if (hdr->arm7_load_addr < MAINRAM_MIN_ADDR ||
        hdr->arm7_load_addr + hdr->arm7_load_size >= MAINRAM_MAX_ADDR)
      return ERR_NDS_BAD_ADDRS;
  }
  if (hdr->arm7_entrypoint < hdr->arm7_load_addr ||
      hdr->arm7_entrypoint > hdr->arm7_load_addr + hdr->arm7_load_size)
    return ERR_NDS_BAD_ENTRYP;

  // Clear the main memory areas where payload can be loaded.
  memset32((void*)MAINRAM_MIN_ADDR,       0, MAINRAM_MAX_PAYLOAD);

  // Process the arm7 payload first. We load and patch it.

  // If the arm7 payload goes into the ARM7-WRAM, copy it temporarily at the
  // begining of the main ram, then move it to VRAM-D. The ARM7 knows how to copy it if needed.
  uint8_t *arm7_addr = arm7_on_wram ? (uint8_t*)((uintptr_t)MAINRAM_TMP_WRAM7_ADDR) :
                                      (uint8_t*)((uintptr_t)hdr->arm7_load_addr);
  if (FR_OK != f_lseek(&fd, hdr->arm7_rom_offset))
    return ERR_FILE_ACCESS;
  if (FR_OK != f_read(&fd, arm7_addr, hdr->arm7_load_size, &rdbytes) ||
                      rdbytes != hdr->arm7_load_size)
    return ERR_FILE_ACCESS;

  if (dldi_driver) {
    int offset = 0;
    while (offset < hdr->arm7_load_size) {
      int next_offset = dldi_stub_find(&arm7_addr[offset], hdr->arm7_load_size - offset);
      if (next_offset < 0)
        break;
      offset += next_offset;

      t_dldi_header *dldi_stub = (t_dldi_header*)&arm7_addr[offset];
      if (dldi_stub_validate(dldi_stub, driver_size))
        dldi_stub_patch((t_dldi_driver*)dldi_stub, driver);
      offset += 4;
    }
  }

  // If we used a temporary address for the ARM7 payload, move it now to its VRAM buffer
  if (arm7_on_wram)
    memcpy32((void*)MAINRAM_TMP_VRAM_ADDR, (void*)MAINRAM_TMP_WRAM7_ADDR, hdr->arm7_load_size);

  // Proceed to load the arm9 payload now
  uint8_t *arm9_addr = (uint8_t*)((uintptr_t)hdr->arm9_load_addr);
  if (FR_OK != f_lseek(&fd, hdr->arm9_rom_offset))
    return ERR_FILE_ACCESS;
  if (FR_OK != f_read(&fd, arm9_addr, hdr->arm9_load_size, &rdbytes) ||
                      rdbytes != hdr->arm9_load_size)
    return ERR_FILE_ACCESS;

  f_close(&fd);

  if (dldi_driver) {
    // Find patching points in the ARM9 payload
    int offset = 0;
    while (offset < hdr->arm9_load_size) {
      int next_offset = dldi_stub_find(&arm9_addr[offset], hdr->arm9_load_size - offset);
      if (next_offset < 0)
        break;
      offset += next_offset;

      t_dldi_header *dldi_stub = (t_dldi_header*)&arm9_addr[offset];
      if (dldi_stub_validate(dldi_stub, driver_size))
        dldi_stub_patch((t_dldi_driver*)dldi_stub, driver);
      offset += 4;
    }
  }

  return 0;
}

