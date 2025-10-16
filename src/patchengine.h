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

#ifndef _PATCHENGINE_H_
#define _PATCHENGINE_H_

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define MAX_PATCH_OPS           128   // (artifically limited to save memory)
#define MAX_PATCH_PRG             4   // Only 4 programs can be encoded so far

typedef struct {
  uint32_t length;
  uint8_t data[60];
} t_patch_prog;

struct struct_t_patch {
  uint8_t wcnt_ops;               // WaitCNT patches
  uint8_t save_ops;               // Save patches
  uint8_t save_mode;              // Save mode type (memory type)
  uint8_t irqh_ops;               // IRQ handler patches
  uint8_t rtc_ops;                // RTC patches
  uint32_t hole_size;             // Hole/trailing info, for ROM free space
  uint32_t hole_addr;
  uint32_t op[MAX_PATCH_OPS];     // Contain patch info for waitcnt and save
  t_patch_prog prgs[MAX_PATCH_PRG];
};

typedef struct struct_t_patch t_patch;

typedef struct {
  unsigned filesize;
  // Save type related info
  unsigned save_type_guess;
  unsigned flash64cnt, flash128cnt;
  // RTC stuff
  bool rtc_guess;
  // Trailing data
  uint32_t ldata, ldatacnt;
  // The actual patch data.
  t_patch p;
} t_patch_builder;

struct struct_t_rtc_state;

void patchmem_dbinfo(const uint8_t *dbptr, uint32_t *pcnt, char *version, char *date, char *creator);
// Lookup routines (builtin, on-disk, etc).
bool patchmem_lookup(const uint8_t *gamecode, const uint8_t *dbptr, t_patch *pdata);
// Actual patching magic
bool patch_apply_rom(uint8_t *buffer, unsigned bufsize, uint32_t baseaddr, bool patch_waitcnt, const t_patch *pdata,
                     const struct struct_t_rtc_state *rtc_clock, uint32_t igmenu_addr, uint32_t ds_addr);

void patchengine_init(t_patch_builder *patch, unsigned filesize);
void patchengine_finalize(t_patch_builder *patch);
// Generates a patch set from a given ROM.
bool patchengine_process_rom(const uint32_t *rom, unsigned romsize, t_patch_builder *patch, void(*progresscb)(unsigned));

// Tries to load patches from disk
bool load_cached_patches(const char *romfn, t_patch *patches);
bool load_rom_patches(const char *romfn, t_patch *patches);
// Saves the patches to disk
bool write_patches_cache(const char *romfn, const t_patch *patches);

int serialize_patch(const t_patch *patch, uint8_t *buffer);
bool unserialize_patch(const uint8_t *buffer, unsigned size, t_patch *patch);


#endif

