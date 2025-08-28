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

// Tests the SRAM, to ensure it actually holds data correctly.
int sram_test() {
  // Similar to above, just way faster :P
  volatile uint8_t *sram_ptr  = (uint8_t*)0x0E000000;
  const static uint8_t tseq[] = { 0xAA, 0x55, 0x00, 0xFF };

  // Test both SRAM banks!
  for (unsigned b = 0; b < 2; b++) {
    set_supercard_mode(MAPPED_SDRAM, b ? true : false, false);
    for (unsigned i = 0; i < 64*1024; i++) {
      uint8_t oval = sram_ptr[i];
      for (unsigned j = 0; j < sizeof(tseq)/sizeof(tseq[0]); j++) {
        sram_ptr[i] = tseq[j];
        if (sram_ptr[i] != tseq[j]) {
          sram_ptr[i] = oval;
          set_supercard_mode(MAPPED_SDRAM, true, true);
          return -i;
        }
      }
      sram_ptr[i] = oval;
    }
  }

  // Ensure we have two banks!
  set_supercard_mode(MAPPED_SDRAM, false, false);
  uint8_t oval0 = sram_ptr[0];
  sram_ptr[0] = 0x0A;
  set_supercard_mode(MAPPED_SDRAM, true, false);
  uint8_t oval1 = sram_ptr[0];
  sram_ptr[0] = 0x05;
  set_supercard_mode(MAPPED_SDRAM, false, false);
  bool mism = (sram_ptr[0] != 0x0A);

  // Restore values before returning.
  set_supercard_mode(MAPPED_SDRAM, false, false);
  sram_ptr[0] = oval0;
  set_supercard_mode(MAPPED_SDRAM, true, false);
  sram_ptr[0] = oval1;

  set_supercard_mode(MAPPED_SDRAM, true, true);

  return mism ? -0x10000 : 0;
}

// Tests whether the SRAM can hold data long term. Fills it with some pseudo random seq.
void sram_pseudo_fill() {
  volatile uint8_t *sram_ptr = (uint8_t*)0x0E000000;
  uint32_t rnval = 0;
  for (unsigned i = 0; i < 64*1024; i++) {
    rnval = rnval * 1103515245 + 12345;    // Generate a new pseudorandom number
    sram_ptr[i] = (uint8_t)(rnval >> 16);
  }
}

unsigned sram_pseudo_check() {
  volatile uint8_t *sram_ptr = (uint8_t*)0x0E000000;
  unsigned errs = 0;
  uint32_t rnval = 0;
  for (unsigned i = 0; i < 64*1024; i++) {
    rnval = rnval * 1103515245 + 12345;    // Generate a new pseudorandom number

    if (sram_ptr[i] != (uint8_t)(rnval >> 16))
      errs++;
  }
  return errs;
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
int sdbench_read(progress_abort_fn progcb) {
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


