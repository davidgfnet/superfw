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

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "gbahw.h"
#include "save.h"
#include "patchengine.h"
#include "settings.h"
#include "ingame.h"
#include "fonts/font_render.h"
#include "supercard_driver.h"
#include "fatfs/ff.h"
#include "directsave.h"
#include "common.h"
#include "util.h"

// Here we have the ROM loading routines.

#define LOAD_BS     (8*1024)     // Load in 8KB chunks

#define GBA_ROM_ADDR                  ((volatile  uint8_t *)0x08000000)
#define GBA_ROM_ADDR16(addr, value)   *((volatile uint16_t *)(0x08000000 + addr)) = (value)
#define GBA_ROM_ADDR32(addr, value)   *((volatile uint32_t *)(0x08000000 + addr)) = (value)

#define ING_PALETTE_BASE    240

extern bool slowsd;

bool validate_gba_header(const uint8_t *header) {
  const t_rom_header *gbah = (t_rom_header*)header;

  // Check that the checksum is OK:
  uint8_t checksum = 0x19;
  for (unsigned i = 0xA0; i < 0xBD; i++)
    checksum += header[i];
  checksum = -checksum;
  if (checksum != gbah->checksum)
    return false;

  // Misc header stuff
  if (header[0xb2] != 0x96)
    return false;

  // Validate the nintendo logo as well
  uint32_t ck = 0;
  for (unsigned i = 0; i < 39; i++)
    ck ^= gbah->logo_data[i];
  if (ck != 0xf8cff8fc)
    return false;

  return true;
}

bool validate_gb_header(const uint8_t *header) {
  // Check that the checksum is OK:
  const t_gbheader* hdr = (t_gbheader*)header;
  uint8_t checksum = 0;
  for (unsigned i = 0x34; i <= 0x4C; i++)
    checksum = checksum - header[i] - 1;
  if (checksum != hdr->checksum)
    return false;

  const uint32_t *logo32 = (uint32_t*)hdr->logo_data;
  uint32_t logocheck = 0;
  for (unsigned i = 0; i < 12; i++)
    logocheck ^= logo32[i];
  if (logocheck != 0x83e1df3b)
    return false;

  return true;
}

void fix_gba_header(volatile uint16_t *header) {
  // Fix header checksum unconditionally.
  header[0xB2 / 2] = 0x0096;     // Device ID/fixed value
  header[0xB4 / 2] = 0x0000;
  header[0xB6 / 2] = 0x0000;
  header[0xB8 / 2] = 0x0000;
  header[0xBA / 2] = 0x0000;
  header[0xBC / 2] = header[0xBC / 2] & 0xFF; // Preserve version num

  uint8_t crc = header[0xBC / 2];
  for (unsigned i = 0; i < 10; i++) {
    uint16_t v = header[0xA0 / 2 + i];
    crc += (v & 0xFF) + (v >> 8);
  }

  header[0xBC / 2] = (-(0x19+crc)) << 8 | header[0xBC / 2];
}

// Loads the in-game menu at the desired address and size (returns success).
// addr must be 4 byte aligned.
void load_ingame_menu(
  uint32_t base_addr, uint32_t total_size, uint32_t ds_addr,
  const t_rom_header *rom_header,
  const char* savefn, const char* statefn,
  bool rtc_patches, unsigned cheats
) {
  const unsigned menu_size = ingame_menu_payload.menu_rsize;
  const unsigned fontsz = font_block_size();

  set_supercard_mode(MAPPED_SDRAM, true, false);

  // Copy the font pack first, using memmove to handle collisions properly.
  // TODO: Allow partial font copying, to reduce memory usage (ie. in 32MiB ROMs)
  uint8_t *ptr = (uint8_t*)base_addr;
  uint8_t *font_ptr = (uint8_t*)ROM_FONTBASE_U8;
  // Copy fonts, and cheats (appended right after the cheat buffer)
  memmove32(&ptr[menu_size], font_ptr, fontsz + cheats);

  // Copy the in-game-menu payload from rodata
  t_igmenu *igm = (t_igmenu*)base_addr;
  memcpy32(ptr, &ingame_menu_payload, ingame_menu_payload_size);

  // Decode the start instruction, calculate branch target.
  uint32_t tgtaddr = ((rom_header->start_branch & 0xFFFFFF) << 2) + 8 + 0x08000000;
  uint16_t hotk = hotkey_list[hotkey_combo].mask;

  // Patch the menu header to provide necessary data.
  igm->startup_addr = tgtaddr;                // Target reset jump
  igm->drv_issdhc = sc_issdhc();              // SD driver data (no init happens)
  igm->drv_rca = sc_rca();
  igm->menu_hotkey = hotk;                    // Configured hotkey
  igm->menu_lang = lang_id;                   // Use the current lang id
  igm->menu_directsave_base = ds_addr;        // DirectSave base addr
  igm->menu_anim_speed = anim_speed;          // Menu animation speed
  igm->menu_font_base = base_addr + menu_size;// Base addr where the fonts live
  igm->menu_cheats_base = cheats ? base_addr + menu_size + fontsz : 0;
  igm->scratch_space_base = base_addr + menu_size + fontsz + cheats;
  igm->scratch_space_size = total_size - (menu_size + fontsz + cheats);
  igm->menu_has_rtc_support = rtc_patches;    // Using RTC patches
  igm->savefile_backups = backup_sram_default;// Backup count
  igm->menu_palette[0] = MEM_PALETTE[ING_PALETTE_BASE];
  igm->menu_palette[1] = MEM_PALETTE[ING_PALETTE_BASE + 1];
  igm->menu_palette[2] = MEM_PALETTE[ING_PALETTE_BASE + 2];
  igm->menu_palette[3] = MEM_PALETTE[ING_PALETTE_BASE + 3];

  if (savefn)
    memcpy32(igm->savefile_pattern, savefn, sizeof(igm->savefile_pattern));
  else
    memset32(igm->savefile_pattern, 0, sizeof(igm->savefile_pattern));

  memcpy32(igm->statefile_pattern, statefn, sizeof(igm->statefile_pattern));

  set_supercard_mode(MAPPED_SDRAM, true, true);
}

void load_directsave_payload(uint32_t address, const t_dirsave_info *dsinfo) {
  // Copy the direct save payload to the specified address offset.
  uint8_t *ptr = ((uint8_t*)address);
  memcpy32(ptr, directsave_payload, directsave_payload_size);

  // Patch header with the necessary fields.
  t_dirsave_header *hdr = (t_dirsave_header*)ptr;
  hdr->base_sector = dsinfo->sector_lba;
  hdr->memory_size = dsinfo->save_size;
  hdr->drv_issdhc = sc_issdhc();
  hdr->drv_rca = sc_rca();
}

// Loads ROM header from disk for inspection.
unsigned preload_gba_rom(const char *fn, uint32_t fs, t_rom_header *romh) {
  FIL fd;
  FRESULT res = f_open(&fd, fn, FA_READ);
  if (res != FR_OK)
    return ERR_LOAD_BADROM;

  UINT rdbytes;
  bool err = (FR_OK != f_read(&fd, romh, sizeof(*romh), &rdbytes) || rdbytes != sizeof(*romh));

  f_close(&fd);
  return err ? ERR_LOAD_BADROM : 0;
}

__attribute__((noinline))
unsigned load_gba_rom(
  const char *fn, uint32_t fs,
  const t_rom_header *rom_header,
  const t_patch *ptch,
  const t_dirsave_info *dsinfo,
  bool ingame_menu,
  const t_rtc_state *rtc_clock,
  unsigned cheats,
  progress_fn progress
) {

  // Determine how much ROM space we need for the IGM and DirSav payloads
  const unsigned igm_reqsz = ingame_menu_payload.menu_rsize + font_block_size();
  // Round it up, reserve ~1KB after the ROM for patches.
  // 32MiB games cannot generate patches beyond the end.
  const unsigned romrsize = ROUND_UP2(fs, 1024) + (fs < MAX_GBA_ROM_SIZE ? 1024 : 0);
  // Required size for these payloads. We should always have enough, since menu checks it.
  const unsigned req_size = (ingame_menu ? igm_reqsz : 0) + (dsinfo ? DIRSAVE_REQ_SPACE : 0);

  uint32_t igm_addr, igm_space, ds_addr;
  if (romrsize + req_size <= MAX_GBA_ROM_SIZE) {
    // Allocate the DirSav payload first
    ds_addr = romrsize;
    // Now the IGM
    igm_addr = ds_addr + (dsinfo ? DIRSAVE_REQ_SPACE : 0);
    // Calculate the total space for the IGM to use
    igm_space = MAX_GBA_ROM_SIZE - igm_addr;
  }
  else {
    // Cannot append it at the end, it's too big. Check if we have a hole.
    if (!ptch || ptch->hole_size < req_size)
      return ERR_NO_PAYLOAD_SPACE;

    ds_addr = ptch->hole_addr;
    igm_addr = ds_addr + (dsinfo ? DIRSAVE_REQ_SPACE : 0);
    igm_space = ptch->hole_size - (dsinfo ? DIRSAVE_REQ_SPACE : 0);
  }
  // Round it down to a KB boundary
  igm_space &= ~1023;

  // Calculate the "hole" limits
  uint32_t gap_start = ds_addr;
  uint32_t gap_end = igm_addr + igm_space;

  // Get aboslute addresses
  ds_addr += GBA_ROM_BASE;
  igm_addr += GBA_ROM_BASE;

  // Install the menu before loading the ROM, otherwise we overwrite relevant assets.
  if (ingame_menu) {
    bool use_rtc_patches = rtc_clock != NULL;
    char sfn[MAX_FN_LEN];
    savestate_filename_calc(fn, sfn);
    if (dsinfo) {
      // If DirSave is enabled, we disable the menu save facilities.
      load_ingame_menu(igm_addr, igm_space, ds_addr, rom_header, NULL, sfn, use_rtc_patches, cheats);
    } else {
      // Calculate the basename, so we can produce proper sav/backup files
      char save_basename[MAX_FN_LEN];
      sram_template_filename_calc(fn, "", save_basename);

      load_ingame_menu(igm_addr, igm_space, 0, rom_header, save_basename, sfn, use_rtc_patches, cheats);
    }
  }

  // Proceed to load the ROM
  FIL fd;
  FRESULT res = f_open(&fd, fn, FA_READ);
  if (res != FR_OK)
    return ERR_LOAD_BADROM;

  // Honor fast loading (switch mirror if appropriate)
  slowsd = use_slowld;

  uint8_t *ptr = (uint8_t*)(GBA_ROM_ADDR);
  for (uint32_t offset = 0; offset < gap_start; offset += LOAD_BS) {
    if (progress && (offset & (256*1024-1)) == 0)
      progress(offset >> 8, fs >> 8);

    unsigned toread = MIN(LOAD_BS, gap_start - offset);
    UINT rdbytes;
    uint32_t tmp[LOAD_BS/4];
    if (FR_OK != f_read(&fd, tmp, toread, &rdbytes)) {
      slowsd = true;
      f_close(&fd);
      return ERR_LOAD_BADROM;
    }

    set_supercard_mode(MAPPED_SDRAM, true, false);
    if (use_slowld)
      rom_copy_write16(&ptr[offset], tmp, toread);
    else
      dma_memcpy32(&ptr[offset], tmp, toread/4);
    set_supercard_mode(MAPPED_SDRAM, true, true);
  }
  for (uint32_t offset = gap_end; offset < fs; offset += LOAD_BS) {
    if (progress && (offset & (256*1024-1)) == 0)
      progress(offset >> 8, fs >> 8);

    unsigned toread = MIN(LOAD_BS, fs - offset);
    UINT rdbytes;
    uint32_t tmp[LOAD_BS/4];
    if (FR_OK != f_read(&fd, tmp, toread, &rdbytes)) {
      slowsd = true;
      f_close(&fd);
      return ERR_LOAD_BADROM;
    }

    set_supercard_mode(MAPPED_SDRAM, true, false);
    if (use_slowld)
      rom_copy_write16(&ptr[offset], tmp, toread);
    else
      dma_memcpy32(&ptr[offset], tmp, toread/4);
    set_supercard_mode(MAPPED_SDRAM, true, true);
  }
  progress(1, 1);  // Mark as complete

  slowsd = true;

  // Close the file, not super necessary really :P
  f_close(&fd);

  // Proceed to patch the ROM
  set_supercard_mode(MAPPED_SDRAM, true, false);

  // Load/Patch the DirectSave payload if necessary.
  if (dsinfo)
    load_directsave_payload(ds_addr, dsinfo);

  // Actually apply patches
  if (ptch)
    patch_apply_rom(ptch, rtc_clock, ingame_menu ? igm_addr : 0, dsinfo ? ds_addr : 0);

  // Fix header checksum unconditionally (just in case we boot to BIOS).
  fix_gba_header((uint16_t*)GBA_ROM_ADDR);

  // Set the ROM into read only mode, disable SD card reader as well.
  set_supercard_mode(MAPPED_SDRAM, false, false);

  launch_reset(boot_bios_splash, use_fastew);

  return 0;
}

__attribute__((noinline))
unsigned launch_gba_nor(
  const uint8_t *normap, unsigned blkcnts
  // TODO: Add more launching options? Patching?
) {

  // Map the game NOR blocks.
  // TODO
  set_supercard_normap(normap);

  // Set the ROM into read only mode, disable SD card reader as well.
  set_supercard_mode(MAPPED_FIRMWARE, false, false);

  launch_reset(boot_bios_splash, use_fastew);

  return 0;
}

// Generic-emulator (ie. NES, SMS, ...) loader
__attribute__((noinline))
unsigned load_extemu_rom(const char *fn, uint32_t fs, const t_emu_loader *ldinfo, progress_fn progress) {
  FIL fd;
  uint8_t *ptr = (uint8_t*)(GBA_ROM_ADDR);
  if (fs > 8*1024*1024)
    return ERR_LOAD_BADROM;

  // Try to find a valid and existing emulator.
  if (!memcmp(ldinfo->emu_name, "vfs:", 4)) {
    // Look the emulator up in the VFS

    set_supercard_mode(MAPPED_SDRAM, true, false);
    const void *emupload = get_vfile_ptr(&ldinfo->emu_name[4]);
    if (!emupload) {
      // In case the emulator is not bundled in.
      set_supercard_mode(MAPPED_SDRAM, true, true);
      return ERR_LOAD_NOEMU;
    }
    ptr += apunpack16(emupload, ptr);
    set_supercard_mode(MAPPED_SDRAM, true, true);
  }
  else {
    char emupath[64];
    strcpy(emupath, EMULATORS_PATH);
    strcat(emupath, ldinfo->emu_name);
    strcat(emupath, ".gba");

    // Load the emulator from disk, check whether it exists tho.
    if (FR_OK != f_open(&fd, emupath, FA_READ))
      return ERR_LOAD_NOEMU;

    while (1) {
      UINT rdbytes;
      uint32_t tmp[LOAD_BS/4];
      if (FR_OK != f_read(&fd, tmp, LOAD_BS, &rdbytes)) {
        f_close(&fd);
        return ERR_LOAD_NOEMU;
      }
      if (!rdbytes)
        break;

      // Copy data into the ROM (disable SD interface to avoid collisions!)
      set_supercard_mode(MAPPED_SDRAM, true, false);
      if (use_slowld)
        rom_copy_write16(ptr, tmp, rdbytes);
      else
        dma_memcpy32(ptr, tmp, rdbytes/4);
      set_supercard_mode(MAPPED_SDRAM, true, true);
      ptr += rdbytes;
    }
    f_close(&fd);
  }

  // Generate rom header and what not.
  if (ldinfo->hndlr)
    ptr += ldinfo->hndlr(ptr, fn, fs);

  // Proceed to load the ROM now.
  if (FR_OK != f_open(&fd, fn, FA_READ))
    return ERR_LOAD_BADROM;

  for (uint32_t offset = 0; offset < fs; offset += LOAD_BS) {
    if (progress && (offset & (64*1024-1)) == 0)
      progress(offset, fs);

    UINT rdbytes;
    uint32_t tmp[LOAD_BS/4];
    if (FR_OK != f_read(&fd, tmp, LOAD_BS, &rdbytes)) {
      f_close(&fd);
      return ERR_LOAD_BADROM;
    }

    // Copy data into the ROM (disable SD interface to avoid collisions!)
    set_supercard_mode(MAPPED_SDRAM, true, false);
    if (use_slowld)
      rom_copy_write16(ptr, tmp, LOAD_BS);
    else
      dma_memcpy32(ptr, tmp, LOAD_BS/4);
    set_supercard_mode(MAPPED_SDRAM, true, true);
    ptr += LOAD_BS;
  }

  // Close the file, not super necessary really :P
  f_close(&fd);

  // Set the ROM into read only mode, disable SD card reader as well.
  set_supercard_mode(MAPPED_SDRAM, false, false);

  REG_WAITCNT = 0x4000;   // Default timings + prefetch
  launch_reset(false, use_fastew);

  return 0;
}


