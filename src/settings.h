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

#ifndef _SETTINGS_H_
#define _SETTINGS_H_

#include <stdint.h>
#include <stdbool.h>

#include "common.h"

typedef struct {
  const char * const cname;
  uint16_t mask;
} t_combo_key;

extern const t_combo_key hotkey_list[13];
#define hotkey_listcnt (sizeof(hotkey_list)/sizeof(hotkey_list[0]))

extern const uint8_t animspd_lut[6];
#define animspd_cnt (sizeof(animspd_lut)/sizeof(animspd_lut[0]))

enum { SaveSavegameDir = 0, SaveSavesDir = 1, SaveRomName = 2, SaveDirCNT = 3 };
enum { StateSavestateDir = 0, StateRomName = 1, StateDirCNT = 2 };

// Menu settings
extern uint32_t menu_theme;
extern uint32_t lang_id;
extern uint32_t recent_menu;
extern uint32_t anim_speed;

// Defaults/Settings
extern t_patch_policy patcher_default;
extern uint32_t boot_bios_splash;
extern uint32_t use_slowsd;
extern uint32_t use_fastew;
extern uint32_t save_path_default;
extern uint32_t state_path_default;
extern uint32_t backup_sram_default;
extern uint32_t hotkey_combo;
extern uint32_t enable_cheats;
extern uint32_t autoload_default;
extern uint32_t autosave_default;
extern uint32_t autosave_prefer_ds;
extern uint32_t ingamemenu_default;
extern uint32_t rtcpatch_default;
extern t_rtc_state rtcvalue_default;

// Setting load/store
bool save_ui_settings();
bool save_settings();
void load_settings();

// ROM-specific setting load/store
bool load_rom_settings(const char *fn, t_rom_settings *rs);
bool save_rom_settings(const char *fn, const t_rom_settings *rs);

#endif

