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
#include <stdbool.h>

#include "supercard_driver.h"

#define SRAM_BASE            0x0E000000

bool validate_config(void);
uint32_t base_sector(void);
uint32_t get_memory_size(void);

static inline uint32_t min32(uint32_t a, uint32_t b) {
  return (a < b) ? a : b;
}

// EEPROM handlers
// SRAM and EEPROM save modes always load savegame data to SRAM, regardless
// of the savepatch used. This allows us to read straight from SRAM and just
// flush on writes.

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

// FLASH handlers
// Read and write data from/to SD card. Reads tend to be large, but smaller
// reads are usually not very efficient. Writes are easy, since we always
// write full 4KiB blocks. Except for byte writes, more on that later :)
// The SRAM is used as scratch/buffer. The first 32KiB are a scratch buffer,
// the last few bytes, store the DirSav config. We also have a cache for
// the byte-write case. This cache is created and destroyed as needed.

#define FLASH_CACHE_MAGIC    0xCAC4ED67

#define SRAM_CACHE_DATA      0x0E00F000
#define SRAM_CACHE_METADATA  0x0E00FF00

typedef struct {
  uint32_t magic;      // Cache magic number
  uint32_t last_addr;  // Last flash addr accessed by the WriteByte function.
  uint32_t dirty;      // Whether the cache holds dirty data.
  uint32_t checksum;   // XOR checksum of this structure.
} t_cache_meta;

// Load the cache from SRAM, check that the struct is valid.
static bool load_cache_metadata(t_cache_meta *cache) {
  // Load the medata block first, byte for byte.
  volatile char * metablk = (char*)0x0E00FF00;
  char *cache_bytes = (char*)cache;
  for (unsigned i = 0; i < sizeof(t_cache_meta); i++)
    cache_bytes[i] = metablk[i];

  // Now proceed to read the local structure and validate it.
  if (cache->magic != FLASH_CACHE_MAGIC)
    return false;

  uint32_t res = cache->magic ^
                 cache->last_addr ^
                 cache->dirty ^
                 cache->checksum;
  return res == 0;
}

// Writes the metadata back into SRAM.
static void save_cache_metadata(t_cache_meta *cache) {
  // Fill any housekeeping fields before writing.
  cache->magic = FLASH_CACHE_MAGIC;
  cache->checksum = cache->magic ^
                    cache->last_addr ^
                    cache->dirty;

  volatile char * metablk = (char*)0x0E00FF00;
  volatile char *cache_bytes = (char*)cache;
  for (unsigned i = 0; i < sizeof(t_cache_meta); i++)
    metablk[i] = cache_bytes[i];
}

// Clear cache metadata (make it invalid)
static void wipe_cache() {
  volatile char * metablk = (char*)0x0E00FF00;
  for (unsigned i = 0; i < sizeof(t_cache_meta); i++)
    metablk[i] = 0xFF;
}

// Flushes the flash cache data to the specified sector
static void flush_flash_cache(uint32_t blknum) {
  const uint8_t * datablk = (uint8_t*)SRAM_CACHE_DATA;
  sdcard_write_blocks(datablk, blknum, 1);
}

// Flushes the flash cache if necessary (when present and dirty)
static void evict_flush_flash_cache() {
  t_cache_meta cacheinfo;
  if (load_cache_metadata(&cacheinfo)) {
    // Check if we have some data we need to flush.
    if (cacheinfo.dirty)
      flush_flash_cache(base_sector() + cacheinfo.last_addr / 512U);
    // Now we can just wipe the cache.
    wipe_cache();
  }
}

// Reads flash bytes (directly from SD card) into a user-defined buffer.
// Uses the first 32KiB of SRAM as scratch area.
int ds_read_flash(uint8_t *buf, uint32_t offset, uint32_t bytecount) {
  if (!validate_config())
    return -1;

  evict_flush_flash_cache();  // Flush any cached data (usually does nothing)

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
      bcnt = 64U;      // Limit to 32KiB

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

  evict_flush_flash_cache();  // Flush any cached data (usually does nothing)

  if (sectnum * 4096 >= get_memory_size())
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

  evict_flush_flash_cache();  // Flush any cached data (usually does nothing)

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

  evict_flush_flash_cache();  // Flush any cached data (usually does nothing)

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

// Writes a single byte to flash. This routine is not used by most games.
// So far only Pokemon seems to use it, to perform a 2-phase write.
int ds_write_byte_flash(uint32_t offset, uint8_t value) {
  if (!validate_config())
    return -1;
  if (offset >= get_memory_size())
    return -1;

  unsigned errs = 0;
  uint8_t *datablk = (uint8_t*)SRAM_CACHE_DATA;
  const uint32_t blkn = offset / 512U;

  t_cache_meta cacheinfo;
  if (!load_cache_metadata(&cacheinfo)) {
    // No cache was found, create a new cache with some sensible values.
    cacheinfo.last_addr = offset;
    cacheinfo.dirty = 0;  // We will flush this one, so clean.

    // Read data for that 512 byte block and patch written byte.
    errs |= sdcard_read_blocks(datablk, base_sector() + blkn, 1);
    datablk[offset % 512U] = value;
    errs |= sdcard_write_blocks(datablk, base_sector() + blkn, 1);
  } else {
    // We usually flush when:
    //  - The write happens the end of the 512 byte block.
    //  - The write is non-sequential.
    //  - The write goes to another block (aka, if we need to evict a dirty block).

    // Write back any dirty block if we are gonna replace it.
    const uint32_t cacheblkn = cacheinfo.last_addr / 512U;
    if (blkn != cacheblkn) {
      if (cacheinfo.dirty)
        errs |= sdcard_write_blocks(datablk, base_sector() + cacheblkn, 1);
      errs |= sdcard_read_blocks(datablk, base_sector() + blkn, 1);
    }

    datablk[offset % 512U] = value;
    cacheinfo.dirty = 1;

    // Is this the end of the block, or was this non-sequential?
    if ((offset % 512U) == 512 - 1 || cacheinfo.last_addr + 1 != offset) {
      errs |= sdcard_write_blocks(datablk, base_sector() + blkn, 1);
      cacheinfo.dirty = 0;
    }

    cacheinfo.last_addr = offset;
  }

  // We now fill the cache metadata to SRAM
  save_cache_metadata(&cacheinfo);

  return errs ? -1 : 0;
}


