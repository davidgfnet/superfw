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

#include "compiler.h"
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
#include "recent.h"
#include "flash_mgr.h"
#include "flash.h"
#include "sha256.h"
#include "supercard_driver.h"

#include "carousel_util.h"
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
  MENUTAB_CAROUSEL,      // Browses the current folder's games as cover art.
  MENUTAB_SETTINGS,      // General settings / defaults
  MENUTAB_UILANG,        // UI / Language settings
  MENUTAB_TOOLS,         // Tools (advaned menu)
  MENUTAB_INFO,          // Info / About / Updater?
  MENUTAB_MAX,
};

#define ANIM_INITIAL_WAIT     128    // Intial wait (in anim cycles)

#define KEY_REPEAT_INITIAL    384    // Initial wait for key repeat
#define KEY_REPEAT_MID        160    // Next key presses
#define KEY_REPEAT_FAST        96    // End speed
#define KEY_REPEAT_CNT1         5

enum {
  POPUP_NONE,
  POPUP_GBA_LOAD,              // Load a GBA ROM
  POPUP_SAVFILE,               // Load/Store a SAV file
  POPUP_FWFLASH,               // Flash a new firmware image
  POPUP_FILE_MGR,              // Write ROM to flash, delete, hide/unhide...
  POPUP_CAR_VIEW,              // Carousel view: games shown and their order
#ifdef SUPPORT_NORGAMES
  POPUP_GBA_NORWRITE,          // Write a GBA ROM to NOR
  POPUP_GBA_NORLOAD,           // Launch a NOR game
#endif
};

#define BROWSER_ROWS                 8
#define RECENT_ROWS                  9
#define NORGAMES_ROWS                8

// Carousel. Covers live at /.superfw/covers/<rom file name>.cov (or for GBA
// games <game code>.cov) and are generated on a PC by tools/make-covers.py.
#define COVER_PATH          SUPERFW_DIR "/covers/"
#define COVER_EXT           ".cov"
#define COVER_SLOTS          5             // Cached covers (selected +-2)
#define COVER_SPACING       76             // Distance between cover centers
#define COVER_CENTER_Y      62             // Vertical center of the covers
#define CAROUSEL_BG_COLOR  COVER_BG_INDEX_V2   // Tinted background (palette entry)

// Carousel game list: all games under the browser's current dir (recursive).
#define CAROUSEL_MAX_GAMES  4096
#define CAROUSEL_POOL_SIZE  (128*1024)     // Relative paths, NUL terminated, word aligned
#define CAROUSEL_MAX_DEPTH     8           // Subdirectory levels scanned
#define CAROUSEL_CACHE_PATH SUPERFW_DIR "/carousel.cache"
#define CAROUSEL_CACHE_MAGIC 0x43434653    // "SFCC"
#define CAROUSEL_CACHE_VER   1
#define CAROUSEL_FAV_PATH    SUPERFW_DIR "/favorites.txt"   // Full paths, one per line
#define CAROUSEL_FAV_TMP     SUPERFW_DIR "/favorites.tmp"

// Carousel views (Start). car_view: CarViewAll, CarViewFav, or CarViewSys +
// a system (0: GBA, 1+: emu_group() + 1). car_sort: CarSort*.
enum { CarViewAll = 0, CarViewFav = 1, CarViewSys = 2 };
enum { CarSortName = 0, CarSortSystem = 1, CarSortRecent = 2, CarSortCNT = 3 };
#define CAROUSEL_MAX_SYS    16             // GBA + the emulated systems
#define CATTR_SYS         0x0F             // carousel attributes: the system,
#define CATTR_FAV         0x10             // favourite,
#define CATTR_RANK_SH        8             // and position in the recent list (0xFF: none)

typedef struct {
  uint32_t filesize;
  uint32_t pathoff;         // Offset of the path (relative to the scan root) in the pool
  uint16_t nameoff;         // Offset of the file name within the path
  uint16_t attr;            // CATTR_* (not cached: set by carousel_load_attrs())
} t_cgame;
_Static_assert (sizeof(t_cgame) % 4 == 0, "t_cgame must be word-friendly");

// Game list cache file: header, then the games and the path pool.
typedef struct {
  uint32_t magic, version;
  uint32_t count, poolsize;
  uint32_t signature;       // Hash of the scan (see cscan_step)
  uint32_t flags;           // Bit0: list truncated, Bit1: hidden files excluded
  char root[MAX_FN_LEN];
} t_ccache_hdr;
_Static_assert (sizeof(t_ccache_hdr) % 4 == 0, "t_ccache_hdr must be word-friendly");

enum {
  CoverNotLoaded = 0,
  CoverLoaded    = 1,
  CoverMissing   = 2,
};

// First entries reserved for the logo palette.
#define FG_COLOR         16
#define BG_COLOR         17
#define FT_COLOR         18
#define HI_COLOR         19
#define IGM_PAL_FG      240
#define IGM_PAL_BG      241
#define IGM_PAL_HI      242
#define IGM_PAL_SH      243
#define IGM_PAL_BL      244
#define SEL_COLOR       255

#define FLASH_UNLOCK_KEYS      (KEY_BUTTDOWN|KEY_BUTTB|KEY_BUTTSTA)
#define FLASH_GO_KEYS          (KEY_BUTTUP|KEY_BUTTL|KEY_BUTTR)

enum {
  UiSetTheme = 0,
  UiSetLang  = 1,
  UiSetRect  = 2,
  UiSetASpd  = 3,
  UiSetHid   = 4,
  UiSetStart = 5,
  UiSetExt   = 6,
  UiSetSave  = 7,
  UiSetMAX   = 7,
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
  SettTitle1 = 0,
  SettHotkey,
  SettBootType,
  SettFastSD,
  #ifdef SUPPORT_NORGAMES
  SettVerifyNOR,
  #endif
  SettFastEWRAM,
  SettSaveLoc,
  #ifdef SUPPORT_NORGAMES
  SettSaveLocNOR,
  #endif
  SettSaveBkp,
  SettStateLoc,
  SettCheatEn,
  SettTitle2,
  DefsPatchEng,
  DefsGamMenu,
  DefsRTCEnb,
  DefsRTCVal,
  DefsRTCSpeed,
  DefsLoadPol,
  DefsSavePol,
  DefsPrefDS,
  SettSave,
  SettMAX
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

// File manager options. The SD browser shows Delete, Hide and Write (NOR); the
// carousel shows the same plus Remove (NOR) / Load from SD for games already
// in flash. spop.fmgr.opts holds the options shown, in order.
enum {
  FiMgrDelete,
  FiMgrHide,
  FiMgrFav,                    // Carousel only: favourite on/off
#ifdef SUPPORT_NORGAMES
  FiMgrWriteNOR,
  FiMgrRemoveNOR,
  FiMgrLoadSD,
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
  { RGB2GBA(0xaaaaaa), RGB2GBA(0xffffff), RGB2GBA(0x000000), RGB2GBA(0xcccccc), RGB2GBA(0x9999bb), RGB2GBA(0xc08888) }, // White
  { RGB2GBA(0xeca551), RGB2GBA(0xe7c092), RGB2GBA(0x000000), RGB2GBA(0xbda27b), RGB2GBA(0x90816e), RGB2GBA(0x615d58) }, // Orange
  { RGB2GBA(0x26879c), RGB2GBA(0x8fb1b8), RGB2GBA(0x000000), RGB2GBA(0x5296a5), RGB2GBA(0x1d7f95), RGB2GBA(0x6f8185) }, // Blue
  { RGB2GBA(0x308855), RGB2GBA(0x88aa99), RGB2GBA(0x000000), RGB2GBA(0x778888), RGB2GBA(0x777777), RGB2GBA(0x606060) }, // Green
  { RGB2GBA(0xad11c8), RGB2GBA(0xe47af6), RGB2GBA(0x000000), RGB2GBA(0xad5dc6), RGB2GBA(0x724095), RGB2GBA(0x72667a) }, // Purple
  { RGB2GBA(0x222222), RGB2GBA(0x444444), RGB2GBA(0xeeeeee), RGB2GBA(0x737573), RGB2GBA(0xaaaaaa), RGB2GBA(0x606060) }, // Dark
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

  // Carousel state (games in the ROM browser's current dir and its subdirs)
  struct {
    int selector;                 // Selected game (position in the view)
    int count;                    // Number of games in the view (the ones shown)
    int total;                    // Number of games in the list (carousel_games)
    uint16_t syscnt[CAROUSEL_MAX_SYS];   // Games per system
    bool truncated;               // Too many games, the list is incomplete
    unsigned scan_frame;          // Frame "Looking for games" was shown (0: not shown)
    int offset;                   // Slide animation offset (pixels, even)
    unsigned lastframe;           // Frame count at the last render
    unsigned browser_gen;         // Browser list generation it was built from
    char root[MAX_FN_LEN];        // Dir that was scanned (ends with '/')
    uint32_t signature;           // Scan hash of the list (to validate the cache)
    bool verifying;               // Checking a cached list in the background
    bool stale;                   // The cached list is outdated, rescan it
    int slot_entry[COVER_SLOTS];  // Game loaded in each cover slot (-1: none)
    uint8_t slot_state[COVER_SLOTS];
    int16_t slot_tint[COVER_SLOTS];   // Cover background tint (-1: none)
    uint16_t tint;                // Current background color (animated)
    bool tint_ok;                 // tint was initialized
    int info_for;                 // Game the info below was computed for (-1: none)
    bool info_save;               // It has a save file
    bool info_last;               // It is the last played game
    bool info_flash;              // It was written to flash (A starts it from there)
    unsigned letter_until;        // Show the big letter until this frame
    char letter[8];               // Letter shown on Up/Down jumps (utf-8)
  } carousel;

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

  // File manager pop up target (outside of the union: it is still needed
  // while its confirmation questions are shown).
  struct {
    char path[MAX_FN_LEN];        // File to act on
    uint32_t filesize;
    uint8_t attr;
    bool carousel;                // Opened from the carousel (compact layout)
    int nor_index;                // Its flash entry (-1: not in flash)
    int cgame;                    // Its carousel game (-1: opened from the browser)
    uint8_t optcnt;
    uint8_t opts[FiMgrCNT];       // Options shown, in order
  } fmgr;

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
      const t_flash_game_entry *e;        // NOR game entry on RAM
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
  t_cgame carousel_games[CAROUSEL_MAX_GAMES];
  char carousel_pool[CAROUSEL_POOL_SIZE];
  uint32_t carousel_view[CAROUSEL_MAX_GAMES];   // Games shown, in order (carousel_games index)
  uint8_t cover_big[COVER_SLOTS][COVER_BIG * COVER_BIG];
  uint8_t cover_small[COVER_SLOTS][COVER_SMALL * COVER_SMALL];
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
static unsigned browser_gen = 0;   // Bumped every time the browser list changes
static bool reboot_on_alert = false;   // Reboot once the alert message is dismissed

unsigned lang_lookup(uint16_t code) {
  for (unsigned i = 0; i < LANG_COUNT; i++)
    if (lang_codes[i] == code)
      return i;

  return 0;  // Fallback to default (english)
}

// Menu strings: the strings of all languages live compressed in the "LANG"
// virtual file (SDRAM). Only the chosen language is kept in EWRAM, in
// lang_blob, and msg_table points into it (see res/messages.py). Unpacking
// takes a while, so the chosen language is also kept unpacked on the SD card
// (lang.cache): boots only read that file, unless the language or the
// firmware (pack CRC) changed.
#define LANG_PACK_MAGIC    0x504E474C    // "LNGP"
#define LANG_PACK_VERSION  2
#define LANG_CACHE_MAGIC   0x434E474C    // "LNGC"
#define LANG_CACHE_PATH    SUPERFW_DIR "/lang.cache"

typedef struct {
  uint32_t magic;
  uint16_t version, nlangs, nmsgs, reserved;
  uint32_t rawsize;         // Size of all the blobs, unpacked
  uint32_t crc;             // CRC32 of the compressed stream (identifies the pack)
} t_lang_pack_hdr;

typedef struct {
  uint32_t magic;
  uint32_t packcrc;         // Pack it was unpacked from
  uint16_t lang, nmsgs;
  uint32_t size;            // Blob size, follows this header
} t_lang_cache_hdr;

typedef struct {
  uint32_t offset, size;    // Blob position in the unpacked data
} t_lang_pack_entry;

// +4: the unpacker can write one byte past the end.
static uint32_t lang_blob[(LANG_BLOB_MAX + 4) / 4] __attribute__((section(".ewram")));

// The unpacked pack stays in the scratch area, so switching languages is fast.
// Anything else using the scratch area must call lang_scratch_used().
static bool lang_unpacked = false;

static void lang_scratch_used() {
  lang_unpacked = false;
}

// Reads the cached blob of a language. False if missing or not for this pack.
static bool lang_cache_read(unsigned lang, uint32_t packcrc, unsigned *blobsize) {
  FIL fd;
  if (FR_OK != f_open(&fd, LANG_CACHE_PATH, FA_READ))
    return false;

  UINT rdbytes;
  t_lang_cache_hdr ch;
  bool ok = FR_OK == f_read(&fd, &ch, sizeof(ch), &rdbytes) && rdbytes == sizeof(ch) &&
            ch.magic == LANG_CACHE_MAGIC && ch.packcrc == packcrc && ch.lang == lang &&
            ch.nmsgs == MSG_COUNT && ch.size >= MSG_COUNT * 2 && ch.size <= LANG_BLOB_MAX &&
            FR_OK == f_read(&fd, lang_blob, ch.size, &rdbytes) && rdbytes == ch.size;
  f_close(&fd);
  if (ok)
    *blobsize = ch.size;
  return ok;
}

static void lang_cache_write(unsigned lang, uint32_t packcrc, unsigned blobsize) {
  t_lang_cache_hdr ch = {
    .magic = LANG_CACHE_MAGIC, .packcrc = packcrc,
    .lang = lang, .nmsgs = MSG_COUNT, .size = blobsize,
  };
  FIL fd;
  f_mkdir(SUPERFW_DIR);
  if (FR_OK != f_open(&fd, LANG_CACHE_PATH, FA_WRITE | FA_CREATE_ALWAYS))
    return;
  UINT wrbytes;
  bool ok = FR_OK == f_write(&fd, &ch, sizeof(ch), &wrbytes) && wrbytes == sizeof(ch) &&
            FR_OK == f_write(&fd, lang_blob, blobsize, &wrbytes) && wrbytes == blobsize;
  f_close(&fd);
  if (!ok)
    f_unlink(LANG_CACHE_PATH);
}

// Loads the strings of a language. Must be called before any text is shown
// and every time lang_id changes. Shows "?" for every string if it fails.
void lang_load(unsigned lang) {
  const uint16_t *offs = (const uint16_t*)lang_blob;
  unsigned blobsize = 0;

  // Read the pack header (the assets area is not reachable with the SD card
  // interface mapped), keep what we need.
  set_supercard_mode(MAPPED_SDRAM, true, false);
  const t_lang_pack_hdr *hdr = get_vfile_ptr("LANG");
  bool valid = hdr && hdr->magic == LANG_PACK_MAGIC && hdr->version == LANG_PACK_VERSION &&
               hdr->nmsgs == MSG_COUNT && lang < hdr->nlangs && hdr->rawsize <= scratch_mem_size - 4;
  const uint32_t packcrc = valid ? hdr->crc : 0;
  set_supercard_mode(MAPPED_SDRAM, true, true);

  // Fast path: the unpacked language is on the SD card already.
  if (valid && !lang_cache_read(lang, packcrc, &blobsize)) {
    set_supercard_mode(MAPPED_SDRAM, true, false);
    const t_lang_pack_entry *entries = (const t_lang_pack_entry*)&hdr[1];
    const t_lang_pack_entry *e = &entries[lang];
    if (e->size >= MSG_COUNT * 2 && e->size <= LANG_BLOB_MAX && !(e->offset & 3) &&
        e->offset + e->size <= hdr->rawsize) {
      // Unpack all the languages to the scratch area (unless they are still
      // there), keep only this one.
      if (!lang_unpacked)
        lang_unpacked = upkr_unpack16(sdr_state->scratch, (const uint8_t*)&entries[hdr->nlangs]) >= hdr->rawsize;
      if (lang_unpacked) {
        memcpy32(lang_blob, &sdr_state->scratch[e->offset], ROUND_UP2(e->size, 4));
        blobsize = e->size;
      }
    }
    set_supercard_mode(MAPPED_SDRAM, true, true);

    if (blobsize)
      lang_cache_write(lang, packcrc, blobsize);
  }

  // Strings must end inside the buffer, even with a damaged cache file.
  memset(&((uint8_t*)lang_blob)[blobsize], 0, 4);

  for (unsigned i = 0; i < MSG_COUNT; i++)
    msg_table[i] = (blobsize && offs[i] < blobsize) ? (const char*)lang_blob + offs[i] : "?";
}

inline uint16_t curr_pressed_keys() {
  return REG_KEYINPUT ^ 0x3FF;
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

NOINLINE int filesort(const void *a, const void *b) {
  const t_centry *ca = *(t_centry**)a;
  const t_centry *cb = *(t_centry**)b;

  // Directories some up first.
  if (ca->isdir != cb->isdir)
    return cb->isdir - ca->isdir;

  // Other files are string-ordered
  return strcmp16(ca->sortname, cb->sortname);
}

NOINLINE int romsort(const void *a, const void *b) {
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

  WRITE_LOG("Patch engine done. Found wcnt: %d save: %d (save mode: %d) irqh: %d rtc: %d",
            pb.p.wcnt_ops, pb.p.save_ops, pb.p.save_mode, pb.p.irqh_ops, pb.p.rtc_ops);

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

  if (!info->patches_datab_found)
    WRITE_LOG("No patches in PatchDB found for '%s' with gamecode %c%c%c%c-%d",
              fn, gamecode[0], gamecode[1], gamecode[2], gamecode[3], (int)gamecode[4]);

  // Attempt to load any existing patch and check also the PE cache dir.
  info->patches_cache_found = load_rom_patches(fn, &info->patches_cache);
  if (!info->patches_cache_found) {
    WRITE_LOG("No patch file found for '%s'", fn);
    info->patches_cache_found = load_cached_patches(fn, &info->patches_cache);
    if (!info->patches_cache_found)
      WRITE_LOG("No patch file found in patches cache dir for '%s'", fn);
  }

  // If PatchAuto is selected, resolve it. Downgrade if not found.
  if (st->patch_policy == PatchAuto) {
    if (info->patches_cache_found)
      info->patch_type = PatchEngine;      // Try existing patches
    else if (info->patches_datab_found)
      info->patch_type = PatchDatabase;    // Try the database then
    else
      info->patch_type = PatchNone;
  }
  else {
    // Use the specified patch type.
    info->patch_type = st->patch_policy;

    // Downgrade to no patches if the specified was not found.
    if ((info->patch_type == PatchDatabase && !info->patches_datab_found) ||
        (info->patch_type == PatchEngine   && !info->patches_cache_found))
      info->patch_type = PatchNone;
  }

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
    data->cheats_found = path_join_ext(data->cheatsfn, "", fn, ".cht") &&
                         check_file_exists(data->cheatsfn);
    if (!data->cheats_found) {
      WRITE_LOG("No cheat file found at '%s'", data->cheatsfn);
      // Create a path using the game ID and version.
      npf_snprintf(data->cheatsfn, sizeof(data->cheatsfn), CHEATS_PATH "%c%c%c%c-%02x.cht",
                   gcode[0], gcode[1], gcode[2], gcode[3], ver);
      data->cheats_found = check_file_exists(data->cheatsfn);
      WRITE_LOG("No cheat file found at '%s'", data->cheatsfn);

      // Load the cheats into memory if enabled.
      if (data->cheats_found) {
        // Load the cheats to the ROM area, just after the font pack. This is for easier relocation.
        uint8_t *cheat_area = (uint8_t*)(ROM_FONTBASE_U8 + font_block_size());
        unsigned max_area = 1536*1024 - font_block_size();    // 1.5MB is reserved at the end.
        int cheatsz = open_read_cheats(cheat_area, max_area, data->cheatsfn);
        if (cheatsz < 0)
          data->cheats_found = false;
        else
          data->cheats_size = cheatsz;

        WRITE_LOG("Loaded cheats returned %d", cheatsz);
      }
    }
  }
  data->use_cheats = enable_cheats && data->cheats_found && prefer_cheats;
}

static void prepare_gba_settings(t_load_gba_lcfg *data, bool uses_dsaving, uint32_t rtcts, bool game_no_save) {
  // Calculate the .sav file name, and check its existance.
  data->savefile_found = check_file_exists(data->savefn);
  if (data->savefile_found)
    WRITE_LOG("Savefile found at '%s'", data->savefn);
  else
    WRITE_LOG("No savefile found for '%s'", data->savefn);

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
      sram_filename_calc(fn, spop.p.load.l.savefn, save_path_default);
      prepare_gba_settings(&spop.p.load.l, spop.p.load.i.use_dsaving, lh_sett.rtcts, game_no_save);

      // Show load ROM menu.
      spop.pop_num = POPUP_GBA_LOAD;
      spop.anim = 0;
      spop.submenu = GbaLoadPopInfo;
      spop.selector = GBALoadButt;
    }
  }
}

#ifdef SUPPORT_NORGAMES
static void browser_open_nor(const t_flash_game_entry * e) {
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
  sram_filename_calc(e->game_name, spop.p.norld.l.savefn, save_path_nor_default);
  WRITE_LOG("loadp: %d savep: %d uses_ds: %d gamens: %d",
            spop.p.norld.l.sram_load_type, spop.p.norld.l.sram_save_type,
            game_uses_dsaving, game_no_save);
  prepare_gba_settings(&spop.p.norld.l, game_uses_dsaving, lh_sett.rtcts, game_no_save);

  // Save entry pointer
  spop.p.norld.e = e;

  // Show load ROM menu.
  spop.pop_num = POPUP_GBA_NORLOAD;
  spop.submenu = GbaLoadPopInfo;
  spop.selector = 0;
}
#endif

void patch_gen_callback(bool confirm) {
  // Generate patches if confirm was selected
  if (confirm) {
    generate_patches_progress(spop.p.load.i.romfn, spop.p.load.i.romfs);
    spop.alert_msg = msgs[lang_id][MSG_PATCHGEN_OK];
  }

  // Either way, show the popup screen afterwards without prompt
  browser_open_gba(spop.p.load.i.romfn, spop.p.load.i.romfs, false);
}

static void load_patchdb_action(bool confirm) {
  if (confirm) {
    // It must fit its SDRAM area, or it would overwrite the assets after it
    // (bundled emulators, language pack).
    if (spop.p.pdb_ld.fs > PATCHDB_MAX_SIZE) {
      spop.alert_msg = msgs[lang_id][MSG_ERR_TOOBIG];
      return;
    }
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

static bool insert_recent_flush(const char *fn, unsigned flags) {
  // Insert element.
  smenu.recent.maxentries = insert_recent_fn(sdr_state->rentries, smenu.recent.maxentries, fn, flags);
  return recent_flush(sdr_state->rentries, smenu.recent.maxentries);
}

static bool delete_recent_flush(unsigned entry_num) {
  smenu.recent.maxentries = delete_recent(sdr_state->rentries, smenu.recent.maxentries, entry_num);

  smenu.recent.selector = MIN(smenu.recent.maxentries - 1, smenu.recent.selector);
  if (!smenu.recent.maxentries)
    smenu.menu_tab = MENUTAB_ROMBROWSE;

  return recent_flush(sdr_state->rentries, smenu.recent.maxentries);
}

static void recent_reload() {
  smenu.recent.selector = 0;
  smenu.recent.seloff = 0;
  smenu.anim_state = 0;
  smenu.recent.maxentries = recent_load(RECENT_FILEPATH, sdr_state->rentries);
}

void start_emu_game(const t_emu_loader *ldinfo, const char *fn, uint32_t fs) {
  // Load: Sav/Reset Save: Reboot/Disable
  sram_filename_calc(fn, spop.p.load.l.savefn, save_path_default);
  t_sram_load_policy lp = check_file_exists(spop.p.load.l.savefn) ? SaveLoadSav : SaveLoadReset;
  unsigned errsave = prepare_sram_based_savegame(lp, SaveReboot, 0, spop.p.load.l.savefn);
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
        insert_recent_flush(fn, FLAG_RECENT_SD);

      errcode = load_extemu_rom(fn, fs, ldinfo, loadrom_progress);
      if (errcode && errcode != ERR_LOAD_NOEMU)
        break;
      ldinfo++;
    }
    // ERR_LOAD_NOEMU means nothing was written yet. Any other error happened
    // after writing to SDRAM (see keypress_popup_loadgba), so reboot.
    reboot_on_alert = (errcode != ERR_LOAD_NOEMU);
    unsigned errmsg = (errcode == ERR_LOAD_NOEMU) ? MSG_ERR_NOEMU :
                                                    MSG_ERR_LOADRST;
    spop.alert_msg = msgs[lang_id][errmsg];
  }
}

NOINLINE static void browser_open(const char *fn, uint32_t fs) {
  if (!rom_path_ok(fn)) {
    spop.alert_msg = msgs[lang_id][MSG_ERR_PATHLEN];
    return;
  }

  const char *ext = find_extension(fn);
  if (ext && !strcasecmp(ext, "gba"))
    // GBA ROMs (most likely)
    browser_open_gba(fn, fs, true);
  else if (ext && !strcasecmp(ext, "sav")) {
    spop.pop_num = POPUP_SAVFILE;
    spop.selector = SavMAX;
    strcpy(spop.p.savopt.savfn, fn);
  }
  else if (ext && !strcasecmp(ext, "fw")) {
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
    if (ext) {
      const t_emu_loader *ldinfo = get_emu_info(ext);
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
  browser_gen++;
}

// Loads a new directory list in the ROM browser.
// TODO: Implement filtering (.gba/.rom/.bin... etc) using settings
static void browser_reload() {
  smenu.anim_state = 0;

  unsigned fcount = 0;
  DIR d;
  if (FR_OK != f_opendir(&d, smenu.browser.cpath)) {
    // Show an empty list (not the previous dir's files, which would be opened
    // with this dir's path) and tell the user. B still goes up.
    smenu.browser.maxentries = 0;
    browser_reload_filter();
    spop.alert_msg = msgs[lang_id][MSG_ERR_DIROPEN];
    return;
  }

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
  f_closedir(&d);
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

  smenu.carousel.info_for = -1;    // Its "in flash" info may have changed
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
  const char *ext = find_extension(path);

  if (ext) {
    static const struct {
      const char *ext;
      unsigned icon;
    } exticon[] = {
      {"gba", ICON_GBACART},
      {"gb",  ICON_GBCART},
      {"gbc", ICON_GBCCART},
      {"nes", ICON_NESCART},
      {"sms", ICON_SMSCART},
      {"fw",  ICON_UPDFILE},
    };
    for (unsigned i = 0; i < sizeof(exticon)/sizeof(exticon[0]); i++)
      if (!strcasecmp(exticon[i].ext, ext))
        return exticon[i].icon;
  }

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
    // Text + 6 spaces + text again (long texts are cut, it's just a marquee).
    char tmpbuf[540];
    unsigned tl = strlen(t);
    if (tl > (sizeof(tmpbuf) - 7) / 2)
      tl = (sizeof(tmpbuf) - 7) / 2;
    memcpy(tmpbuf, t, tl);
    memcpy(&tmpbuf[tl], "      ", 7);
    unsigned pixw = font_width(tmpbuf);
    if (anim > pixw)
      *franim = ANIM_INITIAL_WAIT + ((anim - pixw) << 4);
    memcpy(&tmpbuf[tl + 6], t, tl);
    tmpbuf[tl * 2 + 6] = 0;

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

// A file name as the lists show it: without its extension when "Show
// extensions" is off (names starting with a dot keep theirs). out is a
// MAX_FN_LEN buffer, used only when the name changes.
static const char *list_name(const char *fn, char *out) {
  const char *ext = show_ext ? NULL : find_extension(fn);
  if (!ext || ext == fn + 1)
    return fn;
  unsigned l = ext - fn - 1;
  memcpy(out, fn, l);
  out[l] = 0;
  return out;
}

void render_recent(volatile uint8_t *frame) {
  // Render the list from memory.
  for (unsigned i = 0; i < RECENT_ROWS; i++) {
    if (smenu.recent.seloff + i >= smenu.recent.maxentries)
      break;

    t_rentry *e = &sdr_state->rentries[smenu.recent.seloff + i];
    char *fn = &e->fpath[e->fname_offset];
    render_icon(2, (i+1)*16, guessicon(fn));
    char nbuf[MAX_FN_LEN];
    const char *name = list_name(fn, nbuf);

    // Animate the row entries if they are too long!
    if (i == smenu.recent.selector - smenu.recent.seloff)
      draw_text_ovf_rotate(name, frame, 20, (1 + i) * 16,
                           SCREEN_WIDTH - 24, &smenu.anim_state);
    else
      draw_text_ovf(name, frame, 20, (1 + i) * 16, SCREEN_WIDTH - 24);
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

      const t_flash_game_entry *e = &sdr_state->nordata.games[smenu.fbrowser.seloff + i];
      render_icon(2, (i+1)*16, ICON_GBACART);

      // Animate the row entries if they are too long!
      char szstr[16];
      human_size(szstr, sizeof(szstr), e->numblks * NOR_BLOCK_SIZE);
      draw_rightj_text(szstr, frame, SCREEN_WIDTH - 2, (1 + i) * 16);

      // Animate the row entries if they are too long!
      char nbuf[MAX_FN_LEN];
      const char *romname = list_name(&e->game_name[e->bnoffset], nbuf);
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

      char szstr[16] = {0};
      t_centry *e = sdr_state->fileorder[smenu.browser.seloff + i];

      unsigned iconidx = (e->attr & AM_HID) ? ((e->attr & AM_DIR) ? ICON_HFOLDER : ICON_HFILE) :
                         (e->attr & AM_DIR) ? ICON_FOLDER :
                         guessicon(e->fname);

      render_icon(2, (i+1)*16, iconidx);

      if (!(e->attr & AM_DIR)) {
        human_size(szstr, sizeof(szstr), e->filesize);
        draw_rightj_text(szstr, frame, SCREEN_WIDTH - 2, (1 + i) * 16);
      }

      // Folders keep their whole name.
      char nbuf[MAX_FN_LEN];
      const char *name = (e->attr & AM_DIR) ? e->fname : list_name(e->fname, nbuf);

      // Animate the row entries if they are too long!
      if (i == smenu.browser.selector - smenu.browser.seloff)
        draw_text_ovf_rotate(name, frame, 20, (1 + i) * 16,
                             SCREEN_WIDTH - 26 - font_width(szstr), &smenu.anim_state);
      else
        draw_text_ovf(name, frame, 20, (1 + i) * 16, SCREEN_WIDTH - 26 - font_width(szstr));
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
    draw_button_box(frame, 20, 220, 32 + 28 * i, 32 + 28 * i + 20, spop.selector == i);
    draw_central_text(msgs[lang_id][MSG_SAVOPT_OPT0 + i], frame, 120, 34 + 28 * i);
  }
  draw_button_box(frame, 20, 220, 124, 144, spop.selector == SavQuit);
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

  // The SD browser layout (title + up to 3 options) is unchanged; the carousel
  // one is more compact (up to 4 options plus the flash usage).
  const bool compact = spop.fmgr.carousel;
  const unsigned titley = compact ? 24 : 32;
  const unsigned opty = compact ? 58 : 60, optstep = compact ? 20 : 30;
  const unsigned boxh = compact ? 18 : 20, texty = compact ? 1 : 2;

  const char *bn = file_basename(spop.fmgr.path);
  unsigned twidth = font_width(bn);
  if (twidth > SCREEN_WIDTH - 20)
    draw_text_ovf_rotate(bn, frame, 10, titley, SCREEN_WIDTH - 20, &spop.anim);
  else
    draw_central_text_ovf(bn, frame, SCREEN_WIDTH/2, titley, SCREEN_WIDTH - 20);

  #ifdef SUPPORT_NORGAMES
  if (compact) {
    char tmp[48], tmp1[16], tmp2[16];
    human_size(tmp1, sizeof(tmp1), smenu.fbrowser.usedblks * NOR_BLOCK_SIZE);
    human_size(tmp2, sizeof(tmp2), NOR_GAMEBLOCK_COUNT * NOR_BLOCK_SIZE);
    npf_snprintf(tmp, sizeof(tmp), "Flash: %s/%s", tmp1, tmp2);
    draw_central_text(tmp, frame, SCREEN_WIDTH/2, 40);
  }
  #endif

  for (unsigned i = 0; i < spop.fmgr.optcnt; i++) {
    unsigned msg = MSG_FMGR_DEL;
    switch (spop.fmgr.opts[i]) {
    case FiMgrDelete:    msg = MSG_FMGR_DEL; break;
    case FiMgrHide:      msg = (spop.fmgr.attr & AM_HID) ? MSG_FMGR_UNHIDE : MSG_FMGR_HIDE; break;
    case FiMgrFav:       msg = (spop.fmgr.cgame >= 0 && (sdr_state->carousel_games[spop.fmgr.cgame].attr & CATTR_FAV)) ?
                               MSG_CAR_FAVDEL : MSG_CAR_FAVADD; break;
    #ifdef SUPPORT_NORGAMES
    case FiMgrWriteNOR:  msg = MSG_NOR_WRITE; break;
    case FiMgrRemoveNOR: msg = MSG_NOR_REMOVE; break;
    case FiMgrLoadSD:    msg = MSG_CAR_LOADSD; break;
    #endif
    };
    draw_button_box(frame, 20, 220, opty + i*optstep, opty + boxh + i*optstep, i == spop.selector);
    draw_central_text(msgs[lang_id][msg], frame, 120, opty + texty + i*optstep);
  }
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

  const t_flash_game_entry *e = spop.p.norld.e;
  if (spop.submenu == GbaLoadPopInfo) {
    int save_type = GET_GATTR_SAVEM(e->gattrs);
    char gcode[5] = {
      e->gamecode & 0xFF, (e->gamecode >> 8) & 0xFF, (e->gamecode >> 16) & 0xFF, e->gamecode >> 24, 0
    };
    render_gbarom_info(frame, e->game_name, false, gcode, e->gamever, save_type);
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
  char tmp[128];
  unsigned baseopt = smenu.set.selector <= 2  ? 0 :
                     smenu.set.selector >= SettMAX - 3 ? SettMAX - 5 :
                     smenu.set.selector - 2;

  if (smenu.set.selector > 2)
    draw_central_text("⯅", frame, 120, 15);
  if (smenu.set.selector < SettSave - 2)
    draw_central_text("⯆", frame, 120, 125);

  const unsigned maxrows = 5;
  unsigned optnum = 0, optcnt = 0;
  const unsigned colx = 170;           // Center point for the selection boxes
  const unsigned offy = 29;
  const unsigned rowh = 20;

  if (optnum++ >= baseopt && optcnt < maxrows)
    draw_central_text(msgs[lang_id][MSG_SET_TITL1], frame, SCREEN_WIDTH/2, offy + rowh*optcnt++);

  if (optnum++ >= baseopt && optcnt < maxrows) {
    npf_snprintf(tmp, sizeof(tmp), "< %s >", hotkey_list[hotkey_combo].cname);
    draw_text_ovf(msgs[lang_id][MSG_SETT_HOTK], frame, 8, offy + rowh*optcnt, 224);
    draw_central_text(tmp, frame, colx, offy + rowh*optcnt++);
  }

  if (optnum++ >= baseopt && optcnt < maxrows) {
    draw_text_ovf(msgs[lang_id][MSG_SETT_BOOT], frame, 8, offy + rowh*optcnt, 224);
    draw_central_text(msgs[lang_id][MSG_BOOT_TYPE0 + boot_bios_splash], frame, colx, offy + rowh*optcnt++);
  }

  if (optnum++ >= baseopt && optcnt < maxrows) {
    draw_text_ovf(msgs[lang_id][MSG_SETT_FASTSD], frame, 8, offy + rowh*optcnt, 224);
    draw_central_text(msgs[lang_id][use_slowld ? MSG_KNOB_DISABLED : MSG_KNOB_ENABLED], frame, colx, offy + rowh*optcnt++);
  }

  #ifdef SUPPORT_NORGAMES
  if (optnum++ >= baseopt && optcnt < maxrows) {
    draw_text_ovf(msgs[lang_id][MSG_SETT_VERNOR], frame, 8, offy + rowh*optcnt, 224);
    draw_central_text(msgs[lang_id][use_verify_nor ? MSG_KNOB_ENABLED : MSG_KNOB_DISABLED], frame, colx, offy + rowh*optcnt++);
  }
  #endif

  if (optnum++ >= baseopt && optcnt < maxrows) {
    draw_text_ovf(msgs[lang_id][MSG_SETT_FASTEW], frame, 8, offy + rowh*optcnt, 224);
    draw_central_text(msgs[lang_id][use_fastew ? MSG_KNOB_ENABLED : MSG_KNOB_DISABLED], frame, colx, offy + rowh*optcnt++);
  }

  if (optnum++ >= baseopt && optcnt < maxrows) {
    draw_text_ovf(msgs[lang_id][MSG_SETT_SAVET], frame, 8, offy + rowh*optcnt, 224);

    if (save_path_default == SaveRomName)
      draw_central_text(msgs[lang_id][MSG_NEXTTO_ROM], frame, colx, offy + rowh*optcnt++);
    else {
      npf_snprintf(tmp, sizeof(tmp), "< %s >", save_paths[save_path_default]);
      draw_central_text(tmp, frame, colx, offy + rowh*optcnt++);
    }
  }

  #ifdef SUPPORT_NORGAMES
  if (optnum++ >= baseopt && optcnt < maxrows) {
    draw_text_ovf(msgs[lang_id][MSG_SETT_SAVETX], frame, 8, offy + rowh*optcnt, 224);
    npf_snprintf(tmp, sizeof(tmp), "< %s >", save_paths[save_path_nor_default]);
    draw_central_text(tmp, frame, colx, offy + rowh*optcnt++);
  }
  #endif

  if (optnum++ >= baseopt && optcnt < maxrows) {
    npf_snprintf(tmp, sizeof(tmp), "< %u >", backup_sram_default);
    draw_text_ovf(msgs[lang_id][MSG_SETT_SAVEBK], frame, 8, offy + rowh*optcnt, 224);
    draw_central_text(tmp, frame, colx, offy + rowh*optcnt++ );
  }

  if (optnum++ >= baseopt && optcnt < maxrows) {
    draw_text_ovf(msgs[lang_id][MSG_SETT_STATET], frame, 8, offy + rowh*optcnt, 224);
    npf_snprintf(tmp, sizeof(tmp), "< %s >", savestates_paths_display[state_path_default]);
    draw_central_text(tmp, frame, colx, offy + rowh*optcnt++);
  }

  if (optnum++ >= baseopt && optcnt < maxrows) {
    draw_text_ovf(msgs[lang_id][MSG_SETT_CHTEN], frame, 8, offy + rowh*optcnt, 224);
    draw_central_text(msgs[lang_id][enable_cheats ? MSG_KNOB_ENABLED : MSG_KNOB_DISABLED], frame, colx, offy + rowh*optcnt++);
  }

  if (optnum++ >= baseopt && optcnt < maxrows)
    draw_central_text(msgs[lang_id][MSG_SET_TITL2], frame, SCREEN_WIDTH/2, offy + rowh*optcnt++);

  if (optnum++ >= baseopt && optcnt < maxrows) {
    draw_text_ovf(msgs[lang_id][MSG_DEFS_PATCH], frame, 8, offy + rowh*optcnt, 224);
    draw_central_text(msgs[lang_id][MSG_PATCH_TYPE0 + patcher_default], frame, colx, offy + rowh*optcnt++);
  }

  if (optnum++ >= baseopt && optcnt < maxrows) {
    draw_text_ovf(msgs[lang_id][MSG_LOADER_MENU], frame, 8, offy + rowh*optcnt, 224);
    draw_central_text(msgs[lang_id][MSG_KNOB_DISABLED + ingamemenu_default], frame, colx, offy + rowh*optcnt++);
  }

  if (optnum++ >= baseopt && optcnt < maxrows) {
    draw_text_ovf(msgs[lang_id][MSG_LOADER_RTCE], frame, 8, offy + rowh*optcnt, 224);
    draw_central_text(msgs[lang_id][MSG_KNOB_DISABLED + rtcpatch_default], frame, colx, offy + rowh*optcnt++);
  }

  if (optnum++ >= baseopt && optcnt < maxrows) {
    t_dec_date d;
    timestamp2date(rtcvalue_default, &d);
    npf_snprintf(tmp, sizeof(tmp), "20%02d/%02d/%02d %02d:%02d",
      d.year, d.month, d.day, d.hour, d.min);
    draw_text_ovf(msgs[lang_id][MSG_DEF_RTCVAL], frame, 8, offy + rowh*optcnt, 224);
    draw_central_text(tmp, frame, colx, offy + rowh*optcnt++);
  }

  if (optnum++ >= baseopt && optcnt < maxrows) {
    unsigned spdmsg = rtcspeed_default ? (MSG_UIS_SPD0 + rtcspeed_default - 1) :
                                          MSG_STILLRTC;
    npf_snprintf(tmp, sizeof(tmp), "< %s >", msgs[lang_id][spdmsg]);
    draw_text_ovf(msgs[lang_id][MSG_DEF_SPEED], frame, 8, offy + rowh*optcnt, 224);
    draw_central_text(tmp, frame, colx, offy + rowh*optcnt++);
  }

  if (optnum++ >= baseopt && optcnt < maxrows) {
    draw_text_ovf(msgs[lang_id][MSG_LOADER_LOADP], frame, 8, offy + rowh*optcnt, 224);
    draw_central_text(msgs[lang_id][MSG_DEF_LOADP0 + (autoload_default ^ 1)], frame, colx, offy + rowh*optcnt++);
  }

  if (optnum++ >= baseopt && optcnt < maxrows) {
    draw_text_ovf(msgs[lang_id][MSG_LOADER_SAVEP], frame, 8, offy + rowh*optcnt, 224);
    draw_central_text(msgs[lang_id][autosave_default ? MSG_DEF_SAVEP0 : MSG_DEF_SAVEP1], frame, colx, offy + rowh*optcnt++);
  }

  if (optnum++ >= baseopt && optcnt < maxrows) {
    draw_text_ovf(msgs[lang_id][MSG_LOADER_PREFDS], frame, 8, offy + rowh*optcnt, 224);
    draw_central_text(msgs[lang_id][autosave_prefer_ds ? MSG_KNOB_ENABLED : MSG_KNOB_DISABLED], frame, colx, offy + rowh*optcnt++);
  }

  if (optnum++ >= baseopt && optcnt < maxrows) {
    draw_button_box(frame, 20, 220, 112, 132, smenu.set.selector == SettSave);
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
  }
  else if (smenu.set.selector == SettStateLoc) {
    npf_snprintf(tmp, sizeof(tmp), msgs[lang_id][MSG_STATE_TYPE_PT], savestates_paths[state_path_default]);
    draw_text_ovf_rotate(tmp, frame, 4, SCREEN_HEIGHT - 18, 232, &smenu.anim_state);
  }
  #ifdef SUPPORT_NORGAMES
  else if (smenu.set.selector == SettSaveLocNOR) {
    npf_snprintf(tmp, sizeof(tmp), msgs[lang_id][MSG_SAVE_TYPE_PTX], save_paths[save_path_nor_default]);
    draw_text_ovf_rotate(tmp, frame, 4, SCREEN_HEIGHT - 18, 232, &smenu.anim_state);
  }
  #endif
  else {
    unsigned help_msg = smenu.set.selector == SettBootType ? MSG_BOOT_TYPE_I0 + boot_bios_splash :
                        smenu.set.selector == SettSaveBkp  ? MSG_BACKUP_I :
                        smenu.set.selector == SettFastSD   ? MSG_FASTSD_I :
                        smenu.set.selector == SettFastEWRAM? MSG_FASTEW_I :
                        smenu.set.selector == DefsPatchEng ? MSG_PATCH_TYPE_I0 + patcher_default :
                        smenu.set.selector == DefsLoadPol  ? MSG_DEF_LOADP_I0 + (autoload_default ^ 1) :
                        smenu.set.selector == DefsSavePol  ? MSG_DEF_SAVEP_I0 + (autosave_default ^ 1) :
                        smenu.set.selector == DefsPrefDS   ? MSG_LOADER_PREFDSI :
                        #ifdef SUPPORT_NORGAMES
                        smenu.set.selector == SettVerifyNOR ? MSG_VERNOR_I :
                        #endif
                        MSG_EMPTY;
    draw_text_ovf_rotate(msgs[lang_id][help_msg], frame, 4, SCREEN_HEIGHT - 18, 232, &smenu.anim_state);
  }

  if (smenu.set.selector != SettSave)
    for (unsigned i = 0; i < 240; i += 16)
      render_icon_trans(i, offy + (smenu.set.selector - baseopt) * 20, 63);
}

void render_ui_settings(volatile uint8_t *frame) {
  const unsigned colx = 170;
  const unsigned rowh = 16;
  char tmpbuf[64];
  npf_snprintf(tmpbuf, sizeof(tmpbuf), "< %u >", menu_theme + 1U);
  draw_text_ovf(msgs[lang_id][MSG_UIS_THEME], frame, 8, 22, 224);
  draw_central_text(tmpbuf, frame, colx, 22 );

  npf_snprintf(tmpbuf, sizeof(tmpbuf), "< %s >", msgs[lang_id][MSG_LANG_NAME]);
  draw_text_ovf(msgs[lang_id][MSG_UIS_LANG], frame, 8, 22 + rowh, 224);
  draw_central_text(tmpbuf, frame, colx, 22 + rowh );

  draw_text_ovf(msgs[lang_id][MSG_UIS_RECNT], frame, 8, 22 + rowh * 2, 224);
  draw_central_text(msgs[lang_id][recent_menu ? MSG_KNOB_ENABLED : MSG_KNOB_DISABLED], frame, colx, 22 + rowh * 2 );

  npf_snprintf(tmpbuf, sizeof(tmpbuf), "< %s >", msgs[lang_id][MSG_UIS_SPD0 + anim_speed]);
  draw_text_ovf(msgs[lang_id][MSG_UIS_ANSPD], frame, 8, 22 + rowh * 3, 224);
  draw_central_text(tmpbuf, frame, colx, 22 + rowh * 3 );

  draw_text_ovf(msgs[lang_id][MSG_UIS_BHID], frame, 8, 22 + rowh * 4, 224);
  draw_central_text(msgs[lang_id][hide_hidden ? MSG_KNOB_DISABLED : MSG_KNOB_ENABLED], frame, colx, 22 + rowh * 4 );

  draw_text_ovf(msgs[lang_id][MSG_UIS_START], frame, 8, 22 + rowh * 5, 224);
  draw_central_text(msgs[lang_id][start_tab ? MSG_UIS_STB1 : MSG_UIS_STB0], frame, colx, 22 + rowh * 5 );

  draw_text_ovf(msgs[lang_id][MSG_UIS_EXT], frame, 8, 22 + rowh * 6, 224);
  draw_central_text(msgs[lang_id][show_ext ? MSG_KNOB_ENABLED : MSG_KNOB_DISABLED], frame, colx, 22 + rowh * 6 );

  if (smenu.uiset.selector != UiSetSave)
    for (unsigned i = 0; i < 240; i += 16)
      render_icon_trans(i, 22 + smenu.uiset.selector * rowh, 63);

  draw_button_box(frame, 20, 220, 132, 152, smenu.uiset.selector == UiSetSave);
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
    npf_snprintf(tmp, sizeof(tmp), "SD Card ID: %02x | %04x", sd_info.manufacturer, sd_info.oemid);
    draw_central_text(tmp, frame, 120, 70);
    human_size_kb(tmp2, sizeof(tmp2), sd_info.block_cnt / 2);
    npf_snprintf(tmp, sizeof(tmp), "Type: %s  Size: %s", sd_info.sdhc ? "SDHC" : "SDSC", tmp2);
    draw_central_text(tmp, frame, 120, 90);
    npf_snprintf(tmp, sizeof(tmp), "Info: %c%c%c%c%c (%d/%d)",
      sd_info.prodname[0] ?: '#', sd_info.prodname[1] ?: '#',
      sd_info.prodname[2] ?: '#', sd_info.prodname[3] ?: '#',
      sd_info.prodname[4] ?: '#', 2000 + sd_info.year, sd_info.month);
    draw_central_text(tmp, frame, 120, 110);
    break;
  }

  // Flashing info
  dma_memset16(&frame[138*SCREEN_WIDTH], dup8(FG_COLOR), SCREEN_WIDTH*22/2);
  draw_text_ovf_rotate(enable_flashing ? msgs[lang_id][MSG_FWUP_ENABLED] : msgs[lang_id][MSG_FWUP_HOTKEY], frame,
                       4, 141, SCREEN_WIDTH - 8, &smenu.anim_state);
}

void render_tools(volatile uint8_t *frame) {
  for (unsigned i = 0; i < ToolsMAX; i++)
    draw_text_ovf(msgs[lang_id][MSG_TOOLS0_SDRAM + i], frame, 22, 26 + 22 * i, 144);

  smenu.anim_state = (smenu.anim_state + 1) & 255;
  draw_central_text("▸", frame, 11 + (smenu.anim_state >> 6), 26 + 22 * smenu.tools.selector);

  for (unsigned i = 0; i < 240; i += 16)
    render_icon_trans(i, 26 + smenu.tools.selector * 22, 63);
}

static bool carousel_is_game(const char *fn) {
  const char *ext = find_extension(fn);
  return ext && (!strcasecmp(ext, "gba") || get_emu_info(ext));
}

// Game (carousel_games index) at a view position. The view lives in SDRAM:
// keep it in range.
static unsigned carousel_gi(int n) {
  return sdr_state->carousel_view[n] % CAROUSEL_MAX_GAMES;
}

static const t_cgame *carousel_game(int n) {
  return &sdr_state->carousel_games[carousel_gi(n)];
}

static const char *carousel_name_ptr(const t_cgame *g) {
  return &sdr_state->carousel_pool[(g->pathoff + g->nameoff) % CAROUSEL_POOL_SIZE];
}

static const char *carousel_rel_ptr(const t_cgame *g) {
  return &sdr_state->carousel_pool[g->pathoff % CAROUSEL_POOL_SIZE];
}

// Bounded string copy. The list lives in SDRAM, which a failed ROM load can
// trash, so never trust it to be NUL terminated.
static void carousel_copy(const char *s, char out[MAX_FN_LEN]) {
  unsigned i = 0;
  for (; i < MAX_FN_LEN - 1 && s[i]; i++)
    out[i] = s[i];
  out[i] = 0;
}

static void carousel_fname(int n, char out[MAX_FN_LEN]) {
  carousel_copy(carousel_name_ptr(carousel_game(n)), out);
}

// Full path of a game (scan root + relative path). False if it is too long.
static bool carousel_fullpath(int n, char out[MAX_FN_LEN]) {
  char rel[MAX_FN_LEN];
  carousel_copy(carousel_rel_ptr(carousel_game(n)), rel);
  return path_join(out, smenu.carousel.root, rel, "");
}

#ifdef SUPPORT_NORGAMES
// The flash (NOR) copy of a game: flash entries keep the path of the SD file
// they were written from. NULL if the game is not in flash (or the SD file
// was renamed/moved since, then it just loads from SD).
static const t_flash_game_entry *carousel_nor_entry(int n) {
  char path[MAX_FN_LEN];
  if (!carousel_fullpath(n, path))
    return NULL;
  const unsigned cnt = MIN(sdr_state->nordata.gamecnt, FLASHG_MAXFN_CNT);
  for (unsigned i = 0; i < cnt; i++) {
    const t_flash_game_entry *e = &sdr_state->nordata.games[i];
    if (!strncmp(e->game_name, path, sizeof(e->game_name)))
      return e;
  }
  return NULL;
}
#endif

// Finds a game by its full path. Returns -1 if it is not in the list.
static int carousel_find(const char *fullpath) {
  const unsigned rootlen = strlen(smenu.carousel.root);
  if (strncmp(fullpath, smenu.carousel.root, rootlen))
    return -1;
  for (int i = 0; i < smenu.carousel.count; i++)
    if (!strncmp(carousel_rel_ptr(carousel_game(i)), &fullpath[rootlen], MAX_FN_LEN))
      return i;
  return -1;
}

NOINLINE static int carousel_sort(const void *a, const void *b) {
  return carousel_namecmp(carousel_name_ptr((const t_cgame*)a), carousel_name_ptr((const t_cgame*)b));
}

// Copies between SDRAM and files using a bounce buffer (len multiple of 4).
static bool sdram_fwrite(FIL *fd, const uint8_t *src, unsigned len) {
  for (unsigned off = 0; off < len; off += 1024) {
    uint32_t tmp[1024 / 4];
    const unsigned chunk = MIN(1024, len - off);
    UINT wrbytes;
    memcpy32(tmp, &src[off], chunk);
    if (FR_OK != f_write(fd, tmp, chunk, &wrbytes) || wrbytes != chunk)
      return false;
  }
  return true;
}

static bool sdram_fread(FIL *fd, uint8_t *dst, unsigned len) {
  for (unsigned off = 0; off < len; off += 1024) {
    uint32_t tmp[1024 / 4];
    const unsigned chunk = MIN(1024, len - off);
    UINT rdbytes;
    if (FR_OK != f_read(fd, tmp, chunk, &rdbytes) || rdbytes != chunk)
      return false;
    memcpy32(&dst[off], tmp, chunk);
  }
  return true;
}

// Dir scanner: walks every game under a dir (and its subdirs, up to
// CAROUSEL_MAX_DEPTH levels). It can store them in the list, or only hash
// them to check a cached list in the background, a few entries per frame.
static struct {
  DIR dirs[CAROUSEL_MAX_DEPTH];
  unsigned plen[CAROUSEL_MAX_DEPTH];
  char path[MAX_FN_LEN];
  int depth;                // Current dir level (-1: finished)
  unsigned rootlen;
  unsigned cnt, pooloff;
  uint32_t hash;            // Hash of the paths and sizes, in scan order
  bool store;               // Write the games to the list (or just hash them)
  bool truncated;
} cscan;

static void cscan_stop() {
  while (cscan.depth >= 0)
    f_closedir(&cscan.dirs[cscan.depth--]);
}

static void cscan_start(const char *root, bool store) {
  cscan.store = store;
  cscan.cnt = cscan.pooloff = 0;
  cscan.hash = CAROUSEL_HASH_INIT;
  cscan.truncated = false;
  strcpy(cscan.path, root);
  cscan.rootlen = cscan.plen[0] = strlen(root);
  cscan.depth = (FR_OK == f_opendir(&cscan.dirs[0], cscan.path)) ? 0 : -1;
}

// Processes up to maxent dir entries. Returns true once the scan is finished.
static bool cscan_step(unsigned maxent) {
  while (cscan.depth >= 0 && maxent--) {
    const int depth = cscan.depth;
    FILINFO info;
    if (FR_OK != f_readdir(&cscan.dirs[depth], &info) || !info.fname[0]) {
      // Done with this dir, go back to the parent.
      f_closedir(&cscan.dirs[cscan.depth--]);
      if (cscan.depth >= 0)
        cscan.path[cscan.plen[cscan.depth]] = 0;
      continue;
    }

    // Skip SuperFW's own dir, system dirs and (if requested) hidden entries.
    if (info.fname[0] == '.' || (info.fattrib & AM_SYS) || ((info.fattrib & AM_HID) && hide_hidden))
      continue;

    const unsigned flen = strlen(info.fname);
    if (cscan.plen[depth] + flen + 1 >= MAX_FN_LEN)
      continue;     // Path too long

    if (info.fattrib & AM_DIR) {
      if (depth + 1 < CAROUSEL_MAX_DEPTH) {
        strcat(cscan.path, info.fname);
        strcat(cscan.path, "/");
        if (FR_OK == f_opendir(&cscan.dirs[depth + 1], cscan.path)) {
          cscan.depth++;
          cscan.plen[depth + 1] = cscan.plen[depth] + flen + 1;
        } else
          cscan.path[cscan.plen[depth]] = 0;
      }
      continue;
    }

    if (!carousel_is_game(info.fname))
      continue;

    const unsigned rdir = cscan.plen[depth] - cscan.rootlen;    // Relative dir length
    const unsigned need = ROUND_UP2(rdir + flen + 1, 4);
    if (cscan.cnt >= CAROUSEL_MAX_GAMES || cscan.pooloff + need > CAROUSEL_POOL_SIZE) {
      cscan.truncated = true;
      cscan_stop();
      break;
    }

    // SDRAM only takes 16/32 bit writes: build the entry and copy it by words.
    uint32_t tmp[(MAX_FN_LEN + 4) / 4];
    char *t = (char*)tmp;
    memcpy(t, &cscan.path[cscan.rootlen], rdir);
    memcpy(&t[rdir], info.fname, flen);
    memset(&t[rdir + flen], 0, need - rdir - flen);
    t_cgame g = { (uint32_t)info.fsize, cscan.pooloff, rdir, 0 };

    cscan.hash = carousel_hash(cscan.hash, t, rdir + flen + 1);
    cscan.hash = carousel_hash(cscan.hash, &g.filesize, sizeof(g.filesize));

    if (cscan.store) {
      memcpy32(&sdr_state->carousel_pool[cscan.pooloff], tmp, need);
      memcpy32(&sdr_state->carousel_games[cscan.cnt], &g, sizeof(g));
    }
    cscan.cnt++;
    cscan.pooloff += need;
  }
  return cscan.depth < 0;
}

static void carousel_cache_write(unsigned poolsize) {
  FIL fd;
  f_mkdir(SUPERFW_DIR);
  if (FR_OK != f_open(&fd, CAROUSEL_CACHE_PATH, FA_WRITE | FA_CREATE_ALWAYS))
    return;

  t_ccache_hdr hdr;
  memset(&hdr, 0, sizeof(hdr));
  hdr.magic = CAROUSEL_CACHE_MAGIC;
  hdr.version = CAROUSEL_CACHE_VER;
  hdr.count = smenu.carousel.total;
  hdr.poolsize = poolsize;
  hdr.signature = smenu.carousel.signature;
  hdr.flags = (smenu.carousel.truncated ? 1 : 0) | (hide_hidden ? 2 : 0);
  strcpy(hdr.root, smenu.carousel.root);

  UINT wrbytes;
  bool ok = FR_OK == f_write(&fd, &hdr, sizeof(hdr), &wrbytes) && wrbytes == sizeof(hdr) &&
            sdram_fwrite(&fd, (uint8_t*)sdr_state->carousel_games, hdr.count * sizeof(t_cgame)) &&
            sdram_fwrite(&fd, (uint8_t*)sdr_state->carousel_pool, poolsize);
  f_close(&fd);
  if (!ok)
    f_unlink(CAROUSEL_CACHE_PATH);
}

// Loads the cached list for the current root dir. False if missing or invalid.
static bool carousel_cache_load() {
  FIL fd;
  if (FR_OK != f_open(&fd, CAROUSEL_CACHE_PATH, FA_READ))
    return false;

  UINT rdbytes;
  t_ccache_hdr hdr;
  bool ok = FR_OK == f_read(&fd, &hdr, sizeof(hdr), &rdbytes) && rdbytes == sizeof(hdr);
  hdr.root[MAX_FN_LEN - 1] = 0;
  ok = ok && hdr.magic == CAROUSEL_CACHE_MAGIC && hdr.version == CAROUSEL_CACHE_VER &&
       hdr.count <= CAROUSEL_MAX_GAMES && hdr.poolsize <= CAROUSEL_POOL_SIZE && !(hdr.poolsize & 3) &&
       ((hdr.flags >> 1) & 1) == (hide_hidden ? 1 : 0) && !strcmp(hdr.root, smenu.carousel.root);
  ok = ok && sdram_fread(&fd, (uint8_t*)sdr_state->carousel_games, hdr.count * sizeof(t_cgame)) &&
             sdram_fread(&fd, (uint8_t*)sdr_state->carousel_pool, hdr.poolsize);
  f_close(&fd);

  for (unsigned i = 0; ok && i < hdr.count; i++) {
    const t_cgame *g = &sdr_state->carousel_games[i];
    ok = !(g->pathoff & 3) && g->pathoff + g->nameoff < hdr.poolsize;
  }
  if (!ok)
    return false;

  smenu.carousel.total = hdr.count;
  smenu.carousel.truncated = hdr.flags & 1;
  smenu.carousel.signature = hdr.signature;
  return true;
}

#pragma GCC push_options
#pragma GCC optimize ("Os")   // Not speed critical: keep the flash images small

// ---- Views: which games are shown (all, favourites, a system), and their order ----

// Finds a game in the whole list (not just the view). -1 if it is not there.
static int carousel_find_game(const char *fullpath) {
  const unsigned rootlen = strlen(smenu.carousel.root);
  if (strncmp(fullpath, smenu.carousel.root, rootlen))
    return -1;
  for (int g = 0; g < smenu.carousel.total; g++)
    if (!strncmp(carousel_rel_ptr(&sdr_state->carousel_games[g]), &fullpath[rootlen], MAX_FN_LEN))
      return g;
  return -1;
}

// System of a game: 0 for GBA, emu_group() + 1 for the emulated ones.
static unsigned carousel_system(const char *fname) {
  const char *ext = find_extension(fname);
  const int grp = (ext && strcasecmp(ext, "gba")) ? emu_group(ext) : -1;
  return (grp < 0 || grp + 1 >= CAROUSEL_MAX_SYS) ? 0 : grp + 1;
}

static void car_fav_line(char *line, void *usr) {
  const int g = carousel_find_game(line);
  if (g >= 0)
    sdr_state->carousel_games[g].attr |= CATTR_FAV;
}

// Per game attributes: system, favourite (favorites.txt) and position in the
// recent list.
static void carousel_load_attrs() {
  memset(smenu.carousel.syscnt, 0, sizeof(smenu.carousel.syscnt));
  for (int g = 0; g < smenu.carousel.total; g++) {
    char fname[MAX_FN_LEN];
    carousel_copy(carousel_name_ptr(&sdr_state->carousel_games[g]), fname);
    const unsigned sys = carousel_system(fname);
    sdr_state->carousel_games[g].attr = sys | (0xFF << CATTR_RANK_SH);
    smenu.carousel.syscnt[sys]++;
  }

  // Newest first: the first entry of a game is its position.
  for (int r = 0; r < MIN(smenu.recent.maxentries, 0xFF); r++) {
    char path[MAX_FN_LEN];
    carousel_copy(sdr_state->rentries[r].fpath, path);
    const int g = carousel_find_game(path);
    if (g >= 0 && (sdr_state->carousel_games[g].attr >> CATTR_RANK_SH) == 0xFF)
      sdr_state->carousel_games[g].attr = (sdr_state->carousel_games[g].attr & 0xFF) | (r << CATTR_RANK_SH);
  }

  file_read_lines(CAROUSEL_FAV_PATH, car_fav_line, NULL);
}

// The list is sorted by name: other orders sort the view, ties by name.
NOINLINE static int carousel_view_cmp(const void *a, const void *b) {
  const unsigned ga = *(const uint32_t*)a % CAROUSEL_MAX_GAMES;
  const unsigned gb = *(const uint32_t*)b % CAROUSEL_MAX_GAMES;
  int r = 0;
  if (car_sort == CarSortSystem)
    r = (int)(sdr_state->carousel_games[ga].attr & CATTR_SYS) - (int)(sdr_state->carousel_games[gb].attr & CATTR_SYS);
  else if (car_sort == CarSortRecent)
    r = (int)(sdr_state->carousel_games[ga].attr >> CATTR_RANK_SH) - (int)(sdr_state->carousel_games[gb].attr >> CATTR_RANK_SH);
  return r ? r : (int)ga - (int)gb;
}

static bool carousel_in_view(unsigned g) {
  const uint16_t at = sdr_state->carousel_games[g].attr;
  return car_view == CarViewAll || (car_view == CarViewFav && (at & CATTR_FAV)) ||
         (car_view >= CarViewSys && (at & CATTR_SYS) == car_view - CarViewSys);
}

// Builds the view (the games shown, in order). Stays on game keep if it is
// still shown.
static void carousel_build_view(int keep) {
  int n = 0;
  for (int g = 0; g < smenu.carousel.total; g++)
    if (carousel_in_view(g))
      sdr_state->carousel_view[n++] = g;
  if (car_sort == CarSortSystem || car_sort == CarSortRecent)
    heapsort4(sdr_state->carousel_view, n, 1, carousel_view_cmp);

  smenu.carousel.count = n;
  smenu.carousel.selector = 0;
  for (int i = 0; keep >= 0 && i < n; i++)
    if ((int)carousel_gi(i) == keep)
      smenu.carousel.selector = i;
  smenu.carousel.offset = 0;
  smenu.carousel.info_for = -1;
  for (unsigned i = 0; i < COVER_SLOTS; i++) {
    smenu.carousel.slot_entry[i] = -1;
    smenu.carousel.slot_state[i] = CoverNotLoaded;
  }
}

static void carousel_view_name(unsigned v, char *out, unsigned size) {
  if (v == CarViewAll || v == CarViewFav)
    npf_snprintf(out, size, "%s", msgs[lang_id][v == CarViewAll ? MSG_CAR_VALL : MSG_CAR_VFAV]);
  else if (v == CarViewSys)
    npf_snprintf(out, size, "GBA");
  else
    emu_group_name(v - CarViewSys - 1, out, size);
}

// Adds/removes a game in favorites.txt (defined with the view pop up).
static bool carousel_fav_store(const char *path, bool add);

// Next view in a direction: all, favourites, then the systems on the card.
static unsigned carousel_next_view(unsigned v, int dir) {
  const unsigned nviews = CarViewSys + CAROUSEL_MAX_SYS;
  if (v >= nviews)
    v = CarViewAll;
  for (unsigned i = 0; i < nviews; i++) {
    v = (v + nviews + dir) % nviews;
    if (v < CarViewSys || smenu.carousel.syscnt[v - CarViewSys])
      return v;
  }
  return CarViewAll;
}

#pragma GCC pop_options

// Picks the game to start at: the previous one (if any), the one selected in
// the browser, or the last played one.
static void carousel_pick_start(const char *prevpath) {
  int sel = prevpath[0] ? carousel_find(prevpath) : -1;

  if (sel < 0 && smenu.browser.selector >= 0 && smenu.browser.selector < smenu.browser.dispentries) {
    const t_centry *e = sdr_state->fileorder[smenu.browser.selector];
    char fname[MAX_FN_LEN], path[MAX_FN_LEN];
    carousel_copy(e->fname, fname);
    if (!e->isdir && path_join(path, smenu.browser.cpath, fname, ""))
      sel = carousel_find(path);
  }

  // The last played game (games started from flash also hold their SD path).
  if (sel < 0 && smenu.recent.maxentries) {
    char path[MAX_FN_LEN];
    carousel_copy(sdr_state->rentries[0].fpath, path);
    sel = carousel_find(path);
  }

  smenu.carousel.selector = MAX(sel, 0);
}

// (Re)builds the list: from the cache if possible (then checked in the
// background), otherwise by scanning the browser's current dir.
static void carousel_rebuild() {
  char prevpath[MAX_FN_LEN] = {0};
  if (smenu.carousel.stale && smenu.carousel.count)
    carousel_fullpath(smenu.carousel.selector, prevpath);

  cscan_stop();
  strcpy(smenu.carousel.root, smenu.browser.cpath);
  smenu.carousel.verifying = false;

  if (!smenu.carousel.stale && carousel_cache_load()) {
    cscan_start(smenu.carousel.root, false);
    smenu.carousel.verifying = true;
  } else {
    cscan_start(smenu.carousel.root, true);
    while (!cscan_step(~0U));
    heapsort4(sdr_state->carousel_games, cscan.cnt, sizeof(t_cgame) / sizeof(uint32_t), carousel_sort);
    smenu.carousel.total = cscan.cnt;
    smenu.carousel.truncated = cscan.truncated;
    smenu.carousel.signature = cscan.hash;
    carousel_cache_write(cscan.pooloff);
  }

  carousel_load_attrs();
  carousel_build_view(-1);
  carousel_pick_start(prevpath);
  smenu.carousel.offset = 0;
  smenu.carousel.info_for = -1;
  for (unsigned i = 0; i < COVER_SLOTS; i++) {
    smenu.carousel.slot_entry[i] = -1;
    smenu.carousel.slot_state[i] = CoverNotLoaded;
  }
  smenu.carousel.browser_gen = browser_gen;
  smenu.carousel.stale = false;
  smenu.carousel.scan_frame = 0;
}

// Background check of a cached list: if the card changed, rescan it.
static void carousel_verify_step() {
  if (!smenu.carousel.verifying || !cscan_step(16))
    return;
  smenu.carousel.verifying = false;
  if (cscan.hash != smenu.carousel.signature || cscan.truncated != smenu.carousel.truncated)
    smenu.carousel.stale = true;
}

// Opens a game's cover: /.superfw/covers/<rom file name>.cov, or for GBA ROMs
// /.superfw/covers/<game code>.cov (ie. BPEE.cov), which survives ROM renames.
static bool carousel_open_cover(int n, FIL *fd) {
  char fname[MAX_FN_LEN];
  char path[sizeof(COVER_PATH) + MAX_FN_LEN + sizeof(COVER_EXT)];
  carousel_fname(n, fname);
  strcpy(path, COVER_PATH);
  strcat(path, fname);
  strcat(path, COVER_EXT);
  if (FR_OK == f_open(fd, path, FA_READ))
    return true;

  const char *ext = find_extension(fname);
  char rompath[MAX_FN_LEN];
  if (!ext || strcasecmp(ext, "gba") || !carousel_fullpath(n, rompath))
    return false;

  FIL rom;
  if (FR_OK != f_open(&rom, rompath, FA_READ))
    return false;
  uint8_t code[4];
  UINT rdbytes;
  bool ok = FR_OK == f_lseek(&rom, 0xAC) &&
            FR_OK == f_read(&rom, code, sizeof(code), &rdbytes) && rdbytes == sizeof(code);
  f_close(&rom);

  for (unsigned i = 0; ok && i < sizeof(code); i++)
    ok = (code[i] >= 'A' && code[i] <= 'Z') || (code[i] >= '0' && code[i] <= '9');
  if (!ok)
    return false;

  npf_snprintf(path, sizeof(path), COVER_PATH "%c%c%c%c" COVER_EXT, code[0], code[1], code[2], code[3]);
  return FR_OK == f_open(fd, path, FA_READ);
}

// Reads the cover file for a game into a cover slot (in SDRAM).
static bool carousel_load_cover(int n, unsigned slot) {
  FIL fd;
  if (!carousel_open_cover(n, &fd))
    return false;

  UINT rdbytes;
  t_cover_header hdr;
  bool ok = FR_OK == f_read(&fd, &hdr, sizeof(hdr), &rdbytes) && rdbytes == sizeof(hdr);
  int tint = ok ? cover_header_check(&hdr) : -2;
  ok = ok && tint != -2;

  // Copy both images to SDRAM using a small bounce buffer.
  ok = ok && sdram_fread(&fd, sdr_state->cover_big[slot], COVER_BIG * COVER_BIG) &&
             sdram_fread(&fd, sdr_state->cover_small[slot], COVER_SMALL * COVER_SMALL);

  f_close(&fd);
  smenu.carousel.slot_tint[slot] = ok ? tint : -1;
  return ok;
}

// Loads missing covers, at most one per call (selected game first) to keep
// the UI responsive.
static void carousel_sync() {
  static const int order[] = { 0, 1, -1, 2, -2 };
  for (unsigned i = 0; i < ARRAY_SIZE(order); i++) {
    int n = smenu.carousel.selector + order[i];
    if (n < 0 || n >= smenu.carousel.count)
      continue;

    unsigned slot = n % COVER_SLOTS;
    if (smenu.carousel.slot_entry[slot] != n) {
      smenu.carousel.slot_entry[slot] = n;
      smenu.carousel.slot_state[slot] = carousel_load_cover(n, slot) ? CoverLoaded : CoverMissing;
      return;
    }
  }
}

// Info shown below the title. Computed once the carousel stops moving (it
// needs SD card accesses).
static void carousel_update_info() {
  const int sel = smenu.carousel.selector;
  if (smenu.carousel.info_for == sel || smenu.carousel.offset)
    return;

  char path[MAX_FN_LEN], savefn[MAX_FN_LEN];
  smenu.carousel.info_for = sel;
  smenu.carousel.info_save = smenu.carousel.info_last = smenu.carousel.info_flash = false;
  if (!carousel_fullpath(sel, path) || !rom_path_ok(path))
    return;

  sram_filename_calc(path, savefn, save_path_default);
  smenu.carousel.info_save = check_file_exists(savefn);

  // Recent entries of games started from flash also hold their SD file path.
  if (smenu.recent.maxentries)
    smenu.carousel.info_last = !strncmp(sdr_state->rentries[0].fpath, path, MAX_FN_LEN);

  #ifdef SUPPORT_NORGAMES
  smenu.carousel.info_flash = carousel_nor_entry(sel) != NULL;
  #endif
}

// Background color: the theme's, tinted with the selected cover's color.
static void carousel_update_tint() {
  const uint16_t base = themes[menu_theme].bg_color;
  const unsigned slot = smenu.carousel.selector % COVER_SLOTS;
  uint16_t target = base;
  if (smenu.carousel.count && smenu.carousel.slot_entry[slot] == smenu.carousel.selector &&
      smenu.carousel.slot_state[slot] == CoverLoaded && smenu.carousel.slot_tint[slot] >= 0)
    target = rgb555_mix(base, smenu.carousel.slot_tint[slot], 6);

  // Fade one step (per color channel) each frame. Start at the right color.
  if (!smenu.carousel.tint_ok) {
    smenu.carousel.tint = target;
    smenu.carousel.tint_ok = true;
  }
  uint16_t c = smenu.carousel.tint;
  for (unsigned sh = 0; sh < 15; sh += 5) {
    int cc = (c >> sh) & 31, tc = (target >> sh) & 31;
    cc += (tc > cc) - (tc < cc);
    c = (c & ~(31 << sh)) | (cc << sh);
  }
  smenu.carousel.tint = c;
  MEM_PALETTE[CAROUSEL_BG_COLOR] = c;
}

// Horizontally clipped drawing helpers. left and width must be even.
static void carousel_fill(volatile uint8_t *frame, int left, int top, int w, int h, uint8_t color) {
  int x0 = MAX(left, 0);
  int x1 = MIN(left + w, SCREEN_WIDTH);
  if (x1 <= x0)
    return;
  for (int y = top; y < top + h; y++)
    dma_memset16(&frame[y * SCREEN_WIDTH + x0], dup8(color), (x1 - x0) / 2);
}

static void carousel_blit(volatile uint8_t *frame, const uint8_t *src, int size, int left, int top) {
  int x0 = MAX(0, -left);
  int x1 = MIN(size, SCREEN_WIDTH - left);
  if (x1 <= x0)
    return;
  for (int y = 0; y < size; y++)
    dma_memcpy16(&frame[(top + y) * SCREEN_WIDTH + left + x0], &src[y * size + x0], (x1 - x0) / 2);
}

// Draws the big cover scaled to size x size (nearest neighbour).
static void carousel_blit_scaled(volatile uint8_t *frame, const uint8_t *src, int size, int left, int top) {
  int x0 = MAX(0, -left);
  int x1 = MIN(size, SCREEN_WIDTH - left);
  if (x1 <= x0)
    return;

  // One extra entry: pixels are drawn in pairs, an odd width reads xmap[size].
  uint8_t xmap[COVER_BIG + 1];
  for (int x = 0; x < size; x++)
    xmap[x] = x * COVER_BIG / size;
  xmap[size] = xmap[size - 1];

  for (int y = 0; y < size; y++) {
    uint32_t row[COVER_BIG / 4];
    uint16_t out[COVER_BIG / 2];
    dma_memcpy32(row, &src[(y * COVER_BIG / size) * COVER_BIG], COVER_BIG / 4);
    const uint8_t *r = (uint8_t*)row;
    for (int x = x0; x < x1; x += 2)
      out[(x - x0) / 2] = r[xmap[x]] | (r[xmap[x + 1]] << 8);
    // cppcheck-suppress uninitvar ; every pair copied is written just above
    dma_memcpy16(&frame[(top + y) * SCREEN_WIDTH + left + x0], out, (x1 - x0) / 2);
  }
}

static void carousel_draw_cover(volatile uint8_t *frame, int n, int cx) {
  // Covers grow as they get closer to the center.
  const int dist = cx > SCREEN_WIDTH / 2 ? cx - SCREEN_WIDTH / 2 : SCREEN_WIDTH / 2 - cx;
  const int size = cover_size_at(dist, COVER_SPACING);
  const int top = COVER_CENTER_Y - size / 2;
  const int left = cx - size / 2;
  const unsigned slot = n % COVER_SLOTS;

  if (dist < COVER_SPACING / 2)    // Frame around the selected cover
    carousel_fill(frame, left - 2, top - 2, size + 4, size + 4, FG_COLOR);

  if (smenu.carousel.slot_entry[slot] == n && smenu.carousel.slot_state[slot] == CoverLoaded) {
    if (size == COVER_BIG)
      carousel_blit(frame, sdr_state->cover_big[slot], size, left, top);
    else if (size == COVER_SMALL)
      carousel_blit(frame, sdr_state->cover_small[slot], size, left, top);
    else
      carousel_blit_scaled(frame, sdr_state->cover_big[slot], size, left, top);
  }
  else {
    // No cover art (yet): draw a card with the file type icon.
    carousel_fill(frame, left, top, size, size, FG_COLOR);
    carousel_fill(frame, left + 2, top + 2, size - 4, size - 4, HI_COLOR);
    if (cx >= 8 && cx + 8 <= SCREEN_WIDTH) {
      char fname[MAX_FN_LEN];
      carousel_fname(n, fname);
      render_icon(cx - 8, COVER_CENTER_Y - 8, guessicon(fname));
    }
  }
}

// Big letter shown for a moment after jumping with Up/Down.
static void carousel_draw_letter(volatile uint8_t *frame) {
  if ((int)(smenu.carousel.letter_until - frame_count) <= 0)
    return;

  // Render the glyph in a small buffer, then draw it 3x bigger.
  uint16_t glyph16[16 * 16 / 2];
  uint8_t *glyph = (uint8_t*)glyph16;
  memset(glyph16, 0, sizeof(glyph16));
  unsigned w = MIN(font_width(smenu.carousel.letter), 16U);
  draw_text_idx8_bus16(smenu.carousel.letter, &glyph[(16 - w) / 2], 16, 1);

  const int box = 56, left = SCREEN_WIDTH / 2 - box / 2, top = COVER_CENTER_Y - box / 2;
  carousel_fill(frame, left, top, box, box, FG_COLOR);
  carousel_fill(frame, left + 2, top + 2, box - 4, box - 4, HI_COLOR);
  for (int y = 0; y < 48; y++) {
    uint16_t out[24];
    const uint8_t *g = &glyph[(y / 3) * 16];
    for (int x = 0; x < 48; x += 2)
      out[x / 2] = (g[x / 3] ? FT_COLOR : HI_COLOR) | ((g[(x + 1) / 3] ? FT_COLOR : HI_COLOR) << 8);
    dma_memcpy16(&frame[(top + 4 + y) * SCREEN_WIDTH + left + 4], out, 24);
  }
}

void render_carousel(volatile uint8_t *frame) {
  // Render bar below to show the dir being displayed
  dma_memset16(&frame[240*144], dup8(FG_COLOR), 240*16/2);

  if (smenu.carousel.browser_gen != browser_gen || smenu.carousel.stale) {
    // Show a message first, then scan on the next frame (it can take a bit).
    // If the message was shown a while ago (tab switched away), show it again.
    if (!smenu.carousel.scan_frame || frame_count - smenu.carousel.scan_frame > 4) {
      smenu.carousel.scan_frame = frame_count ? frame_count : 1;
      draw_text_leftovf(smenu.browser.cpath, frame, 8, 144, SCREEN_WIDTH - 8);
      draw_central_text(msgs[lang_id][smenu.carousel.stale ? MSG_CAR_UPDATE : MSG_CAR_SCAN],
                        frame, SCREEN_WIDTH / 2, 64);
      return;
    }
    carousel_rebuild();
  }

  // Tinted background for the main area.
  carousel_update_tint();
  dma_memset16(&frame[16*SCREEN_WIDTH], dup8(CAROUSEL_BG_COLOR), SCREEN_WIDTH*128/2);

  if (!smenu.carousel.count) {
    draw_text_leftovf(smenu.carousel.root, frame, 8, 144, SCREEN_WIDTH - 8);
    draw_central_text_wrapped(msgs[lang_id][smenu.carousel.total ? MSG_CAR_NOVIEW : MSG_CAR_EMPTY],
                              frame, SCREEN_WIDTH / 2, 48, SCREEN_WIDTH - 32);
    carousel_verify_step();
    return;
  }

  carousel_sync();
  carousel_verify_step();
  carousel_update_info();

  // Slide towards the resting position (frame based, so frame drops don't slow it down)
  unsigned elapsed = MIN(frame_count - smenu.carousel.lastframe, 8U);
  smenu.carousel.lastframe = frame_count;
  while (elapsed-- && smenu.carousel.offset) {
    int dist = smenu.carousel.offset > 0 ? smenu.carousel.offset : -smenu.carousel.offset;
    int step = (dist / 4) & ~1;
    if (step < 2)
      step = 2;
    if (smenu.carousel.offset > 0)
      smenu.carousel.offset = MAX(0, smenu.carousel.offset - step);
    else
      smenu.carousel.offset = MIN(0, smenu.carousel.offset + step);
  }

  static const int order[] = { -2, 2, -1, 1, 0 };
  for (unsigned i = 0; i < ARRAY_SIZE(order); i++) {
    int n = smenu.carousel.selector + order[i];
    if (n >= 0 && n < smenu.carousel.count)
      carousel_draw_cover(frame, n, SCREEN_WIDTH / 2 + order[i] * COVER_SPACING + smenu.carousel.offset);
  }

  // Folder of the selected game in the bottom bar.
  char fpath[MAX_FN_LEN];
  if (carousel_fullpath(smenu.carousel.selector, fpath)) {
    char *s = strrchr(fpath, '/');
    if (s)
      s[1] = 0;
    draw_text_leftovf(fpath, frame, 8, 144, SCREEN_WIDTH - 8);
  }

  // Game title and type/size/save info.
  char fname[MAX_FN_LEN], title[MAX_FN_LEN];
  carousel_fname(smenu.carousel.selector, fname);
  carousel_title(fname, title);
  if (font_width(title) <= SCREEN_WIDTH - 16)
    draw_central_text(title, frame, SCREEN_WIDTH / 2, 105);
  else
    draw_text_ovf_rotate(title, frame, 8, 105, SCREEN_WIDTH - 16, &smenu.anim_state);

  char ext[8] = {0}, szstr[16], info[160];
  const char *fext = find_extension(fname);
  for (unsigned i = 0; fext && fext[i] && i < sizeof(ext) - 1; i++)
    ext[i] = (fext[i] >= 'a' && fext[i] <= 'z') ? fext[i] - 'a' + 'A' : fext[i];
  human_size(szstr, sizeof(szstr), carousel_game(smenu.carousel.selector)->filesize);
  npf_snprintf(info, sizeof(info), "%s \xc2\xb7 %s", ext, szstr);
  if (smenu.carousel.info_for == smenu.carousel.selector) {
    const bool fav = sdr_state->carousel_games[carousel_gi(smenu.carousel.selector)].attr & CATTR_FAV;
    const char *tags[] = {
      fav                       ? msgs[lang_id][MSG_CAR_FAV]   : NULL,
      smenu.carousel.info_save  ? msgs[lang_id][MSG_CAR_SAVE]  : NULL,
      smenu.carousel.info_last  ? msgs[lang_id][MSG_CAR_LAST]  : NULL,
      smenu.carousel.info_flash ? msgs[lang_id][MSG_CAR_FLASH] : NULL,
    };
    for (unsigned i = 0; i < ARRAY_SIZE(tags); i++)
      if (tags[i] && strlen(info) + strlen(tags[i]) + 4 < sizeof(info)) {
        strcat(info, " \xc2\xb7 ");
        strcat(info, tags[i]);
      }
  }
  draw_central_text_ovf(info, frame, SCREEN_WIDTH / 2, 122, SCREEN_WIDTH - 16);

  // Position in the list.
  if (smenu.carousel.count > 1) {
    const int track = SCREEN_WIDTH - 16;
    int w = (track / smenu.carousel.count) & ~1;    // Not MAX(): its macro breaks '&'
    if (w < 8)
      w = 8;
    int x = 8 + (((track - w) * smenu.carousel.selector / (smenu.carousel.count - 1)) & ~1);
    carousel_fill(frame, 8, 140, track, 2, FG_COLOR);
    carousel_fill(frame, x, 139, w, 4, FT_COLOR);
  }

  // Position, with the view's name unless all games are shown (and if it
  // fits next to the tab icons).
  char selinfo[48], vname[24] = "";
  if (car_view != CarViewAll)
    carousel_view_name(car_view, vname, sizeof(vname));
  npf_snprintf(selinfo, sizeof(selinfo), "%s%s%d/%d%s", vname, vname[0] ? " " : "",
               smenu.carousel.selector + 1, smenu.carousel.count, smenu.carousel.truncated ? "+" : "");
  if (font_width(selinfo) > 92)
    npf_snprintf(selinfo, sizeof(selinfo), "%d/%d%s", smenu.carousel.selector + 1, smenu.carousel.count,
                 smenu.carousel.truncated ? "+" : "");
  draw_rightj_text(selinfo, frame, SCREEN_WIDTH - 1, 1);

  carousel_draw_letter(frame);
}

#pragma GCC push_options
#pragma GCC optimize ("Os")   // Not speed critical: keep the flash images small

// Start in the carousel: which games are shown, and in which order.
void render_carousel_view(volatile uint8_t *frame) {
  draw_box_outline(frame, 2, 240-2, 18, 158, FG_COLOR);
  for (unsigned i = 0; i < 2; i++) {
    const unsigned y = 34 + i * 54;
    char tmp[64], name[40];
    if (i == 0)
      carousel_view_name(car_view, name, sizeof(name));
    else
      npf_snprintf(name, sizeof(name), "%s", msgs[lang_id][MSG_CAR_SNAME + MIN(car_sort, CarSortCNT - 1)]);
    npf_snprintf(tmp, sizeof(tmp), "< %s >", name);
    draw_central_text(msgs[lang_id][i ? MSG_CAR_SORT : MSG_CAR_VIEW], frame, SCREEN_WIDTH / 2, y);
    draw_central_text(tmp, frame, SCREEN_WIDTH / 2, y + 20);
    if (i == (unsigned)spop.selector)
      for (unsigned x = 8; x < 232; x += 16)
        render_icon_trans(x, y + 20, 63);
  }
}

#pragma GCC pop_options

void reload_theme(unsigned thnum) {
  // Palette 0..15 contains the main menu template colors
  MEM_PALETTE[FG_COLOR] = themes[thnum].fg_color;
  MEM_PALETTE[BG_COLOR] = themes[thnum].bg_color;
  MEM_PALETTE[FT_COLOR] = themes[thnum].ft_color;
  MEM_PALETTE[HI_COLOR] = themes[thnum].hi_color;
  // In-game menu palette
  MEM_PALETTE[IGM_PAL_FG] = themes[thnum].fg_color;
  MEM_PALETTE[IGM_PAL_BG] = themes[thnum].bg_color;
  MEM_PALETTE[IGM_PAL_HI] = themes[thnum].ft_color;
  MEM_PALETTE[IGM_PAL_SH] = themes[thnum].sh_color;
  MEM_PALETTE[IGM_PAL_BL] = themes[thnum].hi_blend;

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
  { render_carousel_view,  1 },
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
  static const uint8_t tab_icons[] = {
    ICON_RECENT,
    ICON_DISK,
    #ifdef SUPPORT_NORGAMES
    ICON_FLASH,
    #endif
    ICON_CAROUSEL,
    ICON_SETTINGS,
    ICON_UILANG_SETTINGS,
    ICON_TOOLS,
    ICON_INFO,
  };
  _Static_assert (sizeof(tab_icons) == MENUTAB_MAX, "one icon per menu tab");

  int mintab = (recent_menu && smenu.recent.maxentries) ? MENUTAB_RECENT : MENUTAB_ROMBROWSE;
  for (unsigned i = mintab; i < MENUTAB_MAX; i++)
    if (i == smenu.menu_tab)
      render_icon((i - mintab)*16, 0, tab_icons[i]);
    else
      render_icon_trans((i - mintab)*16, 0, tab_icons[i]);

  // Render the main area
  dma_memset16(&frame[16*SCREEN_WIDTH], dup8(BG_COLOR), SCREEN_WIDTH*(SCREEN_HEIGHT-16) / 2);

  if (reboot_on_alert) {
    // Failed ROM load, the lists in SDRAM are garbage: only show the alert.
  }
  else if (spop.qpop.message)
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
        render_carousel,
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
  // ui-settings.txt values are used as table indexes but are not range
  // checked when parsed (the file could be damaged or edited by hand).
  if (menu_theme >= THEME_COUNT)
    menu_theme = 0;
  if (anim_speed >= animspd_cnt)
    anim_speed = animspd_cnt / 2;

  // Unpack the strings of the language in use (loaded from the settings).
  lang_load(lang_id);

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

  // Fixed 6x6x6 color cube used by the carousel cover art.
  for (unsigned i = 0; i < COVER_PAL_SIZE; i++) {
    unsigned r = i % 6, g = (i / 6) % 6, b = i / 36;
    MEM_PALETTE[COVER_PAL_BASE + i] = (r * 31 / 5) | ((g * 31 / 5) << 5) | ((b * 31 / 5) << 10);
  }

  smenu.menu_tab = start_tab ? MENUTAB_CAROUSEL :
                   (recent_menu && smenu.recent.maxentries) ? MENUTAB_RECENT : MENUTAB_ROMBROWSE;

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
  lang_scratch_used();
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

// Key handlers run once per key press, not per frame (the work they start,
// ie. loading or flashing, lives in other files): optimize them for size.
#pragma GCC push_options
#pragma GCC optimize ("Os")

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
        insert_recent_flush(spop.p.load.i.romfn, FLAG_RECENT_SD);

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
        .ts_step = rtcspeed_default
      };

      unsigned err = load_gba_rom(
        spop.p.load.i.romfn, spop.p.load.i.romfs,
        spop.p.load.l.sram_save_type == SaveDisable ? NULL : spop.p.load.l.savefn, p,
        spop.p.load.l.sram_save_type == SaveDirect ? &dsinfo : NULL,
        spop.p.load.i.ingame_menu_enabled,
        spop.p.load.i.rtc_patch_enabled ? &rtci : NULL,
        spop.p.load.l.use_cheats ? spop.p.load.l.cheats_size : 0,
        loadrom_progress);
      if (err) {
        // Loading writes the in-game menu and the ROM over the SDRAM, where the
        // menu keeps its file lists, fonts and patch database. Unless it failed
        // before writing anything, the menu cannot continue: reboot instead.
        reboot_on_alert = (err != ERR_NO_PAYLOAD_SPACE);
        spop.alert_msg = msgs[lang_id][reboot_on_alert ? MSG_ERR_LOADRST : MSG_ERR_READ];
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
      if (write_save_sram(spop.p.savopt.savfn, 0))
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
  if ((curr_pressed_keys() & FLASH_GO_KEYS) == FLASH_GO_KEYS)
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
        lang_scratch_used();
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

  const t_flash_game_entry *e = spop.p.norld.e;
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
        .ts_step = rtcspeed_default
      };

      if (recent_menu)
        insert_recent_flush(e->game_name, FLAG_RECENT_NOR);

      // Does not return when the game starts. If it ever does, the in-game
      // menu may already be in SDRAM (over the menu data): reboot.
      if (launch_gba_nor(
            e->game_name,
            spop.p.norld.l.sram_save_type == SaveDisable ? NULL : spop.p.norld.l.savefn,
            e->blkmap, e->numblks,
            uses_dsave ? &dsinfo : NULL,
            uses_rtc ? &rtci : NULL,
            uses_igm,
            spop.p.norld.l.use_cheats ? spop.p.norld.l.cheats_size : 0)) {
        reboot_on_alert = true;
        spop.alert_msg = msgs[lang_id][MSG_ERR_LOADRST];
      }
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

#ifdef SUPPORT_NORGAMES
// Removes a game from the flash (NOR) list and stores the new list. Used by
// the flash tab and the carousel (same question, same behaviour).
static void nor_remove_game(unsigned idx) {
  if (idx >= sdr_state->nordata.gamecnt)
    return;

  // Remove game entry, just memmove the other games on top.
  sdr_state->nordata.gamecnt--;
  memmove32(&sdr_state->nordata.games[idx],
            &sdr_state->nordata.games[idx + 1],
            (sdr_state->nordata.gamecnt - idx) * sizeof(t_flash_game_entry));

  // Go ahead and write a new metadata entry;
  if (!flashmgr_store(ROM_FLASHMETA_ADDR, FLASH_METADATA_SIZE, (t_reg_entry*)&sdr_state->nordata))
    spop.alert_msg = msgs[lang_id][MSG_ERR_NORUPD];
  flashbrowser_reload();   // Force list reload, free block calculation, etc.
}
#endif

// Opens the file manager pop up for a file.
static void filemgr_open(const char *path, uint32_t filesize, uint8_t attr, bool carousel, int nor_index) {
  strcpy(spop.fmgr.path, path);
  spop.fmgr.filesize = filesize;
  spop.fmgr.attr = attr;
  spop.fmgr.carousel = carousel;
  spop.fmgr.nor_index = nor_index;
  spop.fmgr.cgame = carousel ? (int)carousel_gi(smenu.carousel.selector) : -1;

  spop.fmgr.optcnt = 0;
  spop.fmgr.opts[spop.fmgr.optcnt++] = FiMgrDelete;
  spop.fmgr.opts[spop.fmgr.optcnt++] = FiMgrHide;
  #ifdef SUPPORT_NORGAMES
  if (nor_index < 0)
    spop.fmgr.opts[spop.fmgr.optcnt++] = FiMgrWriteNOR;
  else {
    spop.fmgr.opts[spop.fmgr.optcnt++] = FiMgrRemoveNOR;
    spop.fmgr.opts[spop.fmgr.optcnt++] = FiMgrLoadSD;
  }
  #endif
  if (carousel)
    spop.fmgr.opts[spop.fmgr.optcnt++] = FiMgrFav;

  spop.pop_num = POPUP_FILE_MGR;
  spop.anim = 0;
  spop.selector = 0;
}

static void keypress_popup_filemgr(unsigned newkeys) {
  if (newkeys & KEY_BUTTUP)
    spop.selector = MAX(0, spop.selector - 1);
  if (newkeys & KEY_BUTTDOWN)
    spop.selector = MIN(spop.fmgr.optcnt - 1, spop.selector + 1);

  if (newkeys & KEY_BUTTA) {
    switch (spop.fmgr.opts[(unsigned)spop.selector]) {
    case FiMgrDelete:
      {
        void remove_file_action(bool confirm) {
          if (confirm) {
            if (FR_OK != f_unlink(spop.fmgr.path))
              spop.alert_msg = msgs[lang_id][MSG_ERR_DELFILE];
            else
              spop.alert_msg = msgs[lang_id][MSG_OK_DELFILE];

            browser_reload();   // Force reload so the file disappears!
            smenu.carousel.stale = true;
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
      if (FR_OK == f_chmod(spop.fmgr.path, spop.fmgr.attr ^ AM_HID, AM_HID)) {
        if (spop.fmgr.carousel) {
          // The file can be in any folder: reload, the carousel rescans.
          browser_reload();
          smenu.carousel.stale = true;
        }
        else
          sdr_state->fileorder[smenu.browser.selector]->attr ^= AM_HID;
      }
      else
        spop.alert_msg = msgs[lang_id][MSG_ERR_GENERIC];
      spop.pop_num = POPUP_NONE;
      break;

    case FiMgrFav:
      if (spop.fmgr.cgame >= 0) {
        const bool fav = !(sdr_state->carousel_games[spop.fmgr.cgame].attr & CATTR_FAV);
        const int prev = smenu.carousel.selector;
        if (!carousel_fav_store(spop.fmgr.path, fav))
          spop.alert_msg = msgs[lang_id][MSG_ERR_GENERIC];
        else {
          sdr_state->carousel_games[spop.fmgr.cgame].attr ^= CATTR_FAV;
          if (car_view == CarViewFav) {
            // It leaves (or joins) the view: stay around the same place.
            carousel_build_view(spop.fmgr.cgame);
            if (!fav)
              smenu.carousel.selector = MAX(0, MIN(prev, smenu.carousel.count - 1));
          }
        }
      }
      spop.pop_num = POPUP_NONE;
      break;

    #ifdef SUPPORT_NORGAMES
    case FiMgrWriteNOR:
      if (spop.fmgr.filesize > MAX_GBA_ROM_SIZE)
        spop.alert_msg = msgs[lang_id][MSG_ERR_TOOBIG];
      else if (!rom_path_ok(spop.fmgr.path))
        spop.alert_msg = msgs[lang_id][MSG_ERR_PATHLEN];
      else {
        // Load default loading settings if any.
        t_rom_load_settings ld_sett = {
          .patch_policy = patcher_default,
          .use_igm = ingamemenu_default,
          .use_rtc = rtcpatch_default,
          .use_dsaving = autosave_prefer_ds
        };
        load_rom_settings(spop.fmgr.path, &ld_sett, NULL);

        if (!prepare_gba_info(&spop.p.norwr.i, &ld_sett, spop.fmgr.path, spop.fmgr.filesize, false))
          spop.alert_msg = msgs[lang_id][MSG_ERR_READ];
        else {
          spop.pop_num = POPUP_GBA_NORWRITE;
          spop.submenu = GbaLoadPopInfo;
          spop.selector = 0;
        }
      }
      break;

    case FiMgrRemoveNOR:
      {
        void remove_nor_action(bool confirm) {
          if (confirm)
            nor_remove_game(spop.fmgr.nor_index);
        }
        spop.qpop.message = msgs[lang_id][MSG_Q5_DELNORG];
        spop.qpop.default_button = msgs[lang_id][MSG_Q_NO];
        spop.qpop.confirm_button = msgs[lang_id][MSG_Q_YES];
        spop.qpop.option = 0;
        spop.qpop.callback = remove_nor_action;
        spop.qpop.clear_popup_ok = true;
      }
      break;

    case FiMgrLoadSD:
      browser_open(spop.fmgr.path, spop.fmgr.filesize);
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
      #ifdef SUPPORT_NORGAMES
      if (e->flags & FLAG_RECENT_NOR) {
        // Try to find the ROM in the current flash metadata.
        for (unsigned i = 0; i < sdr_state->nordata.gamecnt; i++) {
          const t_flash_game_entry *fe = &sdr_state->nordata.games[i];
          if (!strcmp(e->fpath, fe->game_name)) {
            browser_open_nor(fe);
            return;
          }
        }
        spop.alert_msg = msgs[lang_id][MSG_ERR_GENERIC];
      }
      else
      #endif
      {
        // stat() the file since we need the size, and validate that it exists!
        FILINFO info;
        FRESULT res = f_stat(e->fpath, &info);
        if (res == FR_OK)
          browser_open(e->fpath, info.fsize);
        else
          spop.alert_msg = msgs[lang_id][MSG_ERR_READ];
      }
    }
    else if (newkeys & KEY_BUTTSEL) {
      void recent_del_cb(bool confirm) {
        if (confirm)
          delete_recent_flush(smenu.recent.selector);
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
      char path[MAX_FN_LEN];
      if (e->isdir) {
        if (!path_join(path, smenu.browser.cpath, e->fname, "/")) {
          spop.alert_msg = msgs[lang_id][MSG_ERR_PATHLEN];
          return;
        }
        strcpy(smenu.browser.cpath, path);
        // Push selector history and reset it in the new dir
        memmove(&smenu.browser.selhist[1], &smenu.browser.selhist[0],
                sizeof(smenu.browser.selhist) - sizeof(smenu.browser.selhist[0]));
        smenu.browser.selhist[0] = smenu.browser.selector;
        smenu.browser.selector = 0;
        browser_reload();
      } else {
        if (path_join(path, smenu.browser.cpath, e->fname, ""))
          browser_open(path, e->filesize);
        else
          spop.alert_msg = msgs[lang_id][MSG_ERR_PATHLEN];
      }
    }
    else if (newkeys & KEY_BUTTSEL) {
      // Shows a file management menu.
      t_centry *e = sdr_state->fileorder[smenu.browser.selector];
      char path[MAX_FN_LEN];
      if (path_join(path, smenu.browser.cpath, e->fname, ""))
        filemgr_open(path, e->filesize, e->attr, false, -1);
      else
        spop.alert_msg = msgs[lang_id][MSG_ERR_PATHLEN];
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

    if (newkeys & KEY_BUTTA)
      browser_open_nor(&sdr_state->nordata.games[smenu.fbrowser.selector]);
    else if (newkeys & KEY_BUTTSEL) {
      // Prompt NOR entry deletion.
      void remove_nor_action(bool confirm) {
        if (confirm)
          nor_remove_game(smenu.fbrowser.selector);
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
    smenu.set.selector = MIN(SettMAX - 1, smenu.set.selector + 1);
  if (newkeys & KEY_BUTTLEFT) {
    if (smenu.set.selector == SettHotkey)
      hotkey_combo = (hotkey_combo + hotkey_listcnt - 1) % hotkey_listcnt;
    else if (smenu.set.selector == SettSaveLoc)
      save_path_default = (save_path_default + SaveDirCNT - 1) % SaveDirCNT;
    #ifdef SUPPORT_NORGAMES
    else if (smenu.set.selector == SettSaveLocNOR)
      save_path_nor_default = (save_path_nor_default + SaveDirNORCNT - 1) % SaveDirNORCNT;
    #endif
    else if (smenu.set.selector == SettStateLoc)
      state_path_default = (state_path_default + StateDirCNT - 1) % StateDirCNT;
    else if (smenu.set.selector == SettSaveBkp)
      backup_sram_default = backup_sram_default ? backup_sram_default - 1 : 0;
    else if (smenu.set.selector == DefsPatchEng)
      patcher_default = (patcher_default + PatchTotalCNT - 1) % PatchTotalCNT;
    else if (smenu.set.selector == DefsRTCSpeed)
      rtcspeed_default = (rtcspeed_default + RTC_SPEED_CNT - 1) % RTC_SPEED_CNT;
  }
  if (newkeys & KEY_BUTTRIGHT) {
    if (smenu.set.selector == SettHotkey)
      hotkey_combo = (hotkey_combo + 1) % hotkey_listcnt;
    else if (smenu.set.selector == SettSaveLoc)
      save_path_default = (save_path_default + 1) % SaveDirCNT;
    #ifdef SUPPORT_NORGAMES
    else if (smenu.set.selector == SettSaveLocNOR)
      save_path_nor_default = (save_path_nor_default + 1) % SaveDirNORCNT;
    #endif
    else if (smenu.set.selector == SettStateLoc)
      state_path_default = (state_path_default + 1) % StateDirCNT;
    else if (smenu.set.selector == SettSaveBkp)
      backup_sram_default = MIN(MAX_BACKUP_CNT, backup_sram_default + 1);
    else if (smenu.set.selector == DefsPatchEng)
      patcher_default = (patcher_default + 1) % PatchTotalCNT;
    else if (smenu.set.selector == DefsRTCSpeed)
      rtcspeed_default = (rtcspeed_default + 1) % RTC_SPEED_CNT;
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
    #ifdef SUPPORT_NORGAMES
    else if (smenu.set.selector == SettVerifyNOR)
      use_verify_nor ^= 1;
    #endif
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
    else if (smenu.uiset.selector == UiSetStart)
      start_tab = !start_tab;
    else if (smenu.uiset.selector == UiSetExt)
      show_ext = !show_ext;
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
    else if (smenu.uiset.selector == UiSetStart)
      start_tab = !start_tab;
    else if (smenu.uiset.selector == UiSetExt)
      show_ext = !show_ext;
    else if (smenu.uiset.selector == UiSetLang)
      lang_id = (lang_id + 1) % LANG_COUNT;
  }

  if ((newkeys & (KEY_BUTTLEFT | KEY_BUTTRIGHT)) && smenu.uiset.selector == UiSetLang)
    lang_load(lang_id);

  // Apply the hidden files filter now (not only after changing folders).
  if ((newkeys & (KEY_BUTTLEFT | KEY_BUTTRIGHT)) && smenu.uiset.selector == UiSetHid)
    browser_reload_filter();

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
      if (ret <= 0)
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
  if ((curr_pressed_keys() & FLASH_UNLOCK_KEYS) == FLASH_UNLOCK_KEYS)
    enable_flashing = true;
}

// Up/Down jump key: the first letter, or the system when sorted by system.
static unsigned carousel_letter(int n) {
  if (car_sort == CarSortSystem)
    return sdr_state->carousel_games[carousel_gi(n)].attr & CATTR_SYS;
  return carousel_letter_key(carousel_name_ptr(carousel_game(n)));
}

// Shows the first letter of the selected game's title for a moment.
static void carousel_flash_letter() {
  char fname[MAX_FN_LEN], title[MAX_FN_LEN];
  carousel_fname(smenu.carousel.selector, fname);
  carousel_title(fname, title);
  unsigned len = title[0] ? utf8_chlen(title) : 0;
  if (!len || len >= sizeof(smenu.carousel.letter))
    return;
  memcpy(smenu.carousel.letter, title, len);
  smenu.carousel.letter[len] = 0;
  if (len == 1 && title[0] >= 'a' && title[0] <= 'z')
    smenu.carousel.letter[0] -= 'a' - 'A';
  smenu.carousel.letter_until = frame_count + 45;
}

static void keypress_menu_carousel(unsigned newkeys) {
  // Ignore keys until the list for the current dir has been built.
  if (smenu.carousel.browser_gen != browser_gen || smenu.carousel.stale || !smenu.carousel.total)
    return;

  // Start: the view (which games are shown, and their order).
  if (newkeys & KEY_BUTTSTA) {
    spop.pop_num = POPUP_CAR_VIEW;
    spop.selector = 0;
    spop.anim = 0;
    return;
  }
  if (!smenu.carousel.count)
    return;

  const int cnt = smenu.carousel.count;
  int sel = smenu.carousel.selector;
  bool jumped = false;
  if ((newkeys & KEY_BUTTLEFT) && sel > 0) {
    sel--;
    smenu.carousel.offset = MAX(smenu.carousel.offset - COVER_SPACING, -COVER_SPACING);
  }
  else if ((newkeys & KEY_BUTTRIGHT) && sel < cnt - 1) {
    sel++;
    smenu.carousel.offset = MIN(smenu.carousel.offset + COVER_SPACING, COVER_SPACING);
  }
  else if ((newkeys & (KEY_BUTTUP | KEY_BUTTDOWN)) && car_sort == CarSortRecent) {
    // Recently played order: jump 5 games.
    sel = (newkeys & KEY_BUTTDOWN) ? MIN(sel + 5, cnt - 1) : MAX(sel - 5, 0);
    smenu.carousel.offset = 0;
  }
  else if (newkeys & KEY_BUTTDOWN) {
    // Jump to the first game starting with the next letter (or system)
    unsigned c = carousel_letter(sel);
    while (sel < cnt - 1 && carousel_letter(sel) == c)
      sel++;
    smenu.carousel.offset = 0;
    jumped = true;
  }
  else if (newkeys & KEY_BUTTUP) {
    // Jump to the first game of this letter, or of the previous one if already there.
    unsigned c = carousel_letter(sel);
    while (sel > 0 && carousel_letter(sel - 1) == c)
      sel--;
    if (sel == smenu.carousel.selector && sel > 0) {
      c = carousel_letter(--sel);
      while (sel > 0 && carousel_letter(sel - 1) == c)
        sel--;
    }
    smenu.carousel.offset = 0;
    jumped = true;
  }

  if (sel != smenu.carousel.selector) {
    smenu.carousel.selector = sel;
    smenu.anim_state = 0;
  }
  if (jumped && car_sort == CarSortName)
    carousel_flash_letter();
  else if (newkeys & (KEY_BUTTLEFT | KEY_BUTTRIGHT))
    smenu.carousel.letter_until = frame_count;    // Hide the letter when moving

  // A: start the game (from flash if it was written there).
  // Select: the file manager, like in the SD browser (plus flash options).
  if (newkeys & (KEY_BUTTA | KEY_BUTTSEL)) {
    int nor_index = -1;
    #ifdef SUPPORT_NORGAMES
    const t_flash_game_entry *fe = carousel_nor_entry(sel);
    if (fe && (newkeys & KEY_BUTTA)) {
      browser_open_nor(fe);
      return;
    }
    if (fe)
      nor_index = fe - sdr_state->nordata.games;
    #endif

    char path[MAX_FN_LEN];
    FILINFO info;
    if (!carousel_fullpath(sel, path))
      spop.alert_msg = msgs[lang_id][MSG_ERR_PATHLEN];
    else if (newkeys & KEY_BUTTA)
      browser_open(path, carousel_game(sel)->filesize);
    else if (FR_OK == f_stat(path, &info))
      filemgr_open(path, carousel_game(sel)->filesize, info.fattrib, true, nor_index);
    else
      spop.alert_msg = msgs[lang_id][MSG_ERR_READ];
  }
}

#pragma GCC push_options
#pragma GCC optimize ("Os")   // Not speed critical: keep the flash images small

static void keypress_popup_carview(unsigned newkeys) {
  if (newkeys & (KEY_BUTTUP | KEY_BUTTDOWN))
    spop.selector ^= 1;
  const int dir = (newkeys & KEY_BUTTRIGHT) ? 1 : (newkeys & KEY_BUTTLEFT) ? -1 : 0;
  if (dir) {
    if (spop.selector == 0)
      car_view = carousel_next_view(car_view, dir);
    else
      car_sort = (MIN(car_sort, CarSortCNT - 1) + CarSortCNT + dir) % CarSortCNT;
    // Stay on the selected game if it is still shown.
    carousel_build_view(smenu.carousel.count ? (int)carousel_gi(smenu.carousel.selector) : -1);
    save_ui_settings();
  }
  if (newkeys & (KEY_BUTTA | KEY_BUTTSTA))
    spop.pop_num = POPUP_NONE;
}

// Rewrites favorites.txt without path, and with it at the end if add.
typedef struct {
  FIL fd;
  const char *path;
  bool ok;
} t_fav_update;

static void fav_update_line(char *line, void *usr) {
  t_fav_update *u = (t_fav_update*)usr;
  UINT wr;
  if (strcmp(line, u->path))
    u->ok = u->ok && FR_OK == f_write(&u->fd, line, strlen(line), &wr) &&
            FR_OK == f_write(&u->fd, "\n", 1, &wr);
}

static bool carousel_fav_store(const char *path, bool add) {
  t_fav_update u = { .path = path, .ok = true };
  f_mkdir(SUPERFW_DIR);
  if (FR_OK != f_open(&u.fd, CAROUSEL_FAV_TMP, FA_WRITE | FA_CREATE_ALWAYS))
    return false;
  file_read_lines(CAROUSEL_FAV_PATH, fav_update_line, &u);
  UINT wr;
  if (add)
    u.ok = u.ok && FR_OK == f_write(&u.fd, path, strlen(path), &wr) &&
           FR_OK == f_write(&u.fd, "\n", 1, &wr);
  u.ok = FR_OK == f_close(&u.fd) && u.ok;
  if (!u.ok) {
    f_unlink(CAROUSEL_FAV_TMP);
    return false;
  }
  f_unlink(CAROUSEL_FAV_PATH);
  return FR_OK == f_rename(CAROUSEL_FAV_TMP, CAROUSEL_FAV_PATH);
}

#pragma GCC pop_options

// Reboots into the firmware in flash (used when the menu state can't be trusted).
static void reboot_firmware() {
  // The game never ran, so do not dump its SRAM to the .sav on the next boot.
  program_sram_dump(NULL, 0, 0);

  #ifdef SUPERCHIS_IO
  reset_superchis_normap();
  #endif
  set_supercard_mode(MAPPED_FIRMWARE, false, false);

  REG_WAITCNT = 0;    // Power-on default
  launch_reset(false, false);
}

void menu_keypress(unsigned newkeys) {
  if (spop.alert_msg) {
    // Modal message pop up!
    if (newkeys & (KEY_BUTTA | KEY_BUTTB)) {
      spop.alert_msg = NULL;
      if (reboot_on_alert)
        reboot_firmware();
    }
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
        keypress_popup_carview,
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
      keypress_menu_carousel,
      keypress_menu_settings,
      keypress_menu_uisettings,
      keypress_menu_tools,
      keypress_menu_info,
    };
    keyfns[smenu.menu_tab](newkeys);
  }
}

// Only repeat keys A/B and Dir
const uint16_t keyrep = 0x0F3;
static uint32_t keyreptmr[10] = {0};
static uint8_t  keyrepcnt[10] = {0};
static uint32_t prev_keys = 0;

// Handle button input. Supports key re-press whenever a button is held for a while.
// This key repeat pattern can be tuned for speed and what not.
uint16_t get_keypress() {
  uint32_t ckeys = curr_pressed_keys();
  uint32_t mkeys = 0;
  for (unsigned i = 0; i < 10; i++) {
    if (ckeys & (1 << i)) {
      if (!(prev_keys & (1 << i))) {
        keyreptmr[i] = systime() + KEY_REPEAT_INITIAL;
        mkeys |= (1 << i);
        keyrepcnt[i] = 0;
      }
      else if (((1 << i) & keyrep) && systime() > keyreptmr[i]) {
        if (keyrepcnt[i] > KEY_REPEAT_CNT1)
          keyreptmr[i] = systime() + KEY_REPEAT_FAST;
        else {
          keyreptmr[i] = systime() + KEY_REPEAT_MID;
          keyrepcnt[i]++;
        }
        mkeys |= (1 << i);
      }
    }
    else
      keyreptmr[i] = 0;
  }

  prev_keys = ckeys;
  return mkeys;
}

#pragma GCC pop_options
