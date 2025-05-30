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

#ifndef _COMMON_H__
#define _COMMON_H__

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "emu.h"

#define MAX(a, b) ((a > b) ? (a) : (b))
#define MIN(a, b) ((a < b) ? (a) : (b))

#define ROUND_UP(x, a)  ( (((x) + (a) - 1) / (a)) * (a) )
#define ROUND_UP2(x, a) ( (((x) + (a) - 1) & (~(a - 1))) )

#define MAX_FN_LEN                 256

#define SUPERFW_DIR               "/.superfw"
#define ROMCONFIG_PATH            "/.superfw/config/"
#define PATCHDB_PATH              "/.superfw/patches/"
#define CHEATS_PATH               "/.superfw/cheats/"
#define EMULATORS_PATH            "/.superfw/emulators/"
#define GBC_EMULATOR_PATH         "/.superfw/emulators/gbc-emu.gba"
#define SETTINGS_FILEPATH         "/.superfw/settings.txt"
#define RECENT_FILEPATH           "/.superfw/recent.txt"
#define UISETTINGS_FILEPATH       "/.superfw/ui-settings.txt"
#define FLASHBACKUPTMP_FILEPATH   "/.superfw/flash_backup.tmp"
#define FLASHBACKUP_FILEPTRN      "/.superfw/flash_backup-%02x%02x%02x%02x.bin"

#define PENDING_SAVE_FILEPATH     "/.superfw/pending-save.txt"
#define PENDING_SRAM_TEST         "/.superfw/pending-sram-test.txt"

extern uint8_t dldi_payload[];

// In-game menu requires ~1MB of free space. Lives in the last MB of ROM.
#define GBA_ROM_BASE              0x08000000
#define MAX_GBA_ROM_SIZE          (32*1024*1024)
#define MIN_IGM_ROMGAP_SIZE       (896*1024)           // This is a rough upperbound
#define MAX_ROM_SIZE_IGM          (32*1024*1024 - MIN_IGM_ROMGAP_SIZE)
#define DIRSAVE_REQ_SPACE         (7*1024)     // Limited to 7KiB

// Memory map for assets/objects in SDRAM
#define ROM_OFF_SCRATCH           0x00000000     // At 0x08000000
#define ROM_OFF_FONTS_BASE        0x00F00000     // At 0x08F00000
#define ROM_OFF_HISCRATCH         0x01000000     // At 0x09000000
#define ROM_OFF_USRPATCH_DB       0x01C00000     // At 0x09C00000
#define ROM_OFF_PATCH_DB          0x01D00000     // At 0x09D00000
#define ROM_OFF_ASSETS_BASE       0x01E00000     // At 0x09E00000

#define ROM_SCRATCH_U8          ((volatile uint8_t*)(0x08000000 + ROM_OFF_SCRATCH))
#define ROM_FONTBASE_U8         ((volatile uint8_t*)(0x08000000 + ROM_OFF_FONTS_BASE))
#define ROM_HISCRATCH_U8        ((volatile uint8_t*)(0x08000000 + ROM_OFF_HISCRATCH))
#define ROM_PATCHDB_U8          ((volatile uint8_t*)(0x08000000 + ROM_OFF_PATCH_DB))
#define ROM_ASSETS_U8           ((volatile uint8_t*)(0x08000000 + ROM_OFF_ASSETS_BASE))

#define SUPERFW_COMMENT_DOFFSET         (0xF0 - 0xC0)   // Offset within the ROM header!

typedef enum {
  FileTypeUnknown = 0,
  FileTypeGBA     = 1,
  FileTypeGB      = 2,
  FileTypeNES     = 3,
  FileTypePatchDB = 4,
} EnumFileType;

typedef struct {
  uint32_t entrypoint;
  uint8_t logo_data[48];
  uint8_t gtitle[16];
  uint8_t glic[2];
  uint8_t sbg_flag;
  uint8_t cart_type;
  uint8_t rom_size;
  uint8_t ram_size;
  uint8_t region;
  uint8_t pub;
  uint8_t version;
  uint8_t checksum;
  uint16_t global_checksum;

  uint8_t data[];
} t_gbheader;

typedef struct {
  uint32_t start_branch;
  uint32_t logo_data[39];
  uint8_t gtitle[12];
  uint8_t gcode[4];
  uint8_t gmkcode[2];
  uint8_t fixed;
  uint8_t unit_code;
  uint8_t devtype;
  uint8_t reserved[7];
  uint8_t version;
  uint8_t checksum;
  uint16_t reserved2;

  // Offset 0xC0 here, include the first 256 bytes.
  uint8_t data[0x40];
} t_rom_header;

_Static_assert (offsetof(t_rom_header, data) == 0xC0, "data offset in t_rom_header is wrong!");

typedef enum {
  PatchDatabase = 0,
  PatchEngine = 1,
  PatchNone = 2,
  PatchAuto = 3,
  PatchOptCNT = 3,
  PatchTotalCNT = 4
} t_patch_policy;

typedef enum {
  SaveLoadSav = 0,     // Automatic mode (load if found, otherwise clear)
  SaveLoadReset = 1,   // Start fresh (clear memory)
  SaveLoadDSCNT = 2,
  SaveLoadDisable = 2, // Do nothing at all
  SaveLoadCNT = 3
} t_sram_load_policy;

typedef enum {
  SaveReboot = 0,      // Automatic save on reboot (aka use SRAM)
  SaveDisable = 1,     // Do not save at all!
  SaveCNT = 2,
  SaveDirect = 2,      // Directly read/write from SD card
} t_sram_save_policy;

// ASM auxiliar routines
void launch_reset(bool via_bios, bool ewram_overclock);
void wait_ms(unsigned ms);
bool running_on_nds();
bool test_fast_ewram();
void nds_launch();
void gba_irq_handler();
void set_irq_enable(bool enable);
void rom_copy_write16(void *dst, const void *src, unsigned cnt);

// Decompress (WRAM version), returns written bytes
unsigned apunpack8(const uint8_t *src, uint8_t *dst);
// Decompress (VRAM version), returns written bytes
unsigned apunpack16(const uint8_t *src, uint8_t *dst);

// Some info/misc stuff
typedef struct {
  uint32_t patch_count;
  char version[9];
  char date[9];
  char creator[33];
} t_patchdb_info;
extern t_patchdb_info pdbinfo;
extern uint32_t flash_deviceid;
extern volatile unsigned frame_count;

// Patch information for direct save mode.
typedef struct {
  uint32_t save_size;                  // The file size must be at least this size or bad things can happen
  uint32_t sector_lba;                 // Sector number (we limit it to 32 bits)
} t_dirsave_info;

struct struct_t_rtc_state {
  uint8_t year, month, day, hour, mins;
};
typedef struct struct_t_rtc_state t_rtc_state;

// ROM config settings
typedef struct {
  t_rtc_state rtcval;
  unsigned patch_policy;     // Can only be PatchDatabase, PatchEngine or PatchNone
  bool use_dsaving;
  bool use_igm;
  bool use_cheats;
  bool use_rtc;
} t_rom_settings;

// Menu system
void menu_init(int);    // Initializes meny system (ie. loading resources)
void menu_render(unsigned fcnt);     // Renders the menu to the backframe
void menu_keypress(unsigned newkeys);   // Notifies key press
void menu_flip();       // Swaps front and back buffer to show the last rendered frame.

// Patching system
typedef enum {
  SaveTypeNone = 0,          // The game has no saving memory on the cart.
  SaveTypeSRAM = 1,          // The game uses 32KiB SRAM/FRAM to save progress.
  SaveTypeEEPROM4K = 2,      // The game ships a 512 byte EEPROM device.
  SaveTypeEEPROM64K = 3,     // The game ships a 8192 byte EEPROM device.
  SaveTypeFlash512K = 4,     // The game has a 512Kbit flash chip (64KiB).
  SaveTypeFlash1024K = 5,    // The game has a 1Mbit flash chip (128KiB).
} EnumSavetype;

static inline bool supports_directsave(EnumSavetype st) {
  // Only for types EEPROM/Flash [2..5]
  return 0x3C & (1 << st);
}

static inline unsigned savetype_size(EnumSavetype st) {
  const uint8_t lut[] = {
    0, 15, 9, 13, 16, 17
  };
  // Doesn't work for None, that's OK.
  return 1 << lut[st];
}

typedef void (*progress_fn)(unsigned done, unsigned total);
typedef bool (*progress_abort_fn)(unsigned done, unsigned total);

struct struct_t_patch;

#define ERR_SAVE_BADARG         0x1
#define ERR_SAVE_BADSAVE        0x2
#define ERR_SAVE_CANTWRITE      0x3
#define ERR_SAVE_CANTALLOC      0x4
#define ERR_SAVE_CANTCOPY       0x5

#define ERR_LOAD_BADROM         0x1
#define ERR_LOAD_MENU           0x2
#define ERR_NO_PAYLOAD_SPACE    0x3

#define ERR_LOAD_NOEMU          0x4

// Prepares the save game files, readin and writing files in some cases.
unsigned prepare_savegame(t_sram_load_policy loadp, t_sram_save_policy savep, EnumSavetype stype, t_dirsave_info *dsinfo, const char *savefn);
// Other variants, to use for GB/GBC and to reuse some code
unsigned prepare_sram_based_savegame(t_sram_load_policy loadp, t_sram_save_policy savep, const char *savefn);
// Loads ROM header
unsigned preload_gba_rom(const char *fn, uint32_t fs, t_rom_header *romh);
// Loads a ROM file and launches it.
unsigned load_gba_rom(const char *fn, uint32_t fs, const t_rom_header *rom_header, const struct struct_t_patch *ptch,
                      const t_dirsave_info *dsinfo, bool ingame_menu,
                      const t_rtc_state *rtc_clock, unsigned cheats, progress_fn progress);
void load_gbc_rom(const char *fn, uint32_t fs, progress_fn progress);
unsigned load_extemu_rom(const char *fn, uint32_t fs, const t_emu_loader *ldinfo, progress_fn progress);
bool validate_gba_header(const uint8_t *header);
bool validate_gb_header(const uint8_t *header);

// NDS loader functionality
#define ERR_FILE_ACCESS        0x1
#define ERR_NDS_TOO_BIG        0x2
#define ERR_NDS_BAD_ADDRS      0x3
#define ERR_NDS_BAD_ENTRYP     0x4
#define ERR_NDS_BADHEADER      0x5
unsigned load_nds(const char *filename, const void *dldi_driver);

// Asset management
const void *get_vfile_ptr(const char *fname);
int get_vfile_size(const char *fname);

// RTC patches
extern uint16_t patch_rtc_probe[];
extern uint16_t patch_rtc_probe_end[];
extern uint16_t patch_rtc_getstatus[];
extern uint16_t patch_rtc_getstatus_end[];
extern uint16_t patch_rtc_gettimedate[];
extern uint16_t patch_rtc_gettimedate_end[];
extern uint16_t patch_rtc_reset[];
extern uint16_t patch_rtc_reset_end[];

// EEPROM patches
extern uint16_t patch_eeprom_read_sram64k[];
extern uint16_t patch_eeprom_write_sram64k[];
extern const uint32_t patch_eeprom_read_sram64k_size;
extern const uint32_t patch_eeprom_write_sram64k_size;

extern uint16_t patch_eeprom_read_directsave[];
extern uint16_t patch_eeprom_write_directsave[];
extern const uint32_t patch_eeprom_read_directsave_size;
extern const uint32_t patch_eeprom_write_directsave_size;

// FLASH patches
extern uint16_t patch_flash_read_sram64k[];
extern uint16_t patch_flash_write_sector_sram64k[];
extern uint16_t patch_flash_write_byte_sram64k[];
extern uint16_t patch_flash_erase_sector_sram64k[];
extern uint16_t patch_flash_erase_device_sram64k[];
extern const uint32_t patch_flash_read_sram64k_size;
extern const uint32_t patch_flash_write_byte_sram64k_size;
extern const uint32_t patch_flash_erase_sector_sram64k_size;
extern const uint32_t patch_flash_erase_device_sram64k_size;
extern const uint32_t patch_flash_write_sector_sram64k_size;

extern uint16_t patch_flash_read_sram128k[];
extern uint16_t patch_flash_write_sector_sram128k[];
extern uint16_t patch_flash_write_byte_sram128k[];
extern uint16_t patch_flash_erase_sector_sram128k[];
extern uint16_t patch_flash_erase_device_sram128k[];
extern const uint32_t patch_flash_read_sram128k_size;
extern const uint32_t patch_flash_write_byte_sram128k_size;
extern const uint32_t patch_flash_erase_sector_sram128k_size;
extern const uint32_t patch_flash_erase_device_sram128k_size;
extern const uint32_t patch_flash_write_sector_sram128k_size;

extern uint16_t patch_flash_read_directsave[];
extern uint16_t patch_flash_write_sector_directsave[];
extern uint16_t patch_flash_write_byte_directsave[];
extern uint16_t patch_flash_erase_sector_directsave[];
extern uint16_t patch_flash_erase_device_directsave[];
extern const uint32_t patch_flash_read_directsave_size;
extern const uint32_t patch_flash_write_byte_directsave_size;
extern const uint32_t patch_flash_erase_sector_directsave_size;
extern const uint32_t patch_flash_erase_device_directsave_size;
extern const uint32_t patch_flash_write_sector_directsave_size;

// Firmware update and flashing tools
bool check_superfw(const uint8_t *h, uint32_t *ver);
bool validate_superfw_checksum(const uint8_t *fw, unsigned fwsize);

bool flash_program(const uint8_t *buf, unsigned size);
bool flash_verify(const uint8_t *buf, unsigned size);
bool flash_erase();
uint32_t flash_identify();

// Test/validation stuff
int sram_test();
int sdram_test(progress_abort_fn progcb);
void sram_pseudo_fill();
unsigned sram_pseudo_check();
int check_peding_sram_test();
void program_sram_check();
int sdbench_read(progress_abort_fn progcb);

#endif

