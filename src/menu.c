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
#include "flash_mgr.h"
#include "sha256.h"
#include "supercard_driver.h"

#include "res/icons.h"
#include "res/logo.h"

extern t_card_info sd_info;
extern bool fastew;
extern bool slowsd;

enum {
  MENUTAB_RECENT,        // Browses recently loaded ROMs (can be disabled / hidden)
  MENUTAB_ROMBROWSE,     // Browses ROMs and launches games.
  #ifdef SUPPORT_NORGAMES
  MENUTAB_NORBROWSE,     // Browses Flash games and launches them.
  #endif
  MENUTAB_SETTINGS,      // General settings / defaults
  MENUTAB_UILANG,        // UI / Language settings
  MENUTAB_TOOLS,         // Tools (advaned menu)
  MENUTAB_INFO,          // Info / About / Updater?
  MENUTAB_MAX,
};

#define ANIM_INITIAL_WAIT     128    // Intial wait (in anim cycles)

enum {
  POPUP_NONE,
  POPUP_GBA_LOAD,              // Load a GBA ROM
  POPUP_SAVFILE,               // Load/Store a SAV file
  POPUP_FWFLASH,               // Flash a new firmware image
  POPUP_FILE_MGR,              // Write ROM to flash, delete, hide/unhide...
#ifdef SUPPORT_NORGAMES
  POPUP_GBA_NORWRITE,          // Write a GBA ROM to NOR
  POPUP_GBA_NORLOAD,           // Launch a NOR game
#endif
};

#define BROWSER_MAXFN_CNT     (16*1024)
#define RECENT_MAXFN_CNT          (200)
#define BROWSER_ROWS                 8
#define RECENT_ROWS                  9
#define NORGAMES_ROWS                8

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
  UiSetTheme = 0,
  UiSetLang  = 1,
  UiSetRect  = 2,
  UiSetASpd  = 3,
  UiSetHid   = 4,
  UiSetSave  = 5,
  UiSetMAX   = 5,
};

enum {
  ToolsSDRAMTest = 0,
  ToolsSRAMTest,
  ToolsBatteryTest,
  ToolsSDBench,
  ToolsFlashBak,
  #ifdef SUPPORT_NORGAMES
  ToolsFlashClr,
  #endif
  ToolsMAX,
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
  DefsRTCSpeed = 14,
  DefsLoadPol  = 15,
  DefsSavePol  = 16,
  DefsPrefDS   = 17,
  SettSave     = 18,
  SettMAX      = 18,
};

enum {
  DefsSave     = 4,
  DefsMAX      = 4,
};

enum {
  GbaLoadPopInfo  = 0,
  GbaLoadPopLoadS = 1,
  GbaLoadPopPatch = 2,
  GbaLoadCNT      = 3,

  GbaNorWrPatch   = 1,
  GbaNorWrCNT     = 2,

  GbaNorLoad      = 1,
  GbaNorLoadCNT   = 2,
};

enum {
  GBAInfoCNT   = 1,
  GBALoadButt  = 0,

  GBALdSetCNT    = 5,
  GBALdSetLoadP  = 0,
  GBALdSetSaveP  = 1,
  GBALdSetRTC    = 2,
  GBALdSetCheats = 3,
  GBALdRemember  = 4,

  GBAPatchCNT  = 5,
  GBALoadPatch = 0,
  GBASavePatch = 1,
  GBAInGameMen = 2,
  GBARTCPatch  = 3,
  GBAPatchGen  = 4,
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

enum {
  FiMgrDelete,
  FiMgrHide,
#ifdef SUPPORT_NORGAMES
  FiMgrWriteNOR,
#endif
  FiMgrCNT
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
  { RGB2GBA(0x308855), RGB2GBA(0x88aa99), RGB2GBA(0x000000), RGB2GBA(0x778888), RGB2GBA(0x777777), RGB2GBA(0x606060) },
};
#define THEME_COUNT (sizeof(themes) / sizeof(themes[0]))

typedef struct {
  // ROM information
  char romfn[MAX_FN_LEN];             // File to load/write
  uint32_t romfs;                     // File ROM size
  char gcode[5];                      // ASCII sanitized game code.
  t_rom_header romh;                  // ROM header (for info purposes)
  // Patching info
  t_patch patches_datab;              // Loaded patches (from DB)
  t_patch patches_cache;              // Loaded patches (from patch engine's cache)
  bool patches_datab_found;           // Whether we had a patch match in the database
  bool patches_cache_found;           // Same but for the patch cache
  // Patching configuration
  t_patch_policy patch_type;          // Patching type
  bool use_dsaving;                   // Whether we use direct-saving mode
  bool ingame_menu_enabled;           // Enable the in-game menu.
  bool rtc_patch_enabled;             // Patch for RTC workarounds.
} t_load_gba_info;

typedef struct {
  // Save read/write policies and info
  t_sram_load_policy sram_load_type;  // SRAM loading policy
  t_sram_save_policy sram_save_type;  // SRAM auto-saving policy
  char savefn[MAX_FN_LEN];            // Save file path.
  bool savefile_found;                // Whether there's a .sav file.
  // RTC config
  uint32_t rtcval;                    // Initial RTC value.
  // Cheats policy
  bool use_cheats;                    // Whether we want to load cheats to use them.
  bool cheats_found;                  // Whether there's a cheats file (not parsed tho!)
  unsigned cheats_size;               // Size of the cheat buffer
  char cheatsfn[MAX_FN_LEN];          // Cheats file path.
} t_load_gba_lcfg;

typedef void (*t_mrender_fn)(volatile uint8_t *frame);
typedef void (*t_mkeyupd_fn)(unsigned newkeys);

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
    int dispentries;              // Maximum number of visible entries (filtered)
    uint16_t selhist[16];         // History of directory offsets
  } browser;

  // Flash ROM browser state
  struct {
    int selector;                 // Pointed file offset
    int seloff;                   // Entry at the top of the list
    uint8_t maxentries;           // Total file/dir count in current dir
    uint8_t usedblks, freeblks;   // NOR usage info
  } fbrowser;

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

  uint8_t pop_num;                // Current pop-up in display
  char submenu;                   // Which submenu tab we are in (if any)
  char selector;                  // Option selector (if any)
  unsigned anim;                  // Animation state

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
    t_dec_date val;
    int selector;
    void (*callback)();                   // Function to call on "save"
  } rtcpop;

  union {
    // GBA launch ROM pop up menu
    struct {
      t_load_gba_info i;                  // ROM/Patch info and patch policy.
      t_load_gba_lcfg l;                  // ROM loading info and settings;
    } load;

    // Write GBA game to NOR memory
    struct {
      t_load_gba_info i;                  // ROM/Patch info and patch policy.
    } norwr;
    // Launch GBA game from NOR memory
    struct {
      t_load_gba_lcfg l;                  // ROM loading info and settings;
    } norld;

    // Save file menu (.sav files)
    struct {
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
//  - Scratch area 2MiB (for FW updates)
//  - File list order (~64KiB)
//  - Browser file information (~13MB)
//  - Recently played ROMs table (~64KiB)
//  - Font data (placed by the bootloader at the 15..16MB range)
// At the end of the SDRAM, ro-data can be loaded by the loader.
#define scratch_mem_size (2*1024*1024)
typedef struct {
  uint8_t scratch[scratch_mem_size];
  t_centry *fileorder[BROWSER_MAXFN_CNT];
  t_centry fentries[BROWSER_MAXFN_CNT];
  t_rentry rentries[RECENT_MAXFN_CNT];
  t_reg_entry_max nordata;
} t_sdram_state;

_Static_assert (sizeof(t_sdram_state) <= 14.5*1024*1024, "scratch SDRAM doesn't exceed 14.5MB");

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

__attribute__((noinline))
int romsort(const void *a, const void *b) {
  const t_flash_game_entry *ca = (t_flash_game_entry*)a;
  const t_flash_game_entry *cb = (t_flash_game_entry*)b;

  return strcasecmp(&ca->game_name[ca->bnoffset], &cb->game_name[cb->bnoffset]);
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

  const unsigned fsize = flashinfo.size ? flashinfo.size : FW_MAX_SIZE_KB*1024;
  for (unsigned i = 0; i < fsize; i += 4*1024) {
    const uint8_t *faddr = (uint8_t*)(ROM_FLASHFIRMW_ADDR + i);

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

    loadrom_progress(i >> 10, fsize >> 10);
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


static const t_patch * get_game_patch(const t_load_gba_info *info) {
  return info->patch_type == PatchDatabase && info->patches_datab_found ? &info->patches_datab :
         info->patch_type == PatchEngine   && info->patches_cache_found ? &info->patches_cache : NULL;
}

bool ingame_menu_avail_sdram(const t_load_gba_info *info) {
  const t_patch *p = get_game_patch(info);
  // Necessary size to load the IGM (+fonts +cheats)
  const unsigned igm_reqsz = ROUND_UP2(ingame_menu_payload.menu_rsize + font_block_size() + spop.p.load.l.cheats_size, 1024);

  // If the ROM is too big, must use some hole to load the menu.
  if (info->romfs > MAX_GBA_ROM_SIZE - igm_reqsz) {
    // Discard holes that are too small, or not well formed.
    if (!p || p->hole_size < igm_reqsz || p->hole_addr + p->hole_size > info->romfs)
      return false;   // Too big to fit the menu!
  }

  // Check if the patches exist and have proper IRQ support.
  return p && p->irqh_ops > 0;
}

bool ingame_menu_avail_flash(const t_load_gba_info *info) {
  const t_patch *p = get_game_patch(info);

  // Checks if the ROM is small enough so the last 4MiB block can be remapped.
  if (info->romfs > MAX_GBA_ROM_SIZE - NOR_BLOCK_SIZE) {
    // Otherwise find a gap to flash on NOR our tiny payload
    if (!p || p->hole_size < DIRSAVE_REQ_SPACE || p->hole_addr + p->hole_size > info->romfs)
      return false;   // Too big to fit!
  }

  // Check if the patches exist and have proper IRQ support.
  return p && p->irqh_ops > 0;
}

// Calculates whether DirectSaving can be used given some information.
bool dirsav_avail_sdram(const t_load_gba_info *info) {
  const t_patch *p = get_game_patch(info);

  // Check if there's enough space for it! (Placing it at the end).
  if (info->romfs > MAX_GBA_ROM_SIZE - DIRSAVE_REQ_SPACE) {
    if (!p || p->hole_size < DIRSAVE_REQ_SPACE || p->hole_addr + p->hole_size > info->romfs)
      return false;   // Too big to fit!
  }

  return (p && supports_directsave(p->save_mode));
}

bool dirsav_avail_flash(const t_load_gba_info *info) {
  const t_patch *p = get_game_patch(info);

  // Checks if the ROM is small enough so the last 4MiB block can be remapped.
  if (info->romfs > MAX_GBA_ROM_SIZE - NOR_BLOCK_SIZE) {
    // Otherwise find a gap to flash on NOR our tiny payload
    if (!p || p->hole_size < DIRSAVE_REQ_SPACE || p->hole_addr + p->hole_size > info->romfs)
      return false;   // Too big to fit!
  }

  return (p && supports_directsave(p->save_mode));
}

bool rtcemu_avail(const t_load_gba_info *info) {
  const t_patch *p = get_game_patch(info);
  return (p && p->rtc_ops);
}

static bool prepare_gba_info(
  t_load_gba_info *info, const t_rom_load_settings *st,
  const char *fn, uint32_t fs,
  bool load_sdram
) {
  // Pre-load ROM header
  if (preload_gba_rom(fn, fs, &info->romh))
    return false;

  // Fill/copy ROM info.
  if (fn != info->romfn)
    strcpy(info->romfn, fn);
  info->romfs = fs;

  // Sanitize the game code for display
  for (unsigned i = 0; i < 4; i++)
    info->gcode[i] = isascii(info->romh.gcode[i]) ? info->romh.gcode[i] : 0x1A;
  info->gcode[4] = 0;

  // Look up patches, have them handy.
  uint8_t gamecode[5] = {
    info->romh.gcode[0], info->romh.gcode[1],
    info->romh.gcode[2], info->romh.gcode[3],
    info->romh.version
  };
  set_supercard_mode(MAPPED_SDRAM, true, false);
  info->patches_datab_found = patchmem_lookup(gamecode, (uint8_t*)ROM_PATCHDB_U8, &info->patches_datab);
  set_supercard_mode(MAPPED_SDRAM, true, true);

  // Attempt to load any existing patch and check also the PE cache dir.
  info->patches_cache_found = load_rom_patches(fn, &info->patches_cache);
  if (!info->patches_cache_found)
    info->patches_cache_found = load_cached_patches(fn, &info->patches_cache);

  // If PatchAuto is selected, resolve it. Downgrade if not found.
  if (st->patch_policy == PatchAuto) {
    if (info->patches_cache_found)
      info->patch_type = PatchEngine;      // Try existing patches
    else if (info->patches_datab_found)
      info->patch_type = PatchDatabase;    // Try the database then
    else
      info->patch_type = PatchNone;
  }
  // Downgrade to no patches if the specified was not found.
  else if (st->patch_policy == PatchDatabase) {
    if (!info->patches_datab_found)
      info->patch_type = PatchNone;
  }
  else if (st->patch_policy == PatchEngine) {
    if (!info->patches_cache_found)
      info->patch_type = PatchNone;
  }
  else
    info->patch_type = st->patch_policy;

  // Fill defaults as requested if possible.
  bool allowds = load_sdram ? dirsav_avail_sdram(info) : dirsav_avail_flash(info);
  bool allowigm = load_sdram ? ingame_menu_avail_sdram(info) : ingame_menu_avail_flash(info);

  info->rtc_patch_enabled = st->use_rtc && rtcemu_avail(info);
  info->use_dsaving = st->use_dsaving && allowds;
  info->ingame_menu_enabled = st->use_igm && allowigm;

  return true;
}

static void prepare_gba_cheats(const char *gcode, uint8_t ver, t_load_gba_lcfg *data, const char *fn, bool prefer_cheats) {
  // Attempt to find a cheat file if cheats are enabled.
  data->cheats_size = 0;
  data->cheats_found = false;
  if (enable_cheats) {
    strcpy(data->cheatsfn, fn);
    replace_extension(data->cheatsfn, ".cht");
    data->cheats_found = check_file_exists(data->cheatsfn);
    if (!data->cheats_found) {
      // Create a path using the game ID and version.
      npf_snprintf(data->cheatsfn, sizeof(data->cheatsfn), CHEATS_PATH "%c%c%c%c-%02x.cht",
                   gcode[0], gcode[1], gcode[2], gcode[3], ver);
      data->cheats_found = check_file_exists(data->cheatsfn);

      // Load the cheats into memory if enabled.
      if (data->cheats_found) {
        // Load the cheats to the ROM area, just after the font pack. This is for easier relocation.
        uint8_t *cheat_area = (uint8_t*)(ROM_FONTBASE_U8 + font_block_size());
        unsigned max_area = 1024*1024 - font_block_size();    // 1MB is reserved at the ROM end.
        int cheatsz = open_read_cheats(cheat_area, max_area, data->cheatsfn);
        if (cheatsz < 0)
          data->cheats_found = false;
        else
          data->cheats_size = cheatsz;
      }
    }
  }
  data->use_cheats = enable_cheats && data->cheats_found && prefer_cheats;
}

static void prepare_gba_settings(t_load_gba_lcfg *data, bool uses_dsaving, uint32_t rtcts, bool game_no_save, const char *fn) {
  // Calculate the .sav file name, and check its existance.
  sram_template_filename_calc(fn, ".sav", data->savefn);
  data->savefile_found = check_file_exists(data->savefn);

  // Use default settings (and file existance) to fill in default choice.
  // DirectSaving enabled overrides the other settings.
  if (uses_dsaving) {
    data->sram_load_type = data->savefile_found ? SaveLoadSav : SaveLoadReset;
    data->sram_save_type = SaveDirect;
  }
  else {
    data->sram_load_type = game_no_save         ? SaveLoadDisable :
                           !autoload_default    ? SaveLoadDisable :
                           data->savefile_found ? SaveLoadSav :
                                                  SaveLoadReset;
    data->sram_save_type = autosave_default && !game_no_save ? SaveReboot : SaveDisable;
  }

  data->rtcval = rtcts;
}


static void browser_open_gba(const char *fn, uint32_t fs, bool prompt_patchgen) {
  if (fs > MAX_GBA_ROM_SIZE) {
    // The ROM is too big to be loaded!
    spop.alert_msg = msgs[lang_id][MSG_ERR_TOOBIG];
  } else {
    // Default to global settings (in case the file is not found).
    t_rom_load_settings ld_sett = {
      .patch_policy = patcher_default,
      .use_igm = ingamemenu_default,
      .use_rtc = rtcpatch_default,
      .use_dsaving = autosave_prefer_ds
    };
    t_rom_launch_settings lh_sett = {
      .use_cheats = true,              // Defaults to true (just preferred, might be disabled/N/A)
      .rtcts = rtcvalue_default
    };
    // Check for any game-specific config file, so we don't have to guess the config.
    // The config file can be partial, hence the defaults.
    load_rom_settings(fn, &ld_sett, &lh_sett);

    if (!prepare_gba_info(&spop.p.load.i, &ld_sett, fn, fs, true))
      spop.alert_msg = msgs[lang_id][MSG_ERR_READ];
    else {
      const t_rom_header *rmh = &spop.p.load.i.romh;

      // If patch engine is selected but no patches found, prompt for generation.
      // If auto is selected and no patches nor DB entries found, do prompt too.
      bool no_patches = (ld_sett.patch_policy == PatchAuto &&
                         !spop.p.load.i.patches_datab_found && !spop.p.load.i.patches_cache_found);
      bool no_engine  = (ld_sett.patch_policy == PatchEngine && !spop.p.load.i.patches_cache_found);
      bool issfw = is_superfw(rmh);

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

      // What if the game doesn't have a save method? Select sane defaults.
      const t_patch *p = get_game_patch(&spop.p.load.i);
      bool game_no_save = (p && p->save_mode == SaveTypeNone) || issfw;

      // Attempt to find a cheat file if cheats are enabled.
      prepare_gba_cheats((char*)&rmh->gcode[0], rmh->version, &spop.p.load.l, fn, lh_sett.use_cheats);

      // Load and set default and sane settings honoring defaults and preferences.
      prepare_gba_settings(&spop.p.load.l, ld_sett.use_dsaving, lh_sett.rtcts, game_no_save, fn);

      // Show load ROM menu.
      spop.pop_num = POPUP_GBA_LOAD;
      spop.anim = 0;
      spop.submenu = GbaLoadPopInfo;
      spop.selector = GBALoadButt;
    }
  }
}

void patch_gen_callback(bool confirm) {
  // Generate patches if confirm was selected
  if (confirm) {
    generate_patches_progress(spop.p.load.i.romfn, spop.p.load.i.romfs);
    spop.alert_msg = msgs[lang_id][MSG_PATCHGEN_OK];
  }

  // Either way, show the popup screen afterwards without prompt
  browser_open_gba(spop.p.load.i.romfn, spop.p.load.i.romfs, false);
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

__attribute__((noinline))
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

void start_emu_game(const t_emu_loader *ldinfo, const char *fn, uint32_t fs) {
  // Load: Sav/Reset Save: Reboot/Disable
  sram_template_filename_calc(fn, ".sav", spop.p.load.l.savefn);
  t_sram_load_policy lp = check_file_exists(spop.p.load.l.savefn) ? SaveLoadSav : SaveLoadReset;
  unsigned errsave = prepare_sram_based_savegame(lp, SaveReboot, spop.p.load.l.savefn);
  if (errsave) {
    unsigned errmsg = (errsave == ERR_SAVE_BADSAVE)   ? MSG_ERR_SAVERD :
                                                        MSG_ERR_SAVEWR;
    spop.alert_msg = msgs[lang_id][errmsg];
  }
  else {
    // Try to load the emu and ROM, keep trying if there's more than one emulatior option.
    unsigned errcode = ERR_LOAD_NOEMU;
    while (ldinfo->emu_name) {
      if (recent_menu)
        insert_recent_flush(fn);

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

__attribute__((noinline))
static void browser_open(const char *fn, uint32_t fs) {
  unsigned l = strlen(fn);
  if (!strcasecmp(&fn[l-4], ".gba"))
    // GBA ROMs (most likely)
    browser_open_gba(fn, fs, true);
  else if (!strcasecmp(&fn[l-4], ".sav")) {
    spop.pop_num = POPUP_SAVFILE;
    spop.selector = SavMAX;
    strcpy(spop.p.savopt.savfn, fn);
  }
  else if (!strcasecmp(&fn[l-3], ".fw")) {
    // A SuperFW firmware update is selected!
    if (!enable_flashing)
      spop.alert_msg = msgs[lang_id][MSG_FWUP_DISABLED];
    else if (fs > FW_MAX_SIZE_KB*1024 || (flashinfo.size && fs > flashinfo.size))
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
            start_emu_game(get_emu_info("gbc"), fn, fs);
            break;
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

static void browser_reload_filter() {
  // Instead of sorting the actual list of files, which requires moving lots
  // of memory, we use a list of pointers.
  unsigned fcount = 0;
  for (unsigned i = 0; i < smenu.browser.maxentries; i++) {
    if ((sdr_state->fentries[i].attr & AM_HID) && hide_hidden)
      continue;

    sdr_state->fileorder[fcount++] = &sdr_state->fentries[i];
  }

  heapsort4(sdr_state->fileorder, fcount, sizeof(t_centry*) / sizeof(uint32_t), filesort);

  if (smenu.browser.selector >= fcount)
    smenu.browser.selector = fcount - 1;
  smenu.browser.seloff = MAX(0, smenu.browser.selector - BROWSER_ROWS / 2);
  smenu.browser.dispentries = fcount;
}

// Loads a new directory list in the ROM browser.
// TODO: Implement filtering (.gba/.rom/.bin... etc) using settings
static void browser_reload() {
  smenu.anim_state = 0;

  unsigned fcount = 0;
  DIR d;
  if (FR_OK != f_opendir(&d, smenu.browser.cpath))
    return;   // FIXME: Implement error reporting!

  while (1) {
    FILINFO info;
    if (f_readdir(&d, &info) != FR_OK || !info.fname[0])
      break;

    if (fcount >= BROWSER_MAXFN_CNT)
      break;

    t_centry *e = &sdr_state->fentries[fcount++];
    e->filesize = (uint32_t) info.fsize;  // TODO: Support 4GB+ files?
    e->isdir = (info.fattrib & AM_DIR) ? 1 : 0;
    e->attr = info.fattrib;
    dma_memcpy16(e->fname, info.fname, MAX_FN_LEN/2);
    sortable_utf8_u16(info.fname, e->sortname);
  }
  smenu.browser.maxentries = fcount;

  // Filter and sort list of files/dirs
  browser_reload_filter();
}

// Loads NOR game entries so they can be browsed.
static void flashbrowser_reload() {
  #ifdef SUPPORT_NORGAMES
  smenu.fbrowser.selector = 0;
  smenu.anim_state = 0;

  if (!flashmgr_load(ROM_FLASHMETA_ADDR, FLASH_METADATA_SIZE, (t_reg_entry*)&sdr_state->nordata))
    // No data found, reset the entries
    memset(&sdr_state->nordata, 0, sizeof(sdr_state->nordata));

  // Calculate block usage, free space, etc.
  smenu.fbrowser.usedblks = 0;
  for (unsigned i = 0; i < sdr_state->nordata.gamecnt; i++) {
    const t_flash_game_entry *e = &sdr_state->nordata.games[i];
    for (unsigned j = 0; j < MAX_GAME_BLOCKS; j++)
      if (e->blkmap[j])
        smenu.fbrowser.usedblks++;
  }
  smenu.fbrowser.freeblks = NOR_GAMEBLOCK_COUNT - smenu.fbrowser.usedblks;

  smenu.fbrowser.maxentries = sdr_state->nordata.gamecnt;
  heapsort4(sdr_state->nordata.games, smenu.fbrowser.maxentries, sizeof(t_flash_game_entry) / sizeof(uint32_t), romsort);
  #endif
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

static void draw_text_leftovf(const char *t, volatile uint8_t *frame, unsigned x, unsigned y, unsigned maxw) {
  uint8_t *basept = (uint8_t*)&frame[y * SCREEN_WIDTH + x];
  unsigned numchars = font_width_lcap(t, maxw - THREEDOTS_WIDTH);
  if (numchars) {
    draw_text_idx8_bus16("...", basept, SCREEN_WIDTH, FT_COLOR);
    draw_text_idx8_bus16(&t[numchars], basept + THREEDOTS_WIDTH, SCREEN_WIDTH, FT_COLOR);
  } else {
    draw_text_idx8_bus16(t, basept, SCREEN_WIDTH, FT_COLOR);
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
    char tmp[128];
    unsigned outw;
    unsigned linechars = font_width_cap_space(t, maxw, &outw);
    unsigned charcnt = linechars ?: utf8_strlen(t);
    uint8_t *basept = (uint8_t*)&frame[y * SCREEN_WIDTH + x - outw / 2];

    memcpy(tmp, t, charcnt);
    tmp[charcnt] = 0;
    draw_text_idx8_bus16(tmp, basept, SCREEN_WIDTH, FT_COLOR);

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

#ifdef SUPPORT_NORGAMES
void render_flashbrowser(volatile uint8_t *frame) {
  // Render bar below to show block info
  dma_memset16(&frame[240*144], dup8(FG_COLOR), 240*16/2);

  // Render the list from memory.
  if (!smenu.fbrowser.maxentries)
    draw_central_text(msgs[lang_id][MSG_NOR_EMPTY], frame, SCREEN_WIDTH/2, SCREEN_HEIGHT/2-8);
  else {
    for (unsigned i = 0; i < NORGAMES_ROWS; i++) {
      if (smenu.fbrowser.seloff + i >= smenu.fbrowser.maxentries)
        break;

      t_flash_game_entry *e = &sdr_state->nordata.games[smenu.fbrowser.seloff + i];
      render_icon(2, (i+1)*16, ICON_GBACART);

      // Animate the row entries if they are too long!
      char szstr[16];
      human_size(szstr, sizeof(szstr), e->numblks * NOR_BLOCK_SIZE);
      draw_rightj_text(szstr, frame, SCREEN_WIDTH - 2, (1 + i) * 16);

      // Animate the row entries if they are too long!
      const char *romname = &e->game_name[e->bnoffset];
      if (i == smenu.fbrowser.selector - smenu.fbrowser.seloff)
        draw_text_ovf_rotate(romname, frame, 20, (1 + i) * 16,
                             SCREEN_WIDTH - 26 - font_width(szstr), &smenu.anim_state);
      else
        draw_text_ovf(romname, frame, 20, (1 + i) * 16, SCREEN_WIDTH - 26 - font_width(szstr));
    }

    for (unsigned i = 0; i < 240; i += 16)
      render_icon_trans(i, (smenu.fbrowser.selector - smenu.fbrowser.seloff + 1)*16, 63);
  }

  char tmp[32], tmp1[32], tmp2[32];
  npf_snprintf(tmp, sizeof(tmp), "%u/%d", smenu.fbrowser.selector + 1, smenu.fbrowser.maxentries);
  draw_rightj_text(tmp, frame, SCREEN_WIDTH - 1, 1);

  human_size(tmp1, sizeof(tmp1), smenu.fbrowser.usedblks * NOR_BLOCK_SIZE);
  human_size(tmp2, sizeof(tmp2), NOR_GAMEBLOCK_COUNT * NOR_BLOCK_SIZE);
  npf_snprintf(tmp, sizeof(tmp), "Flash usage: %s/%s", tmp1, tmp2);
  draw_text_ovf(tmp, frame, 8, 144, SCREEN_WIDTH - 16);
}
#endif

void render_browser(volatile uint8_t *frame) {
  // Render bar below to show path URI
  dma_memset16(&frame[240*144], dup8(FG_COLOR), 240*16/2);

  if (!smenu.browser.dispentries)
    draw_central_text(msgs[lang_id][MSG_BROW_EMPTY], frame, SCREEN_WIDTH/2, SCREEN_HEIGHT/2-8);
  else {
    for (unsigned i = 0; i < BROWSER_ROWS; i++) {
      if (smenu.browser.seloff + i >= smenu.browser.dispentries)
        break;

      t_centry *e = sdr_state->fileorder[smenu.browser.seloff + i];

      unsigned iconidx = (e->attr & AM_HID) ? ((e->attr & AM_DIR) ? ICON_HFOLDER : ICON_HFILE) :
                         (e->attr & AM_DIR) ? ICON_FOLDER :
                         guessicon(e->fname);

      render_icon(2, (i+1)*16, iconidx);

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

    for (unsigned i = 0; i < 240; i += 16)
      render_icon_trans(i, (smenu.browser.selector - smenu.browser.seloff + 1)*16, 63);
  }

  // Draw path, cut left part if necessary.
  draw_text_leftovf(smenu.browser.cpath, frame, 8, 144, SCREEN_WIDTH - 8);

  char selinfo[16];
  npf_snprintf(selinfo, sizeof(selinfo), "%u/%d", smenu.browser.selector + 1, smenu.browser.dispentries);
  draw_rightj_text(selinfo, frame, SCREEN_WIDTH - 1, 1);
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
    if (spop.selector == i)
      draw_box_full(frame, 20, 220, 32 + 28 * i, 32 + 28 * i + 20, FG_COLOR, HI_COLOR);
    else
      draw_box_outline(frame, 20, 220, 32 + 28 * i, 32 + 28 * i + 20, FG_COLOR);
    draw_central_text(msgs[lang_id][MSG_SAVOPT_OPT0 + i], frame, 120, 34 + 28 * i);
  }
  if (spop.selector == SavQuit)
      draw_box_full(frame, 20, 220, 124, 144, FG_COLOR, HI_COLOR);
  else
    draw_box_outline(frame, 20, 220, 124, 144, FG_COLOR);
  draw_central_text(msgs[lang_id][MSG_CANCEL], frame, 120, 126);
}

static void render_gbarom_info(volatile uint8_t *frame, const char *dispname,
                               bool issf, const char *gcode, uint8_t ver, int save_type) {
  char tmp[64];
  draw_central_text(msgs[lang_id][MSG_GBALOAD_MINFO], frame, SCREEN_WIDTH/2, 23);

  const char *romname = file_basename(dispname);
  unsigned twidth = font_width(romname);
  if (twidth > SCREEN_WIDTH - 20)
    draw_text_ovf_rotate(romname, frame, 10, 52,
                         SCREEN_WIDTH - 20, &spop.anim);
  else
    draw_central_text_ovf(romname, frame, SCREEN_WIDTH/2, 52, SCREEN_WIDTH - 20);

  npf_snprintf(tmp, sizeof(tmp), msgs[lang_id][MSG_LOADINFO_GAME], gcode, ver);
  draw_central_text_ovf(tmp, frame, SCREEN_WIDTH/2, 82, SCREEN_WIDTH - 20);

  if (save_type < 0)
    draw_central_text_ovf(msgs[lang_id][MSG_LOADINFO_UNKW], frame, SCREEN_WIDTH/2, 102, SCREEN_WIDTH - 20);
  else if (issf)
    draw_central_text_ovf("SuperFW firmware", frame, SCREEN_WIDTH/2, 102, SCREEN_WIDTH - 20);
  else {
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

    npf_snprintf(tmp, sizeof(tmp), msgs[lang_id][MSG_LOADINFO_SAVE], stype[save_type], ssize[save_type]);
    draw_central_text_ovf(tmp, frame, SCREEN_WIDTH/2, 102, SCREEN_WIDTH - 20);
  }

  draw_box_full(frame, 20, 220, 132, 152, FG_COLOR, HI_COLOR);
}

static const char *render_gbarom_patching(volatile uint8_t *frame, const t_load_gba_info *info, int selector) {
  draw_central_text(msgs[lang_id][MSG_GBALOAD_MPATCH], frame, SCREEN_WIDTH/2, 23);
  draw_text_ovf(msgs[lang_id][MSG_DEFS_PATCH], frame, 12, 44, 224);
  draw_central_text(msgs[lang_id][MSG_PATCH_TYPE0 + info->patch_type], frame, 162, 44);
  draw_text_ovf(msgs[lang_id][MSG_LOADER_SAVET], frame, 12, 62, 224);
  draw_central_text(msgs[lang_id][MSG_LOADER_ST0 + (info->use_dsaving ? 0 : 1)], frame, 170, 62);
  draw_text_ovf(msgs[lang_id][MSG_LOADER_MENU], frame, 12, 80, 224);
  draw_central_text(msgs[lang_id][info->ingame_menu_enabled ? MSG_KNOB_ENABLED : MSG_KNOB_DISABLED], frame, 170, 80);
  draw_text_ovf(msgs[lang_id][MSG_LOADER_RTCE], frame, 12, 98, 224);
  draw_central_text(msgs[lang_id][info->rtc_patch_enabled ? MSG_KNOB_ENABLED : MSG_KNOB_DISABLED], frame, 170, 98);

  draw_text_ovf(msgs[lang_id][MSG_LOADER_PTCH], frame, 12, 116, 224);
  draw_box_outline(frame, 170 - 20, 170 + 20, 115, 133, FG_COLOR);
  draw_central_text("▸", frame, 170, 116);

  return (selector == GBALoadPatch) ? msgs[lang_id][MSG_PATCH_TYPE_I0 + info->patch_type] :
         (selector == GBASavePatch) ? msgs[lang_id][MSG_LOADER_ST_I0 + (info->use_dsaving ? 0 : 1)] :
         (selector == GBAInGameMen) ? msgs[lang_id][MSG_INGAME_I] :
         (selector == GBARTCPatch)  ? msgs[lang_id][MSG_PATCHRTC_I] :
         (selector == GBAPatchGen)  ? msgs[lang_id][MSG_PATCHE_I] : NULL;
}

static const char *render_gbarom_loading(volatile uint8_t *frame, const t_load_gba_lcfg *data, bool rtc_patching, int selector) {
  char tmp[64];
  draw_central_text(msgs[lang_id][MSG_GBALOAD_OPTS], frame, SCREEN_WIDTH/2, 23);
  draw_text_ovf(msgs[lang_id][MSG_LOADER_LOADP], frame, 12, 44, 224);
  draw_central_text(msgs[lang_id][MSG_LOADER_LOADP0 + data->sram_load_type], frame, 170, 44);
  draw_text_ovf(msgs[lang_id][MSG_LOADER_SAVEP], frame, 12, 62, 224);
  draw_central_text(msgs[lang_id][MSG_LOADER_SAVEP0 + data->sram_save_type], frame, 170, 62);
  draw_text_ovf(msgs[lang_id][MSG_DEF_RTCVAL], frame, 12, 80, 224);
  if (rtc_patching) {
    t_dec_date d;
    timestamp2date(data->rtcval, &d);
    npf_snprintf(tmp, sizeof(tmp), "20%02d/%02d/%02d %02d:%02d",
      d.year, d.month, d.day, d.hour, d.min);
    draw_central_text(tmp, frame, 170, 80);
  }
  else
    draw_central_text("-", frame, 170, 80);
  draw_text_ovf(msgs[lang_id][MSG_SETT_LDCHT], frame, 12, 98, 224);
  draw_central_text(msgs[lang_id][data->use_cheats ? MSG_KNOB_ENABLED : MSG_KNOB_DISABLED], frame, 170, 98);

  draw_box_outline(frame, 170 - 20, 170 + 20, 115, 133, FG_COLOR);
  draw_text_ovf(msgs[lang_id][MSG_SETT_REMEMB], frame, 12, 116, 224);
  render_icon(170-8, 116, ICON_DISK);

  return (selector == GBALdSetLoadP) ? msgs[lang_id][MSG_LOADER_LOADP_I0 + data->sram_load_type] :
         (selector == GBALdSetSaveP) ? msgs[lang_id][MSG_LOADER_SAVEP_I0 + data->sram_save_type] :
         (selector == GBALdSetCheats && !enable_cheats) ? msgs[lang_id][MSG_CHEATSDIS_I] :
         (selector == GBALdSetCheats && !data->cheats_found) ? msgs[lang_id][MSG_CHEATSNOA_I] :
         (selector == GBALdRemember) ? msgs[lang_id][MSG_REMEMB_I] : NULL;
}

void render_gba_load_popup(volatile uint8_t *frame) {
  draw_box_outline(frame, 2, 240-2, 18, 158, FG_COLOR);
  draw_text_ovf("⯇", frame, 10, 23, 64);
  draw_rightj_text("⯈", frame, SCREEN_WIDTH - 10, 23);

  const t_load_gba_info *info = &spop.p.load.i;
  const t_patch *p = get_game_patch(info);
  const char *ht = NULL;
  switch (spop.submenu) {
  case GbaLoadPopInfo:
    render_gbarom_info(frame, info->romfn, is_superfw(&info->romh), info->gcode,
                       info->romh.version, p ? p->save_mode : -1);
    draw_central_text(msgs[lang_id][MSG_LOAD_GBA], frame, 120, 134);
    break;
  case GbaLoadPopLoadS:
    ht = render_gbarom_loading(frame, &spop.p.load.l, info->rtc_patch_enabled, spop.selector);
    break;
  case GbaLoadPopPatch:
    ht = render_gbarom_patching(frame, &spop.p.load.i, spop.selector);
    break;
  };

  // Show some help if necessary
  if (ht) {
    unsigned twidth = font_width(ht);
    if (twidth > SCREEN_WIDTH - 20)
      draw_text_ovf_rotate(ht, frame, 10, 137, SCREEN_WIDTH - 20, &spop.anim);
    else
      draw_central_text_ovf(ht, frame, SCREEN_WIDTH/2, 137, SCREEN_WIDTH - 20);
  }

  if (spop.submenu != GbaLoadPopInfo) {
    const unsigned offy = 43;
    for (unsigned i = 8; i < 232; i += 16) {
      render_icon_trans(i, offy + 0 + spop.selector * 18, 63);
      render_icon_trans(i, offy + 2 + spop.selector * 18, 63);
    }
  }
}

void render_filemgr(volatile uint8_t *frame) {
  // Draw the file name and the options available
  draw_box_outline(frame, 2, 240-2, 18, 158, FG_COLOR);

  t_centry *e = sdr_state->fileorder[smenu.browser.selector];
  const char *bn = file_basename(e->fname);

  unsigned twidth = font_width(bn);
  if (twidth > SCREEN_WIDTH - 20)
    draw_text_ovf_rotate(bn, frame, 10, 32, SCREEN_WIDTH - 20, &spop.anim);
  else
    draw_central_text_ovf(bn, frame, SCREEN_WIDTH/2, 32, SCREEN_WIDTH - 20);

  for (unsigned i = 0; i < FiMgrCNT; i++)
    if (i == spop.selector)
      draw_box_full(frame, 20, 220, 60 + i*30, 80 + i*30, FG_COLOR, HI_COLOR);
    else
      draw_box_outline(frame, 20, 220, 60 + i*30, 80 + i*30, FG_COLOR);

  draw_central_text(msgs[lang_id][MSG_FMGR_DEL], frame, 120, 62 + 30*FiMgrDelete);
  draw_central_text(msgs[lang_id][(e->attr & AM_HID) ? MSG_FMGR_UNHIDE : MSG_FMGR_HIDE], frame, 120, 62 + 30*FiMgrHide);

  #ifdef SUPPORT_NORGAMES
  draw_central_text(msgs[lang_id][MSG_NOR_WRITE], frame, 120, 62 + 30*FiMgrWriteNOR);
  #endif
}


#ifdef SUPPORT_NORGAMES
void render_gba_norwrite(volatile uint8_t *frame) {
  draw_box_outline(frame, 2, 240-2, 18, 158, FG_COLOR);

  draw_text_ovf("⯇", frame, 10, 23, 64);
  draw_rightj_text("⯈", frame, SCREEN_WIDTH - 10, 23);

  if (spop.submenu == GbaLoadPopInfo) {
    const t_load_gba_info *info = &spop.p.norwr.i;
    const t_patch *p = get_game_patch(info);
    render_gbarom_info(frame, info->romfn, is_superfw(&info->romh),
                       info->gcode, info->romh.version, p ? p->save_mode : -1);
    draw_central_text(msgs[lang_id][MSG_NOR_WRITE], frame, 120, 134);
  } else {
    const char *ht = render_gbarom_patching(frame, &spop.p.norwr.i, spop.selector);
    if (ht) {
      unsigned twidth = font_width(ht);
      if (twidth > SCREEN_WIDTH - 20)
        draw_text_ovf_rotate(ht, frame, 10, 137, SCREEN_WIDTH - 20, &spop.anim);
      else
        draw_central_text_ovf(ht, frame, SCREEN_WIDTH/2, 137, SCREEN_WIDTH - 20);
    }
    const unsigned offy = 43;
    for (unsigned i = 8; i < 232; i += 16) {
      render_icon_trans(i, offy + 0 + spop.selector * 18, 63);
      render_icon_trans(i, offy + 2 + spop.selector * 18, 63);
    }
  }
}

void render_gba_norload(volatile uint8_t *frame) {
  draw_box_outline(frame, 2, 240-2, 18, 158, FG_COLOR);

  draw_text_ovf("⯇", frame, 10, 23, 64);
  draw_rightj_text("⯈", frame, SCREEN_WIDTH - 10, 23);

  t_flash_game_entry *e = &sdr_state->nordata.games[smenu.fbrowser.selector];
  if (spop.submenu == GbaLoadPopInfo) {
    int save_type = GET_GATTR_SAVEM(e->gattrs);
    render_gbarom_info(frame, e->game_name, false, (const char*)&e->gamecode, e->gamever, save_type);
    draw_central_text(msgs[lang_id][MSG_NOR_LAUNCH], frame, 120, 134);
  } else {
    bool rtc_patching = e->gattrs & GATTR_RTC;
    const char *ht = render_gbarom_loading(frame, &spop.p.norld.l, rtc_patching, spop.selector);
    if (ht) {
      unsigned twidth = font_width(ht);
      if (twidth > SCREEN_WIDTH - 20)
        draw_text_ovf_rotate(ht, frame, 10, 137, SCREEN_WIDTH - 20, &spop.anim);
      else
        draw_central_text_ovf(ht, frame, SCREEN_WIDTH/2, 137, SCREEN_WIDTH - 20);
    }
    const unsigned offy = 43;
    for (unsigned i = 8; i < 232; i += 16) {
      render_icon_trans(i, offy + 0 + spop.selector * 18, 63);
      render_icon_trans(i, offy + 2 + spop.selector * 18, 63);
    }
  }
}
#endif

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

  const t_dec_date *v = &spop.rtcpop.val;
  char thour[3] = {'0' + v->hour /10, '0' + v->hour  % 10, 0};
  char tmins[3] = {'0' + v->min  /10, '0' + v->min   % 10, 0};
  char tdays[3] = {'0' + v->day  /10, '0' + v->day   % 10, 0};
  char tmont[3] = {'0' + v->month/10, '0' + v->month % 10, 0};
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
  char tmp[80];
  unsigned baseopt = smenu.set.selector <= 2  ? 0 :
                     smenu.set.selector >= SettMAX - 2 ? SettMAX - 4 :
                     smenu.set.selector - 2;

  if (smenu.set.selector > 2)
    draw_central_text("⯅", frame, 120, 15);
  if (smenu.set.selector < SettSave - 2)
    draw_central_text("⯆", frame, 120, 125);

  unsigned msk = 0x1F << baseopt;
  unsigned optcnt = 0;
  const unsigned colx = 170;           // Center point for the selection boxes
  const unsigned offy = 29;
  const unsigned rowh = 20;

  if (msk & 0x00001)
    draw_central_text(msgs[lang_id][MSG_SET_TITL1], frame, SCREEN_WIDTH/2, offy + rowh*optcnt++);

  if (msk & 0x00002) {
    npf_snprintf(tmp, sizeof(tmp), "< %s >", hotkey_list[hotkey_combo].cname);
    draw_text_ovf(msgs[lang_id][MSG_SETT_HOTK], frame, 8, offy + rowh*optcnt, 224);
    draw_central_text(tmp, frame, colx, offy + rowh*optcnt++);
  }

  if (msk & 0x00004) {
    draw_text_ovf(msgs[lang_id][MSG_SETT_BOOT], frame, 8, offy + rowh*optcnt, 224);
    draw_central_text(msgs[lang_id][MSG_BOOT_TYPE0 + boot_bios_splash], frame, colx, offy + rowh*optcnt++);
  }

  if (msk & 0x00008) {
    draw_text_ovf(msgs[lang_id][MSG_SETT_FASTSD], frame, 8, offy + rowh*optcnt, 224);
    draw_central_text(msgs[lang_id][use_slowld ? MSG_KNOB_DISABLED : MSG_KNOB_ENABLED], frame, colx, offy + rowh*optcnt++);
  }

  if (msk & 0x00010) {
    draw_text_ovf(msgs[lang_id][MSG_SETT_FASTEW], frame, 8, offy + rowh*optcnt, 224);
    draw_central_text(msgs[lang_id][use_fastew ? MSG_KNOB_ENABLED : MSG_KNOB_DISABLED], frame, colx, offy + rowh*optcnt++);
  }

  if (msk & 0x00020) {
    draw_text_ovf(msgs[lang_id][MSG_SETT_SAVET], frame, 8, offy + rowh*optcnt, 224);

    if (save_path_default == SaveRomName)
      draw_central_text(msgs[lang_id][MSG_NEXTTO_ROM], frame, colx, offy + rowh*optcnt++);
    else {
      npf_snprintf(tmp, sizeof(tmp), "< %s >", save_paths[save_path_default]);
      draw_central_text(tmp, frame, colx, offy + rowh*optcnt++);
    }
  }

  if (msk & 0x00040) {
    npf_snprintf(tmp, sizeof(tmp), "< %lu >", backup_sram_default);
    draw_text_ovf(msgs[lang_id][MSG_SETT_SAVEBK], frame, 8, offy + rowh*optcnt, 224);
    draw_central_text(tmp, frame, colx, offy + rowh*optcnt++ );
  }

  if (msk & 0x00080) {
    draw_text_ovf(msgs[lang_id][MSG_SETT_STATET], frame, 8, offy + rowh*optcnt, 224);
    if (state_path_default == StateRomName)
      draw_central_text(msgs[lang_id][MSG_NEXTTO_ROM], frame, colx, offy + rowh*optcnt++);
    else {
      npf_snprintf(tmp, sizeof(tmp), "< %s >", savestates_paths[state_path_default]);
      draw_central_text(tmp, frame, colx, offy + rowh*optcnt++);
    }
  }

  if (msk & 0x00100) {
    draw_text_ovf(msgs[lang_id][MSG_SETT_CHTEN], frame, 8, offy + rowh*optcnt, 224);
    draw_central_text(msgs[lang_id][enable_cheats ? MSG_KNOB_ENABLED : MSG_KNOB_DISABLED], frame, colx, offy + rowh*optcnt++);
  }

  if (msk & 0x00200)
    draw_central_text(msgs[lang_id][MSG_SET_TITL2], frame, SCREEN_WIDTH/2, offy + rowh*optcnt++);

  if (msk & 0x00400) {
    draw_text_ovf(msgs[lang_id][MSG_DEFS_PATCH], frame, 8, offy + rowh*optcnt, 224);
    draw_central_text(msgs[lang_id][MSG_PATCH_TYPE0 + patcher_default], frame, colx, offy + rowh*optcnt++);
  }

  if (msk & 0x00800) {
    draw_text_ovf(msgs[lang_id][MSG_LOADER_MENU], frame, 8, offy + rowh*optcnt, 224);
    draw_central_text(msgs[lang_id][MSG_KNOB_DISABLED + ingamemenu_default], frame, colx, offy + rowh*optcnt++);
  }

  if (msk & 0x01000) {
    draw_text_ovf(msgs[lang_id][MSG_LOADER_RTCE], frame, 8, offy + rowh*optcnt, 224);
    draw_central_text(msgs[lang_id][MSG_KNOB_DISABLED + rtcpatch_default], frame, colx, offy + rowh*optcnt++);
  }

  if (msk & 0x02000) {
    t_dec_date d;
    timestamp2date(rtcvalue_default, &d);
    npf_snprintf(tmp, sizeof(tmp), "20%02d/%02d/%02d %02d:%02d",
      d.year, d.month, d.day, d.hour, d.min);
    draw_text_ovf(msgs[lang_id][MSG_DEF_RTCVAL], frame, 8, offy + rowh*optcnt, 224);
    draw_central_text(tmp, frame, colx, offy + rowh*optcnt++);
  }

  if (msk & 0x04000) {
    unsigned spdmsg = rtcspeed_default ? (MSG_UIS_SPD0 + rtcspeed_default - 1) :
                                          MSG_STILLRTC;
    draw_text_ovf(msgs[lang_id][MSG_DEF_SPEED], frame, 8, offy + rowh*optcnt, 224);
    draw_central_text(msgs[lang_id][spdmsg], frame, colx, offy + rowh*optcnt++);
  }

  if (msk & 0x08000) {
    draw_text_ovf(msgs[lang_id][MSG_LOADER_LOADP], frame, 8, offy + rowh*optcnt, 224);
    draw_central_text(msgs[lang_id][MSG_DEF_LOADP0 + (autoload_default ^ 1)], frame, colx, offy + rowh*optcnt++);
  }

  if (msk & 0x10000) {
    draw_text_ovf(msgs[lang_id][MSG_LOADER_SAVEP], frame, 8, offy + rowh*optcnt, 224);
    draw_central_text(msgs[lang_id][autosave_default ? MSG_DEF_SAVEP0 : MSG_DEF_SAVEP1], frame, colx, offy + rowh*optcnt++);
  }

  if (msk & 0x20000) {
    draw_text_ovf(msgs[lang_id][MSG_LOADER_PREFDS], frame, 8, offy + rowh*optcnt, 224);
    draw_central_text(msgs[lang_id][autosave_prefer_ds ? MSG_KNOB_ENABLED : MSG_KNOB_DISABLED], frame, colx, offy + rowh*optcnt++);
  }

  if (msk & 0x40000) {
    if (smenu.set.selector != SettSave)
      draw_box_outline(frame, 20, 220, 112, 132, FG_COLOR);
    else
      draw_box_full(frame, 20, 220, 112, 132, FG_COLOR, HI_COLOR);
    draw_central_text(msgs[lang_id][MSG_UIS_SAVE], frame, 132, 114);
  }

  // Render bar below for help messge
  dma_memset16(&frame[240*140], dup8(FG_COLOR), 240*20/2);

  if (smenu.set.selector == SettSaveLoc) {
    if (save_path_default == SaveRomName)
      draw_text_ovf_rotate(msgs[lang_id][MSG_SAVE_TYPE_NR], frame, 4, SCREEN_HEIGHT - 18, 232, &smenu.anim_state);
    else {
      npf_snprintf(tmp, sizeof(tmp), msgs[lang_id][MSG_SAVE_TYPE_PT], save_paths[save_path_default]);
      draw_text_ovf_rotate(tmp, frame, 4, SCREEN_HEIGHT - 18, 232, &smenu.anim_state);
    }
  } else {
    unsigned help_msg = smenu.set.selector == SettBootType ? MSG_BOOT_TYPE_I0 + boot_bios_splash :
                        smenu.set.selector == SettSaveBkp  ? MSG_BACKUP_I :
                        smenu.set.selector == SettFastSD   ? MSG_FASTSD_I :
                        smenu.set.selector == SettFastEWRAM? MSG_FASTEW_I :
                        smenu.set.selector == DefsPatchEng ? MSG_PATCH_TYPE_I0 + patcher_default :
                        smenu.set.selector == DefsLoadPol  ? MSG_DEF_LOADP_I0 + (autoload_default ^ 1) :
                        smenu.set.selector == DefsSavePol  ? MSG_DEF_SAVEP_I0 + (autosave_default ^ 1) :
                        smenu.set.selector == DefsPrefDS   ? MSG_LOADER_PREFDSI :
                        MSG_EMPTY;
    draw_text_ovf_rotate(msgs[lang_id][help_msg], frame, 4, SCREEN_HEIGHT - 18, 232, &smenu.anim_state);
  }

  if (smenu.set.selector != SettSave)
    for (unsigned i = 0; i < 240; i += 16)
      render_icon_trans(i, offy + (smenu.set.selector - baseopt) * 20, 63);
}

void render_ui_settings(volatile uint8_t *frame) {
  const unsigned colx = 170;
  char tmpbuf[64];
  npf_snprintf(tmpbuf, sizeof(tmpbuf), "< %lu >", menu_theme + 1U);
  draw_text_ovf(msgs[lang_id][MSG_UIS_THEME], frame, 8, 22, 224);
  draw_central_text(tmpbuf, frame, colx, 22 );

  npf_snprintf(tmpbuf, sizeof(tmpbuf), "< %s >", msgs[lang_id][MSG_LANG_NAME]);
  draw_text_ovf(msgs[lang_id][MSG_UIS_LANG], frame, 8, 22 + 20, 224);
  draw_central_text(tmpbuf, frame, colx, 22 + 20 );

  draw_text_ovf(msgs[lang_id][MSG_UIS_RECNT], frame, 8, 22 + 40, 224);
  draw_central_text(msgs[lang_id][recent_menu ? MSG_KNOB_ENABLED : MSG_KNOB_DISABLED], frame, colx, 22 + 40 );

  draw_text_ovf(msgs[lang_id][MSG_UIS_ANSPD], frame, 8, 22 + 60, 224);
  draw_central_text(msgs[lang_id][MSG_UIS_SPD0 + anim_speed], frame, colx, 22 + 60 );

  draw_text_ovf(msgs[lang_id][MSG_UIS_BHID], frame, 8, 22 + 80, 224);
  draw_central_text(msgs[lang_id][hide_hidden ? MSG_KNOB_DISABLED : MSG_KNOB_ENABLED], frame, colx, 22 + 80 );

  if (smenu.uiset.selector != UiSetSave)
    for (unsigned i = 0; i < 240; i += 16)
      render_icon_trans(i, 22 + smenu.uiset.selector * 20, 63);

  if (smenu.uiset.selector != UiSetSave)
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
    draw_central_text("by davidgf", frame, 120, 70);
    npf_snprintf(tmp, sizeof(tmp), "Version %lu.%lu (%08lx)", vmaj, vmin, gitver);
    draw_central_text(tmp, frame, 120, 95);
    draw_central_text(FW_FLAVOUR " variant", frame, 120, 114);
    break;
  case 1:
    draw_central_text("Flash info", frame, 120, 70);
    npf_snprintf(tmp, sizeof(tmp), "Dev ID: %08lx", flashinfo.deviceid);
    draw_central_text(tmp, frame, 120, 95);
    if (flashinfo.size && flashinfo.blksize && flashinfo.blkcount) {
      human_size_kb(tmp2, sizeof(tmp2), flashinfo.size >> 10);
      npf_snprintf(tmp, sizeof(tmp), "%s [%lu * %lu]", tmp2, flashinfo.blksize, flashinfo.blkcount);
      if (flashinfo.regioncnt != 1)
        strcat(tmp, " !");
      draw_central_text(tmp, frame, 120, 115);
    } else {
      npf_snprintf(tmp, sizeof(tmp), "No CFI! (hardwired %dKiB)", FW_MAX_SIZE_KB);
      draw_central_text(tmp, frame, 120, 115);
    }
    break;
  case 2:
    draw_central_text(msgs[lang_id][MSG_DBPINFO], frame, 120, 70);
    npf_snprintf(tmp, sizeof(tmp), "%s - %s", pdbinfo.version, pdbinfo.date);
    draw_central_text(tmp, frame, 120, 90);
    npf_snprintf(tmp, sizeof(tmp), "Game count: %lu", pdbinfo.patch_count);
    draw_central_text(tmp, frame, 120, 110);
    break;
  case 3:
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
  for (unsigned i = 0; i < ToolsMAX; i++) {
    draw_text_ovf(msgs[lang_id][MSG_TOOLS0_SDRAM + i], frame, 12, 24 + 2 + 22 * i, 144);
    draw_button_box(frame, 150, 232, 24 + 22 * i, 24 + 20 + 22 * i, smenu.tools.selector == i);
    draw_central_text(msgs[lang_id][MSG_TOOLS_RUN], frame, 191, 24 + 2 + 22 * i);
  }
}

void reload_theme(unsigned thnum) {
  // Palette 0..15 contains the main menu template colors
  MEM_PALETTE[FG_COLOR] = themes[thnum].fg_color;
  MEM_PALETTE[BG_COLOR] = themes[thnum].bg_color;
  MEM_PALETTE[FT_COLOR] = themes[thnum].ft_color;
  MEM_PALETTE[HI_COLOR] = themes[thnum].hi_color;
  // In-game menu palette
  MEM_PALETTE[INGMENU_PAL_FG] = themes[thnum].fg_color;
  MEM_PALETTE[INGMENU_PAL_BG] = themes[thnum].bg_color;
  MEM_PALETTE[INGMENU_PAL_HI] = themes[thnum].ft_color;
  MEM_PALETTE[INGMENU_PAL_SH] = themes[thnum].sh_color;

  // Palette entries for icons and other objects
  MEM_PALETTE[256 + SEL_COLOR] = themes[thnum].hi_blend;
}

static const struct {
  const t_mrender_fn render;
  const int max_submenu;
} popup_windows[] = {
  { render_gba_load_popup, GbaLoadCNT },
  { render_sav_menu_popup, 1 },
  { render_fw_flash_popup, 1 },
  { render_filemgr,        1 },
  #ifdef SUPPORT_NORGAMES
  { render_gba_norwrite,   GbaNorWrCNT },
  { render_gba_norload,    GbaNorLoadCNT },
  #endif
};

// Renders the menu. Arg0 represents the frame count difference with the
// previous rendered frame (for animations and similar stuff).
void menu_render(unsigned fcnt) {
  objnum = 0;
  volatile uint8_t *frame = &MEM_VRAM_U8[0xA000*framen];

  // Render the tab menu on top (rows 0..15), highlighting the selected option
  dma_memset16(&frame[0], dup8(FG_COLOR), SCREEN_WIDTH*16/2);

  // Render icon bar
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
      popup_windows[spop.pop_num - 1].render(frame);
      spop.anim += fcnt * animspd_lut[anim_speed];
    } else {
      static const t_mrender_fn renderfns[] = {
        render_recent,
        render_browser,
        #ifdef SUPPORT_NORGAMES
        render_flashbrowser,
        #endif
        render_settings,
        render_ui_settings,
        render_tools,
        render_info,
      };
      renderfns[smenu.menu_tab](frame);
      smenu.anim_state += fcnt * animspd_lut[anim_speed];
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
  flashbrowser_reload();

  // Load recent ROMs (we could disable this for speed)
  recent_reload();

  reload_theme(menu_theme);

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
    if (validate_superfw && !validate_superfw_variant(sdr_state->scratch))
      spop.alert_msg = msgs[lang_id][MSG_FWUP_BADFL];
    else if (validate_superfw && !validate_superfw_checksum(sdr_state->scratch, fwsize))
      spop.alert_msg = msgs[lang_id][MSG_FWUPD_BADCHK];
    else {
      // Can start the flashing!
      spop.p.update.curr_state = FlashingErasing;
      menu_render(1); menu_flip();

      bool erased_ok;
      #ifdef SUPPORT_NORGAMES
      if (flashinfo.blksize)
        erased_ok = flash_erase_sectors(ROM_FLASHFIRMW_ADDR, flashinfo.blksize,
                                        (fwsize + flashinfo.blksize - 1) / flashinfo.blksize);
      else
      #endif
        erased_ok = flash_erase_chip();

      if (!erased_ok)
        spop.alert_msg = msgs[lang_id][MSG_FWUP_ERRCL];
      else {
        spop.p.update.curr_state = FlashingWriting;
        menu_render(1); menu_flip();

        bool programmed_ok;
        #ifdef SUPPORT_NORGAMES
        if (flashinfo.size && flashinfo.blksize && flashinfo.blkcount && flashinfo.blkwrite)
          programmed_ok = flash_program_buffered(ROM_FLASHFIRMW_ADDR, sdr_state->scratch, fwsize, flashinfo.blkwrite);
        else
        #endif
          programmed_ok = flash_program(ROM_FLASHFIRMW_ADDR, sdr_state->scratch, fwsize);

        if (!programmed_ok)
          spop.alert_msg = msgs[lang_id][MSG_FWUP_ERRPG];
        else {
          if (!flash_verify(ROM_FLASHFIRMW_ADDR, sdr_state->scratch, fwsize))
            spop.alert_msg = msgs[lang_id][MSG_FWUP_ERRVR];
          else {
            // Done! Show a pop up, also go up with pop ups too.
            spop.alert_msg = msgs[lang_id][MSG_FWUPD_DONE];
          }
        }
      }
    }
    spop.pop_num = 0;
  }
}

static void keypress_popup_loadgba(unsigned newkeys) {
  const unsigned maxm[] = {
    GBAInfoCNT,
    GBALdSetCNT,
    GBAPatchCNT,
  };
  unsigned maxsel = maxm[(int)spop.submenu];

  const int psel = spop.selector;
  if (newkeys & KEY_BUTTUP)
    spop.selector += maxsel - 1;
  if (newkeys & KEY_BUTTDOWN)
    spop.selector++;

  // Limit selector to its max value
  spop.selector %= maxsel;

  if (newkeys & KEY_BUTTLEFT) {
    if (spop.submenu == GbaLoadPopLoadS) {
      if (spop.selector == GBALdSetCheats)
        spop.p.load.l.use_cheats = !spop.p.load.l.use_cheats;
      if (spop.p.load.i.use_dsaving) {
        if (spop.selector == GBALdSetLoadP)
          spop.p.load.l.sram_load_type = (spop.p.load.l.sram_load_type + SaveLoadDSCNT - 1) % SaveLoadDSCNT;
      } else {
        if (spop.selector == GBALdSetLoadP)
          spop.p.load.l.sram_load_type = (spop.p.load.l.sram_load_type + SaveLoadCNT - 1) % SaveLoadCNT;
        else if (spop.selector == GBALdSetSaveP)
          spop.p.load.l.sram_save_type = (spop.p.load.l.sram_save_type + SaveCNT - 1) % SaveCNT;
      }
    }
    else if (spop.submenu == GbaLoadPopPatch) {
      if (spop.selector == GBALoadPatch)
        spop.p.load.i.patch_type = (spop.p.load.i.patch_type + PatchOptCNT - 1) % PatchOptCNT;
      else if (spop.selector == GBAInGameMen)
        spop.p.load.i.ingame_menu_enabled = !spop.p.load.i.ingame_menu_enabled;
      else if (spop.selector == GBASavePatch)
        spop.p.load.i.use_dsaving = !spop.p.load.i.use_dsaving;
      else if (spop.selector == GBARTCPatch)
        spop.p.load.i.rtc_patch_enabled = !spop.p.load.i.rtc_patch_enabled;
    }

    // Handle the different cases where the user attempts to select an invalid option.
    if (!spop.p.load.i.patches_cache_found && spop.p.load.i.patch_type == PatchEngine)
      spop.p.load.i.patch_type = PatchDatabase;  // Might be invalid, handled below.
    if (!spop.p.load.i.patches_datab_found && spop.p.load.i.patch_type == PatchDatabase)
      spop.p.load.i.patch_type = PatchNone;

    if (!dirsav_avail_sdram(&spop.p.load.i))
      spop.p.load.i.use_dsaving = false;

    // DirSav forces automatic saving
    if (spop.p.load.i.use_dsaving)
      spop.p.load.l.sram_save_type = SaveDirect;
    else if (spop.p.load.l.sram_save_type == SaveDirect)
      spop.p.load.l.sram_save_type = autosave_default ? SaveReboot : SaveDisable;

    // If DS is selected, do not allow manual mode.
    if (spop.p.load.l.sram_load_type == SaveLoadDisable && spop.p.load.i.use_dsaving)
      spop.p.load.l.sram_load_type = SaveLoadSav;
    // If no .sav is available, do not allow that option!
    if (spop.p.load.l.sram_load_type == SaveLoadSav && !spop.p.load.l.savefile_found)
      spop.p.load.l.sram_load_type = spop.p.load.i.use_dsaving ? SaveLoadReset : SaveLoadDisable;
  }
  if (newkeys & KEY_BUTTRIGHT) {
    if (spop.submenu == GbaLoadPopLoadS) {
      if (spop.selector == GBALdSetCheats)
        spop.p.load.l.use_cheats = !spop.p.load.l.use_cheats;
      if (spop.p.load.i.use_dsaving) {
        if (spop.selector == GBALdSetLoadP)
          spop.p.load.l.sram_load_type = (spop.p.load.l.sram_load_type + 1) % SaveLoadDSCNT;
      } else {
        if (spop.selector == GBALdSetLoadP)
          spop.p.load.l.sram_load_type = (spop.p.load.l.sram_load_type + 1) % SaveLoadCNT;
        else if (spop.selector == GBALdSetSaveP)
          spop.p.load.l.sram_save_type = (spop.p.load.l.sram_save_type + 1) % SaveCNT;
      }
    }
    else if (spop.submenu == GbaLoadPopPatch) {
      if (spop.selector == GBALoadPatch)
        spop.p.load.i.patch_type = (spop.p.load.i.patch_type + 1) % PatchOptCNT;
      else if (spop.selector == GBAInGameMen)
        spop.p.load.i.ingame_menu_enabled = !spop.p.load.i.ingame_menu_enabled;
      else if (spop.selector == GBASavePatch)
        spop.p.load.i.use_dsaving = !spop.p.load.i.use_dsaving;
      else if (spop.selector == GBARTCPatch)
        spop.p.load.i.rtc_patch_enabled = !spop.p.load.i.rtc_patch_enabled;
    }

    // If the database has no entry, then do not let the user select that mode.
    if (!spop.p.load.i.patches_datab_found && spop.p.load.i.patch_type == PatchDatabase)
      spop.p.load.i.patch_type = PatchEngine;  // Might be invalid, handled below.
    if (!spop.p.load.i.patches_cache_found && spop.p.load.i.patch_type == PatchEngine)
      spop.p.load.i.patch_type = PatchNone;

    if (!dirsav_avail_sdram(&spop.p.load.i))
      spop.p.load.i.use_dsaving = false;

    // DirSav forces automatic saving
    if (spop.p.load.i.use_dsaving)
      spop.p.load.l.sram_save_type = SaveDirect;
    else if (spop.p.load.l.sram_save_type == SaveDirect)
      spop.p.load.l.sram_save_type = autosave_default ? SaveReboot : SaveDisable;

    // If DS is selected, do not allow manual mode.
    if (spop.p.load.l.sram_load_type == SaveLoadDisable && spop.p.load.i.use_dsaving)
      spop.p.load.l.sram_load_type = SaveLoadSav;
    // If no .sav is available, do not allow that option!
    if (spop.p.load.l.sram_load_type == SaveLoadSav && !spop.p.load.l.savefile_found)
      spop.p.load.l.sram_load_type = SaveLoadReset;
  }

  // Disable ingame-menu if not available.
  if (!ingame_menu_avail_sdram(&spop.p.load.i))
    spop.p.load.i.ingame_menu_enabled = false;

  // If no RTC patches are available, force them to false.
  if (!rtcemu_avail(&spop.p.load.i))
    spop.p.load.i.rtc_patch_enabled = false;

  // Disable cheat loading if no cheats are avail, or IGM is disabled
  if (!spop.p.load.l.cheats_found || !spop.p.load.i.ingame_menu_enabled)
    spop.p.load.l.use_cheats = false;

  if (newkeys & KEY_BUTTA) {
    if (spop.submenu == GbaLoadPopLoadS && spop.selector == GBALdSetRTC && spop.p.load.i.rtc_patch_enabled) {
      void accept_rtc() {
        spop.p.load.l.rtcval = date2timestamp(&spop.rtcpop.val);
      }
      if (spop.p.load.i.rtc_patch_enabled) {
        timestamp2date(spop.p.load.l.rtcval, &spop.rtcpop.val);
        spop.rtcpop.callback = accept_rtc;
      }
    }
    else if (spop.submenu == GbaLoadPopPatch && spop.selector == GBAPatchGen) {
      generate_patches_progress(spop.p.load.i.romfn, spop.p.load.i.romfs);
      spop.alert_msg = msgs[lang_id][MSG_PATCHGEN_OK];
      // Try/Load the just-generated patches.
      spop.p.load.i.patches_cache_found = load_cached_patches(spop.p.load.i.romfn, &spop.p.load.i.patches_cache);
    }
    else if (spop.submenu == GbaLoadPopLoadS && spop.selector == GBALdRemember) {
      // Save settings to disk now!
      t_rom_load_settings ld_sett = {
        .patch_policy = spop.p.load.i.patch_type,
        .use_igm = spop.p.load.i.ingame_menu_enabled,
        .use_rtc = spop.p.load.i.rtc_patch_enabled,
        .use_dsaving = spop.p.load.i.use_dsaving
      };
      t_rom_launch_settings lh_sett = {
        .use_cheats = spop.p.load.l.use_cheats,
        .rtcts = spop.p.load.l.rtcval
      };

      save_rom_settings(spop.p.load.i.romfn, &ld_sett, &lh_sett);
      spop.alert_msg = msgs[lang_id][MSG_REMEMB_CFG_OK];
    }
    else if (GbaLoadPopInfo == spop.submenu) {
      // Insert the ROM into the recent list (or move it around). Flush to disk!
      if (recent_menu)
        insert_recent_flush(spop.p.load.i.romfn);

      // Honor load.patch_type.
      const t_patch *p = get_game_patch(&spop.p.load.i);
      EnumSavetype st = p ? p->save_mode : SaveTypeNone;

      // Prepare the savegame (load and store stuff, directsave...)
      t_dirsave_info dsinfo;
      unsigned errsave = prepare_savegame(
        spop.p.load.l.sram_load_type, spop.p.load.l.sram_save_type,
        st, &dsinfo, spop.p.load.l.savefn);
      if (errsave) {
        unsigned errmsg = (errsave == ERR_SAVE_BADSAVE)   ? MSG_ERR_SAVERD :
                          (errsave == ERR_SAVE_CANTALLOC) ? MSG_ERR_SAVEPR :
                          (errsave == ERR_SAVE_BADARG)    ? MSG_ERR_SAVEIT :
                                                            MSG_ERR_SAVEWR;
        spop.alert_msg = msgs[lang_id][errmsg];
        return;
      }

      t_rtc_info rtci = {
        .timestamp = spop.p.load.l.rtcval,
        .ts_step = rtc_speed(rtcspeed_default)
      };

      unsigned err = load_gba_rom(
        spop.p.load.i.romfn, spop.p.load.i.romfs, p,
        spop.p.load.l.sram_save_type == SaveDirect ? &dsinfo : NULL,
        spop.p.load.i.ingame_menu_enabled,
        spop.p.load.i.rtc_patch_enabled ? &rtci : NULL,
        spop.p.load.l.use_cheats ? spop.p.load.l.cheats_size : 0,
        loadrom_progress);
      if (err) {
        // Show any errors that might have happened!
        spop.alert_msg = msgs[lang_id][MSG_ERR_READ];
        // TODO: We cannot (in many cases) continue since we trash the SDRAM!
      }
    }
  }

  if (psel != spop.selector)
    spop.anim = 0;
}

static void keypress_popup_savefile(unsigned newkeys) {
  if (newkeys & KEY_BUTTUP)
    spop.selector = MAX(0, spop.selector - 1);
  if (newkeys & KEY_BUTTDOWN)
    spop.selector = MIN(SavMAX, spop.selector + 1);

  if (newkeys & KEY_BUTTA) {
    switch (spop.selector) {
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
}

static void keypress_popup_flash(unsigned newkeys) {
  if ((newkeys & FLASH_GO_KEYS) == FLASH_GO_KEYS)
    start_flash_update(spop.p.update.fn, spop.p.update.fw_size, spop.p.update.issfw);
}

#ifdef SUPPORT_NORGAMES
static void keypress_popup_norwrite(unsigned newkeys) {
  if (newkeys & KEY_BUTTUP)
    spop.selector = MAX(0, spop.selector - 1);
  if (newkeys & KEY_BUTTDOWN)
    spop.selector = MIN(GBAPatchCNT - 1, spop.selector + 1);

  if (spop.submenu == GbaNorWrPatch) {
    if (newkeys & (KEY_BUTTLEFT|KEY_BUTTRIGHT)) {
      if (spop.selector == GBALoadPatch)
        spop.p.norwr.i.patch_type = (spop.p.norwr.i.patch_type +
                                     ((newkeys & KEY_BUTTRIGHT) ? 1 : PatchOptCNT - 1)) % PatchOptCNT;
      else if (spop.selector == GBAInGameMen)
        spop.p.norwr.i.ingame_menu_enabled = !spop.p.norwr.i.ingame_menu_enabled;
      else if (spop.selector == GBASavePatch)
        spop.p.norwr.i.use_dsaving = !spop.p.norwr.i.use_dsaving;
      else if (spop.selector == GBARTCPatch)
        spop.p.norwr.i.rtc_patch_enabled = !spop.p.norwr.i.rtc_patch_enabled;
    }

    if (newkeys & KEY_BUTTLEFT) {
      // Handle the different cases where the user attempts to select an invalid option.
      if (!spop.p.norwr.i.patches_cache_found && spop.p.norwr.i.patch_type == PatchEngine)
        spop.p.norwr.i.patch_type = PatchDatabase;  // Might be invalid, handled below.
      if (!spop.p.norwr.i.patches_datab_found && spop.p.norwr.i.patch_type == PatchDatabase)
        spop.p.norwr.i.patch_type = PatchNone;
    }
    if (newkeys & KEY_BUTTRIGHT) {
      // If the database has no entry, then do not let the user select that mode.
      if (!spop.p.norwr.i.patches_datab_found && spop.p.norwr.i.patch_type == PatchDatabase)
        spop.p.norwr.i.patch_type = PatchEngine;  // Might be invalid, handled below.
      if (!spop.p.norwr.i.patches_cache_found && spop.p.norwr.i.patch_type == PatchEngine)
        spop.p.norwr.i.patch_type = PatchNone;
    }

    // Disable certain features (depends on patch types)
    if (!dirsav_avail_flash(&spop.p.norwr.i))
      spop.p.norwr.i.use_dsaving = false;
    if (!ingame_menu_avail_flash(&spop.p.norwr.i))
      spop.p.norwr.i.ingame_menu_enabled = false;
    if (!rtcemu_avail(&spop.p.norwr.i))
      spop.p.norwr.i.rtc_patch_enabled = false;

    if ((newkeys & KEY_BUTTA) && spop.selector == GBAPatchGen) {
      generate_patches_progress(spop.p.norwr.i.romfn, spop.p.norwr.i.romfs);
      spop.alert_msg = msgs[lang_id][MSG_PATCHGEN_OK];
      // Try/Load the just-generated patches.
      spop.p.norwr.i.patches_cache_found = load_cached_patches(spop.p.norwr.i.romfn, &spop.p.norwr.i.patches_cache);
    }
  } else {
    if (newkeys & KEY_BUTTA) {
      // Check whether we have enough space.
      unsigned blkcnt = (spop.p.norwr.i.romfs + NOR_BLOCK_SIZE - 1) / NOR_BLOCK_SIZE;
      if (smenu.fbrowser.freeblks < blkcnt || smenu.fbrowser.maxentries + 1 >= FLASHG_MAXFN_CNT)
        spop.alert_msg = msgs[lang_id][MSG_ERR_NORSPC];
      else {
        const t_load_gba_info *info = &spop.p.norwr.i;
        const t_patch *p = get_game_patch(info);

        // Allocate the last entry for the new game.
        t_flash_game_entry ne = {
          .gamecode = *(uint32_t*)info->romh.gcode,
          .gamever = info->romh.version,
          .numblks = blkcnt,
          .gattrs = (info->use_dsaving         ? GATTR_SAVEDS : 0) |
                    (info->ingame_menu_enabled ? GATTR_IGM    : 0) |
                    (info->rtc_patch_enabled   ? GATTR_RTC    : 0) |
                    GATTR_SAVEM(p),
          .bnoffset = (uint8_t)(file_basename(info->romfn) - info->romfn),
          .entry_addr = ROM_ENTRYPOINT(info->romh)
        };
        memset(&ne.blkmap, 0, sizeof(ne.blkmap));
        strcpy(ne.game_name, info->romfn);

        flashmgr_allocate_blocks(ne.blkmap, blkcnt, (t_reg_entry*)&sdr_state->nordata);

        // Go ahead and start the flasher-loader with patching support.
        unsigned errc = flash_gba_nor(info->romfn, info->romfs, &info->romh, p,
                                      info->use_dsaving, info->ingame_menu_enabled,
                                      info->rtc_patch_enabled,
                                      ne.blkmap, loadrom_progress,
                                      sdr_state->scratch, scratch_mem_size);
        if (errc)
          spop.alert_msg = msgs[lang_id][errc == ERR_LOAD_BADROM ? MSG_ERR_READ : MSG_ERR_NORUPD];
        else {
          // Now we can just write the metadata entry!
          memcpy32(&sdr_state->nordata.games[smenu.fbrowser.maxentries], &ne, sizeof(ne));
          sdr_state->nordata.gamecnt++;
          if (!flashmgr_store(ROM_FLASHMETA_ADDR, FLASH_METADATA_SIZE, (t_reg_entry*)&sdr_state->nordata))
            spop.alert_msg = msgs[lang_id][MSG_ERR_NORUPD];
          else {
            spop.alert_msg = msgs[lang_id][MSG_NOR_WROK];
            spop.pop_num = POPUP_NONE;
          }
        }

        flashbrowser_reload();
      }
    }
  }
}

static void keypress_popup_norload(unsigned newkeys) {
  if (newkeys & KEY_BUTTUP)
    spop.selector = MAX(0, spop.selector - 1);
  if (newkeys & KEY_BUTTDOWN)
    spop.selector = MIN(GBALdSetCNT - 1, spop.selector + 1);

  const t_flash_game_entry *e = &sdr_state->nordata.games[smenu.fbrowser.selector];
  bool uses_dsave = e->gattrs & GATTR_SAVEDS;
  bool uses_igm   = e->gattrs & GATTR_IGM;
  bool uses_rtc   = e->gattrs & GATTR_RTC;

  if (newkeys & KEY_BUTTLEFT) {
    if (spop.submenu == GbaNorLoad) {
      if (spop.selector == GBALdSetCheats)
        spop.p.norld.l.use_cheats = !spop.p.norld.l.use_cheats;
      if (uses_dsave) {
        if (spop.selector == GBALdSetLoadP)
          spop.p.norld.l.sram_load_type = (spop.p.norld.l.sram_load_type + SaveLoadDSCNT - 1) % SaveLoadDSCNT;
      } else {
        if (spop.selector == GBALdSetLoadP)
          spop.p.norld.l.sram_load_type = (spop.p.norld.l.sram_load_type + SaveLoadCNT - 1) % SaveLoadCNT;
        else if (spop.selector == GBALdSetSaveP)
          spop.p.norld.l.sram_save_type = (spop.p.norld.l.sram_save_type + SaveCNT - 1) % SaveCNT;
      }
    }

    // DirSav forces automatic saving
    if (uses_dsave)
      spop.p.norld.l.sram_save_type = SaveDirect;
    else if (spop.p.norld.l.sram_save_type == SaveDirect)
      spop.p.norld.l.sram_save_type = autosave_default ? SaveReboot : SaveDisable;

    // If DS is selected, do not allow manual mode.
    if (spop.p.norld.l.sram_load_type == SaveLoadDisable && uses_dsave)
      spop.p.norld.l.sram_load_type = SaveLoadSav;
    // If no .sav is available, do not allow that option!
    if (spop.p.norld.l.sram_load_type == SaveLoadSav && !spop.p.norld.l.savefile_found)
      spop.p.norld.l.sram_load_type = uses_dsave ? SaveLoadReset : SaveLoadDisable;
  }
  if (newkeys & KEY_BUTTRIGHT) {
    if (spop.submenu == GbaNorLoad) {
      if (spop.selector == GBALdSetCheats)
        spop.p.norld.l.use_cheats = !spop.p.norld.l.use_cheats;
      if (uses_dsave) {
        if (spop.selector == GBALdSetLoadP)
          spop.p.norld.l.sram_load_type = (spop.p.norld.l.sram_load_type + 1) % SaveLoadDSCNT;
      } else {
        if (spop.selector == GBALdSetLoadP)
          spop.p.norld.l.sram_load_type = (spop.p.norld.l.sram_load_type + 1) % SaveLoadCNT;
        else if (spop.selector == GBALdSetSaveP)
          spop.p.norld.l.sram_save_type = (spop.p.norld.l.sram_save_type + 1) % SaveCNT;
      }
    }

    // DirSav forces automatic saving
    if (uses_dsave)
      spop.p.norld.l.sram_save_type = SaveDirect;
    else if (spop.p.norld.l.sram_save_type == SaveDirect)
      spop.p.norld.l.sram_save_type = autosave_default ? SaveReboot : SaveDisable;

    // If DS is selected, do not allow manual mode.
    if (spop.p.norld.l.sram_load_type == SaveLoadDisable && uses_dsave)
      spop.p.norld.l.sram_load_type = SaveLoadSav;
    // If no .sav is available, do not allow that option!
    if (spop.p.norld.l.sram_load_type == SaveLoadSav && !spop.p.norld.l.savefile_found)
      spop.p.norld.l.sram_load_type = SaveLoadReset;
  }

  // Disable cheat loading if no cheats are avail, or IGM is disabled
  if (!spop.p.norld.l.cheats_found || !uses_igm)
    spop.p.norld.l.use_cheats = false;

  if (newkeys & KEY_BUTTA) {
    if (spop.submenu == GbaLoadPopInfo) {
      const t_flash_game_entry *e = &sdr_state->nordata.games[smenu.fbrowser.selector];
      const int stype = GET_GATTR_SAVEM(e->gattrs);
      const EnumSavetype st = stype < 0 ? SaveTypeNone : stype;
      bool uses_dsave = e->gattrs & GATTR_SAVEDS;
      bool uses_igm   = e->gattrs & GATTR_IGM;
      bool uses_rtc   = e->gattrs & GATTR_RTC;

      t_dirsave_info dsinfo;
      unsigned errsave = prepare_savegame(
        spop.p.norld.l.sram_load_type, spop.p.norld.l.sram_save_type,
        st, &dsinfo, spop.p.norld.l.savefn);
      if (errsave) {
        unsigned errmsg = (errsave == ERR_SAVE_BADSAVE)   ? MSG_ERR_SAVERD :
                          (errsave == ERR_SAVE_CANTALLOC) ? MSG_ERR_SAVEPR :
                          (errsave == ERR_SAVE_BADARG)    ? MSG_ERR_SAVEIT :
                                                            MSG_ERR_SAVEWR;
        spop.alert_msg = msgs[lang_id][errmsg];
        return;
      }
      t_rtc_info rtci = {
        .timestamp = spop.p.norld.l.rtcval,
        .ts_step = rtc_speed(rtcspeed_default)
      };

      // TODO Handle errors, finish missing stuff.
      unsigned err = launch_gba_nor(
        e->game_name,
        e->blkmap, e->numblks,
        uses_dsave ? &dsinfo : NULL,
        uses_rtc ? &rtci : NULL,
        uses_igm,
        spop.p.norld.l.use_cheats ? spop.p.norld.l.cheats_size : 0);
    }
    else if (spop.selector == GBALdRemember) {
      // Save settings to disk now!
      t_rom_load_settings ld_sett = {  // Use defaults in case it doesn't really exist
        .patch_policy = patcher_default,
        .use_igm = ingamemenu_default,
        .use_rtc = rtcpatch_default,
        .use_dsaving = autosave_prefer_ds
      };
      t_rom_launch_settings lh_sett = {
        .use_cheats = spop.p.norld.l.use_cheats,
        .rtcts = spop.p.norld.l.rtcval
      };

      const t_flash_game_entry *e = &sdr_state->nordata.games[smenu.fbrowser.selector];

      // We load the loading settings to ensure we do not overwrite them.
      load_rom_settings(e->game_name, &ld_sett, NULL);
      save_rom_settings(e->game_name, &ld_sett, &lh_sett);
      spop.alert_msg = msgs[lang_id][MSG_REMEMB_CFG_OK];
    }
    else if (spop.selector == GBALdSetRTC) {
      void accept_rtc() {
        spop.p.norld.l.rtcval = date2timestamp(&spop.rtcpop.val);
      }
      if (uses_rtc) {
        timestamp2date(spop.p.norld.l.rtcval, &spop.rtcpop.val);
        spop.rtcpop.callback = accept_rtc;
      }
    }
  }
}
#endif

static void keypress_popup_filemgr(unsigned newkeys) {
  if (newkeys & KEY_BUTTUP)
    spop.selector = MAX(0, spop.selector - 1);
  if (newkeys & KEY_BUTTDOWN)
    spop.selector = MIN(FiMgrCNT - 1, spop.selector + 1);

  if (newkeys & KEY_BUTTA) {
    t_centry *e = sdr_state->fileorder[smenu.browser.selector];
    switch (spop.selector) {
    case FiMgrDelete:
      {
        void remove_file_action(bool confirm) {
          char tmpfn[MAX_FN_LEN];
          strcpy(tmpfn, smenu.browser.cpath);
          strcat(tmpfn, sdr_state->fileorder[smenu.browser.selector]->fname);

          if (confirm) {
            if (FR_OK != f_unlink(tmpfn))
              spop.alert_msg = msgs[lang_id][MSG_ERR_DELFILE];
            else
              spop.alert_msg = msgs[lang_id][MSG_OK_DELFILE];

            browser_reload();   // Force reload so the file disappears!
          }
        }
        spop.qpop.message = msgs[lang_id][MSG_Q0_DELFILE];
        spop.qpop.default_button = msgs[lang_id][MSG_Q_NO];
        spop.qpop.confirm_button = msgs[lang_id][MSG_Q_YES];
        spop.qpop.option = 0;
        spop.qpop.callback = remove_file_action;
        spop.qpop.clear_popup_ok = true;
      }
      break;
    case FiMgrHide:
      {
        char tmpfn[MAX_FN_LEN];
        strcpy(tmpfn, smenu.browser.cpath);
        strcat(tmpfn, sdr_state->fileorder[smenu.browser.selector]->fname);

        if (FR_OK == f_chmod(tmpfn, e->attr ^ AM_HID, AM_HID))
          e->attr ^= AM_HID;
        else
          spop.alert_msg = msgs[lang_id][MSG_ERR_GENERIC];
      }
      spop.pop_num = POPUP_NONE;
      break;

    #ifdef SUPPORT_NORGAMES
    case FiMgrWriteNOR:
      if (e->filesize > MAX_GBA_ROM_SIZE)
        spop.alert_msg = msgs[lang_id][MSG_ERR_TOOBIG];
      else {
        char path[MAX_FN_LEN];
        strcpy(path, smenu.browser.cpath);
        strcat(path, e->fname);

        // Load default loading settings if any.
        t_rom_load_settings ld_sett = {
          .patch_policy = patcher_default,
          .use_igm = ingamemenu_default,
          .use_rtc = rtcpatch_default,
          .use_dsaving = autosave_prefer_ds
        };
        load_rom_settings(path, &ld_sett, NULL);

        if (!prepare_gba_info(&spop.p.norwr.i, &ld_sett, path, e->filesize, false))
          spop.alert_msg = msgs[lang_id][MSG_ERR_READ];
        else {
          spop.pop_num = POPUP_GBA_NORWRITE;
          spop.submenu = GbaLoadPopInfo;
          spop.selector = 0;
        }
      }
      break;
    #endif
    };
  }
}

static void keypress_menu_recent(unsigned newkeys) {
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
      void recent_del_cb(bool confirm) {
        if (confirm) {
          delete_recent_flush(smenu.recent.selector);
        }
      }
      spop.qpop.message = msgs[lang_id][MSG_Q4_DELREC];
      spop.qpop.default_button = msgs[lang_id][MSG_Q_NO];
      spop.qpop.confirm_button = msgs[lang_id][MSG_Q_YES];
      spop.qpop.option = 0;
      spop.qpop.callback = recent_del_cb;
      spop.qpop.clear_popup_ok = false;
    }
  }

  if (smenu.recent.selector < smenu.recent.seloff)
    smenu.recent.seloff = smenu.recent.selector;
  else if (smenu.recent.selector >= smenu.recent.seloff + RECENT_ROWS)
    smenu.recent.seloff = smenu.recent.selector - RECENT_ROWS + 1;
}

static void keypress_menu_browse(unsigned newkeys) {
  if (smenu.browser.dispentries) {
    // Move menu up and down
    if (newkeys & KEY_BUTTUP)
      smenu.browser.selector = MAX(0, smenu.browser.selector - 1);
    if (newkeys & KEY_BUTTDOWN)
      smenu.browser.selector = MIN(smenu.browser.dispentries - 1, smenu.browser.selector + 1);
    if (newkeys & KEY_BUTTLEFT) {
      smenu.browser.selector = MAX(0, smenu.browser.selector - BROWSER_ROWS);
      smenu.browser.seloff   = MAX(0, smenu.browser.seloff - BROWSER_ROWS);
    }
    if (newkeys & KEY_BUTTRIGHT) {
      smenu.browser.selector = MIN(smenu.browser.dispentries - 1, smenu.browser.selector + BROWSER_ROWS);
      smenu.browser.seloff   = MIN(smenu.browser.dispentries - 1, smenu.browser.seloff   + BROWSER_ROWS);
    }
    // Move into a new dir and/or open a file
    if (newkeys & KEY_BUTTA) {
      t_centry *e = sdr_state->fileorder[smenu.browser.selector];
      if (e->isdir) {
        strcat(smenu.browser.cpath, e->fname);
        strcat(smenu.browser.cpath, "/");
        // Push selector history and reset it in the new dir
        memmove(&smenu.browser.selhist[1], &smenu.browser.selhist[0],
                sizeof(smenu.browser.selhist) - sizeof(smenu.browser.selhist[0]));
        smenu.browser.selhist[0] = smenu.browser.selector;
        smenu.browser.selector = 0;
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
      spop.pop_num = POPUP_FILE_MGR;
      spop.anim = 0;
      spop.selector = 0;
    }
  }
  if (newkeys & KEY_BUTTB) {
    // Try to go up in the dir structure
    if (movedir_up()) {
      smenu.browser.selector = smenu.browser.selhist[0];
      memmove(&smenu.browser.selhist[0], &smenu.browser.selhist[1],
              sizeof(smenu.browser.selhist) - sizeof(smenu.browser.selhist[0]));
      browser_reload();
    }
  }

  // Selector was updated, figure out how we update the menu params so it
  // can be rendered properly.
  if (smenu.browser.selector < smenu.browser.seloff)
    smenu.browser.seloff = smenu.browser.selector;
  else if (smenu.browser.selector >= smenu.browser.seloff + BROWSER_ROWS)
    smenu.browser.seloff = smenu.browser.selector - BROWSER_ROWS + 1;
}

#ifdef SUPPORT_NORGAMES
static void keypress_menu_norbrowse(unsigned newkeys) {
  if (smenu.fbrowser.maxentries) {
    if (newkeys & KEY_BUTTUP)
      smenu.fbrowser.selector = MAX(0, smenu.fbrowser.selector - 1);
    if (newkeys & KEY_BUTTDOWN)
      smenu.fbrowser.selector = MIN(smenu.fbrowser.maxentries - 1, smenu.fbrowser.selector + 1);
    if (newkeys & KEY_BUTTLEFT) {
      smenu.fbrowser.selector = MAX(0, smenu.fbrowser.selector - NORGAMES_ROWS);
      smenu.fbrowser.seloff   = MAX(0, smenu.fbrowser.seloff - NORGAMES_ROWS);
    }
    if (newkeys & KEY_BUTTRIGHT) {
      smenu.fbrowser.selector = MIN(smenu.fbrowser.maxentries - 1, smenu.fbrowser.selector + NORGAMES_ROWS);
      smenu.fbrowser.seloff   = MIN(smenu.fbrowser.maxentries - 1, smenu.fbrowser.seloff   + NORGAMES_ROWS);
    }

    if (newkeys & KEY_BUTTA) {
      t_flash_game_entry *e = &sdr_state->nordata.games[smenu.fbrowser.selector];

      // Use attributes to determine patched save method.
      const bool game_no_save = GET_GATTR_SAVEM(e->gattrs) <= SaveTypeNone;
      const bool game_uses_dsaving = (e->gattrs & GATTR_SAVEDS);

      t_rom_launch_settings lh_sett = {
        .use_cheats = true,              // Defaults to true (just preferred, might be disabled/N/A)
        .rtcts = rtcvalue_default
      };
      load_rom_settings(e->game_name, NULL, &lh_sett);

      // Attempt to find a cheat file if cheats are enabled.
      prepare_gba_cheats((char*)&e->gamecode, e->gamever, &spop.p.norld.l, e->game_name, lh_sett.use_cheats);

      // Load and set default and sane settings honoring defaults and preferences.
      prepare_gba_settings(&spop.p.norld.l, game_uses_dsaving, lh_sett.rtcts, game_no_save, e->game_name);

      // Show load ROM menu.
      spop.pop_num = POPUP_GBA_NORLOAD;
      spop.submenu = GbaLoadPopInfo;
      spop.selector = 0;
    }
    else if (newkeys & KEY_BUTTSEL) {
      // Prompt NOR entry deletion.
      void remove_nor_action(bool confirm) {
        if (!confirm)
          return;

        // Remove game entry, just memmove the other games on top.
        sdr_state->nordata.gamecnt--;
        memmove32(&sdr_state->nordata.games[smenu.fbrowser.selector],
                  &sdr_state->nordata.games[smenu.fbrowser.selector + 1],
                  (sdr_state->nordata.gamecnt - smenu.fbrowser.selector) * sizeof(t_flash_game_entry));

        // Go ahead and write a new metadata entry;
        if (!flashmgr_store(ROM_FLASHMETA_ADDR, FLASH_METADATA_SIZE, (t_reg_entry*)&sdr_state->nordata))
          spop.alert_msg = msgs[lang_id][MSG_ERR_NORUPD];
        flashbrowser_reload();   // Force list reload, free block calculation, etc.
      }
      spop.qpop.message = msgs[lang_id][MSG_Q5_DELNORG];
      spop.qpop.default_button = msgs[lang_id][MSG_Q_NO];
      spop.qpop.confirm_button = msgs[lang_id][MSG_Q_YES];
      spop.qpop.option = 0;
      spop.qpop.callback = remove_nor_action;
      spop.qpop.clear_popup_ok = true;
    }

    if (smenu.fbrowser.selector < smenu.fbrowser.seloff)
      smenu.fbrowser.seloff = smenu.fbrowser.selector;
    else if (smenu.fbrowser.selector >= smenu.fbrowser.seloff + NORGAMES_ROWS)
      smenu.fbrowser.seloff = smenu.fbrowser.selector - NORGAMES_ROWS + 1;
  }
}
#endif

static void keypress_menu_settings(unsigned newkeys) {
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
    else if (smenu.set.selector == DefsRTCSpeed)
      rtcspeed_default = (rtcspeed_default + rtc_speed_cnt() - 1) % rtc_speed_cnt();
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
    else if (smenu.set.selector == DefsRTCSpeed)
      rtcspeed_default = (rtcspeed_default + 1) % rtc_speed_cnt();
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
      use_slowld ^= 1;
    else if (smenu.set.selector == SettFastEWRAM)
      use_fastew = fastew ? (use_fastew ^ 1) : 0;
  }

  if (newkeys & KEY_BUTTA && smenu.set.selector == DefsRTCVal) {
    void accept_rtc() {
      rtcvalue_default = date2timestamp(&spop.rtcpop.val);
    }
    timestamp2date(rtcvalue_default, &spop.rtcpop.val);
    spop.rtcpop.callback = accept_rtc;
  }
  if (newkeys & KEY_BUTTA && smenu.set.selector == SettSave) {
    smenu.set.selector = 0;
    if (save_settings())
      spop.alert_msg = msgs[lang_id][MSG_OK_SETSAVE];
    else
      spop.alert_msg = msgs[lang_id][MSG_ERR_SETSAVE];
  }
}

static void keypress_menu_uisettings(unsigned newkeys) {
  if (newkeys & KEY_BUTTUP)
    smenu.uiset.selector = MAX(0, smenu.uiset.selector - 1);
  if (newkeys & KEY_BUTTDOWN)
    smenu.uiset.selector = MIN(UiSetMAX, smenu.uiset.selector + 1);
  if (newkeys & KEY_BUTTLEFT) {
    if (smenu.uiset.selector == UiSetTheme)
      menu_theme = menu_theme ? menu_theme - 1 : 0;
    else if (smenu.uiset.selector == UiSetASpd)
      anim_speed = anim_speed ? anim_speed - 1 : 0;
    else if (smenu.uiset.selector == UiSetHid)
      hide_hidden ^= 1;
    else if (smenu.uiset.selector == UiSetRect)
      recent_menu ^= 1;
    else if (smenu.uiset.selector == UiSetLang)
      lang_id = (lang_id + LANG_COUNT - 1) % LANG_COUNT;
  }
  if (newkeys & KEY_BUTTRIGHT) {
    if (smenu.uiset.selector == UiSetTheme)
      menu_theme = MIN(THEME_COUNT - 1, menu_theme + 1);
    else if (smenu.uiset.selector == UiSetASpd)
      anim_speed = MIN(animspd_cnt - 1, anim_speed + 1);
    else if (smenu.uiset.selector == UiSetHid)
      hide_hidden ^= 1;
    else if (smenu.uiset.selector == UiSetRect)
      recent_menu ^= 1;
    else if (smenu.uiset.selector == UiSetLang)
      lang_id = (lang_id + 1) % LANG_COUNT;
  }

  if (newkeys & KEY_BUTTA && smenu.uiset.selector == UiSetSave) {
    smenu.uiset.selector = 0;
    if (save_ui_settings())
      spop.alert_msg = msgs[lang_id][MSG_OK_SETSAVE];
    else
      spop.alert_msg = msgs[lang_id][MSG_ERR_SETSAVE];
  }

  reload_theme(menu_theme);
}

static void keypress_menu_tools(unsigned newkeys) {
  if (newkeys & KEY_BUTTUP)
    smenu.tools.selector = MAX(0, smenu.tools.selector - 1);
  if (newkeys & KEY_BUTTDOWN)
    smenu.tools.selector = MIN(ToolsMAX - 1, smenu.tools.selector + 1);

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
      slowsd = use_slowld;
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

      browser_reload();
    }
    #ifdef SUPPORT_NORGAMES
    else if (smenu.tools.selector == ToolsFlashClr) {
      void flash_clear_callback(bool confirm) {
        if (confirm) {
          // Delete all metadata (data is not really wiped, takes too long)
          if (flashmgr_wipe(ROM_FLASHMETA_ADDR, FLASH_METADATA_SIZE))
            spop.alert_msg = msgs[lang_id][MSG_NOR_CLOK];
          else
            spop.alert_msg = msgs[lang_id][MSG_ERR_NORUPD];

          flashbrowser_reload();     // Ensure we clear the NOR entries from RAM.
        }
      }
      // Prompt the user for clearing the memory.
      spop.qpop.message = msgs[lang_id][MSG_Q6_CLRNOR];
      spop.qpop.default_button = msgs[lang_id][MSG_Q_NO];
      spop.qpop.confirm_button = msgs[lang_id][MSG_Q_YES];
      spop.qpop.option = 0;
      spop.qpop.callback = flash_clear_callback;
      spop.qpop.clear_popup_ok = true;
    }
    #endif
  }
}

static void keypress_menu_info(unsigned newkeys) {
  if (newkeys & KEY_BUTTA)
    smenu.info.selector = (smenu.info.selector + 1) % 4;
  if ((newkeys & FLASH_UNLOCK_KEYS) == FLASH_UNLOCK_KEYS)
    enable_flashing = true;
}


void menu_keypress(unsigned newkeys) {
  if (spop.alert_msg) {
    // Modal message pop up!
    if (newkeys & (KEY_BUTTA | KEY_BUTTB))
      spop.alert_msg = NULL;
  }
  else if (spop.qpop.message) {
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
  }
  else if (spop.rtcpop.callback) {
    if (newkeys & KEY_BUTTLEFT)
      spop.rtcpop.selector = MAX(0, spop.rtcpop.selector - 1);
    if (newkeys & KEY_BUTTRIGHT)
      spop.rtcpop.selector = MIN(4, spop.rtcpop.selector + 1);

    if (newkeys & KEY_BUTTUP)
      ((uint8_t*)&spop.rtcpop.val)[spop.rtcpop.selector]++;
    if (newkeys & KEY_BUTTDOWN)
      ((uint8_t*)&spop.rtcpop.val)[spop.rtcpop.selector]--;

    if (newkeys & (KEY_BUTTUP|KEY_BUTTDOWN))
      fixdate(&spop.rtcpop.val);

    if (newkeys & KEY_BUTTB) {
      spop.rtcpop.selector = 0;
      spop.rtcpop.callback = NULL;
    }
    else if (newkeys & KEY_BUTTA) {
      spop.rtcpop.selector = 0;
      spop.rtcpop.callback();
      spop.rtcpop.callback = NULL;
    }
  }
  else if (spop.pop_num) {
    const int subcnt = popup_windows[spop.pop_num - 1].max_submenu;
    if (newkeys & KEY_BUTTL)
      spop.submenu = (spop.submenu + subcnt - 1) % subcnt;
    if (newkeys & KEY_BUTTR)
      spop.submenu = (spop.submenu + 1) % subcnt;

    // Close pop-up on B button
    if (newkeys & KEY_BUTTB)
      spop.pop_num = 0;
    else {
      const t_mkeyupd_fn keyfns[] = {
        NULL,
        keypress_popup_loadgba,
        keypress_popup_savefile,
        keypress_popup_flash,
        keypress_popup_filemgr,
        #ifdef SUPPORT_NORGAMES
        keypress_popup_norwrite,
        keypress_popup_norload,
        #endif
      };
      keyfns[spop.pop_num](newkeys);
    }
  } else {
    // Menu change via trigger buttons
    int mintab = (recent_menu && smenu.recent.maxentries) ? MENUTAB_RECENT : MENUTAB_ROMBROWSE;
    if (newkeys & KEY_BUTTL)
      smenu.menu_tab = MAX((int)smenu.menu_tab - 1, mintab);
    else if (newkeys & KEY_BUTTR)
      smenu.menu_tab = MIN(smenu.menu_tab + 1, MENUTAB_MAX - 1);

    if (newkeys & (KEY_BUTTL | KEY_BUTTR | KEY_BUTTUP | KEY_BUTTDOWN))
      smenu.anim_state = 0;

    const t_mkeyupd_fn keyfns[] = {
      keypress_menu_recent,
      keypress_menu_browse,
      #ifdef SUPPORT_NORGAMES
      keypress_menu_norbrowse,
      #endif
      keypress_menu_settings,
      keypress_menu_uisettings,
      keypress_menu_tools,
      keypress_menu_info,
    };
    keyfns[smenu.menu_tab](newkeys);
  }
}

