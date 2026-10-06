/*
 * Copyright (C) 2026 David Guillen Fandos <david@davidgf.net>
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

#include <stdbool.h>
#include <stdint.h>

// Firmware update and flashing tools
typedef struct {
  uint32_t deviceid;
  uint32_t size;         // Size in bytes
  uint32_t regioncnt;    // Erase region count (ideally 1, or perhaps 0)
  uint32_t blksize;      // Block size in bytes
  uint32_t blkcount;     // Number of blocks
  uint32_t blkwrite;     // Buffer writing capabilities (zero means disabled)
} t_flash_info;

typedef struct {
  uint32_t baseaddr;
  uint32_t sectorsize;
  uint32_t sectorcount;
  uint32_t currsect;
  uint32_t timeout;
} t_flash_erase_state;

bool flash_identify(t_flash_info *info);
bool flash_erase_chip();
bool flash_erase_sector(uintptr_t addr);
bool flash_erase_sectors(uint32_t baseaddr, unsigned sectsize, unsigned sectcount);
void flash_read(uint32_t baseaddr, uint8_t *buf, unsigned size);
bool flash_check_erased(uintptr_t addr, unsigned size);
bool flash_program(uint32_t baseaddr, const uint8_t *buf, unsigned size);
bool flash_program_buffered(uint32_t baseaddr, const uint8_t *buf, unsigned size, unsigned bufsize);
bool flash_verify(uint32_t baseaddr, const uint8_t *buf, unsigned size);
void flash_erase_fsm_start(t_flash_erase_state *st, uint32_t baseaddr, unsigned sectsize, unsigned sectorcnt);
int flash_erase_fsm_step(t_flash_erase_state *st);

bool check_superfw(const uint8_t *h, uint32_t *ver, uint32_t *patch);
bool validate_superfw_variant(const uint8_t *fw);
bool validate_superfw_checksum(const uint8_t *fw, unsigned fwsize);

extern t_flash_info flashinfo;

