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

#define NOR_ENTRY_MAGIC      0x6A7E60D1     // Change if compat requires it.

// Flash (NOR) management
#define NOR_FLASH_SIZE       (128*1024*1024)
#define NOR_BLOCK_SIZE       (4*1024*1024)
#define NOR_BLOCK_COUNT      (NOR_FLASH_SIZE / NOR_BLOCK_SIZE)
#define NOR_GAMEBLOCK_COUNT  (NOR_BLOCK_COUNT - 1)
#define MAX_GAME_BLOCKS      (32*1024*1024 / NOR_BLOCK_SIZE)

#define GATTR_SAVEDS         0x80      // Uses Direct-Saving.
#define GATTR_IGM            0x40      // IGM is enabled.
#define GATTR_RTC            0x20      // RTC emulation is enabled.
#define GATTR_SAVETYPE_MSK   0x07      // Store the save type

#define GATTR_SAVEM(p)       (p ? p->save_mode : 0x7)
#define GET_GATTR_SAVEM(at)  ((((at) & 0x7) == 0x7) ? -1 : (at) & 0x7)


// Describes a game entry in flash (aka NOR)
typedef struct {
  uint32_t gamecode;                   // Game code ID
  uint8_t gamever;                     // Game version byte
  uint8_t numblks;                     // Number of blocks used by this game.
  uint8_t gattrs;                      // Bitfield attributes
  uint8_t bnoffset;                    // Base name offset (byte offset in game_name)
  uint32_t entry_addr;                 // Address of the game entrypoint
  uint8_t blkmap[MAX_GAME_BLOCKS];     // Block mapping (blocks used, in order)
  char game_name[256];                 // UTF-8 encoded file name.
} t_flash_game_entry;

typedef struct {
  uint32_t magic;                      // Harcoded magic value
  uint32_t crc;                        // Simple XOR checksum
  uint32_t gamecnt;                    // Number of games in this entry.
  uint32_t wr_cycles[NOR_BLOCK_COUNT]; // Block write stats, for wear balancing
  t_flash_game_entry games[];
} t_reg_entry;

typedef struct {
  uint32_t magic;                      // Harcoded magic value
  uint32_t crc;                        // Simple XOR checksum
  uint32_t gamecnt;                    // Number of games in this entry.
  uint32_t wr_cycles[NOR_BLOCK_COUNT]; // Block write stats, for wear balancing
  t_flash_game_entry games[FLASHG_MAXFN_CNT];
} t_reg_entry_max;

_Static_assert (sizeof(t_reg_entry) % 4 == 0, "t_reg_entry must be word-friendly");
_Static_assert (sizeof(t_reg_entry_max) % 4 == 0, "t_reg_entry_max must be word-friendly");
_Static_assert (sizeof(t_flash_game_entry) % 4 == 0, "t_flash_game_entry must be word-friendly");

bool flashmgr_load(uint32_t baseaddr, unsigned maxsize, t_reg_entry *ndata);
bool flashmgr_store(uint32_t baseaddr, unsigned maxsize, t_reg_entry *ndata);
bool flashmgr_allocate_blocks(uint8_t *blockmap, unsigned nalloc, t_reg_entry *ndata);
bool flashmgr_wipe(uint32_t baseaddr, unsigned maxsize);


