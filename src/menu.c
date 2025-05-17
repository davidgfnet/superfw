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

#include "gbahw.h"
#include "patchengine.h"
#include "fatfs/ff.h"
#include "common.h"
#include "settings.h"
#include "util.h"
#include "utf_util.h"
#include "fonts/font_render.h"
#include "nanoprintf.h"
#include "messages.h"
#include "save.h"
#include "cheats.h"
#include "ingame.h"
#include "emu.h"
#include "sha256.h"
#include "supercard_driver.h"

#include "res/icons.h"
#include "res/logo.h"

extern t_card_info sd_info;
extern bool fastew;
extern bool slowsd;

#ifdef SUPERCARD_LITE_IO
  #define FW_FLAVOUR           "Lite"
  #define FW_MAX_SIZE          (496*1024)
#else
  #define FW_FLAVOUR           "SD"
  #define FW_MAX_SIZE          (512*1024)
#endif

#define MENUTAB_RECENT          0    // Browses recently loaded ROMs (can be disabled / hidden)
#define MENUTAB_ROMBROWSE       1    // Browses ROMs and launches games.
#define MENUTAB_SETTINGS        2    // General settings / defaults
#define MENUTAB_UILANG          3    // UI / Language settings
#define MENUTAB_TOOLS           4    // Tools (advaned menu)
#define MENUTAB_INFO            5    // Info / About / Updater?
#define MENUTAB_MAX             6

#define ANIM_INITIAL_WAIT     128    // Intial wait (in anim cycles)

#define POPUP_NONE              0
#define POPUP_GBA_LOAD          1    // Load a GBA ROM
#define POPUP_SAVFILE           2    // Load/Store a SAV file
#define POPUP_FWFLASH           3    // Flash a new firmware image

#define BROWSER_MAXFN_CNT     (16*1024)
#define RECENT_MAXFN_CNT          (200)
#define BROWSER_ROWS                 8
#define RECENT_ROWS                  9

// First entries reserved for the logo palette.
#define FG_COLOR         16
#define BG_COLOR         17
#define FT_COLOR         18
#define HI_COLOR         19
#define INGMENU_PAL_FG  240
#define INGMENU_PAL_BG  241
#define INGMENU_PAL_HI  242
#define INGMENU_PAL_SH  243
#define SEL_COLOR       255

#define FLASH_UNLOCK_KEYS      (KEY_BUTTDOWN|KEY_BUTTB|KEY_BUTTSTA)
#define FLASH_GO_KEYS          (KEY_BUTTUP|KEY_BUTTL|KEY_BUTTR)


enum {
  ToolsSDRAMTest   = 0,
  ToolsSRAMTest    = 1,
  ToolsBatteryTest = 2,
  ToolsSDBench     = 3,
  ToolsFlashBak    = 4,
  ToolsMAX         = 4,
};

enum {
  SettTitle1   =  0,
  SettHotkey   =  1,
  SettBootType =  2,
  SettFastSD   =  3,
  SettFastEWRAM = 4,
  SettSaveLoc  =  5,
  SettSaveBkp  =  6,
  SettStateLoc =  7,
  SettCheatEn  =  8,
  SettTitle2   =  9,
  DefsPatchEng = 10,
  DefsGamMenu  = 11,
  DefsRTCEnb   = 12,
  DefsRTCVal   = 13,
  DefsLoadPol  = 14,
  DefsSavePol  = 15,
  DefsPrefDS   = 16,
  SettSave     = 17,
  SettMAX      = 17,
};

enum {
  DefsSave     = 4,
  DefsMAX      = 4,
};

enum {
  GbaLoadPopInfo  = 0,
  GbaLoadPopSave  = 1,
  GbaLoadPopPatch = 2,
  GbaLoadPopSett  = 3,
  GbaLoadCNT      = 4,
};

enum {
  GBAInfoCNT   = 1,
  GBALoadButt  = 0,

  GBASaveCNT   = 4,
  GBASaveMode  = 1,
  GBASaveLoadP = 2,
  GBASaveSaveP = 3,

  GBAPatchCNT  = 4,
  GBALoadPatch = 1,
  GBAInGameMen = 2,
  GBAPatchGen  = 3,

  GBASettCNT   = 4,
  GBASetRTCEn  = 1,
  GBASetLdCht  = 2,
  GBASetRememb = 3,
};

enum {
  SaveWrite  = 0,
  SavLoad    = 1,
  SavClear   = 2,
  SavQuit    = 3,
  SavMAX     = 3,
};

enum {
  FlashingReady    = 0,
  FlashingLoading  = 1,
  FlashingChecking = 2,
  FlashingErasing  = 3,
  FlashingWriting  = 4,
};

const struct {
  uint16_t fg_color;     // Foreground elements color
  uint16_t bg_color;     // Background color
  uint16_t ft_color;     // Font color
  uint16_t hi_color;     // Item/Buttom highlight
  uint16_t hi_blend;     // Menu highlight color (browser)
  uint16_t sh_color;     // Menu shadow/disabled color
} themes[] = {
  { RGB2GBA(0xeca551), RGB2GBA(0xe7c092), RGB2GBA(0x000000), RGB2GBA(0xbda27b), RGB2GBA(0x90816e), RGB2GBA(0x615d58) },
  { RGB2GBA(0x26879c), RGB2GBA(0x8fb1b8), RGB2GBA(0x000000), RGB2GBA(0x5296a5), RGB2GBA(0x1d7f95), RGB2GBA(0x6f8185) },
  { RGB2GBA(0xad11c8), RGB2GBA(0xe47af6), RGB2GBA(0x000000), RGB2GBA(0xad5dc6), RGB2GBA(0x724095), RGB2GBA(0x72667a) },
  { RGB2GBA(0x222222), RGB2GBA(0x444444), RGB2GBA(0xeeeeee), RGB2GBA(0x737573), RGB2GBA(0xaaaaaa), RGB2GBA(0x606060) },
};
#define THEME_COUNT (sizeof(themes) / sizeof(themes[0]))

typedef void (*t_mrender_fn)(volatile uint8_t *frame);

// Info and state for the menu tab
static struct {
  uint8_t menu_tab;

  unsigned anim_state;            // Animation (text rotation) status.

  // Recent ROMs state
  struct {
    int selector;                 // Pointed file offset
    int seloff;                   // Entry at the top of the list
    int maxentries;               // Total file/dir count in current dir
  } recent;

  // ROM browser state
  struct {
    char cpath[MAX_FN_LEN];       // Current path
    int selector;                 // Pointed file offset
    int seloff;                   // Entry at the top of the list
    int maxentries;               // Total file/dir count in current dir
  } browser;

  // UI settings
  struct {
    int selector;                 // Pointed option
  } uiset;

  // Main settings
  struct {
    int selector;                 // Pointed option
  } set;

  // Tools menu
  struct {
    int selector;                 // Render panel
  } tools;

  // Info/About menu
  struct {
    int selector;                 // Render panel
    char tstr[64];                // Temp message render
  } info;
} smenu;

// Same but for popups.
static struct {
  const char *alert_msg;          // Extra pop-up message

  uint8_t pop_num;

  // Pop up message (for whatever action). Allows returning to previous popup.
  struct {
    const char *message;
    const char *default_button;
    const char *confirm_button;
    void (*callback)(bool confirm);       // Function to call on "confirm".
    uint8_t option;                       // Selected button
    bool clear_popup_ok;                  // Whether any pop up must be cleared.
  } qpop;

  // RTC time set pop up, a bit special.
  struct {
    t_rtc_state val;
    int selector;
    void (*callback)();                   // Function to call on "save"
  } rtcpop;

  union {
    // GBA launch ROM pop up menu
    struct {
      int submenu;                        // Which submenu tab we are in
      int selector;                       // Option selector
      unsigned anim;
      char romfn[MAX_FN_LEN];             // File to launch
      uint32_t romfs;                     // File ROM size
      bool write_config;                  // Update the per-game config file.
      t_patch_policy patch_type;          // Patching type
      bool use_dsaving;                   // Whether we use direct-saving mode
      t_sram_load_policy sram_load_type;  // SRAM loading policy
      t_sram_save_policy sram_save_type;  // SRAM auto-saving policy
      bool ingame_menu_enabled;           // Enable the in-game menu.
      bool rtc_patch_enabled;             // Patch for RTC workarounds.
      t_rtc_state rtcval;                 // Initial RTC value.
      char gcode[5];                      // ASCII sanitized game code.
      t_rom_header romh;                  // ROM header (for info purposes)
      t_patch patches_datab;              // Loaded patches (from DB)
      t_patch patches_cache;              // Loaded patches (from patch engine's cache)
      bool patches_datab_found;           // Whether we had a patch match in the database
      bool patches_cache_found;           // Same but for the patch cache
      bool use_cheats;                    // Whether we want to load cheats to use them.
      bool cheats_found;                  // Whether there's a cheats file (not parsed tho!)
      unsigned cheats_size;               // Size of the cheat buffer
      char cheatsfn[MAX_FN_LEN];          // Cheats file path.
      char savefn[MAX_FN_LEN];            // Save file path.
      bool savefile_found;                // Whether there's a .sav file.
    } load;
    // Save file menu (.sav files)
    struct {
      int selector;                       // Selected button
      char savfn[MAX_FN_LEN];             // SAV file to load/store/mangle
    } savopt;
    // Update menu (for .fw files)
    struct {
      char fn[MAX_FN_LEN];                // FW file to load and flash
      bool issfw;                         // The firmware is a superFW image.
      uint32_t superfw_ver;               // Reported FW version.
      uint32_t fw_size;                   // Size in bytes reported by stat.
      unsigned curr_state;                // Flashing FSM state.
    } update;

    // Not really a pop up, but used as "popup" data for menu questions.
    struct {
      char fn[MAX_FN_LEN];
      unsigned fs;
    } pdb_ld;
  } p;
} spop;

typedef struct {
  uint32_t filesize;
  uint16_t isdir;
  uint16_t attr;
  char fname[MAX_FN_LEN];
  uint16_t sortname[MAX_FN_LEN];       // Pre-decoded and sort-friendly name.
} t_centry;
_Static_assert (sizeof(t_centry) % 4 == 0, "t_centry must be word-friendly");

typedef struct {
  uint32_t fname_offset;     // Basename offset in fpath (precalculated!)
  char fpath[MAX_FN_LEN];
} t_rentry;
_Static_assert (sizeof(t_rentry) % 4 == 0, "t_rentry must be word-friendly");

// Pointer to SDRAM, where we place some data:
//  - Scratch area 512KB (for FW updates)
//  - File list order (~64KiB)
//  - Browser file information (~13MB)
//  - Recently played ROMs table (~64KiB)
//  - Font data (placed by the bootloader at the 15..16MB range)
// At the end of the SDRAM, ro-data can be loaded by the loader.
typedef struct {
  uint8_t scratch[512*1024];
  t_centry *fileorder[BROWSER_MAXFN_CNT];
  t_centry fentries[BROWSER_MAXFN_CNT];
  t_rentry rentries[RECENT_MAXFN_CNT];
} t_sdram_state;

_Static_assert (sizeof(t_sdram_state) <= 15*1024*1024, "scratch SDRAM doesn't exceed 15MB");

typedef struct {
  unsigned char name;
  void (*render)(char* buf, int buflen);
  void (*on_change)(void);
  
  uint32_t *from_bool;

  uint32_t *from_int;
  unsigned from_int_offset;
  uint32_t from_int_max;
} UiMenuItem;

t_sdram_state *sdr_state = (t_sdram_state*)0x08000000;
uint8_t *hiscratch = (uint8_t*)ROM_HISCRATCH_U8;

typedef struct {
  uint16_t x, y;
  unsigned tn;
} t_oamobj;

static bool enable_flashing = false;
static unsigned framen = 0;
static unsigned objnum = 0;
static t_oamobj fobjs[64];

unsigned lang_lookup(uint16_t code) {
  for (unsigned i = 0; i < LANG_COUNT; i++)
    if (lang_codes[i] == code)
      return i;

  return 0;  // Fallback to default (english)
}

uint16_t lang_getcode() {
  return lang_codes[lang_id];
}

inline bool isascii(char code) {
  // Abuse signed :D
  return code >= 32;
}

bool is_superfw(const t_rom_header *h) {
  return !memcmp(&h->data[SUPERFW_COMMENT_DOFFSET], "SUPERFW~DAVIDGF", 16);
}

static int strcmp16(const uint16_t *a, const uint16_t *b) {
  while (*a && *a == *b) {
    a++;
    b++;
  }
  return *a - *b;
}

__attribute__((noinline))
int filesort(const void *a, const void *b) {
  const t_centry *ca = *(t_centry**)a;
  const t_centry *cb = *(t_centry**)b;

  // Directories some up first.
  if (ca->isdir != cb->isdir)
    return cb->isdir - ca->isdir;

  // Other files are string-ordered
  return strcmp16(ca->sortname, cb->sortname);
}

static void human_size(char *s, unsigned ml, uint32_t sz) {
  if (sz < 1024)
    memcpy(s, "1K", 3);
  else if (sz < 1024*1024)
    npf_snprintf(s, ml, "%luK", sz >> 10);
  else
    npf_snprintf(s, ml, "%luM", sz >> 20);
}

static void human_size_kb(char *s, unsigned ml, uint32_t sz) {
  if (sz < 1024)
    memcpy(s, "<1MiB", 3);
  else if (sz < 1024*1024)
    npf_snprintf(s, ml, "%lu.%luMiB", sz >> 10, (sz / 100) % 10);
  else
    npf_snprintf(s, ml, "%lu.%luGiB", sz >> 20, ((sz >> 10) / 100) % 10);
}

static void loadrom_progress(unsigned done, unsigned total) {
  // Draws and flips the buffer, do not care about vsync here
  volatile uint8_t *frame = &MEM_VRAM_U8[0xA000*framen];

  // Render the full background to a solid color
  dma_memset16(&frame[0], dup8(BG_COLOR), SCREEN_WIDTH*SCREEN_HEIGHT/2);

  // Render a simple progress bar
  unsigned prog = done * 200 / total;
  for (unsigned i = 76; i < 84; i++)
    dma_memset16(&frame[SCREEN_WIDTH * i + 20], dup8(FG_COLOR), prog/2);

  dma_memset16(MEM_OAM, 0, 256);  // Clear icons

  REG_DISPCNT = (REG_DISPCNT & ~0x10) | (framen << 4);
  framen ^= 1;
}

static bool loadrom_progress_abort(unsigned done, unsigned total) {
  loadrom_progress(done, total);

  // Capture A/B buttons to abort the progress
  return ((~REG_KEYINPUT) & KEY_BUTTSTA);
}


bool generate_patches_progress(const char *fn, unsigned fs) {
  // Open ROM and load it in the SDRAM. We load it in 4MB chunks. Not ideal but
  // we want to preserve the data loaded in the SDRAM (ie. fonts).
  FIL fd;
  FRESULT res = f_open(&fd, fn, FA_READ);
  if (res != FR_OK)
    return false;

  t_patch_builder pb;
  patchengine_init(&pb, fs);
  const unsigned max_hiscratch = 8*1024*1024;

  for (unsigned i = 0; i < fs; i += max_hiscratch) {
    for (unsigned j = 0; j < max_hiscratch && i + j < fs; j += 4096) {
      UINT rdbytes;
      uint32_t tmp[4096/4];
      if (FR_OK != f_read(&fd, tmp, sizeof(tmp), &rdbytes))
        return false;

      set_supercard_mode(MAPPED_SDRAM, true, false);
      dma_memcpy32(&hiscratch[j], tmp, sizeof(tmp)/4);
      set_supercard_mode(MAPPED_SDRAM, true, true);
      if (j & ~0xFFFF)
        loadrom_progress((i*2 + j) >> 8, fs >> 7);
    }
    // Amount to process.
    unsigned blksize = MIN(max_hiscratch, fs - i);

    void upd_pe_prog(unsigned prog) {
      unsigned p = i*2 + blksize + prog*4;
      loadrom_progress(p >> 8, fs >> 7);
    }

    // Process patches. Adds them to the existing patchset.
    set_supercard_mode(MAPPED_SDRAM, true, false);
    patchengine_process_rom((uint32_t*)hiscratch, blksize, &pb, upd_pe_prog);
    set_supercard_mode(MAPPED_SDRAM, true, true);
  }

  f_close(&fd);
  patchengine_finalize(&pb);

  // Proceed to write patches to their cache.
  return write_patches_cache(fn, &pb.p);
}

bool dump_flashmem_backup() {
  f_mkdir(SUPERFW_DIR);

  // Use a different file name to ensure we do not overwrite firmwares by
  // accident. This adds some minimal overhead.
  SHA256_State st;
  sha256_init(&st);

  FIL fd;
  FRESULT res = f_open(&fd, FLASHBACKUPTMP_FILEPATH, FA_WRITE | FA_CREATE_ALWAYS);
  if (res != FR_OK)
    return false;

  for (unsigned i = 0; i < 512*1024; i += 4*1024) {
    const uint8_t *faddr = (uint8_t*)(0x08000000 + i);

    uint32_t tmp[4096/4];
    set_supercard_mode(MAPPED_FIRMWARE, true, false);
    dma_memcpy32(tmp, faddr, 1024);
    set_supercard_mode(MAPPED_SDRAM, true, true);

    sha256_transform(&st, tmp, sizeof(tmp));

    UINT wrbytes;
    if (FR_OK != f_write(&fd, tmp, sizeof(tmp), &wrbytes) || wrbytes != sizeof(tmp)) {
      f_close(&fd);
      return false;
    }

    loadrom_progress(i >> 10, 512);
  }

  f_close(&fd);

  // Calculate the final hash, use a hash prefix as the filename.
  uint8_t h256[32];
  sha256_finalize(&st, h256);

  char finalfn[64];
  npf_snprintf(finalfn, sizeof(finalfn), FLASHBACKUP_FILEPTRN,
               h256[0], h256[1], h256[2], h256[3]);
  f_rename(FLASHBACKUPTMP_FILEPATH, finalfn);

  return true;
}

void patch_gen_callback(bool confirm);

void sram_battery_test_callback(bool confirm) {
  if (confirm) {
    // Fill SRAM with some pseudorandom data to test later.
    sram_pseudo_fill();
    // Program a check on the next reboot!
    program_sram_check();

    spop.alert_msg = msgs[lang_id][MSG_SRAMTST_RDY];
  }
}

bool ingame_menu_avail() {
  const t_patch *p = spop.p.load.patch_type == PatchDatabase && spop.p.load.patches_datab_found ? &spop.p.load.patches_datab :
                     spop.p.load.patch_type == PatchEngine   && spop.p.load.patches_cache_found ? &spop.p.load.patches_cache : NULL;
  // Necessary size to load the IGM (+fonts +cheats)
  const unsigned igm_reqsz = ROUND_UP2(ingame_menu_payload.menu_rsize + font_block_size() + spop.p.load.cheats_size, 1024);

  // If the ROM is too big, must use some hole to load the menu.
  if (spop.p.load.romfs > MAX_GBA_ROM_SIZE - igm_reqsz) {
    // Discard holes that are too small, or not well formed.
    if (!p || p->hole_size < igm_reqsz || p->hole_addr + p->hole_size > spop.p.load.romfs)
      return false;   // Too big to fit the menu!
  }

  // Check if the patches exist and have proper IRQ support.
  return p && p->irqh_ops > 0;
}

bool dirsav_avail() {
  const t_patch *p = spop.p.load.patch_type == PatchDatabase && spop.p.load.patches_datab_found ? &spop.p.load.patches_datab :
                     spop.p.load.patch_type == PatchEngine   && spop.p.load.patches_cache_found ? &spop.p.load.patches_cache : NULL;

  // Check if there's enough space for it!
  if (spop.p.load.romfs > MAX_GBA_ROM_SIZE - DIRSAVE_REQ_SPACE) {
    if (!p || p->hole_size < DIRSAVE_REQ_SPACE || p->hole_addr + p->hole_size > spop.p.load.romfs)
      return false;   // Too big to fit!
  }

  return (p && supports_directsave(p->save_mode));
}

static void browser_open_gba(const char *fn, uint32_t fs, bool prompt_patchgen) {
  if (fs > MAX_GBA_ROM_SIZE) {
    // The ROM is too big to be loaded!
    spop.alert_msg = msgs[lang_id][MSG_ERR_TOOBIG];
  } else if (preload_gba_rom(fn, fs, &spop.p.load.romh)) {
    spop.alert_msg = msgs[lang_id][MSG_ERR_READ];
  } else {
    // Fill in the requested ROM info (some hacky tricks used!)
    if (fn != spop.p.load.romfn)
      strcpy(spop.p.load.romfn, fn);
    spop.p.load.romfs = fs;

    // Sanitize the game code for display
    for (unsigned i = 0; i < 4; i++)
      spop.p.load.gcode[i] = isascii(spop.p.load.romh.gcode[i]) ? spop.p.load.romh.gcode[i] : 0x1A;
    spop.p.load.gcode[4] = 0;

    // Look up patches, have them handy.
    const t_rom_header *rmh = &spop.p.load.romh;
    uint8_t gamecode[5] = {
      rmh->gcode[0], rmh->gcode[1],
      rmh->gcode[2], rmh->gcode[3],
      rmh->version
    };
    set_supercard_mode(MAPPED_SDRAM, true, false);
    spop.p.load.patches_datab_found = patchmem_lookup(
            gamecode, (uint8_t*)ROM_PATCHDB_U8, &spop.p.load.patches_datab);
    set_supercard_mode(MAPPED_SDRAM, true, true);

    bool issfw = is_superfw(&spop.p.load.romh);

    // Attempt to load any existing patch and check also the PE cache dir.
    spop.p.load.patches_cache_found = load_rom_patches(fn, &spop.p.load.patches_cache);
    if (!spop.p.load.patches_cache_found)
      spop.p.load.patches_cache_found = load_cached_patches(fn, &spop.p.load.patches_cache);

    // Default to global settings (in case the file is not found).
    t_rom_settings savedcfg = {
      .rtcval = rtcvalue_default,
      .patch_policy = patcher_default,
      .use_dsaving = autosave_prefer_ds,
      .use_igm = ingamemenu_default,
      .use_cheats = true,              // Defaults to true (just preferred, might be disabled/N/A)
      .use_rtc = rtcpatch_default
    };
    // Check for any game-specific config file, so we don't have to guess the config.
    // The config file can be partial, hence the defaults.
    spop.p.load.write_config = load_rom_settings(fn, &savedcfg);

    // Attempt to find a cheat file if cheats are enabled.
    spop.p.load.cheats_size = 0;
    spop.p.load.cheats_found = false;
    if (enable_cheats) {
      strcpy(spop.p.load.cheatsfn, fn);
      replace_extension(spop.p.load.cheatsfn, ".cht");
      spop.p.load.cheats_found = check_file_exists(spop.p.load.cheatsfn);
      if (!spop.p.load.cheats_found) {
        // Create a path using the game ID and version.
        npf_snprintf(spop.p.load.cheatsfn, sizeof(spop.p.load.cheatsfn), CHEATS_PATH "%c%c%c%c-%02x.cht",
                     rmh->gcode[0], rmh->gcode[1], rmh->gcode[2], rmh->gcode[3], rmh->version);
        spop.p.load.cheats_found = check_file_exists(spop.p.load.cheatsfn);

        // Load the cheats into memory if enabled.
        if (spop.p.load.cheats_found) {
          // Load the cheats to the ROM area, just after the font pack. This is for easier relocation.
          uint8_t *cheat_area = (uint8_t*)(ROM_FONTBASE_U8 + font_block_size());
          unsigned max_area = 1024*1024 - font_block_size();    // 1MB is reserved at the ROM end.
          int cheatsz = open_read_cheats(cheat_area, max_area, spop.p.load.cheatsfn);
          if (cheatsz < 0)
            spop.p.load.cheats_found = false;
          else
            spop.p.load.cheats_size = cheatsz;
        }
      }
    }
    spop.p.load.use_cheats = enable_cheats && spop.p.load.cheats_found && savedcfg.use_cheats;

    // If patch engine is selected but no patches found, prompt for generation.
    // If auto is selected and no patches nor DB entries found, do prompt too.
    bool no_patches = (savedcfg.patch_policy == PatchAuto && !spop.p.load.patches_datab_found && !spop.p.load.patches_cache_found);
    bool no_engine  = (savedcfg.patch_policy == PatchEngine && !spop.p.load.patches_cache_found);

    if (prompt_patchgen && !issfw && (no_patches || no_engine)) {
      // No patches found, ask the user if they want to generate patches
      // using the patch engine.
      spop.qpop.message = msgs[lang_id][no_patches ? MSG_Q1_NOPATCH : MSG_Q1_PATCHENG];
      spop.qpop.default_button = msgs[lang_id][MSG_Q_NO];
      spop.qpop.confirm_button = msgs[lang_id][MSG_Q_YES];
      spop.qpop.option = 0;
      spop.qpop.callback = patch_gen_callback;
      spop.qpop.clear_popup_ok = true;
      return;
    }

    // Calculate the .sav file name, and check its existance.
    sram_template_filename_calc(fn, ".sav", spop.p.load.savefn);
    spop.p.load.savefile_found = check_file_exists(spop.p.load.savefn);

    // If PatchAuto is selected, resolve it. Downgrade if not found.
    if (savedcfg.patch_policy == PatchAuto) {
      if (spop.p.load.patches_cache_found)
        spop.p.load.patch_type = PatchEngine;      // Try existing patches
      else if (spop.p.load.patches_datab_found)
        spop.p.load.patch_type = PatchDatabase;    // Try the database then
      else
        spop.p.load.patch_type = PatchNone;
    }
    // Downgrade to no patches if the specified was not found.
    else if (savedcfg.patch_policy == PatchDatabase) {
      if (!spop.p.load.patches_datab_found)
        spop.p.load.patch_type = PatchNone;
    }
    else if (savedcfg.patch_policy == PatchEngine) {
      if (!spop.p.load.patches_cache_found)
        spop.p.load.patch_type = PatchNone;
    }
    else
      spop.p.load.patch_type = savedcfg.patch_policy;

    const t_patch *p = spop.p.load.patch_type == PatchDatabase ? &spop.p.load.patches_datab :
                       spop.p.load.patch_type == PatchEngine   ? &spop.p.load.patches_cache :
                                                                 NULL;
    bool ds_default = savedcfg.use_dsaving && dirsav_avail();

    // What if the game doesn't have a save method?
    bool game_no_save = (p && p->save_mode == SaveTypeNone) || issfw;

    // Show load ROM menu.
    spop.pop_num = POPUP_GBA_LOAD;
    spop.p.load.submenu = GbaLoadPopInfo;
    spop.p.load.anim = 0;
    spop.p.load.selector = GBALoadButt;
    spop.p.load.use_dsaving = ds_default;

    // Use default settings (and file existance) to fill in default choice.
    // DirectSaving enabled overrides the other settings.
    if (ds_default) {
      spop.p.load.sram_load_type = spop.p.load.savefile_found ? SaveLoadSav : SaveLoadReset;
      spop.p.load.sram_save_type = SaveDirect;
    }
    else {
      spop.p.load.sram_load_type = game_no_save               ? SaveLoadDisable :
                                   !autoload_default          ? SaveLoadDisable :
                                   spop.p.load.savefile_found ? SaveLoadSav :
                                                                SaveLoadReset;
      spop.p.load.sram_save_type = autosave_default && !game_no_save ? SaveReboot : SaveDisable;
    }

    spop.p.load.ingame_menu_enabled = ingame_menu_avail() && savedcfg.use_igm;
    // Use default, if availabe.
    spop.p.load.rtc_patch_enabled = savedcfg.use_rtc && spop.p.load.patches_datab.rtc_ops;
    strcpy(spop.p.load.romfn, fn);
    // This is only used if the RTC patches are available and enabled.
    spop.p.load.rtcval = savedcfg.rtcval;
  }
}

void patch_gen_callback(bool confirm) {
  // Generate patches if confirm was selected
  if (confirm) {
    generate_patches_progress(spop.p.load.romfn, spop.p.load.romfs);
    spop.alert_msg = msgs[lang_id][MSG_PATCHGEN_OK];
  }

  // Either way, show the popup screen afterwards without prompt
  browser_open_gba(spop.p.load.romfn, spop.p.load.romfs, false);
}

const t_emu_loader * get_emu_info(const char *ext) {
  for (unsigned i = 0; emu_platforms[i].extension; i++)
    if (!strcasecmp(ext, emu_platforms[i].extension))
      return emu_platforms[i].loaders;

  return NULL;
}

static void load_patchdb_action(bool confirm) {
  if (confirm) {
    FIL fd;
    FRESULT res = f_open(&fd, spop.p.pdb_ld.fn, FA_READ);
    if (res != FR_OK) {
      spop.alert_msg = msgs[lang_id][MSG_ERR_GENERIC];
      return;
    } else {
      for (unsigned off = 0; off < spop.p.pdb_ld.fs; off += 1024) {
        UINT rdbytes;
        uint32_t tmp[1024/4];
        if (FR_OK != f_read(&fd, tmp, sizeof(tmp), &rdbytes)) {
          spop.alert_msg = msgs[lang_id][MSG_ERR_GENERIC];
          return;
        }

        set_supercard_mode(MAPPED_SDRAM, true, false);
        dma_memcpy32(ROM_PATCHDB_U8 + off, tmp, sizeof(tmp)/4);
        set_supercard_mode(MAPPED_SDRAM, true, true);
      }
    }
    spop.alert_msg = msgs[lang_id][MSG_OK_GENERIC];
  }
}

unsigned guess_file_type(const uint8_t *header) {
  const t_rom_header *gbah = (t_rom_header*)header;
  uint32_t sig = *(uint32_t*)header;

  if (gbah->fixed == 0x96 && gbah->unit_code == 0x00 && gbah->devtype == 0x00 &&
      header[3] == 0xEA /* Starts with an unconditional branch */ &&
      validate_gba_header(header))
    return FileTypeGBA;
  else if (validate_gb_header(&header[0x100]))
    return FileTypeGB;
  else if (sig == 0x1A53454E)
    return FileTypeNES;
  else if (sig == 0x31424450)
    return FileTypePatchDB;

  return FileTypeUnknown;
}

void start_emu_game(const t_emu_loader *ldinfo, const char *fn, uint32_t fs) {
  // Load: Sav/Reset Save: Reboot/Disable
  sram_template_filename_calc(fn, ".sav", spop.p.load.savefn);
  t_sram_load_policy lp = check_file_exists(spop.p.load.savefn) ? SaveLoadSav : SaveLoadReset;
  unsigned errsave = prepare_sram_based_savegame(lp, SaveReboot, spop.p.load.savefn);
  if (errsave) {
    unsigned errmsg = (errsave == ERR_SAVE_BADSAVE)   ? MSG_ERR_SAVERD :
                                                        MSG_ERR_SAVEWR;
    spop.alert_msg = msgs[lang_id][errmsg];
  }
  else {
    // Try to load the emu and ROM, keep trying if there's more than one emulatior option.
    unsigned errcode = ERR_LOAD_NOEMU;
    while (ldinfo->emu_name) {
      unsigned errcode = load_extemu_rom(fn, fs, ldinfo, loadrom_progress);
      if (errcode && errcode != ERR_LOAD_NOEMU)
        break;
      ldinfo++;
    }
    unsigned errmsg = (errcode == ERR_LOAD_NOEMU) ? MSG_ERR_NOEMU :
                                                    MSG_ERR_READ;
    spop.alert_msg = msgs[lang_id][errmsg];
  }
}

static void gbc_launch(const char *fn, uint32_t fs) {
  // GB or GBA roms. See if we have a custom emulator.
  if (check_file_exists(GBC_EMULATOR_PATH)) {
    const t_emu_loader *ldinfo = get_emu_info("gbc");
    start_emu_game(ldinfo, fn, fs);
  }
  else {
    // Load: Sav/Reset Save: Reboot/Disable
    sram_template_filename_calc(fn, ".sav", spop.p.load.savefn);
    t_sram_load_policy lp = check_file_exists(spop.p.load.savefn) ? SaveLoadSav : SaveLoadReset;
    unsigned errsave = prepare_sram_based_savegame(lp, SaveReboot, spop.p.load.savefn);
    if (errsave) {
      unsigned errmsg = (errsave == ERR_SAVE_BADSAVE)   ? MSG_ERR_SAVERD :
                                                          MSG_ERR_SAVEWR;
      spop.alert_msg = msgs[lang_id][errmsg];
    }
    else {
      load_gbc_rom(fn, fs, loadrom_progress);
      spop.alert_msg = msgs[lang_id][MSG_ERR_NOEMU];
    }
  }
}

__attribute__((noinline))
static void browser_open(const char *fn, uint32_t fs) {
  unsigned l = strlen(fn);
  if (!strcasecmp(&fn[l-4], ".gba"))
    // GBA ROMs (most likely)
    browser_open_gba(fn, fs, true);
  else if (!strcasecmp(&fn[l-4], ".gbc") || !strcasecmp(&fn[l-3], ".gb"))
    gbc_launch(fn, fs);
  else if (!strcasecmp(&fn[l-4], ".sav")) {
    spop.pop_num = POPUP_SAVFILE;
    spop.p.savopt.selector = SavMAX;
    strcpy(spop.p.savopt.savfn, fn);
  }
  else if (!strcasecmp(&fn[l-3], ".fw")) {
    // A SuperFW firmware update is selected!
    if (!enable_flashing)
      spop.alert_msg = msgs[lang_id][MSG_FWUP_DISABLED];
    else if (fs > FW_MAX_SIZE)
      spop.alert_msg = msgs[lang_id][MSG_FWUP_ERRSZ];
    else {
      // Read the header and perform some more basic checks!
      FIL fd;
      FRESULT res = f_open(&fd, fn, FA_READ);
      if (res != FR_OK)
        spop.alert_msg = msgs[lang_id][MSG_FWUP_ERRRD];
      else {
        UINT rdbytes;
        uint8_t tmp[512];
        if (FR_OK != f_read(&fd, tmp, sizeof(tmp), &rdbytes) || rdbytes != sizeof(tmp))
          spop.alert_msg = msgs[lang_id][MSG_FWUP_ERRRD];
        else if (!validate_gba_header(tmp))  // Is it a valid GBA ROM header?
          spop.alert_msg = msgs[lang_id][MSG_FWUP_BADHD];
        else {
          spop.p.update.issfw = check_superfw(tmp, &spop.p.update.superfw_ver);
          spop.p.update.fw_size = fs;
          spop.p.update.curr_state = FlashingReady;
          spop.pop_num = POPUP_FWFLASH;
          strcpy(spop.p.update.fn, fn);
          f_close(&fd);
        }
      }
    }
  }
  else {
    // Any emulator-based console supported
    const char *ext = find_extension(fn);
    if (ext) {
      const t_emu_loader *ldinfo = get_emu_info(&ext[1]);
      if (ldinfo) {
        start_emu_game(ldinfo, fn, fs);
        return;
      }
    }

    // Attempt to load the file magic and detect what kind of file this is.
    if (fs >= 512) {
      FIL fi;
      if (FR_OK == f_open(&fi, fn, FA_READ)) {
        uint32_t tmphdr[512 / 4];
        UINT rdbytes;
        if (FR_OK == f_read(&fi, tmphdr, sizeof(tmphdr), &rdbytes) && rdbytes == sizeof(tmphdr)) {
          unsigned guesstype = guess_file_type((uint8_t*)tmphdr);
          switch (guesstype) {
          case FileTypeGBA:
            browser_open_gba(fn, fs, true); break;
          case FileTypeGB:
            gbc_launch(fn, fs); break;
          case FileTypePatchDB:
            strcpy(spop.p.pdb_ld.fn, fn);
            spop.p.pdb_ld.fs = fs;
            spop.qpop.message = msgs[lang_id][MSG_Q3_LOADPDB];
            spop.qpop.default_button = msgs[lang_id][MSG_Q_NO];
            spop.qpop.confirm_button = msgs[lang_id][MSG_Q_YES];
            spop.qpop.option = 0;
            spop.qpop.callback = load_patchdb_action;
            spop.qpop.clear_popup_ok = false;
            break;
          default:
            spop.alert_msg = msgs[lang_id][MSG_ERR_UNKTYP];
            break;
          };
        }
        f_close(&fi);
      }
    }
  }
}

static void insert_recent_fn(const char *fn) {
  for (unsigned i = 0; i < smenu.recent.maxentries; i++) {
    if (!strcmp(sdr_state->rentries[i].fpath, fn)) {
      // Found a matching file, move it to position 0, unless it's there already.
      if (i) {
        t_rentry tmp;
        dma_memcpy16(&tmp, &sdr_state->rentries[i], sizeof(tmp) / 2);   // Copy entry to tmp
        memmove32(&sdr_state->rentries[1], &sdr_state->rentries[0], i * sizeof(sdr_state->rentries[0]));
        dma_memcpy16(&sdr_state->rentries[0], &tmp, sizeof(tmp) / 2);
      }
      return;
    }
  }

  // Not in the list, push all items back and insert it in the first position
  if (smenu.recent.maxentries) {
    unsigned movecnt = MIN(smenu.recent.maxentries, RECENT_MAXFN_CNT - 1);
    memmove32(&sdr_state->rentries[1], &sdr_state->rentries[0], movecnt * sizeof(sdr_state->rentries[0]));
  }

  const char *pbn = file_basename(fn);
  sdr_state->rentries[0].fname_offset = pbn - fn;
  dma_memcpy16(sdr_state->rentries[0].fpath, fn, (strlen(fn) + 1 + 1) / 2);
  smenu.recent.maxentries++;
}

static bool recent_flush() {
  // Flush to disk!
  FIL fo;
  if (FR_OK != f_open(&fo, RECENT_FILEPATH, FA_WRITE | FA_CREATE_ALWAYS))
    return false;

  // Write stuff to disk. Use a 1KiB buffer and flush as full blocks fill.
  unsigned coff = 0;
  char tmpbuf[1024];
  tmpbuf[0] = 0;

  for (unsigned i = 0; i < smenu.recent.maxentries; i++) {
    unsigned fnlen = strlen(sdr_state->rentries[i].fpath);
    memcpy(&tmpbuf[coff], sdr_state->rentries[i].fpath, fnlen);
    coff += fnlen;
    tmpbuf[coff++] = '\n';

    if (coff >= 512) {
      UINT wrbytes;
      if (FR_OK != f_write(&fo, tmpbuf, 512, &wrbytes) || wrbytes != 512) {
        f_close(&fo);
        return false;
      }
      // Consume the first 512 written bytes
      memmove(&tmpbuf[0], &tmpbuf[512], coff - 512);
      coff -= 512;
    }
  }

  // Flush the last bytes (if any!)
  if (coff) {
    UINT wrbytes;
    if (FR_OK != f_write(&fo, tmpbuf, coff, &wrbytes) || wrbytes != coff) {
      f_close(&fo);
      return false;
    }
  }

  f_close(&fo);
  return true;
}

static bool insert_recent_flush(const char *fn) {
  // Insert element.
  insert_recent_fn(fn);
  return recent_flush();
}

static bool delete_recent_flush(unsigned entry_num) {
  if (entry_num + 1 < smenu.recent.maxentries)
    memmove32(&sdr_state->rentries[entry_num], &sdr_state->rentries[entry_num + 1],
              (smenu.recent.maxentries - (entry_num + 1)) * sizeof(sdr_state->rentries[0]));

  smenu.recent.maxentries--;
  smenu.recent.selector = MIN(smenu.recent.maxentries - 1, smenu.recent.selector);

  if (!smenu.recent.maxentries)
    smenu.menu_tab = MENUTAB_ROMBROWSE;

  return recent_flush();
}

static void recent_reload() {
  smenu.recent.selector = 0;
  smenu.recent.maxentries = 0;
  smenu.recent.seloff = 0;
  smenu.anim_state = 0;

  FIL fi;
  if (FR_OK != f_open(&fi, RECENT_FILEPATH, FA_READ))
    return;

  // Read data block by block.
  char tmp[1024 + 4];
  unsigned bcount = 0;
  while (1) {
    if (bcount <= 512) {
      UINT rdbytes;
      if (FR_OK != f_read(&fi, &tmp[bcount], 512, &rdbytes))
        return;
      bcount += rdbytes;
      tmp[bcount] = 0;
    }

    if (!bcount)
      break;

    // Attempt to parse the next path.
    char *p = strchr(tmp, '\n');
    if (!p)
      p = strchr(tmp, '\0');
    if (!p)
      break;       // Some path is way too long!

    *p = 0;        // Add the string end char.

    unsigned cnt = strlen(tmp) + 1;
    if (cnt > 1) {
      const char *pbn = file_basename(tmp);
      sdr_state->rentries[smenu.recent.maxentries].fname_offset = pbn - tmp;
      dma_memcpy16(sdr_state->rentries[smenu.recent.maxentries].fpath, tmp, (cnt + 1) / 2);
      smenu.recent.maxentries++;
    }

    // Consume the bytes
    memmove(&tmp[0], &tmp[cnt], bcount - cnt);
    bcount -= cnt;
  }

  f_close(&fi);
}

// Loads a new directory list in the ROM browser.
// Sets selector pointer at offset zero
// TODO: Implement filtering (.gba/.rom/.bin... etc) using settings
static void browser_reload() {
  smenu.browser.selector = 0;
  smenu.anim_state = 0;

  unsigned fcount = 0;
  DIR d;
  if (FR_OK != f_opendir(&d, smenu.browser.cpath))
    return;   // FIXME: Implement error reporting!

  while (1) {
    FILINFO info;
    if (f_readdir(&d, &info) != FR_OK || !info.fname[0])
      break;

    if (!show_hidden_files) {
      if (info.fname[0] == '.') // Skip dot files
        continue;
      if (info.fattrib & AM_HID) // Skip hidden files
        continue;
    }

    if (fcount >= BROWSER_MAXFN_CNT)
      break;

    t_centry *e = &sdr_state->fentries[fcount++];
    e->filesize = (uint32_t) info.fsize;  // TODO: Support 4GB+ files?
    e->isdir = (info.fattrib & AM_DIR) ? 1 : 0;
    e->attr = info.fattrib;
    dma_memcpy16(e->fname, info.fname, MAX_FN_LEN/2);
    sortable_utf8_u16(info.fname, e->sortname);
  }

  // Instead of sorting the actual list of files, which requires moving lots
  // of memory, we use a list of pointers.
  for (unsigned i = 0; i < fcount; i++)
    sdr_state->fileorder[i] = &sdr_state->fentries[i];

  heapsort4(sdr_state->fileorder, fcount, sizeof(t_centry*) / sizeof(uint32_t), filesort);

  smenu.browser.maxentries = fcount;
}

static inline void render_icon(unsigned x, unsigned y, unsigned iconn) {
  fobjs[objnum++] = (t_oamobj){x, y, 8*iconn };
}

static inline void render_icon_trans(unsigned x, unsigned y, unsigned iconn) {
  fobjs[objnum++] = (t_oamobj){x, y | 0x0400, 8*iconn };
}

// Guess the file type based on the file name.
static unsigned guessicon(const char *path) {
  unsigned l = strlen(path);
  if (l < 4)
    return ICON_BINFILE;

  if (!strcasecmp(&path[l-4], ".gba"))
    return ICON_GBACART;
  else if (!strcasecmp(&path[l-3], ".gb"))
    return ICON_GBCART;
  else if (!strcasecmp(&path[l-4], ".gbc"))
    return ICON_GBCCART;
  else if (!strcasecmp(&path[l-4], ".nes"))
    return ICON_NESCART;
  else if (!strcasecmp(&path[l-4], ".sms"))
    return ICON_SMSCART;
  else if (!strcasecmp(&path[l-3], ".fw"))
    return ICON_UPDFILE;

  return ICON_BINFILE;
}

// Draws text adding some support for overflow.
#define THREEDOTS_WIDTH  9
static void draw_text_ovf(const char *t, volatile uint8_t *frame, unsigned x, unsigned y, unsigned maxw) {
  uint8_t *basept = (uint8_t*)&frame[y * SCREEN_WIDTH + x];
  unsigned twidth = font_width(t);
  if (twidth <= maxw)
    draw_text_idx8_bus16(t, basept, SCREEN_WIDTH, FT_COLOR);
  else {
    char tmpbuf[256];
    unsigned numchars = font_width_cap(t, maxw - THREEDOTS_WIDTH);
    memcpy(tmpbuf, t, numchars);
    memcpy(&tmpbuf[numchars], "...", 4);
    draw_text_idx8_bus16(tmpbuf, basept, SCREEN_WIDTH, FT_COLOR);
  }
}

static void draw_text_ovf_rotate(const char *t, volatile uint8_t *frame, unsigned x, unsigned y, unsigned maxw, unsigned *franim) {
  uint8_t *basept = (uint8_t*)&frame[y * SCREEN_WIDTH + x];
  unsigned twidth = font_width(t);
  if (twidth <= maxw)
    draw_text_idx8_bus16(t, basept, SCREEN_WIDTH, FT_COLOR);
  else {
    unsigned anim = *franim > ANIM_INITIAL_WAIT ? (*franim - ANIM_INITIAL_WAIT) >> 4 : 0;

    // Wrap around once the text end reaches the mid point aprox.
    char tmpbuf[540];
    strcpy(tmpbuf, t);
    strcat(tmpbuf, "      ");
    unsigned pixw = font_width(tmpbuf);
    if (anim > pixw)
      *franim = ANIM_INITIAL_WAIT + ((anim - pixw) << 4);
    strcat(tmpbuf, t);

    draw_text_idx8_bus16_range(tmpbuf, basept, anim, maxw, SCREEN_WIDTH, FT_COLOR);
  }
}

static void draw_box_outline(volatile uint8_t *frame, unsigned left, unsigned right, unsigned top, unsigned bottom, uint8_t color) {
  dma_memset16(&frame[SCREEN_WIDTH * top + left], dup8(color), (right - left) / 2);
  dma_memset16(&frame[SCREEN_WIDTH * (top + 1) + left], dup8(color), (right - left) / 2);
  dma_memset16(&frame[SCREEN_WIDTH * (bottom - 1) + left], dup8(color), (right - left) / 2);
  dma_memset16(&frame[SCREEN_WIDTH * (bottom - 2) + left], dup8(color), (right - left) / 2);
  while (top < bottom) {
    *((uint16_t*)&frame[SCREEN_WIDTH * top + left]) = dup8(color);
    *((uint16_t*)&frame[SCREEN_WIDTH * top + right - 2]) = dup8(color);
    top++;
  }
}

static void draw_box_full(
  volatile uint8_t *frame, unsigned left, unsigned right, unsigned top, unsigned bottom,
  uint8_t outlinecolor, uint8_t bgcolor
) {
  draw_box_outline(frame, left, right, top, bottom, outlinecolor);
  for (unsigned i = top + 2; i < bottom - 2; i++)
    dma_memset16(&frame[SCREEN_WIDTH * i + left + 2], dup8(bgcolor), (right - left - 4) / 2);
}

static void draw_button_box(
  volatile uint8_t *frame, unsigned left, unsigned right, unsigned top, unsigned bottom, bool selected
) {
  if (selected)
    draw_box_full(frame, left, right, top, bottom, FG_COLOR, HI_COLOR);
  else
    draw_box_outline(frame, left, right, top, bottom, FG_COLOR);
}


static void draw_rightj_text(const char *t, volatile uint8_t *frame, unsigned x, unsigned y) {
  unsigned twidth = font_width(t);
  uint8_t *basept = (uint8_t*)&frame[y * SCREEN_WIDTH + x - twidth];
  draw_text_idx8_bus16(t, basept, SCREEN_WIDTH, FT_COLOR);
}

static void draw_central_text(const char *t, volatile uint8_t *frame, unsigned x, unsigned y) {
  unsigned twidth = font_width(t);
  uint8_t *basept = (uint8_t*)&frame[y * SCREEN_WIDTH + x - twidth / 2];
  draw_text_idx8_bus16(t, basept, SCREEN_WIDTH, FT_COLOR);
}

static void draw_central_text_ovf(const char *t, volatile uint8_t *frame, unsigned x, unsigned y, unsigned maxw) {
  unsigned twidth = font_width(t);
  if (twidth <= maxw) {
    uint8_t *basept = (uint8_t*)&frame[y * SCREEN_WIDTH + x - twidth / 2];
    draw_text_idx8_bus16(t, basept, SCREEN_WIDTH, FT_COLOR);
  } else {
    char tmpbuf[256];
    unsigned numchars = font_width_cap(t, maxw - THREEDOTS_WIDTH);
    memcpy(tmpbuf, t, numchars);
    memcpy(&tmpbuf[numchars], "...", 4);
    uint8_t *basept = (uint8_t*)&frame[y * SCREEN_WIDTH + x - maxw / 2];
    draw_text_idx8_bus16(tmpbuf, basept, SCREEN_WIDTH, FT_COLOR);
  }
}

static void draw_central_text_wrapped(const char *t, volatile uint8_t *frame, unsigned x, unsigned y, unsigned maxw) {
  while (*t) {
    unsigned outw;
    unsigned linechars = font_width_cap_space(t, maxw, &outw);
    unsigned charcnt = linechars ?: utf8_strlen(t);
    uint8_t *basept = (uint8_t*)&frame[y * SCREEN_WIDTH + x - outw / 2];
    draw_text_idx8_bus16_count(t, basept, charcnt, SCREEN_WIDTH, FT_COLOR);

    t += charcnt;      // Advance text
    y += 16;           // Move down in the buffer
  }
}

void render_recent(volatile uint8_t *frame) {
  // Render the list from memory.
  for (unsigned i = 0; i < RECENT_ROWS; i++) {
    if (smenu.recent.seloff + i >= smenu.recent.maxentries)
      break;

    t_rentry *e = &sdr_state->rentries[smenu.recent.seloff + i];
    char *fn = &e->fpath[e->fname_offset];
    render_icon(2, (i+1)*16, guessicon(fn));

    // Animate the row entries if they are too long!
    if (i == smenu.recent.selector - smenu.recent.seloff)
      draw_text_ovf_rotate(fn, frame, 20, (1 + i) * 16,
                           SCREEN_WIDTH - 24, &smenu.anim_state);
    else
      draw_text_ovf(fn, frame, 20, (1 + i) * 16, SCREEN_WIDTH - 24);
  }

  for (unsigned i = 0; i < 240; i += 16)
    render_icon_trans(i, (smenu.recent.selector - smenu.recent.seloff + 1)*16, 63);
}

void render_browser(volatile uint8_t *frame) {
  // Render bar below to show path URI
  dma_memset16(&frame[240*144], dup8(FG_COLOR), 240*16/2);

  for (unsigned i = 0; i < BROWSER_ROWS; i++) {
    if (smenu.browser.seloff + i >= smenu.browser.maxentries)
      break;

    t_centry *e = sdr_state->fileorder[smenu.browser.seloff + i];

    if (e->attr & AM_DIR)
      render_icon(2, (i+1)*16, ICON_FOLDER);
    else
      render_icon(2, (i+1)*16, guessicon(e->fname));

    char szstr[16];
    human_size(szstr, sizeof(szstr), e->filesize);
    draw_rightj_text(szstr, frame, SCREEN_WIDTH - 2, (1 + i) * 16);

    // Animate the row entries if they are too long!
    if (i == smenu.browser.selector - smenu.browser.seloff)
      draw_text_ovf_rotate(e->fname, frame, 20, (1 + i) * 16,
                           SCREEN_WIDTH - 26 - font_width(szstr), &smenu.anim_state);
    else
      draw_text_ovf(e->fname, frame, 20, (1 + i) * 16, SCREEN_WIDTH - 26 - font_width(szstr));
  }

  draw_text_ovf(smenu.browser.cpath, frame, 16, 144, 224);

  char selinfo[16];
  npf_snprintf(selinfo, sizeof(selinfo), "%u/%d", smenu.browser.selector + 1, smenu.browser.maxentries);
  draw_rightj_text(selinfo, frame, SCREEN_WIDTH - 1, 1);

  for (unsigned i = 0; i < 240; i += 16)
    render_icon_trans(i, (smenu.browser.selector - smenu.browser.seloff + 1)*16, 63);
}

void render_fw_flash_popup(volatile uint8_t *frame) {
  // Render a box to give a pop-up feeling
  draw_box_outline(frame, 2, 240-2, 18, 158, FG_COLOR);

  draw_central_text(msgs[lang_id][MSG_FWUPD_MENU], frame, 120, 30);

  draw_box_outline(frame, 16, 224, 64, 92, FG_COLOR);
  if (spop.p.update.issfw) {
    char tmp[32];
    npf_snprintf(tmp, sizeof(tmp), "SuperFW (ver %lu.%lu)",
                 spop.p.update.superfw_ver >> 16,
                 spop.p.update.superfw_ver & 0xFFFF);
    draw_central_text(tmp, frame, 120, 70);
  } else {
    draw_central_text(msgs[lang_id][MSG_FWUPD_UNK], frame, 120, 70);
  }

  const char *smsg[] = {
    msgs[lang_id][MSG_FWUPD_GO],
    msgs[lang_id][MSG_FWUPD_LOADING],
    msgs[lang_id][MSG_FWUPD_CHECKING],
    msgs[lang_id][MSG_FWUPD_ERASING],
    msgs[lang_id][MSG_FWUPD_PROGRAM],
  };

  draw_central_text(smsg[spop.p.update.curr_state], frame, 120, 120);
}

void render_sav_menu_popup(volatile uint8_t *frame) {
  // Render a box to give a pop-up feeling
  draw_box_outline(frame, 2, 240-2, 18, 158, FG_COLOR);

  for (unsigned i = 0; i < 3; i++) {
    if (spop.p.savopt.selector == i)
      draw_box_full(frame, 20, 220, 32 + 28 * i, 32 + 28 * i + 20, FG_COLOR, HI_COLOR);
    else
      draw_box_outline(frame, 20, 220, 32 + 28 * i, 32 + 28 * i + 20, FG_COLOR);
    draw_central_text(msgs[lang_id][MSG_SAVOPT_OPT0 + i], frame, 120, 34 + 28 * i);
  }
  if (spop.p.savopt.selector == SavQuit)
      draw_box_full(frame, 20, 220, 124, 144, FG_COLOR, HI_COLOR);
  else
    draw_box_outline(frame, 20, 220, 124, 144, FG_COLOR);
  draw_central_text(msgs[lang_id][MSG_CANCEL], frame, 120, 126);
}

void render_gba_load_popup(volatile uint8_t *frame, unsigned fcnt) {
  char tmp[64];
  draw_box_outline(frame, 2, 240-2, 18, 158, FG_COLOR);

  spop.p.load.anim += fcnt * animspd_lut[anim_speed];

  draw_text_ovf("⯇", frame, 10, 24, 64);
  draw_rightj_text("⯈", frame, SCREEN_WIDTH - 10, 24);

  const char *ht = NULL;

  switch (spop.p.load.submenu) {
  case GbaLoadPopInfo:
    draw_central_text(msgs[lang_id][MSG_GBALOAD_MINFO], frame, SCREEN_WIDTH/2, 24);
    {
      const char *romname = file_basename(spop.p.load.romfn);
      unsigned twidth = font_width(romname);
      if (twidth > SCREEN_WIDTH - 20)
        draw_text_ovf_rotate(romname, frame, 10, 52,
                             SCREEN_WIDTH - 20, &spop.p.load.anim);
      else
        draw_central_text_ovf(romname, frame, SCREEN_WIDTH/2, 52, SCREEN_WIDTH - 20);

      npf_snprintf(tmp, sizeof(tmp), msgs[lang_id][MSG_LOADINFO_GAME],
                   spop.p.load.gcode, spop.p.load.romh.version);
      draw_central_text_ovf(tmp, frame, SCREEN_WIDTH/2, 82, SCREEN_WIDTH - 20);

      bool pfound = spop.p.load.patch_type == PatchDatabase ? spop.p.load.patches_datab_found :
                    spop.p.load.patch_type == PatchEngine   ? spop.p.load.patches_cache_found :
                                                              false;

      if (pfound) {
        const t_patch *p = spop.p.load.patch_type == PatchDatabase ? &spop.p.load.patches_datab :
                           spop.p.load.patch_type == PatchEngine   ? &spop.p.load.patches_cache :
                                                                     NULL;

        const char *stype[] = {
          msgs[lang_id][MSG_SAVETYPE_NONE],       // SaveTypeNone
          msgs[lang_id][MSG_SAVETYPE_SRAM],       // SaveTypeSRAM
          msgs[lang_id][MSG_SAVETYPE_EEPROM],     // SaveTypeEEPROM4K
          msgs[lang_id][MSG_SAVETYPE_EEPROM],     // SaveTypeEEPROM64K
          msgs[lang_id][MSG_SAVETYPE_FLASH],      // SaveTypeFlash512K
          msgs[lang_id][MSG_SAVETYPE_FLASH],      // SaveTypeFlash1024K
        };
        const char *ssize[] = {
          "0KB",       // SaveTypeNone
          "32KB",      // SaveTypeSRAM
          "0.5KB",     // SaveTypeEEPROM4K
          "8KB",       // SaveTypeEEPROM64K
          "64KB",      // SaveTypeFlash512K
          "128KB",     // SaveTypeFlash1024K
        };

        npf_snprintf(tmp, sizeof(tmp), msgs[lang_id][MSG_LOADINFO_SAVE],
                     stype[p->save_mode], ssize[p->save_mode]);
        draw_central_text_ovf(tmp, frame, SCREEN_WIDTH/2, 102, SCREEN_WIDTH - 20);
      } else if (is_superfw(&spop.p.load.romh)) {
        draw_central_text_ovf("SuperFW firmware", frame, SCREEN_WIDTH/2, 102, SCREEN_WIDTH - 20);
      } else {
        draw_central_text_ovf(msgs[lang_id][MSG_LOADINFO_UNKW], frame, SCREEN_WIDTH/2, 102, SCREEN_WIDTH - 20);
      }
    }
    break;
  case GbaLoadPopSave:
    draw_central_text(msgs[lang_id][MSG_GBALOAD_MSAVE], frame, SCREEN_WIDTH/2, 24);
    draw_text_ovf(msgs[lang_id][MSG_LOADER_SAVET], frame, 12, 48, 224);
    draw_central_text(msgs[lang_id][MSG_LOADER_ST0 + (spop.p.load.use_dsaving ? 0 : 1)], frame, 170, 48);
    draw_text_ovf(msgs[lang_id][MSG_LOADER_LOADP], frame, 12, 68, 224);
    draw_central_text(msgs[lang_id][MSG_LOADER_LOADP0 + spop.p.load.sram_load_type], frame, 170, 68);
    draw_text_ovf(msgs[lang_id][MSG_LOADER_SAVEP], frame, 12, 88, 224);
    draw_central_text(msgs[lang_id][MSG_LOADER_SAVEP0 + spop.p.load.sram_save_type], frame, 170, 88);

    ht = (spop.p.load.selector == GBASaveLoadP) ? msgs[lang_id][MSG_LOADER_LOADP_I0 + spop.p.load.sram_load_type] :
         (spop.p.load.selector == GBASaveSaveP) ? msgs[lang_id][MSG_LOADER_SAVEP_I0 + spop.p.load.sram_save_type] :
         (spop.p.load.selector == GBASaveMode)  ? msgs[lang_id][MSG_LOADER_ST_I0 + (spop.p.load.use_dsaving ? 0 : 1)] : NULL;
    break;
  case GbaLoadPopPatch:
    draw_central_text(msgs[lang_id][MSG_GBALOAD_MPATCH], frame, SCREEN_WIDTH/2, 24);
    draw_text_ovf(msgs[lang_id][MSG_DEFS_PATCH], frame, 12, 48, 224);
    draw_central_text(msgs[lang_id][MSG_PATCH_TYPE0 + spop.p.load.patch_type], frame, 162, 48);
    draw_text_ovf(msgs[lang_id][MSG_LOADER_MENU], frame, 12, 68, 224);
    draw_central_text(msgs[lang_id][spop.p.load.ingame_menu_enabled ? MSG_KNOB_ENABLED : MSG_KNOB_DISABLED], frame, 162, 68);
    draw_text_ovf(msgs[lang_id][MSG_LOADER_PTCH], frame, 12, 88, 224);
    draw_box_outline(frame, 112, 212, 86, 106, FG_COLOR);
    draw_central_text(msgs[lang_id][MSG_TOOLS_RUN], frame, 162, 88);

    ht = (spop.p.load.selector == GBALoadPatch) ? msgs[lang_id][MSG_PATCH_TYPE_I0 + spop.p.load.patch_type] :
         (spop.p.load.selector == GBAInGameMen) ? msgs[lang_id][MSG_INGAME_I] :
         (spop.p.load.selector == GBAPatchGen)  ? msgs[lang_id][MSG_PATCHE_I] : NULL;

    break;
  case GbaLoadPopSett:
    draw_central_text(msgs[lang_id][MSG_GBALOAD_MSETT], frame, SCREEN_WIDTH/2, 24);

    npf_snprintf(tmp, sizeof(tmp), "20%02d/%02d/%02d %02d:%02d",
      spop.p.load.rtcval.year, spop.p.load.rtcval.month + 1, spop.p.load.rtcval.day + 1,
      spop.p.load.rtcval.hour, spop.p.load.rtcval.mins);

    draw_text_ovf(msgs[lang_id][MSG_LOADER_RTCE], frame, 12, 48, 224);
    draw_central_text(spop.p.load.rtc_patch_enabled ? tmp : msgs[lang_id][MSG_KNOB_DISABLED], frame, 170, 48);
    draw_text_ovf(msgs[lang_id][MSG_SETT_LDCHT], frame, 12, 68, 224);
    draw_central_text(msgs[lang_id][spop.p.load.use_cheats ? MSG_KNOB_ENABLED : MSG_KNOB_DISABLED], frame, 170, 68);
    draw_text_ovf(msgs[lang_id][MSG_SETT_REMEMB], frame, 12, 88, 224);
    draw_central_text(msgs[lang_id][spop.p.load.write_config ? MSG_KNOB_ENABLED : MSG_KNOB_DISABLED], frame, 170, 88);

    ht = (spop.p.load.selector == GBASetRememb) ? msgs[lang_id][MSG_REMEMB_I] :
         (spop.p.load.selector == GBASetLdCht && !enable_cheats) ? msgs[lang_id][MSG_CHEATSDIS_I] :
         (spop.p.load.selector == GBASetLdCht && !spop.p.load.cheats_found) ? msgs[lang_id][MSG_CHEATSNOA_I] :
         (spop.p.load.selector == GBASetRTCEn)  ? msgs[lang_id][MSG_PATCHRTC_I] : NULL;
    break;
  };

  // Show some help if necessary
  if (ht) {
    unsigned twidth = font_width(ht);
    if (twidth > SCREEN_WIDTH - 20)
      draw_text_ovf_rotate(ht, frame, 10, 110,
                           SCREEN_WIDTH - 20, &spop.p.load.anim);
    else
      draw_central_text_ovf(ht, frame, SCREEN_WIDTH/2, 110, SCREEN_WIDTH - 20);
  }

  if (GBALoadButt == spop.p.load.selector) {
    draw_box_full(frame, 20, 220, 132, 152, FG_COLOR, HI_COLOR);
  } else {
    for (unsigned i = 8; i < 232; i += 16) {
      render_icon_trans(i, 26 + spop.p.load.selector * 20, 63);
      render_icon_trans(i, 30 + spop.p.load.selector * 20, 63);
    }
    draw_box_outline(frame, 20, 220, 132, 152, FG_COLOR);
  }
  draw_central_text(msgs[lang_id][MSG_LOAD_GBA], frame, 120, 134);
}

void render_popupq(volatile uint8_t *frame, unsigned fcnt) {
  draw_box_outline(frame, 2, 240-2, 18, 158, FG_COLOR);

  // Draw question and two buttons
  draw_central_text_wrapped(spop.qpop.message, frame, SCREEN_WIDTH/2, 32, SCREEN_WIDTH - 20);

  if (spop.qpop.option == 0) {
    draw_box_full(frame, 20, 220, 90, 90 + 20, FG_COLOR, HI_COLOR);
    draw_box_outline(frame, 20, 220, 120, 120 + 20, FG_COLOR);
  } else {
    draw_box_full(frame, 20, 220, 120, 120 + 20, FG_COLOR, HI_COLOR);
    draw_box_outline(frame, 20, 220, 90, 90 + 20, FG_COLOR);
  }

  draw_central_text(spop.qpop.default_button, frame, 120, 92);
  draw_central_text(spop.qpop.confirm_button, frame, 120, 122);
}

void render_rtcpop(volatile uint8_t *frame) {
  draw_box_outline(frame, 2, 240-2, 18, 158, FG_COLOR);

  draw_central_text(msgs[lang_id][MSG_DEF_RTCVAL], frame, SCREEN_WIDTH/2, 32);

  const t_rtc_state *v = &spop.rtcpop.val;
  char thour[3] = {'0' + v->hour/10, '0' + v->hour % 10, 0};
  char tmins[3] = {'0' + v->mins/10, '0' + v->mins % 10, 0};
  char tdays[3] = {'0' + (v->day + 1)/10, '0' + (v->day + 1)  % 10, 0};
  char tmont[3] = {'0' + (v->month + 1)/10, '0' + (v->month + 1) % 10, 0};
  char tyear[5] = {'2', '0', '0' + v->year/10, '0' + v->year % 10, 0};

  draw_central_text(tyear, frame,  60, 70);
  draw_central_text("-",   frame,  80, 70);
  draw_central_text(tmont, frame,  94, 70);
  draw_central_text("-",   frame, 106, 70);
  draw_central_text(tdays, frame, 120, 70);
  draw_central_text(thour, frame, 154, 70);
  draw_central_text(":",   frame, 166, 70);
  draw_central_text(tmins, frame, 180, 70);

  const uint8_t cox[] = {
    60, 94, 120, 154, 180
  };
  draw_central_text("⯅", frame, cox[spop.rtcpop.selector], 54);
  draw_central_text("⯆", frame, cox[spop.rtcpop.selector], 84);
}

void render_settings(volatile uint8_t *frame) {
  char tmp[32];
  unsigned baseopt = smenu.set.selector <= 1  ? 0 :
                     smenu.set.selector >= SettMAX - 3 ? SettMAX - 4 :
                     smenu.set.selector - 1;

  unsigned msk = 0xF << baseopt;
  unsigned optcnt = 0;
  const unsigned colx = 170;           // Center point for the selection boxes

  if (msk & 0x00001)
    draw_central_text(msgs[lang_id][MSG_SET_TITL1], frame, SCREEN_WIDTH/2, 22 + 20*optcnt++);

  if (msk & 0x00002) {
    npf_snprintf(tmp, sizeof(tmp), "< %s >", hotkey_list[hotkey_combo].cname);
    draw_text_ovf(msgs[lang_id][MSG_SETT_HOTK], frame, 8, 22 + 20*optcnt, 224);
    draw_central_text(tmp, frame, colx, 22 + 20*optcnt++);
  }

  if (msk & 0x00004) {
    draw_text_ovf(msgs[lang_id][MSG_SETT_BOOT], frame, 8, 22 + 20*optcnt, 224);
    draw_central_text(msgs[lang_id][MSG_BOOT_TYPE0 + boot_bios_splash], frame, colx, 22 + 20*optcnt++);
  }

  if (msk & 0x00008) {
    draw_text_ovf(msgs[lang_id][MSG_SETT_FASTSD], frame, 8, 22 + 20*optcnt, 224);
    draw_central_text(msgs[lang_id][use_slowsd ? MSG_KNOB_DISABLED : MSG_KNOB_ENABLED], frame, colx, 22 + 20*optcnt++);
  }

  if (msk & 0x00010) {
    draw_text_ovf(msgs[lang_id][MSG_SETT_FASTEW], frame, 8, 22 + 20*optcnt, 224);
    draw_central_text(msgs[lang_id][use_fastew ? MSG_KNOB_ENABLED : MSG_KNOB_DISABLED], frame, colx, 22 + 20*optcnt++);
  }

  if (msk & 0x00020) {
    draw_text_ovf(msgs[lang_id][MSG_SETT_SAVET], frame, 8, 22 + 20*optcnt, 224);
    draw_central_text(msgs[lang_id][MSG_SAVE_TYPE0 + save_path_default], frame, colx, 22 + 20*optcnt++);
  }

  if (msk & 0x00040) {
    npf_snprintf(tmp, sizeof(tmp), "< %lu >", backup_sram_default);
    draw_text_ovf(msgs[lang_id][MSG_SETT_SAVEBK], frame, 8, 22 + 20*optcnt, 224);
    draw_central_text(tmp, frame, colx, 22 + 20*optcnt++ );
  }

  if (msk & 0x00080) {
    draw_text_ovf(msgs[lang_id][MSG_SETT_STATET], frame, 8, 22 + 20*optcnt, 224);
    draw_central_text(msgs[lang_id][MSG_STTE_TYPE0 + state_path_default], frame, colx, 22 + 20*optcnt++);
  }

  if (msk & 0x00100) {
    draw_text_ovf(msgs[lang_id][MSG_SETT_CHTEN], frame, 8, 22 + 20*optcnt, 224);
    draw_central_text(msgs[lang_id][enable_cheats ? MSG_KNOB_ENABLED : MSG_KNOB_DISABLED], frame, colx, 22 + 20*optcnt++);
  }

  if (msk & 0x00200)
    draw_central_text(msgs[lang_id][MSG_SET_TITL2], frame, SCREEN_WIDTH/2, 22 + 20*optcnt++);

  if (msk & 0x00400) {
    draw_text_ovf(msgs[lang_id][MSG_DEFS_PATCH], frame, 8, 22 + 20*optcnt, 224);
    draw_central_text(msgs[lang_id][MSG_PATCH_TYPE0 + patcher_default], frame, colx, 22 + 20*optcnt++);
  }

  if (msk & 0x00800) {
    draw_text_ovf(msgs[lang_id][MSG_LOADER_MENU], frame, 8, 22 + 20*optcnt, 224);
    draw_central_text(msgs[lang_id][MSG_KNOB_DISABLED + ingamemenu_default], frame, colx, 22 + 20*optcnt++);
  }

  if (msk & 0x01000) {
    draw_text_ovf(msgs[lang_id][MSG_LOADER_RTCE], frame, 8, 22 + 20*optcnt, 224);
    draw_central_text(msgs[lang_id][MSG_KNOB_DISABLED + rtcpatch_default], frame, colx, 22 + 20*optcnt++);
  }

  if (msk & 0x02000) {
    npf_snprintf(tmp, sizeof(tmp), "20%02d/%02d/%02d %02d:%02d",
      rtcvalue_default.year, rtcvalue_default.month + 1, rtcvalue_default.day + 1,
      rtcvalue_default.hour, rtcvalue_default.mins);
    draw_text_ovf(msgs[lang_id][MSG_DEF_RTCVAL], frame, 8, 22 + 20*optcnt, 224);
    draw_central_text(tmp, frame, colx, 22 + 20*optcnt++);
  }

  if (msk & 0x04000) {
    draw_text_ovf(msgs[lang_id][MSG_LOADER_LOADP], frame, 8, 22 + 20*optcnt, 224);
    draw_central_text(msgs[lang_id][MSG_DEF_LOADP0 + (autoload_default ^ 1)], frame, colx, 22 + 20*optcnt++);
  }

  if (msk & 0x08000) {
    draw_text_ovf(msgs[lang_id][MSG_LOADER_SAVEP], frame, 8, 22 + 20*optcnt, 224);
    draw_central_text(msgs[lang_id][autosave_default ? MSG_DEF_SAVEP0 : MSG_DEF_SAVEP1], frame, colx, 22 + 20*optcnt++);
  }

  if (msk & 0x10000) {
    draw_text_ovf(msgs[lang_id][MSG_LOADER_PREFDS], frame, 8, 22 + 20*optcnt, 224);
    draw_central_text(msgs[lang_id][autosave_prefer_ds ? MSG_KNOB_ENABLED : MSG_KNOB_DISABLED], frame, colx, 22 + 20*optcnt++);
  }

  // Render bar below for help messge
  dma_memset16(&frame[240*140], dup8(FG_COLOR), 240*20/2);

  unsigned help_msg = smenu.set.selector == SettBootType ? MSG_BOOT_TYPE_I0 + boot_bios_splash :
                      smenu.set.selector == SettSaveLoc  ? MSG_SAVE_TYPE_I0 + save_path_default :
                      smenu.set.selector == SettSaveBkp  ? MSG_BACKUP_I :
                      smenu.set.selector == SettFastSD   ? MSG_FASTSD_I :
                      smenu.set.selector == SettFastEWRAM? MSG_FASTEW_I :
                      smenu.set.selector == DefsPatchEng ? MSG_PATCH_TYPE_I0 + patcher_default :
                      smenu.set.selector == DefsLoadPol  ? MSG_DEF_LOADP_I0 + (autoload_default ^ 1) :
                      smenu.set.selector == DefsSavePol  ? MSG_DEF_SAVEP_I0 + (autosave_default ^ 1) :
                      smenu.set.selector == DefsPrefDS   ? MSG_LOADER_PREFDSI :
                      MSG_EMPTY;
  draw_text_ovf_rotate(msgs[lang_id][help_msg], frame, 4, SCREEN_HEIGHT - 18, 232, &smenu.anim_state);


  if (smenu.set.selector != SettSave) {
    for (unsigned i = 0; i < 240; i += 16)
      render_icon_trans(i, 22 + (smenu.set.selector - baseopt) * 20, 63);
    draw_box_outline(frame, 20, 220, 112, 132, FG_COLOR);
  }
  else
    draw_box_full(frame, 20, 220, 112, 132, FG_COLOR, HI_COLOR);
  draw_central_text(msgs[lang_id][MSG_UIS_SAVE], frame, 120, 114);
}

void render_ui_setting_theme(char *buf, int buflen) {
  npf_snprintf(buf, buflen, "< %lu >", menu_theme + 1U);
}

void render_ui_setting_lang(char *buf, int buflen) {
  npf_snprintf(buf, buflen, "< %s >", msgs[lang_id][MSG_LANG_NAME]);
}

void reload_theme() {
  // Palette 0..15 contains the main menu template colors
  MEM_PALETTE[FG_COLOR] = themes[menu_theme].fg_color;
  MEM_PALETTE[BG_COLOR] = themes[menu_theme].bg_color;
  MEM_PALETTE[FT_COLOR] = themes[menu_theme].ft_color;
  MEM_PALETTE[HI_COLOR] = themes[menu_theme].hi_color;
  // In-game menu palette
  MEM_PALETTE[INGMENU_PAL_FG] = themes[menu_theme].fg_color;
  MEM_PALETTE[INGMENU_PAL_BG] = themes[menu_theme].bg_color;
  MEM_PALETTE[INGMENU_PAL_HI] = themes[menu_theme].ft_color;
  MEM_PALETTE[INGMENU_PAL_SH] = themes[menu_theme].sh_color;

  // Palette entries for icons and other objects
  MEM_PALETTE[256 + SEL_COLOR] = themes[menu_theme].hi_blend;
}

static const UiMenuItem ui_settings_items[] = {
  { .name = MSG_UIS_THEME, .render = render_ui_setting_theme, .from_int = &menu_theme, .from_int_max = THEME_COUNT, .on_change = reload_theme },
  { .name = MSG_UIS_LANG, .render = render_ui_setting_lang, .from_int = &lang_id, .from_int_max = LANG_COUNT },
  { .name = MSG_UIS_RECNT, .from_bool = &recent_menu },
  { .name = MSG_UIS_SHOWHIDDEN, .from_bool = &show_hidden_files, .on_change = browser_reload },
  { .name = MSG_UIS_ANSPD, .from_int = &anim_speed, .from_int_offset = MSG_UIS_SPD0, .from_int_max = animspd_cnt },
};
#define UI_SETTINGS_ITEMS_COUNT (sizeof(ui_settings_items)/sizeof(UiMenuItem))

void render_ui_settings(volatile uint8_t *frame) {
  const unsigned colx = 170;
  char tmpbuf[64];

  int y = 0;
  for (unsigned i = 0; i < UI_SETTINGS_ITEMS_COUNT; i++) {
    const UiMenuItem *item = &ui_settings_items[i];
    if (item->render) {
      item->render(tmpbuf, sizeof(tmpbuf));
      draw_text_ovf(msgs[lang_id][item->name], frame, 8, 22 + y, 224);
      draw_central_text(tmpbuf, frame, colx, 22 + y);
    } else if (item->from_bool) {
      draw_text_ovf(msgs[lang_id][item->name], frame, 8, 22 + y, 224);
      draw_central_text(msgs[lang_id][*item->from_bool ? MSG_KNOB_ENABLED : MSG_KNOB_DISABLED], frame, colx, 22 + y);
    } else if (item->from_int) {
      draw_text_ovf(msgs[lang_id][item->name], frame, 8, 22 + y, 224);
      draw_central_text(msgs[lang_id][item->from_int_offset + *item->from_int], frame, colx, 22 + y);
    }
    y += 20;
  }

  if (smenu.uiset.selector != UI_SETTINGS_ITEMS_COUNT)
    for (unsigned i = 0; i < 240; i += 16)
      render_icon_trans(i, 22 + smenu.uiset.selector * 20, 63);

  if (smenu.uiset.selector != UI_SETTINGS_ITEMS_COUNT)
    draw_box_outline(frame, 20, 220, 132, 152, FG_COLOR);
  else
    draw_box_full(frame, 20, 220, 132, 152, FG_COLOR, HI_COLOR);
  draw_central_text(msgs[lang_id][MSG_UIS_SAVE], frame, 120, 134);
}

void render_info(volatile uint8_t *frame) {
  uint32_t vmaj = VERSION_WORD >> 16;
  uint32_t vmin = VERSION_WORD & 0xFFFF;
  uint32_t gitver = VERSION_SLUG_WORD;
  char tmp[64], tmp2[32];

  init_logo_palette(&MEM_PALETTE[1]);
  render_logo((uint16_t*)frame, SCREEN_WIDTH/2, 40, 4);

  switch (smenu.info.selector) {
  case 0:
    draw_central_text("by davidgf", frame, 120, 60);
    npf_snprintf(tmp, sizeof(tmp), "Version %lu.%lu " FW_FLAVOUR " (%08lx)", vmaj, vmin, gitver);
    draw_central_text(tmp, frame, 120, 90);
    npf_snprintf(tmp, sizeof(tmp), "Flash device ID: %08lx", flash_deviceid);
    draw_central_text(tmp, frame, 120, 110);
    break;
  case 1:
    draw_central_text(msgs[lang_id][MSG_DBPINFO], frame, 120, 70);
    npf_snprintf(tmp, sizeof(tmp), "%s - %s", pdbinfo.version, pdbinfo.date);
    draw_central_text(tmp, frame, 120, 90);
    npf_snprintf(tmp, sizeof(tmp), "Game count: %lu", pdbinfo.patch_count);
    draw_central_text(tmp, frame, 120, 110);
    break;
  case 2:
    if (sd_info.sdhc)
      draw_central_text("SD card type: SDHC", frame, 120, 70);
    else
      draw_central_text("SD card type: SDSC", frame, 120, 70);
    human_size_kb(tmp2, sizeof(tmp2), sd_info.block_cnt / 2);
    npf_snprintf(tmp, sizeof(tmp), msgs[lang_id][MSG_CAPACITY], tmp2);
    draw_central_text(tmp, frame, 120, 90);
    npf_snprintf(tmp, sizeof(tmp), "Card ID: %02x | %04x", sd_info.manufacturer, sd_info.oemid);
    draw_central_text(tmp, frame, 120, 110);
    break;
  }

  // Flashing info
  dma_memset16(&frame[138*SCREEN_WIDTH], dup8(FG_COLOR), SCREEN_WIDTH*22/2);
  draw_text_ovf_rotate(enable_flashing ? msgs[lang_id][MSG_FWUP_ENABLED] : msgs[lang_id][MSG_FWUP_HOTKEY], frame,
                       4, 141, SCREEN_WIDTH - 8, &smenu.anim_state);
}

void render_tools(volatile uint8_t *frame) {
  for (unsigned i = 0; i <= ToolsMAX; i++) {
    draw_text_ovf(msgs[lang_id][MSG_TOOLS0_SDRAM + i], frame, 12, 24 + 2 + 24 * i, 144);
    draw_button_box(frame, 150, 232, 24 + 24 * i, 24 + 20 + 24 * i, smenu.tools.selector == i);
    draw_central_text(msgs[lang_id][MSG_TOOLS_RUN], frame, 191, 24 + 2 + 24 * i);
  }
}

// Renders the menu. Arg0 represents the frame count difference with the
// previous rendered frame (for animations and similar stuff).
void menu_render(unsigned fcnt) {
  objnum = 0;
  volatile uint8_t *frame = &MEM_VRAM_U8[0xA000*framen];

  // Render the tab menu on top (rows 0..15), highlighting the selected option
  dma_memset16(&frame[0], dup8(FG_COLOR), SCREEN_WIDTH*16/2);

  // Render icons
  int mintab = (recent_menu && smenu.recent.maxentries) ? MENUTAB_RECENT : MENUTAB_ROMBROWSE;
  for (unsigned i = mintab; i < MENUTAB_MAX; i++)
    if (i == smenu.menu_tab)
      render_icon((i - mintab)*16, 0, i + ICON_RECENT);
    else
      render_icon_trans((i - mintab)*16, 0, i + ICON_RECENT);

  // Render the main area
  dma_memset16(&frame[16*SCREEN_WIDTH], dup8(BG_COLOR), SCREEN_WIDTH*(SCREEN_HEIGHT-16) / 2);

  if (spop.qpop.message)
    render_popupq(frame, fcnt);
  else if (spop.rtcpop.callback)
    render_rtcpop(frame);
  else {
    if (spop.pop_num) {
      switch (spop.pop_num) {
      case POPUP_GBA_LOAD:
        render_gba_load_popup(frame, fcnt);
        break;
      case POPUP_SAVFILE:
        render_sav_menu_popup(frame);
        break;
      case POPUP_FWFLASH:
        render_fw_flash_popup(frame);
        break;
      };
    } else {
      smenu.anim_state += fcnt * animspd_lut[anim_speed];

      static const t_mrender_fn renderfns[] = {
        render_recent,
        render_browser,
        render_settings,
        render_ui_settings,
        render_tools,
        render_info,
      };
      renderfns[smenu.menu_tab](frame);
    }
  }

  // Render popup window. Use windowing to ensure the pop up is not covered by OBJs.
  if (spop.alert_msg) {
    draw_box_full(frame, 15, 227, SCREEN_HEIGHT / 2 - 20, SCREEN_HEIGHT / 2 + 20, FG_COLOR, HI_COLOR);
    draw_central_text(spop.alert_msg, frame, SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2 - 8);
    REG_WIN0H = 226 | (14 << 8);
    REG_WIN0V = (SCREEN_HEIGHT / 2 + 20) | ((SCREEN_HEIGHT / 2 - 20) << 8);
  } else {
    REG_WIN0H = 0;
    REG_WIN0V = 0;
  }
}

void menu_flip() {
  for (unsigned i = 0; i < objnum; i++) {
    MEM_OAM[i*4+0] = fobjs[i].y | 0x2000;  // Use 256 entries palette
    MEM_OAM[i*4+1] = fobjs[i].x | 0x4000;  // Size 16x16
    MEM_OAM[i*4+2] = fobjs[i].tn + 512;    // OBJ numbers start at 512 for Mode 4
  }
  dma_memset16(&MEM_OAM[objnum*4], 0, 256 - objnum*2);  // Clear unused objects
  REG_DISPCNT = (REG_DISPCNT & ~0x10) | (framen << 4);
  framen ^= 1;
}

void menu_init(int sram_testres) {
  // Reset to ROM browser and SD card root.
  memset(&smenu, 0, sizeof(smenu));
  memset(&spop, 0, sizeof(spop));

  // Reset the file browser as well.
  strcpy(smenu.browser.cpath, "/");
  browser_reload();

  // Load recent ROMs (we could disable this for speed)
  recent_reload();

  reload_theme();

  smenu.menu_tab = (recent_menu && smenu.recent.maxentries) ? MENUTAB_RECENT : MENUTAB_ROMBROWSE;

  // Load icons into VRAM
  dma_memcpy16(MEM_VRAM_OBJS, icons_img, sizeof(icons_img) / 2);
  dma_memcpy16(&MEM_PALETTE[256], icons_pal, sizeof(icons_pal) / 2);
  // Generate some icons (selector)
  dma_memset16(&MEM_VRAM_OBJS[63 * 256], dup8(SEL_COLOR), 256 / 2);

  // Further setup initial video regs. BG2 is setup in the bootloader already!
  REG_WININ  = 0x0004;     // Only BG2 is enabled in Win0
  REG_WINOUT = 0x0014;     // BG2 and OBJ enabled outside of Win0
  REG_WIN0H = 0;
  REG_WIN0V = 0;
  REG_DISPCNT |= 0x2000;   // Enable window 0

  // Setup alpha blending for the selector knob
  REG_BLDCNT = 0x1F40;
  REG_BLDALPHA = 0x0808;  // 50% alpha

  // If there's a test result to report, create a popup
  if (sram_testres >= 0)
    spop.alert_msg = sram_testres ? msgs[lang_id][MSG_SRAMTST_FAIL] :
                                    msgs[lang_id][MSG_SRAMTST_OK];
}

int movedir_up() {
  char *p = smenu.browser.cpath;
  p = &p[strlen(p)-1];

  if (p != smenu.browser.cpath) {
    do {
      p--;
      if (*p == '/') {
        p[1] = 0;   // Shorten the path here
        return 1;
      }
    } while (p != smenu.browser.cpath);
  }
  return 0;
}

void start_flash_update(const char *fn, unsigned fwsize, bool validate_superfw) {
  // We read the file into SDRAM, apply the update from there.
  FIL fd;
  FRESULT res = f_open(&fd, fn, FA_READ);
  if (res != FR_OK)
    spop.alert_msg = msgs[lang_id][MSG_FWUP_ERRRD];
  else {
    // Loading file...
    spop.p.update.curr_state = FlashingLoading;
    menu_render(1); menu_flip();
    for (unsigned i = 0; i < fwsize; i += 4*1024) {
      UINT rdbytes;
      unsigned tord = fwsize >= i + 4*1024 ? 4*1024 : fwsize - i;
      uint32_t tmp[1024];
      if (FR_OK != f_read(&fd, tmp, tord, &rdbytes) || rdbytes != tord) {
        spop.alert_msg = msgs[lang_id][MSG_FWUP_ERRRD];
        return;
      }
      // Copy (ensure aligned copy!)
      dma_memcpy32(&sdr_state->scratch[i], tmp, 1024);
    }
    spop.p.update.curr_state = FlashingChecking;
    menu_render(1); menu_flip();

    // Now proceed to validate the superfw if necessary.
    if (validate_superfw && !validate_superfw_checksum(sdr_state->scratch, fwsize))
      spop.alert_msg = msgs[lang_id][MSG_FWUPD_BADCHK];
    else {
      // Can start the flashing!
      spop.p.update.curr_state = FlashingErasing;
      menu_render(1); menu_flip();

      if (!flash_erase())
        spop.alert_msg = msgs[lang_id][MSG_FWUP_ERRCL];
      else {
        spop.p.update.curr_state = FlashingWriting;
        menu_render(1); menu_flip();

        if (!flash_program(sdr_state->scratch, fwsize))
          spop.alert_msg = msgs[lang_id][MSG_FWUP_ERRPG];
        else {
          if (!flash_verify(sdr_state->scratch, fwsize))
            spop.alert_msg = msgs[lang_id][MSG_FWUP_ERRVR];
          else {
            // Done! Show a pop up, also go up with pop ups too.
            spop.alert_msg = msgs[lang_id][MSG_FWUPD_DONE];
            spop.pop_num = 0;
          }
        }
      }
    }
  }
}

void menu_keypress(unsigned newkeys) {
  if (spop.alert_msg) {
    // Modal message pop up!
    if (newkeys & (KEY_BUTTA | KEY_BUTTB))
      spop.alert_msg = NULL;
    return;
  }

  if (spop.qpop.message) {
    // Modal confirm/question dialog
    if (newkeys & (KEY_BUTTUP | KEY_BUTTDOWN))
      spop.qpop.option ^= 1;
    else if (newkeys & KEY_BUTTB)
      spop.qpop.message = NULL;   // Exit the modal dialog.
    else if (newkeys & KEY_BUTTA) {
      if (spop.qpop.callback) {
        if (spop.qpop.option && spop.qpop.clear_popup_ok)
          spop.pop_num = POPUP_NONE;
        spop.qpop.callback(spop.qpop.option);
      }
      spop.qpop.message = NULL;   // Exit the modal dialog.
    }
    return;
  }

  if (spop.rtcpop.callback) {
    const uint8_t rmax[] = { 99, 11, 30, 23, 59 };
    if (newkeys & KEY_BUTTLEFT)
      spop.rtcpop.selector = MAX(0, spop.rtcpop.selector - 1);
    if (newkeys & KEY_BUTTRIGHT)
      spop.rtcpop.selector = MIN(4, spop.rtcpop.selector + 1);

    if (newkeys & KEY_BUTTUP)
      ((uint8_t*)&spop.rtcpop.val)[spop.rtcpop.selector]++;
    if (newkeys & KEY_BUTTDOWN)
      ((uint8_t*)&spop.rtcpop.val)[spop.rtcpop.selector] += rmax[spop.rtcpop.selector];
    if (newkeys & (KEY_BUTTUP|KEY_BUTTDOWN)) {
      spop.rtcpop.val.year %= 100;
      spop.rtcpop.val.month %= 12;
      spop.rtcpop.val.day %= 31;
      spop.rtcpop.val.hour %= 24;
      spop.rtcpop.val.mins %= 60;
    }

    if (newkeys & KEY_BUTTB) {
      spop.rtcpop.selector = 0;
      spop.rtcpop.callback = NULL;
    }
    else if (newkeys & KEY_BUTTA) {
      spop.rtcpop.selector = 0;
      spop.rtcpop.callback();
      spop.rtcpop.callback = NULL;
    }
    return;
  }

  if (spop.pop_num) {
    // Close pop-up on B button
    if (newkeys & KEY_BUTTB)
      spop.pop_num = 0;

    switch (spop.pop_num) {
    case POPUP_GBA_LOAD:
    {
      if (newkeys & KEY_BUTTL)
        spop.p.load.submenu = (spop.p.load.submenu + GbaLoadCNT - 1) % GbaLoadCNT;
      if (newkeys & KEY_BUTTR)
        spop.p.load.submenu = (spop.p.load.submenu + 1) % GbaLoadCNT;

      const unsigned maxm[] = {
        GBAInfoCNT,
        GBASaveCNT,
        GBAPatchCNT,
        GBASettCNT,
      };
      unsigned maxsel = maxm[spop.p.load.submenu];

      unsigned psel = spop.p.load.selector;
      if (newkeys & KEY_BUTTUP)
        spop.p.load.selector += maxsel - 1;
      if (newkeys & KEY_BUTTDOWN)
        spop.p.load.selector += 1;


      // Limit selector to its max value
      spop.p.load.selector %= maxsel;

      if (newkeys & KEY_BUTTLEFT) {
        if (spop.p.load.submenu == GbaLoadPopSave) {
          if (spop.p.load.selector == GBASaveMode)
            spop.p.load.use_dsaving = !spop.p.load.use_dsaving && dirsav_avail();

          if (spop.p.load.use_dsaving) {
            if (spop.p.load.selector == GBASaveLoadP)
              spop.p.load.sram_load_type = (spop.p.load.sram_load_type + SaveLoadDSCNT - 1) % SaveLoadDSCNT;
          } else {
            if (spop.p.load.selector == GBASaveLoadP)
              spop.p.load.sram_load_type = (spop.p.load.sram_load_type + SaveLoadCNT - 1) % SaveLoadCNT;
            else if (spop.p.load.selector == GBASaveSaveP)
              spop.p.load.sram_save_type = (spop.p.load.sram_save_type + SaveCNT - 1) % SaveCNT;
          }
        }
        else if (spop.p.load.submenu == GbaLoadPopPatch) {
          if (spop.p.load.selector == GBALoadPatch)
            spop.p.load.patch_type = (spop.p.load.patch_type + PatchOptCNT - 1) % PatchOptCNT;
          else if (spop.p.load.selector == GBAInGameMen)
            spop.p.load.ingame_menu_enabled = !spop.p.load.ingame_menu_enabled;
        }
        else if (spop.p.load.submenu == GbaLoadPopSett) {
          if (spop.p.load.selector == GBASetLdCht)
            spop.p.load.use_cheats = !spop.p.load.use_cheats;
          else if (spop.p.load.selector == GBASetRTCEn)
            spop.p.load.rtc_patch_enabled = !spop.p.load.rtc_patch_enabled;
          else if (spop.p.load.selector == GBASetRememb)
            spop.p.load.write_config = !spop.p.load.write_config;
        }

        // Handle the different cases where the user attempts to select an invalid option.
        if (!spop.p.load.patches_cache_found && spop.p.load.patch_type == PatchEngine)
          spop.p.load.patch_type = PatchDatabase;  // Might be invalid, handled below.
        if (!spop.p.load.patches_datab_found && spop.p.load.patch_type == PatchDatabase)
          spop.p.load.patch_type = PatchNone;

        if (!dirsav_avail())
          spop.p.load.use_dsaving = false;

        // DirSav forces automatic saving
        if (spop.p.load.use_dsaving)
          spop.p.load.sram_save_type = SaveDirect;
        else if (spop.p.load.sram_save_type == SaveDirect)
          spop.p.load.sram_save_type = autosave_default ? SaveReboot : SaveDisable;

        // If DS is selected, do not allow manual mode.
        if (spop.p.load.sram_load_type == SaveLoadDisable && spop.p.load.use_dsaving)
          spop.p.load.sram_load_type = SaveLoadSav;
        // If no .sav is available, do not allow that option!
        if (spop.p.load.sram_load_type == SaveLoadSav && !spop.p.load.savefile_found)
          spop.p.load.sram_load_type = spop.p.load.use_dsaving ? SaveLoadReset : SaveLoadDisable;
      }
      if (newkeys & KEY_BUTTRIGHT) {
        if (spop.p.load.submenu == GbaLoadPopSave) {
          if (spop.p.load.selector == GBASaveMode)
            spop.p.load.use_dsaving = !spop.p.load.use_dsaving && dirsav_avail();

          if (spop.p.load.use_dsaving) {
            if (spop.p.load.selector == GBASaveLoadP)
              spop.p.load.sram_load_type = (spop.p.load.sram_load_type + 1) % SaveLoadDSCNT;
          } else {
            if (spop.p.load.selector == GBASaveLoadP)
              spop.p.load.sram_load_type = (spop.p.load.sram_load_type + 1) % SaveLoadCNT;
            else if (spop.p.load.selector == GBASaveSaveP)
              spop.p.load.sram_save_type = (spop.p.load.sram_save_type + 1) % SaveCNT;
          }
        }
        else if (spop.p.load.submenu == GbaLoadPopPatch) {
          if (spop.p.load.selector == GBALoadPatch)
            spop.p.load.patch_type = (spop.p.load.patch_type + 1) % PatchOptCNT;
          else if (spop.p.load.selector == GBAInGameMen)
            spop.p.load.ingame_menu_enabled = !spop.p.load.ingame_menu_enabled;
        }
        else if (spop.p.load.submenu == GbaLoadPopSett) {
          if (spop.p.load.selector == GBASetLdCht)
            spop.p.load.use_cheats = !spop.p.load.use_cheats;
          else if (spop.p.load.selector == GBASetRTCEn)
            spop.p.load.rtc_patch_enabled = !spop.p.load.rtc_patch_enabled;
          else if (spop.p.load.selector == GBASetRememb)
            spop.p.load.write_config = !spop.p.load.write_config;
        }

        // If the database has no entry, then do not let the user select that mode.
        if (!spop.p.load.patches_datab_found && spop.p.load.patch_type == PatchDatabase)
          spop.p.load.patch_type = PatchEngine;  // Might be invalid, handled below.
        if (!spop.p.load.patches_cache_found && spop.p.load.patch_type == PatchEngine)
          spop.p.load.patch_type = PatchNone;

        if (!dirsav_avail())
          spop.p.load.use_dsaving = false;

        // DirSav forces automatic saving
        if (spop.p.load.use_dsaving)
          spop.p.load.sram_save_type = SaveDirect;
        else if (spop.p.load.sram_save_type == SaveDirect)
          spop.p.load.sram_save_type = autosave_default ? SaveReboot : SaveDisable;

        // If DS is selected, do not allow manual mode.
        if (spop.p.load.sram_load_type == SaveLoadDisable && spop.p.load.use_dsaving)
          spop.p.load.sram_load_type = SaveLoadSav;
        // If no .sav is available, do not allow that option!
        if (spop.p.load.sram_load_type == SaveLoadSav && !spop.p.load.savefile_found)
          spop.p.load.sram_load_type = SaveLoadReset;
      }

      // Disable ingame-menu if not available.
      if (!ingame_menu_avail())
        spop.p.load.ingame_menu_enabled = false;

      // If no RTC patches are available, force them to false.
      if (!spop.p.load.patches_datab.rtc_ops)
        spop.p.load.rtc_patch_enabled = false;

      // Disable cheat loading if no cheats are avail, or IGM is disabled
      if (!spop.p.load.cheats_found || !spop.p.load.ingame_menu_enabled)
        spop.p.load.use_cheats = false;

      if (newkeys & KEY_BUTTA) {
        if (spop.p.load.submenu == GbaLoadPopSett && spop.p.load.selector == GBASetRTCEn && spop.p.load.rtc_patch_enabled) {
          void accept_rtc() {
            spop.p.load.rtcval = spop.rtcpop.val;
          }
          if (spop.p.load.rtc_patch_enabled) {
            spop.rtcpop.val = spop.p.load.rtcval;
            spop.rtcpop.callback = accept_rtc;
          }
        }
        else if (spop.p.load.submenu == GbaLoadPopPatch && spop.p.load.selector == GBAPatchGen) {
          generate_patches_progress(spop.p.load.romfn, spop.p.load.romfs);
          spop.alert_msg = msgs[lang_id][MSG_PATCHGEN_OK];
          // Try/Load the just-generated patches.
          spop.p.load.patches_cache_found = load_cached_patches(spop.p.load.romfn, &spop.p.load.patches_cache);
        }
        else if (GBALoadButt == spop.p.load.selector) {
          // Insert the ROM into the recent list (or move it around). Flush to disk!
          if (recent_menu)
            insert_recent_flush(spop.p.load.romfn);

          // Honor load.patch_type.
          const t_patch *p = spop.p.load.patch_type == PatchDatabase ? &spop.p.load.patches_datab :
                             spop.p.load.patch_type == PatchEngine   ? &spop.p.load.patches_cache :
                                                                       NULL;
          EnumSavetype st = p ? p->save_mode : SaveTypeNone;

          // Save settings if the option was enabled
          if (spop.p.load.write_config) {
            t_rom_settings savedcfg = {
              .rtcval = spop.p.load.rtcval,
              .patch_policy = spop.p.load.patch_type,
              .use_dsaving = spop.p.load.use_dsaving,
              .use_igm = spop.p.load.ingame_menu_enabled,
              .use_cheats = spop.p.load.use_cheats,
              .use_rtc = spop.p.load.rtc_patch_enabled
            };
            save_rom_settings(spop.p.load.romfn, &savedcfg);
          }

          // Prepare the savegame (load and store stuff, directsave...)
          t_dirsave_info dsinfo;
          unsigned errsave = prepare_savegame(
            spop.p.load.sram_load_type, spop.p.load.sram_save_type,
            st, &dsinfo, spop.p.load.savefn);
          if (errsave) {
            unsigned errmsg = (errsave == ERR_SAVE_BADSAVE)   ? MSG_ERR_SAVERD :
                              (errsave == ERR_SAVE_CANTALLOC) ? MSG_ERR_SAVEPR :
                              (errsave == ERR_SAVE_BADARG)    ? MSG_ERR_SAVEIT :
                                                                MSG_ERR_SAVEWR;
            spop.alert_msg = msgs[lang_id][errmsg];
            return;
          }

          unsigned err = load_gba_rom(
            spop.p.load.romfn, spop.p.load.romfs,
            &spop.p.load.romh, p,
            spop.p.load.sram_save_type == SaveDirect ? &dsinfo : NULL,
            spop.p.load.ingame_menu_enabled,
            spop.p.load.rtc_patch_enabled ? &spop.p.load.rtcval : NULL,
            spop.p.load.use_cheats ? spop.p.load.cheats_size : 0,
            loadrom_progress);
          if (err) {
            // Show any errors that might have happened!
            spop.alert_msg = msgs[lang_id][MSG_ERR_READ];
            // TODO: We cannot (in many cases) continue since we trash the SDRAM!
          }
        }
      }

      if (psel != spop.p.load.selector)
        spop.p.load.anim = 0;

      break;
    }

    case POPUP_SAVFILE:
      if (newkeys & KEY_BUTTUP)
        spop.p.savopt.selector = MAX(0, spop.p.savopt.selector - 1);
      if (newkeys & KEY_BUTTDOWN)
        spop.p.savopt.selector = MIN(SavMAX, spop.p.savopt.selector + 1);

      if (newkeys & KEY_BUTTA) {
        switch (spop.p.savopt.selector) {
        case SaveWrite:
          if (write_save_sram(spop.p.savopt.savfn))
            spop.alert_msg = msgs[lang_id][MSG_SAVOPT_MSG0];
          else
            spop.alert_msg = msgs[lang_id][MSG_SAVOPT_MSG_WERR];
          break;
        case SavLoad:
          if (load_save_sram(spop.p.savopt.savfn))
            spop.alert_msg = msgs[lang_id][MSG_SAVOPT_MSG1];
          else
            spop.alert_msg = msgs[lang_id][MSG_SAVOPT_MSG_RERR];
          break;
        case SavClear:
          if (wipe_sav_file(spop.p.savopt.savfn))
            spop.alert_msg = msgs[lang_id][MSG_SAVOPT_MSG2];
          else
            spop.alert_msg = msgs[lang_id][MSG_SAVOPT_MSG_WERR];
          break;
        case SavQuit:
          spop.pop_num = 0;
          break;
        };
      }
      break;

    case POPUP_FWFLASH:
      if ((newkeys & FLASH_GO_KEYS) == FLASH_GO_KEYS)
        start_flash_update(spop.p.update.fn, spop.p.update.fw_size, spop.p.update.issfw);
      else if (newkeys & KEY_BUTTB)
        spop.pop_num = 0;

      break;
    };
  } else {
    // Menu change via trigger buttons
    int mintab = (recent_menu && smenu.recent.maxentries) ? MENUTAB_RECENT : MENUTAB_ROMBROWSE;
    if (newkeys & KEY_BUTTL)
      smenu.menu_tab = MAX((int)smenu.menu_tab - 1, mintab);
    else if (newkeys & KEY_BUTTR)
      smenu.menu_tab = MIN(smenu.menu_tab + 1, MENUTAB_MAX - 1);

    if (newkeys & (KEY_BUTTL | KEY_BUTTR | KEY_BUTTUP | KEY_BUTTDOWN))
      smenu.anim_state = 0;

    switch (smenu.menu_tab) {
    case MENUTAB_RECENT:
      if (smenu.recent.maxentries) {
        if (newkeys & KEY_BUTTUP)
          smenu.recent.selector = MAX(0, smenu.recent.selector - 1);
        else if (newkeys & KEY_BUTTDOWN)
          smenu.recent.selector = MIN(smenu.recent.maxentries - 1, smenu.recent.selector + 1);
        if (newkeys & KEY_BUTTLEFT) {
          smenu.recent.selector = MAX(0, smenu.recent.selector - RECENT_ROWS);
          smenu.recent.seloff   = MAX(0, smenu.recent.seloff - RECENT_ROWS);
        }
        else if (newkeys & KEY_BUTTRIGHT) {
          smenu.recent.selector = MIN(smenu.recent.maxentries - 1, smenu.recent.selector + RECENT_ROWS);
          smenu.recent.seloff   = MIN(smenu.recent.maxentries - 1, smenu.recent.seloff   + RECENT_ROWS);
        }
        if (newkeys & KEY_BUTTA) {
          t_rentry *e = &sdr_state->rentries[smenu.recent.selector];
          // stat() the file since we need the size, and validate that it exists!
          FILINFO info;
          FRESULT res = f_stat(e->fpath, &info);
          if (res == FR_OK) {
            browser_open(e->fpath, info.fsize);
          } else {
            spop.alert_msg = msgs[lang_id][MSG_ERR_READ];
          }
        }
        else if (newkeys & KEY_BUTTSEL) {
          void sram_battery_test_callback(bool confirm) {
            if (confirm) {
              delete_recent_flush(smenu.recent.selector);
            }
          }
          spop.qpop.message = msgs[lang_id][MSG_Q4_DELREC];
          spop.qpop.default_button = msgs[lang_id][MSG_Q_NO];
          spop.qpop.confirm_button = msgs[lang_id][MSG_Q_YES];
          spop.qpop.option = 0;
          spop.qpop.callback = sram_battery_test_callback;
          spop.qpop.clear_popup_ok = false;
        }
      }

      if (smenu.recent.selector < smenu.recent.seloff)
        smenu.recent.seloff = smenu.recent.selector;
      else if (smenu.recent.selector >= smenu.recent.seloff + RECENT_ROWS)
        smenu.recent.seloff = smenu.recent.selector - RECENT_ROWS + 1;

      break;
    case MENUTAB_ROMBROWSE:
      // Move menu up and down
      if (smenu.browser.maxentries) {
        if (newkeys & KEY_BUTTUP)
          smenu.browser.selector = MAX(0, smenu.browser.selector - 1);
        if (newkeys & KEY_BUTTDOWN)
          smenu.browser.selector = MIN(smenu.browser.maxentries - 1, smenu.browser.selector + 1);
        if (newkeys & KEY_BUTTLEFT) {
          smenu.browser.selector = MAX(0, smenu.browser.selector - BROWSER_ROWS);
          smenu.browser.seloff   = MAX(0, smenu.browser.seloff - BROWSER_ROWS);
        }
        if (newkeys & KEY_BUTTRIGHT) {
          smenu.browser.selector = MIN(smenu.browser.maxentries - 1, smenu.browser.selector + BROWSER_ROWS);
          smenu.browser.seloff   = MIN(smenu.browser.maxentries - 1, smenu.browser.seloff   + BROWSER_ROWS);
        }
        // Move into a new dir and/or open a file
        if (newkeys & KEY_BUTTA) {
          t_centry *e = sdr_state->fileorder[smenu.browser.selector];
          if (e->isdir) {
            strcat(smenu.browser.cpath, e->fname);
            strcat(smenu.browser.cpath, "/");
            browser_reload();
          } else {
            char path[MAX_FN_LEN];
            strcpy(path, smenu.browser.cpath);
            strcat(path, e->fname);
            browser_open(path, e->filesize);
          }
        }
        else if (newkeys & KEY_BUTTSEL) {
          // Shows a file management menu.
          t_centry *e = sdr_state->fileorder[smenu.browser.selector];
          if (!e->isdir) {
            void remove_file_action(bool confirm) {
              char tmpfn[MAX_FN_LEN];
              t_centry *e = sdr_state->fileorder[smenu.browser.selector];
              strcpy(tmpfn, smenu.browser.cpath);
              strcat(tmpfn, e->fname);

              if (confirm) {
                if (FR_OK != f_unlink(tmpfn))
                  spop.alert_msg = msgs[lang_id][MSG_ERR_DELFILE];
                else
                  spop.alert_msg = msgs[lang_id][MSG_OK_DELFILE];

                browser_reload();   // Force reload so the file disappears! TODO: maintain scroll position?
              }
            }
            spop.qpop.message = msgs[lang_id][MSG_Q0_DELFILE];
            spop.qpop.default_button = msgs[lang_id][MSG_Q_NO];
            spop.qpop.confirm_button = msgs[lang_id][MSG_Q_YES];
            spop.qpop.option = 0;
            spop.qpop.callback = remove_file_action;
            spop.qpop.clear_popup_ok = true;
          }
        }
      }
      if (newkeys & KEY_BUTTB) {
        // Try to go up in the dir structure
        if (movedir_up())
          browser_reload();
      }

      // Selector was updated, figure out how we update the menu params so it
      // can be rendered properly.
      if (smenu.browser.selector < smenu.browser.seloff)
        smenu.browser.seloff = smenu.browser.selector;
      else if (smenu.browser.selector >= smenu.browser.seloff + BROWSER_ROWS)
        smenu.browser.seloff = smenu.browser.selector - BROWSER_ROWS + 1;
    break;

    case MENUTAB_UILANG:
      if (newkeys & KEY_BUTTUP)
        smenu.uiset.selector = MAX(0, smenu.uiset.selector - 1);
      if (newkeys & KEY_BUTTDOWN)
        smenu.uiset.selector = MIN(UI_SETTINGS_ITEMS_COUNT, smenu.uiset.selector + 1);
      if (smenu.uiset.selector < UI_SETTINGS_ITEMS_COUNT) {
        if (newkeys & KEY_BUTTLEFT) {
          const UiMenuItem *item = &ui_settings_items[smenu.uiset.selector];
          if (item->from_bool != NULL) {
            *item->from_bool ^= 1;
          } else if (item->from_int != NULL && *item->from_int > 0) {
            *item->from_int = *item->from_int - 1;
          }
          if (item->on_change) {
            item->on_change();
          }
        }
        if (newkeys & KEY_BUTTRIGHT) {
          const UiMenuItem *item = &ui_settings_items[smenu.uiset.selector];
          if (item->from_bool != NULL) {
            *item->from_bool ^= 1;
          } else if (item->from_int != NULL) {
            *item->from_int = MIN(item->from_int_max - 1, *item->from_int + 1);
          }
          if (item->on_change) {
            item->on_change();
          }
        }
      }

      if (newkeys & KEY_BUTTA && smenu.uiset.selector == UI_SETTINGS_ITEMS_COUNT) {
        smenu.uiset.selector = 0;
        if (save_ui_settings())
          spop.alert_msg = msgs[lang_id][MSG_OK_SETSAVE];
        else
          spop.alert_msg = msgs[lang_id][MSG_ERR_SETSAVE];
      }

      break;

    case MENUTAB_TOOLS:
      if (newkeys & KEY_BUTTUP)
        smenu.tools.selector = MAX(0, smenu.tools.selector - 1);
      if (newkeys & KEY_BUTTDOWN)
        smenu.tools.selector = MIN(ToolsMAX, smenu.tools.selector + 1);

      if (newkeys & KEY_BUTTA) {
        if (smenu.tools.selector == ToolsSDRAMTest) {
          // Performs a test on the SRAM/SDRAM, ensure they are fine.
          set_supercard_mode(MAPPED_SDRAM, true, false);

          if (sdram_test(loadrom_progress_abort))
            spop.alert_msg = msgs[lang_id][MSG_BAD_SDRAM];
          else
            spop.alert_msg = msgs[lang_id][MSG_GOOD_RAM];

          set_supercard_mode(MAPPED_SDRAM, true, true);
        }
        if (smenu.tools.selector == ToolsSRAMTest) {
          if (sram_test())
            spop.alert_msg = msgs[lang_id][MSG_BAD_SRAM];
          else
            spop.alert_msg = msgs[lang_id][MSG_GOOD_RAM];
        }
        else if (smenu.tools.selector == ToolsBatteryTest) {
          // Go ahead and fill in SRAM with a pattern.
          spop.qpop.message = msgs[lang_id][MSG_Q2_SRAMTST];
          spop.qpop.default_button = msgs[lang_id][MSG_Q_NO];
          spop.qpop.confirm_button = msgs[lang_id][MSG_Q_YES];
          spop.qpop.option = 0;
          spop.qpop.callback = sram_battery_test_callback;
          spop.qpop.clear_popup_ok = true;
        }
        else if (smenu.tools.selector == ToolsSDBench) {
          slowsd = use_slowsd;
          int ret = sdbench_read(loadrom_progress_abort);
          slowsd = true;
          if (ret < 0)
            spop.alert_msg = msgs[lang_id][MSG_ERR_GENERIC];
          else {
            unsigned speed = 8*1024*1024 / (unsigned)ret;
            npf_snprintf(smenu.info.tstr, sizeof(smenu.info.tstr), msgs[lang_id][MSG_BENCHSPD], speed);
            spop.alert_msg = smenu.info.tstr;
          }
        }
        else if (smenu.tools.selector == ToolsFlashBak) {
          // Backup the flash contents to a file.
          if (dump_flashmem_backup())
            spop.alert_msg = msgs[lang_id][MSG_FLASH_READOK];
          else
            spop.alert_msg = msgs[lang_id][MSG_ERR_GENERIC];
        }
      }
      break;

    case MENUTAB_SETTINGS:
      if (newkeys & KEY_BUTTUP)
        smenu.set.selector = MAX(0, smenu.set.selector - 1);
      if (newkeys & KEY_BUTTDOWN)
        smenu.set.selector = MIN(SettMAX, smenu.set.selector + 1);
      if (newkeys & KEY_BUTTLEFT) {
        if (smenu.set.selector == SettHotkey)
          hotkey_combo = (hotkey_combo + hotkey_listcnt - 1) % hotkey_listcnt;
        else if (smenu.set.selector == SettSaveLoc)
          save_path_default = (save_path_default + SaveDirCNT - 1) % SaveDirCNT;
        else if (smenu.set.selector == SettStateLoc)
          state_path_default = (state_path_default + StateDirCNT - 1) % StateDirCNT;
        else if (smenu.set.selector == SettSaveBkp)
          backup_sram_default = backup_sram_default ? backup_sram_default - 1 : 0;
        else if (smenu.set.selector == DefsPatchEng)
          patcher_default = (patcher_default + PatchTotalCNT - 1) % PatchTotalCNT;
      }
      if (newkeys & KEY_BUTTRIGHT) {
        if (smenu.set.selector == SettHotkey)
          hotkey_combo = (hotkey_combo + 1) % hotkey_listcnt;
        else if (smenu.set.selector == SettSaveLoc)
          save_path_default = (save_path_default + 1) % SaveDirCNT;
        else if (smenu.set.selector == SettStateLoc)
          state_path_default = (state_path_default + 1) % StateDirCNT;
        else if (smenu.set.selector == SettSaveBkp)
          backup_sram_default = MIN(16, backup_sram_default + 1);
        else if (smenu.set.selector == DefsPatchEng)
          patcher_default = (patcher_default + 1) % PatchTotalCNT;
      }
      if (newkeys & (KEY_BUTTLEFT | KEY_BUTTRIGHT)) {
        if (smenu.set.selector == SettBootType)
          boot_bios_splash ^= 1;
        else if (smenu.set.selector == SettCheatEn)
          enable_cheats ^= 1;
        else if (smenu.set.selector == DefsGamMenu)
          ingamemenu_default ^= 1;
        else if (smenu.set.selector == DefsRTCEnb)
          rtcpatch_default ^= 1;
        else if (smenu.set.selector == DefsLoadPol)
          autoload_default ^= 1;
        else if (smenu.set.selector == DefsSavePol)
          autosave_default ^= 1;
        else if (smenu.set.selector == DefsPrefDS)
          autosave_prefer_ds ^= 1;
        else if (smenu.set.selector == SettFastSD)
          use_slowsd ^= 1;
        else if (smenu.set.selector == SettFastEWRAM)
          use_fastew = fastew ? (use_fastew ^ 1) : 0;
      }

      if (newkeys & KEY_BUTTA && smenu.set.selector == DefsRTCVal) {
        void accept_rtc() {
          rtcvalue_default = spop.rtcpop.val;
        }
        spop.rtcpop.val = rtcvalue_default;
        spop.rtcpop.callback = accept_rtc;
      }
      if (newkeys & KEY_BUTTA && smenu.set.selector == SettSave) {
        smenu.set.selector = 0;
        if (save_settings())
          spop.alert_msg = msgs[lang_id][MSG_OK_SETSAVE];
        else
          spop.alert_msg = msgs[lang_id][MSG_ERR_SETSAVE];
      }

      break;

    case MENUTAB_INFO:
      if (newkeys & KEY_BUTTA)
        smenu.info.selector = (smenu.info.selector + 1) % 3;
      if ((newkeys & FLASH_UNLOCK_KEYS) == FLASH_UNLOCK_KEYS)
        enable_flashing = true;
      break;
    };
  }
}

