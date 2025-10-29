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

// Direct save payload info structure (lives in SRAM)
// This is used to load and patch the menu with the required values.
// This header is used both by gcc and as!

#define DIRSAV_CFG_MAGIC               0xDBDD5CF6

#define DIRSAV_CFG_SIZE                24

#define DIRSAV_CFG_MAGIC_OFF           0
#define DIRSAV_CFG_CHKS_OFF            4
#define DIRSAV_CFG_NRAND_OFF           8
#define DIRSAV_CFG_MEMSIZE_OFF         12
#define DIRSAV_CFG_BSECT_OFF           16
#define DIRSAV_CFG_RCA_OFF             20
#define DIRSAV_CFG_ISSDHC_OFF          22
#define DIRSAV_CFG_MUTEX_OFF           23

#ifndef __ASSEMBLER__

// Config: loaded on every load to SRAM, can change (ie. SD sector).
typedef struct {
  uint32_t magic;                      // Magic constant to ensure the config is valid
  uint32_t checksum;                   // Checksum to ensure the config is not corrupted
  uint32_t nrandom;                    // Random value to ensure the checksum is not constant.
  uint32_t memory_size;                // Memory size in bytes the game declared.
  uint32_t base_sector;                // Sector number where the contiguous save file lives.
  uint16_t drv_rca;                    // SD card RCA id (16 bit)
  uint8_t  drv_issdhc;                 // Boolean (is SDHC card)
  uint8_t  sd_mutex;                   // Mutex value (set to one when DS is using the SD card)
} t_dirsave_config;

// Built-in assets
extern const uint8_t directsave_payload[];
extern const uint32_t directsave_payload_size;

_Static_assert (sizeof(t_dirsave_config) == DIRSAV_CFG_SIZE, "t_dirsave_config size mismatch");
_Static_assert (DIRSAV_CFG_SIZE % 4 == 0, "t_dirsave_config size is not word aligned");

_Static_assert (offsetof(t_dirsave_config, magic) == DIRSAV_CFG_MAGIC_OFF, "magic offset mismatch");
_Static_assert (offsetof(t_dirsave_config, checksum) == DIRSAV_CFG_CHKS_OFF, "checksum offset mismatch");
_Static_assert (offsetof(t_dirsave_config, nrandom) == DIRSAV_CFG_NRAND_OFF, "nnrandom offset mismatch");
_Static_assert (offsetof(t_dirsave_config, memory_size) == DIRSAV_CFG_MEMSIZE_OFF, "memory_size offset mismatch");
_Static_assert (offsetof(t_dirsave_config, base_sector) == DIRSAV_CFG_BSECT_OFF, "base_sector offset mismatch");
_Static_assert (offsetof(t_dirsave_config, drv_rca) == DIRSAV_CFG_RCA_OFF, "drv_rca offset mismatch");
_Static_assert (offsetof(t_dirsave_config, drv_issdhc) == DIRSAV_CFG_ISSDHC_OFF, "drv_issdhc offset mismatch");
_Static_assert (offsetof(t_dirsave_config, sd_mutex) == DIRSAV_CFG_MUTEX_OFF, "sd_mutex offset mismatch");

#endif


