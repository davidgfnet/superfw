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

#include <stdint.h>

#include "supercard_driver.h"

#define SRAM_BASE            0x0E000000

bool validate_config(void);
uint32_t base_sector(void);
uint32_t get_memory_size(void);

inline uint32_t min32(uint32_t a, uint32_t b) {
  return (a < b) ? a : b;
}

// Reads EEPROM data, directly from SRAM (cached)
int ds_read_eeprom(uint32_t block_num, uint8_t *buf) {
  if (!validate_config())
    return -1;

  // Check memory size to avoid overflow
  if (block_num * 8 >= get_memory_size())
    return -1;

  // We read straight from RAM.
  const volatile uint8_t *sram = (uint8_t*)(SRAM_BASE);
  for (unsigned i = 0; i < 8; i++)
    buf[i] = sram[block_num * 8 + 7 - i];

  return 0;
}

// Writes EEPROM data, updates SRAM buffer and flushes to SD card.
int ds_write_eeprom(uint32_t block_num, const uint8_t *buf) {
  if (!validate_config())
    return -1;

  // Check memory size to avoid overflow
  if (block_num * 8 >= get_memory_size())
    return -1;

  const unsigned sram_off = block_num * 8;
  const unsigned sram_sdoff = sram_off & ~511;

  // Update the data on SRAM too for faster reads.
  volatile uint8_t *sram = (uint8_t*)(SRAM_BASE);
  for (unsigned i = 0; i < 8; i++)
    sram[sram_off + 7 - i] = buf[i];

  // We flush the updated SD sector.
  const uint32_t sdblocknum = block_num / (512 / 8) + base_sector();
  unsigned ret = sdcard_write_blocks((uint8_t*)&sram[sram_sdoff], sdblocknum, 1);
  return ret ? -1 : 0;
}

// Reads flash bytes (directly from SD card) into a user-defined buffer.
int ds_read_flash(uint8_t *buf, uint32_t offset, uint32_t bytecount) {
  if (!validate_config())
    return -1;

  const uint32_t msize = get_memory_size();
  if (offset > msize || bytecount > msize || offset + bytecount > msize)
    return -1;

  // We must read 512byte aligned blocks from SD card, therefore we
  // start reading from the first sector, and keep doing that. We read up to
  // 64 blocks in one go.
  const uint32_t basen = base_sector();

  uint8_t *tmpbuf = (uint8_t*)0x0E000000;
  while (bytecount) {
    uint32_t start_blk = offset / 512U;
    uint32_t end_blk = (offset + bytecount - 1U) / 512U;
    unsigned bcnt = end_blk - start_blk + 1U;
    if (bcnt > 64U)
      bcnt = 64U;

    unsigned ret = sdcard_read_blocks(tmpbuf, basen + start_blk, bcnt);
    if (ret)
      return -1;

    // Copy the data we read to the user buffer
    unsigned blkoff = offset & 511U;
    unsigned tocpy = min32(bytecount, bcnt * 512U - blkoff);        // Max data we can copy.

    // Copy data to user buffer, advance pointer.
    for (unsigned i = 0; i < tocpy; i++)
      *buf++ = tmpbuf[blkoff + i];
    // Adjust the offsets and byte counts.
    offset += tocpy;
    bytecount -= tocpy;
  }

  return 0;
}

// Writes a full sector (4KBytes) to the flash device. This was previously erased.
int ds_write_sector_flash(const uint8_t *buf, uint32_t sectnum) {
  const uint32_t blpersector = 4096 / 512;

  if (!validate_config())
    return -1;
  if (sectnum * 4096 > get_memory_size())
    return -1;

  if (sdcard_write_blocks(buf, base_sector() + sectnum * blpersector, blpersector))
    return -1;

  return 0;
}

// Erases the full chip (the entire flash memory)
int ds_erase_chip_flash(void) {
  const uint32_t blrun = 32;   // Erase 32 Blocks in a row.
  if (!validate_config())
    return -1;

  // Clear buffer and write that to the SD card
  uint8_t *tmpbuf = (uint8_t*)0x0E000000;
  for (unsigned i = 0; i < blrun*512; i++)
    tmpbuf[i] = 0xff;

  // Erase in 32 block chunks (16KiB)
  const uint32_t memblks = get_memory_size() / 512U;
  for (uint32_t s = 0; s < memblks; s += blrun)
    if (sdcard_write_blocks(tmpbuf, base_sector() + s, min32(blrun, memblks - s)))
      return -1;

  return 0;
}

// Erases one flash sector (4KiB).
int ds_erase_sector_flash(uint32_t sectnum) {
  const uint32_t blpersector = 4096 / 512;

  if (!validate_config())
    return -1;
  if (sectnum * 4096 > get_memory_size())
    return -1;

  // Clear buffer and write that to the SD card
  uint8_t *tmpbuf = (uint8_t*)0x0E000000;
  for (unsigned i = 0; i < 4096; i++)
    tmpbuf[i] = 0xff;

  if (sdcard_write_blocks(tmpbuf, base_sector() + sectnum * blpersector, blpersector))
      return -1;

  return 0;
}


