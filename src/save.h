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

#ifndef _SAVE_H_
#define _SAVE_H_

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "fatfs/ff.h"

#define ERR_SAVE_FLUSH_NOSENTINEL   1
#define ERR_SAVE_FLUSH_WRITEFAIL    2
#define ERR_SAVE_FLUSH_RENAME       3
#define ERR_SAVE_FLUSH_READFAIL     4   // The sentinel exists but can't be read

// The pending save is still only in the SRAM: keep its sentinel, retry later.
static inline bool save_flush_retry(unsigned err) {
  return err == ERR_SAVE_FLUSH_WRITEFAIL || err == ERR_SAVE_FLUSH_READFAIL;
}


// Calculate save game name based on config.
void sram_filename_calc(const char *rom, char *savefn, unsigned save_path);
void savestate_filename_calc(const char *rom, char *statefn);

// Reads/Writes data to SRAM
void read_sram_buffer(uint8_t *buffer, unsigned offset, unsigned len);
void write_sram_buffer(const uint8_t *buf, unsigned offset, unsigned len);

// Loads save file to SRAM
bool load_save_sram(const char *savefn);

// Clears a save file on disk
bool wipe_sav_file(const char *fn);

// Writes SRAM to disk
bool write_save_sram(const char *fn);

// Writes an SRAM file to disk, maintaining backups and whatnot.
bool write_save_sram_rotate(const char *templ_fn, unsigned max_backups);

// Writes a save game from SRAM using a pending file sentinel as input.
unsigned flush_pending_sram();

// Writes/Clears a sentinel file to indicate that SRAM must be dumped and
// stored during the next boot, to preserve the current game save storage.
bool program_sram_dump(const char *save_filename, unsigned backup_count);

// Erases the SRAM (using ones since it seems to be the most common mem type)
void erase_sram();

// Check a contiguous file and return its LBA address
bool file_is_contiguous(const char *fn, LBA_t *lba);

#endif

