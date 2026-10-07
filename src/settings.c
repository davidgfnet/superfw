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

#include <string.h>
#include <stdlib.h>

#include "settings.h"
#include "fatfs/ff.h"
#include "common.h"
#include "nanoprintf.h"
#include "util.h"

#pragma GCC optimize ("Os")

unsigned lang_lookup(uint16_t code);
uint16_t lang_getcode();

const t_combo_key hotkey_list[] = {
  {"L+R+Start",     0x00F7},
  {"L+R+Select",    0x00FB},
  {"L+R+Start+Sel", 0x00F3},
  {"L+R",           0x00FF},
  {"L+R+A",         0x00FE},
  {"L+R+B",         0x00FD},
  {"L+R+⯇+A",       0x00DE},
  {"L+R+⯈+B",       0x00ED},
  {"L+R+⯅+A",       0x00BE},
  {"L+R+⯆+A",       0x007E},
  {"A+B+Start",     0x03F4},
  {"A+B+Select",    0x03F8},
  {"A+B+Start+Sel", 0x03F0},
};

const char *save_paths[] = {
  "/SAVEGAME/",
  "/SAVES/",
};

const char *savestates_paths[] = {
  "/SAVESTATE/",
  "/.superfw/savestate/",
};

const char *savestates_paths_display[] = {
  "/SAVESTATE/",
  ".sfw/savestate",
};

const uint8_t animspd_lut[] = {
  2,    //  8 pix/second
  3,    // 12 pix/second
  6,    // 24 pix/second
  8,    // 32 pix/second
  12,   // 48 pix/second
};

// Menu settings
uint8_t menu_theme = 0;
uint8_t lang_id = 0;
uint8_t recent_menu = 1;
uint8_t hide_hidden = 0;
uint8_t anim_speed = animspd_cnt / 2;

// Default settings
t_patch_policy patcher_default = PatchAuto;

uint8_t boot_bios_splash = 0;   // Whether the BIOS boots to the splash screen
uint8_t use_slowld = 0;         // Use slow mirrors for ROM loading, check loaded data.
uint8_t use_fastew = 0;         // Overclock EWRAM while playing.
uint8_t use_verify_nor = 0;     // Verify flash writes

uint8_t save_path_default = SaveSavegameDir;
uint8_t save_path_nor_default = SaveSavegameDir;
uint8_t state_path_default = StateSavestateDir;

uint8_t backup_sram_default = 0;  // Number of older SRAM save to keep as backup

uint8_t hotkey_combo = 0;  // Hotkey Combo number
uint8_t enable_cheats = 0; // By default cheats are disabled (it's slightly faster)

uint8_t autoload_default = 1;
uint8_t autosave_default = 1;
uint8_t autosave_prefer_ds = 1;
uint8_t ingamemenu_default = 1;
uint8_t rtcpatch_default = 1;
uint8_t rtcspeed_default = 3;

uint32_t rtcvalue_default = 45568800U;

// Setting loading/saving routines
bool save_ui_settings() {
  // Create the directory (just in case it doesn't exist
  f_mkdir(SUPERFW_DIR);
  // Make it hidden
  f_chmod(SUPERFW_DIR, AM_HID, AM_HID);

  // Proceed to create the file
  FIL fd;
  if (FR_OK != f_open(&fd, UISETTINGS_FILEPATH, FA_WRITE | FA_CREATE_ALWAYS))
    return false;

  // Serialize the settings
  uint16_t lc = lang_getcode();
  char buf[512];
  npf_snprintf(buf, sizeof(buf),
    "theme=%u\n"
    "langcode=%c%c\n"
    "recent_menu=%u\n"
    "anim_speed=%u\n"
    "hide_hidden=%u\n",
    menu_theme, (lc & 0xFF), (lc >> 8), recent_menu, anim_speed, hide_hidden);

  UINT wrbytes;
  FRESULT res = f_write(&fd, buf, strlen(buf), &wrbytes);
  f_close(&fd);

  return FR_OK == res;
}

bool save_settings() {
  // Create the directory (just in case it doesn't exist
  f_mkdir(SUPERFW_DIR);
  // Make it hidden
  f_chmod(SUPERFW_DIR, AM_HID, AM_HID);

  // Proceed to create the file
  FIL fd;
  if (FR_OK != f_open(&fd, SETTINGS_FILEPATH, FA_WRITE | FA_CREATE_ALWAYS))
    return false;

  // Serialize the settings
  char buf[512];
  npf_snprintf(buf, sizeof(buf),
    "hotkey_opt=%u\n"
    "boot_to_bios=%u\n"
    "save_path_policy=%u\n"
    "save_path_nor_policy=%u\n"
    "state_path_policy=%u\n"
    "sram_backup_count=%u\n"
    "enable_cheats=%u\n"
    "enable_slowld=%u\n"
    "enable_fastewram=%u\n"
    "enable_norwrcheck=%u\n"
    "default_patcher=%u\n"
    "default_igmenu=%u\n"
    "default_rtcpatch=%u\n"
    "default_rtctick=%u\n"
    "default_loadgame=%u\n"
    "default_savegame=%u\n"
    "prefer_directsave=%u\n"
    "default_rtcts=%lu\n",
    hotkey_combo, boot_bios_splash, save_path_default, save_path_nor_default,
    state_path_default, backup_sram_default, enable_cheats, use_slowld, use_fastew,
    use_verify_nor, (unsigned int)patcher_default, ingamemenu_default, rtcpatch_default,
    rtcspeed_default, autoload_default, autosave_default, autosave_prefer_ds,
    rtcvalue_default);

  UINT wrbytes;
  FRESULT res = f_write(&fd, buf, strlen(buf), &wrbytes);
  f_close(&fd);

  return FR_OK == res;
}

static void parse_settings(void *usr, const char *var, const char *value) {
  unsigned valu = parseuint(value);
  if (!strcmp(var, "default_rtcts"))
    rtcvalue_default = valu;
  else {
    static const struct {
      const char *s;
      uint8_t * const var;
    } bolset[] = {
      { "boot_to_bios",      &boot_bios_splash },
      { "enable_cheats",     &enable_cheats },
      { "default_igmenu",    &ingamemenu_default },
      { "enable_slowld",     &use_slowld },
      { "enable_fastewram",  &use_fastew },
      { "enable_norwrcheck", &use_verify_nor },
      { "default_rtcpatch",  &rtcpatch_default },
      { "default_loadgame",  &autoload_default },
      { "default_savegame",  &autosave_default },
      { "prefer_directsave", &autosave_prefer_ds },
    };
    for (unsigned i = 0; i < sizeof(bolset)/sizeof(bolset[0]); i++)
      if (!strcmp(var, bolset[i].s)) {
        *bolset[i].var = valu & 1;
        break;
      }

    static const struct {
      const char *s;
      uint8_t * const var;
      const unsigned modval;
    } uintset[] = {
      { "hotkey_opt",           &hotkey_combo,          sizeof(hotkey_list)/sizeof(hotkey_list[0]) },
      { "save_path_policy",     &save_path_default,     SaveDirCNT },
      { "save_path_nor_policy", &save_path_nor_default, SaveDirNORCNT },
      { "state_path_policy",    &state_path_default,    StateDirCNT },
      { "sram_backup_count",    &backup_sram_default,   MAX_BACKUP_CNT + 1 },
      { "default_patcher",      &patcher_default,       PatchTotalCNT },
      { "default_rtctick",      &rtcspeed_default,      RTC_SPEED_CNT },
    };
    for (unsigned i = 0; i < sizeof(uintset)/sizeof(uintset[0]); i++)
      if (!strcmp(var, uintset[i].s)) {
        *uintset[i].var = valu % uintset[i].modval;
        break;
      }
  }
}

static void parse_ui_settings(void *usr, const char *var, const char *value) {
  if (!strcmp(var, "langcode")) {
    uint16_t code = ((uint8_t)value[0]) | (((uint8_t)value[1]) << 8);
    lang_id = lang_lookup(code);
  } else {
    static const struct {
      const char *s;
      uint8_t * const var;
    } uintset[] = {
      { "theme",       &menu_theme },
      { "recent_menu", &recent_menu },
      { "hide_hidden", &hide_hidden },
      { "anim_speed",  &anim_speed },
    };
    unsigned valu = parseuint(value);
    for (unsigned i = 0; i < sizeof(uintset)/sizeof(uintset[0]); i++)
      if (!strcmp(var, uintset[i].s)) {
        *uintset[i].var = valu;
        break;
      }
  }
}

// Parses a config file and calls the user callback with varname+value
// Pointers are only valid for the duration of the callback!
static void parse_file(char *buf, void(*parse_cb)(void *usr, const char*, const char*), void *usrptr) {
  char *p = buf;
  while (1) {
    char *e = strchr(p, '\n');
    if (e)
      *e = 0;

    char *a = strchr(p, '=');
    if (a) {
      *a = 0;
      parse_cb(usrptr, p, &a[1]);
      *a = '=';
    }

    if (!e)
      break;

    *e = '\n';
    p = &e[1];  // Advance to the next line
  }
}

void load_settings() {
  FIL fd;
  UINT rdbytes;
  char buf[512];
  if (FR_OK == f_open(&fd, SETTINGS_FILEPATH, FA_READ)) {
    if (FR_OK == f_read(&fd, buf, sizeof(buf) - 1, &rdbytes)) {
      buf[rdbytes] = 0;
      parse_file(buf, parse_settings, NULL);
    }
    f_close(&fd);
  }

  if (FR_OK == f_open(&fd, UISETTINGS_FILEPATH, FA_READ)) {
    if (FR_OK == f_read(&fd, buf, sizeof(buf) - 1, &rdbytes)) {
      buf[rdbytes] = 0;
      parse_file(buf, parse_ui_settings, NULL);
    }
    f_close(&fd);
  }
}

void sram_filename_calc(const char *rom, char *savefn, unsigned save_path) {
  if (save_path == SaveRomName) {
    strcpy(savefn, rom);   // Use the full ROM path
  }
  else {
    const char *p = file_basename(rom);
    const char *path = save_paths[save_path];
    strcpy(savefn, path);   // Add the base path
    strcat(savefn, p);      // Append just the basename
  }

  replace_extension(savefn, ".sav");
}

void savestate_filename_calc(const char *rom, char *statefn) {
  const char *p = file_basename(rom);
  strcpy(statefn, savestates_paths[state_path_default]);   // Add the base path
  strcat(statefn, p);               // Append just the basename
  replace_extension(statefn, "");
}

static void parse_rom_load_settings(void *usr, const char *var, const char *value) {
  t_rom_load_settings *rs = (t_rom_load_settings*)usr;
  unsigned valu = parseuint(value);
  if (!strcmp(var, "rtc"))
    rs->use_rtc = valu & 1;
  else if (!strcmp(var, "igm"))
    rs->use_igm = valu & 1;
  else if (!strcmp(var, "directsaving"))
    rs->use_dsaving = valu & 1;
  else if (!strcmp(var, "patchmode"))
    rs->patch_policy = valu % PatchOptCNT;
}

static void parse_rom_launch_settings(void *usr, const char *var, const char *value) {
  t_rom_launch_settings *rs = (t_rom_launch_settings*)usr;
  unsigned valu = parseuint(value);
  if (!strcmp(var, "cheats"))
    rs->use_cheats = valu & 1;
  else if (!strcmp(var, "rtcts"))
    rs->rtcts = valu;
}

bool load_rom_settings(const char *fn, t_rom_load_settings *rld, t_rom_launch_settings *rlh) {
  char buf[512];
  strcpy(buf, ROMCONFIG_PATH);
  strcat(buf, file_basename(fn));
  replace_extension(buf, ".config");

  // Attempt to open and read the file.
  FIL fd;
  if (FR_OK != f_open(&fd, buf, FA_READ))
    return false;

  UINT rdbytes;
  if (FR_OK == f_read(&fd, buf, sizeof(buf) - 1, &rdbytes)) {
    buf[rdbytes] = 0;
    if (rld)
      parse_file(buf, parse_rom_load_settings, rld);
    if (rlh)
      parse_file(buf, parse_rom_launch_settings, rlh);
  }
  f_close(&fd);

  return true;
}

bool save_rom_settings(const char *fn, const t_rom_load_settings *rld, const t_rom_launch_settings *rlh) {
  // Create the directory (just in case it doesn't exist
  f_mkdir(SUPERFW_DIR);
  f_mkdir(ROMCONFIG_PATH);
  // Make it hidden
  f_chmod(SUPERFW_DIR, AM_HID, AM_HID);

  char buf[512];
  strcpy(buf, ROMCONFIG_PATH);
  strcat(buf, file_basename(fn));
  replace_extension(buf, ".config");

  // Proceed to create the file
  FIL fd;
  if (FR_OK != f_open(&fd, buf, FA_WRITE | FA_CREATE_ALWAYS))
    return false;

  // Serialize the ROM settings
  npf_snprintf(buf, sizeof(buf),
    "patchmode=%u\n"
    "igm=%u\n"
    "rtc=%u\n"
    "directsaving=%u\n"
    "cheats=%u\n"
    "rtcts=%u\n",
    rld->patch_policy,
    rld->use_igm ? 1 : 0,
    rld->use_rtc ? 1 : 0,
    rld->use_dsaving ? 1 : 0,
    rlh->use_cheats ? 1 : 0,
    (unsigned int)rlh->rtcts);

  UINT wrbytes;
  FRESULT res = f_write(&fd, buf, strlen(buf), &wrbytes);
  f_close(&fd);

  return FR_OK == res;
}


