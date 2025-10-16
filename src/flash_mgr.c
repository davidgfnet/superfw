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

#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "flash_mgr.h"
#include "util.h"

// Flash management routines
//
// A big flash memory with mappeable regions exists and can be used to write
// and load game ROMs.
// Some flash region is devoted to metadata storage. This allows to play
// regardless of the SD card files/state.

static uint32_t xorh(const uint32_t *p, unsigned wc) {
  uint32_t ret = 0;
  while (wc--)
    ret ^= *p++;
  return ret;
}

// Walks and finds the most up to data TOC entry and returns a pointer to it.
static int find_latest(uint32_t flash_addr, unsigned max_size, t_reg_entry *hdr) {
  int ret = -1;    // No last valid entry found
  for (unsigned off = 0; off < max_size; ) {
    flash_read(flash_addr + off, (uint8_t*)hdr, sizeof(*hdr));
    if (hdr->magic != NOR_ENTRY_MAGIC || hdr->gamecnt > FLASHG_MAXFN_CNT)
      break;

    unsigned esz = (sizeof(t_reg_entry) + sizeof(t_flash_game_entry) * hdr->gamecnt);
    ret = off;
    off += esz;
  }

  flash_read(flash_addr + ret, (uint8_t*)hdr, sizeof(*hdr));
  return ret;
}

static bool flashmgr_erase(uint32_t baseaddr, unsigned size) {
  // Ensure the flash has CFI and we know about block size.
  // Currently only homogeneous sector sizes are supported
  if (!flashinfo.size || !flashinfo.blksize || !flashinfo.blkcount || flashinfo.regioncnt != 1)
    return false;

  // The block size has to make sense for our purposes.
  if (size < flashinfo.blksize || (size % flashinfo.blksize != 0))
    return false;

  // Wipe the area block by block
  for (unsigned i = 0; i < size; i += flashinfo.blksize) {
    // Check if the sector is already cleared and skip it.
    if (flash_check_erased(baseaddr + i, flashinfo.blksize))
      continue;

    if (!flash_erase_sector(baseaddr + i))
      return false;
  }

  return true;
}

// Fills the most up to data TOC on flash and returns entry count (or error).
bool flashmgr_load(uint32_t baseaddr, unsigned maxsize, t_reg_entry *ndata) {
  int off = find_latest(baseaddr, maxsize, ndata);
  if (off < 0)
    return false;

  if (ndata->gamecnt > FLASHG_MAXFN_CNT)
    return false;

  unsigned gsize = sizeof(t_flash_game_entry) * ndata->gamecnt;
  unsigned dsize = (sizeof(t_reg_entry) + gsize);
  flash_read(baseaddr + off, (uint8_t*)ndata, dsize);

  // Check the checksum
  uint32_t crc = xorh((uint32_t*)ndata->games, gsize / 4) ^ ndata->gamecnt;
  if (crc != ndata->crc)
    return false;

  // Now check that the game block mapping is well formed.
  uint32_t blkm[BMSIZE(NOR_BLOCK_COUNT, uint32_t)] = {0};

  for (unsigned i = 0; i < ndata->gamecnt; i++) {
    for (unsigned j = 0; j < MAX_GAME_BLOCKS; j++) {
      uint8_t n = ndata->games[i].blkmap[j];
      if (n) {
        if (BM_TEST(blkm, n))
          return false;      // Block is used twice!
        BM_SET(blkm, n);
      }
    }
  }

  return true;
}

// Appends some new entries to the metada flash block.
bool flashmgr_store(uint32_t baseaddr, unsigned maxsize, t_reg_entry *ndata) {
  t_reg_entry hdr;
  const unsigned reqsz = sizeof(t_reg_entry) + sizeof(t_flash_game_entry) * ndata->gamecnt;

  int off = find_latest(baseaddr, maxsize, &hdr);
  if (off < 0 || off + reqsz > maxsize) {
    // Flash looks bogus, or is full. Let's wipe it!
    if (!flashmgr_erase(baseaddr, maxsize))
      return false;

    off = 0;  // Start writing at the top now that it's empty.
  }
  else {
    const unsigned currsz = sizeof(t_reg_entry) + sizeof(t_flash_game_entry) * hdr.gamecnt;
    off += currsz;      // Skip to the end of the entry
  }

  // Repurpose the last header (should contain the right block balancing data).
  ndata->magic = NOR_ENTRY_MAGIC;
  ndata->crc = xorh((uint32_t*)ndata->games, (sizeof(t_flash_game_entry) * ndata->gamecnt) / 4) ^ ndata->gamecnt;

  if (!flash_program_buffered(baseaddr + off, (uint8_t*)ndata, reqsz, flashinfo.blkwrite))
    return false;

  if (!flash_verify(baseaddr + off, (uint8_t*)ndata, reqsz))
    return false;

  return true;
}

// Allocates blocks based on wear and updates write cycles information.
bool flashmgr_allocate_blocks(uint8_t *blockmap, unsigned nalloc, t_reg_entry *ndata) {
  // Check which blocks are free and allocate some free blocks to use.
  uint32_t blkm[BMSIZE(NOR_BLOCK_COUNT, uint32_t)] = {0};
  for (unsigned i = 0; i < ndata->gamecnt; i++)
    for (unsigned j = 0; j < MAX_GAME_BLOCKS; j++)
      BM_SET(blkm, ndata->games[i].blkmap[j]);

  // Allocate blocks prioritizing blocks with less write cycles.
  for (unsigned a = 0; a < nalloc; a++) {
    uint8_t cand = 0;
    uint32_t wrcyc = ~0U;
    for (unsigned i = 1; i < NOR_BLOCK_COUNT; i++) {
      if (!BM_TEST(blkm, i) && ndata->wr_cycles[i] < wrcyc) {
        cand = i;
        wrcyc = ndata->wr_cycles[i];
      }
    }

    if (!cand)
      return false;

    // Mark the block as used, book it and increase cycle count.
    BM_SET(blkm, cand);
    blockmap[a] = cand;
    ndata->wr_cycles[cand]++;
  }

  return true;
}

