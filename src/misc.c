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

#include "compiler.h"
#include "common.h"
#include "util.h"
#include "save.h"
#include "supercard_driver.h"
#include "fatfs/ff.h"

static const uint32_t start_seed = 0xdeadbeef;
static uint32_t lcg32(uint32_t s) {
  return s * 1664525U + 1013904223U;
}

// Tests the SDRAM, to ensure it actually holds data correctly.
ARM_CODE IWRAM_CODE NOINLINE
int sdram_test(progress_abort_fn progcb) {
  // Write stuff to SDRAM and check that it indeed was properly written.
  uint32_t tmp[2][512];
  volatile uint32_t *sdram_ptr  = (uint32_t*)GBA_ROM_BASE;
  uint32_t rndgen = start_seed;
  uint32_t pos = 0;
  int ret = 0;

  for (unsigned i = 0; i < 8*1024*1024; i++) {
    // Validate the previously written position
    if (i >= 512) {
      uint32_t prevpos = (pos - 22541U * 512U) & (8*1024*1024U - 1U);
      if (tmp[1][i & 511] != sdram_ptr[prevpos]) {
        ret = -i;
        break;
      }
      sdram_ptr[prevpos] = tmp[0][i & 511];
    }

    // Store the current SDRAM value and the random value we are writing.
    tmp[0][i & 511] = sdram_ptr[pos];
    tmp[1][i & 511] = rndgen;

    // Write to SDRAM
    sdram_ptr[pos] = rndgen;

    // Advance pointers
    pos = (pos + 22541) & (8*1024*1024U - 1U);
    rndgen = lcg32(rndgen);

    // Update progress every now and then.
    if (!((i+1) & 0xFFFF)) {
      if (progcb(i >> 16, 128))
        break;
    }
  }

  // Restore tmp data
  for (unsigned i = 0; i < 512; i++) {
    uint32_t prevpos = (pos - 22541U * 512U) & (8*1024*1024U - 1U);
    sdram_ptr[prevpos] = tmp[0][i & 511];
    pos = (pos + 22541) & (8*1024*1024U - 1U);
  }

  return ret;
}

// Tests whether the SRAM can hold data long term. Fills it with some pseudo random seq.
NOINLINE void sram_pseudo_fill() {
  uint32_t rnval = 0;
  uint8_t tmp[512];
  for (unsigned i = 0; i < 128*1024; i += sizeof(tmp)) {
    for (unsigned j = 0; j < sizeof(tmp); j++) {
      rnval = rnval * 1103515245 + 12345;    // Generate a new pseudorandom number
      tmp[j] = (uint8_t)(rnval >> 16);
    }
    write_sram_buffer(tmp, i, sizeof(tmp));
  }
}

NOINLINE unsigned sram_pseudo_check() {
  unsigned errs = 0;
  uint32_t rnval = 0;
  uint8_t tmp[512];
  for (unsigned i = 0; i < 128*1024; i += sizeof(tmp)) {
    read_sram_buffer(tmp, i, sizeof(tmp));

    for (unsigned j = 0; j < sizeof(tmp); j++) {
      rnval = rnval * 1103515245 + 12345;    // Generate a new pseudorandom number
      if (tmp[j] != (uint8_t)(rnval >> 16))
        errs++;
    }
  }
  return errs;
}

// Tests the SRAM, to ensure it actually holds data correctly. Destroys data!
unsigned sram_test() {
  // Just piggyback on the battery test implementation.
  sram_pseudo_fill();
  return sram_pseudo_check();
}

void program_sram_check() {
  // Just drop a file to schedule an SRAM test next boot.
  f_mkdir(SUPERFW_DIR);

  FIL fout;
  if (FR_OK == f_open(&fout, PENDING_SRAM_TEST, FA_WRITE | FA_CREATE_ALWAYS))
    f_close(&fout);
}

int check_peding_sram_test() {
  if (check_file_exists(PENDING_SRAM_TEST)) {
    // Remove the file, avoid doing this again!
    f_unlink(PENDING_SRAM_TEST);

    return sram_pseudo_check();
  }
  return -1;
}

// Tests the SD card by reading blocks (directly) and discarding the data.
NOINLINE int sdbench_read(progress_abort_fn progcb) {
  // Just read consecutive blocks without repeating them (to avoid any caching)
  // Assume ~1MB/s read rate, aim for 8 seconds aprox. Read 8MiB in 8KiB blocks.
  unsigned start_frame = frame_count;
  for (unsigned i = 0; i < 1024; i++) {
    uint32_t buf[8 * 1024 / sizeof(uint32_t)];
    const unsigned blkcnt = sizeof(buf) / 512;

    unsigned ret = sdcard_read_blocks((uint8_t*)buf, i * blkcnt, blkcnt);
    if (ret)
      return -1;

    // Update progress every now and then (aim for 0.25s)
    if (!(i & 0x1F)) {
      if (progcb(i, 1024))
        return 0;
    }
  }
  unsigned end_frame = frame_count;

  // Return milliseconds, do math in 1K microseconds.
  return ((end_frame - start_frame) * 17067) >> 10;
}


