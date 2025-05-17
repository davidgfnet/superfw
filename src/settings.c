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

const uint8_t animspd_lut[] = {
  2,    //  8 pix/second
  3,    // 12 pix/second
  6,    // 24 pix/second
  8,    // 32 pix/second
  12,   // 48 pix/second
};

// Menu settings
uint32_t menu_theme = 0;
uint32_t lang_id = 0;
uint32_t recent_menu = 1;
uint32_t show_hidden_files = 1;
uint32_t anim_speed = animspd_cnt / 2;

// Default settings
t_patch_policy patcher_default = PatchAuto;

uint32_t boot_bios_splash = 0;   // Whether the BIOS boots to the splash screen
uint32_t use_slowsd = 0;         // Use slow mirrors for ROM loading.
uint32_t use_fastew = 0;         // Overclock EWRAM while playing.

uint32_t save_path_default = SaveSavegameDir;
uint32_t state_path_default = StateSavestateDir;

uint32_t backup_sram_default = 0;  // Number of older SRAM save to keep as backup

uint32_t hotkey_combo = 0;  // Hotkey Combo number
uint32_t enable_cheats = 0; // By default cheats are disabled (it's slightly faster)

uint32_t autoload_default = 1;
uint32_t autosave_default = 1;
uint32_t autosave_prefer_ds = 1;
uint32_t ingamemenu_default = 1;
uint32_t rtcpatch_default = 1;
t_rtc_state rtcvalue_default = { 20, 1, 26, 12, 0 };

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
    "menu_theme=%lu\n"
    "langcode=%c%c\n"
    "recent_menu=%lu\n"
    "show_hidden_files=%lu\n"
    "anim_speed=%lu\n",
    menu_theme, (lc & 0xFF), (lc >> 8), recent_menu, show_hidden_files, anim_speed);

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
    "hotkey_opt=%lu\n"
    "boot_to_bios=%lu\n"
    "save_path_policy=%lu\n"
    "state_path_policy=%lu\n"
    "sram_backup_count=%lu\n"
    "enable_cheats=%lu\n"
    "enable_slowsd=%lu\n"
    "enable_fastewram=%lu\n"
    "default_patcher=%u\n"
    "default_igmenu=%lu\n"
    "default_rtcpatch=%lu\n"
    "default_rtcval=%02u%02u%02u%02u%02u\n"
    "default_loadgame=%lu\n"
    "default_savegame=%lu\n"
    "prefer_directsave=%lu\n",
    hotkey_combo, boot_bios_splash, save_path_default, state_path_default,
    backup_sram_default, enable_cheats, use_slowsd, use_fastew,
    (unsigned int)patcher_default, ingamemenu_default, rtcpatch_default,
    rtcvalue_default.hour, rtcvalue_default.mins,
    rtcvalue_default.day + 1, rtcvalue_default.month + 1, rtcvalue_default.year,
    autoload_default, autosave_default, autosave_prefer_ds);

  UINT wrbytes;
  FRESULT res = f_write(&fd, buf, strlen(buf), &wrbytes);
  f_close(&fd);

  return FR_OK == res;
}

static void parse_settings(void *usr, const char *var, const char *value) {
  unsigned valu = parseuint(value);
  if (!strcmp(var, "hotkey_opt"))
    hotkey_combo = valu % hotkey_listcnt;
  else if (!strcmp(var, "save_path_policy"))
    save_path_default = valu % SaveDirCNT;
  else if (!strcmp(var, "state_path_policy"))
    state_path_default = valu % StateDirCNT;
  else if (!strcmp(var, "sram_backup_count"))
    backup_sram_default = valu;
  else if (!strcmp(var, "default_patcher"))
    patcher_default = valu % PatchTotalCNT;
  else if (!strcmp(var, "default_rtcval")) {
    rtcvalue_default.year = valu % 100U; valu /= 100U;
    rtcvalue_default.month = (((valu - 1) % 100U) % 12U); valu /= 100U;
    rtcvalue_default.day = (((valu - 1) % 100U) % 31U); valu /= 100U;
    rtcvalue_default.mins = ((valu % 100U) % 60U); valu /= 100U;
    rtcvalue_default.hour = valu % 24U;
  } else {
    const struct {
      const char *s;
      uint32_t *var;
    } bolset[] = {
      { "boot_to_bios",      &boot_bios_splash },
      { "enable_cheats",     &enable_cheats },
      { "default_igmenu",    &ingamemenu_default },
      { "enable_slowsd",     &use_slowsd },
      { "enable_fastewram",  &use_fastew },
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
  }
}

static void parse_ui_settings(void *usr, const char *var, const char *value) {
  unsigned valu = parseuint(value);
  if (!strcmp(var, "menu_theme"))
    menu_theme = valu;
  else if (!strcmp(var, "recent_menu"))
    recent_menu = valu;
  else if (!strcmp(var, "show_hidden_files"))
    show_hidden_files = valu;
  else if (!strcmp(var, "anim_speed"))
    anim_speed = valu;
  else if (!strcmp(var, "langcode")) {
    uint16_t code = ((uint8_t)value[0]) | (((uint8_t)value[1]) << 8);
    lang_id = lang_lookup(code);
  }
}

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
    }

    if (!e)
      break;
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

void sram_template_filename_calc(const char *rom, const char * extension, char *savefn) {
  if (save_path_default == SaveRomName) {
    strcpy(savefn, rom);   // Use the full ROM path
  }
  else {
    const char *p = file_basename(rom);
    const char *path = save_path_default == SaveSavesDir ? "/SAVES/" : "/SAVEGAME/";
    strcpy(savefn, path);   // Add the base path
    strcat(savefn, p);      // Append just the basename
  }

  replace_extension(savefn, extension);
}

void savestate_filename_calc(const char *rom, char *statefn) {
  if (state_path_default == StateRomName) {
    strcpy(statefn, rom);   // Use the full ROM path
  }
  else {
    const char *p = file_basename(rom);
    strcpy(statefn, "/SAVESTATE/");   // Add the base path
    strcat(statefn, p);               // Append just the basename
  }
  replace_extension(statefn, "");
}

void sram_filename_calc(const char *rom, char *savefn) {
  sram_template_filename_calc(rom, ".sav", savefn);
}

static void parse_rom_settings(void *usr, const char *var, const char *value) {
  t_rom_settings *rs = (t_rom_settings*)usr;
  unsigned valu = parseuint(value);
  if (!strcmp(var, "rtc"))
    rs->use_rtc = valu & 1;
  else if (!strcmp(var, "cheats"))
    rs->use_cheats = valu & 1;
  else if (!strcmp(var, "igm"))
    rs->use_igm = valu & 1;
  else if (!strcmp(var, "directsaving"))
    rs->use_dsaving = valu & 1;
  else if (!strcmp(var, "patchmode"))
    rs->patch_policy = valu % 3;
  else if (!strcmp(var, "rtcval")) {
    rs->rtcval.year = valu % 100U; valu /= 100U;
    rs->rtcval.month = (((valu - 1) % 100U) % 12U); valu /= 100U;
    rs->rtcval.day = (((valu - 1) % 100U) % 31U); valu /= 100U;
    rs->rtcval.mins = ((valu % 100U) % 60U); valu /= 100U;
    rs->rtcval.hour = valu % 24U;
  }
}

bool load_rom_settings(const char *fn, t_rom_settings *rs) {
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
    parse_file(buf, parse_rom_settings, rs);
  }
  f_close(&fd);

  return true;
}

bool save_rom_settings(const char *fn, const t_rom_settings *rs) {
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
    "rtc=%u\n"
    "cheats=%u\n"
    "igm=%u\n"
    "directsaving=%u\n"
    "patchmode=%u\n"
    "rtcval=%02u%02u%02u%02u%02u\n",
    rs->use_rtc ? 1 : 0, rs->use_cheats ? 1 : 0,
    rs->use_igm ? 1 : 0, rs->use_dsaving ? 1 : 0,
    rs->patch_policy,
    rs->rtcval.hour, rs->rtcval.mins,
    rs->rtcval.day + 1, rs->rtcval.month + 1, rs->rtcval.year);

  UINT wrbytes;
  FRESULT res = f_write(&fd, buf, strlen(buf), &wrbytes);
  f_close(&fd);

  return FR_OK == res;
}


