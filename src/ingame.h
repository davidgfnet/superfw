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


#define MIN_SCRATCH_SPACE       (160*1024)        // IWRAM/EWRAM/VRAM + regs

#define EWRAM_SPILL_SIZE        (62 * 1024)
#define IWRAM_SPILL_SIZE        (16 * 1024)
#define VRAM_SPILL_SIZE         (80 * 1024)

#define IGM_ENTRYPOINT_NOCHEATS_OFF     ( 8*4)    // Offsets in ingame.S
#define IGM_ENTRYPOINT_CHEATS_OFF       (12*4)

#define FLASH_IGM_TRAMP_NOCHEATS_OFF    ( 8*4)    // Offsets in ingame_trampoline.S
#define FLASH_IGM_TRAMP_CHEATS_OFF      (32*4)

#ifndef __ASSEMBLER__

// In-game menu patching structure
// This is used to load and patch the menu with the required values.

typedef struct {
  uint32_t startup_insts[8];           // Initial startup instructions

  uint32_t tramp1_insts[4];            // NOR trampolines
  uint32_t tramp2_insts[4];

  uint32_t menu_rsize;                 // Payload size (including scratch area).

  uint32_t drv_issdhc;                 // Boolean (is SDHC card)
  uint32_t drv_rca;                    // SD card RCA id

  uint32_t menu_hotkey;                // Magic key combo to trigger menu
  uint32_t menu_lang;                  // Menu language code
  uint32_t menu_use_directsave;        // Whether direct save is in use.
  uint32_t menu_font_base;             // Font rendering graphic binary base address
  uint32_t menu_cheats_base;           // Cheats database entry for the current game
  uint32_t scratch_space_base;         // Empty scratch space for savestate purposes
  uint32_t scratch_space_size;
  uint32_t menu_has_rtc_support;       // Whether the game is running with RTC patches
  uint32_t menu_anim_speed;            // Menu animation speed
  uint16_t menu_palette[4];            // Palette colors for the menu
  uint32_t savefile_backups;           // Backup count
  char savefile_pattern[256];          // File name (without the .sav) pattern
  char statefile_pattern[256];         // File name (without the .X.state) pattern
} t_igmenu;

// Built-in assets
extern const t_igmenu ingame_menu_payload;
extern const uint32_t ingame_menu_payload_size;

// Savestate structure
typedef struct {

  uint16_t tm_cnt[4];          // REG_TMxCNT
  uint16_t dma_cnt[4];         // REG_DMAxCNT
  uint16_t dispcnt;
  uint16_t dispstat;
  uint16_t bg_cnt[4];          // REG_BGxCNT
  uint16_t bldcnt;
  uint16_t bldalpha;
  uint32_t soundcnt;

  uint32_t cpu_regs[16];

  uint32_t cpsr;

  uint32_t irq_regs[3];        // SP, LR and SPSR for IRQs mode
  uint32_t fiq_regs[3];        // SP, LR and SPSR for FIQ mode
  uint32_t sup_regs[3];        // SP, LR and SPSR for Supervisor mode
  uint32_t abt_regs[3];        // SP, LR and SPSR for abort mode
  uint32_t und_regs[3];        // SP, LR and SPSR for undefined mode

  uint8_t palette[1024];
  uint8_t low_vram[VRAM_SPILL_SIZE];
  uint8_t low_iwram[IWRAM_SPILL_SIZE];
  uint8_t low_ewram[EWRAM_SPILL_SIZE];

} t_spilled_region;

#define SIGNATURE_A          0x45505553     // SUPERFWSNAP
#define SIGNATURE_B          0x53574652
#define SIGNATURE_C          0x0050414e

typedef struct {
  uint32_t signature[3];       // Some signature for the file on disk
  uint32_t version;            // Savestate version.
  uint16_t pad[496 / 2];       // Unused header state
} t_savestate_header;

typedef struct {
  uint32_t cpu_regs[16];

  uint32_t cpsr;

  uint32_t irq_regs[3];        // SP, LR and SPSR for IRQs mode
  uint32_t fiq_regs[3];        // SP, LR and SPSR for FIQ mode
  uint32_t sup_regs[3];        // SP, LR and SPSR for Supervisor mode
  uint32_t abt_regs[3];        // SP, LR and SPSR for abort mode
  uint32_t und_regs[3];        // SP, LR and SPSR for undefined mode

  uint16_t pad[384 / 2];      // Unused space for now
} t_savestate_regs;

typedef struct {
  t_savestate_header header;

  t_savestate_regs regs;

  uint8_t ioram[1024];
  uint8_t palette[1024];
  uint8_t oamem[1024];
  uint8_t vram[96 * 1024];
  uint8_t iwram[32 * 1024];
  uint8_t ewram[256 * 1024];

} t_savestate_snapshot;

// I/O RAM mapping (only certain areas that need to be used)
typedef struct {
  // LCD registers
  uint16_t dispcnt;
  uint16_t pad1;

  uint16_t dispstat;
  uint16_t vcount;

  uint16_t bg_cnt[4];          // REG_BGxCNT
  uint32_t bg_ofs[4];          // REG_BGxHOFS + REG_BGxVOFS

  uint16_t bg2_rotscl[4];      // BG2P(ABCD)
  uint32_t bg2_ref[2];         // BG2X/BG2Y

  uint16_t bg3_rotscl[4];      // BG3P(ABCD)
  uint32_t bg3_ref[2];         // BG3X/BG3Y

  uint16_t win0h, win1h, win0v, win1v;
  uint16_t winin, winout;
  uint16_t mosaic;
  uint16_t pad2;

  uint16_t bldcnt, bldalpha, bldy;
  uint16_t pad3[5];

  // Sound registers
  uint32_t sound1cnt, sound1cnt_x;
  uint32_t sound2cnt_l, sound2cnt_h;
  uint32_t sound3cnt, sound3cnt_x;
  uint32_t sound4cnt_l, sound4cnt_h;
  uint32_t soundcnt, soundcnt_x;
  uint16_t soundbias;
  uint16_t pad4[3];

  uint16_t sound_wav[8];
  uint32_t sound_fifoA, sound_fifoB;
  uint32_t pad5[2];

  // DMA engine
  struct {
    uint32_t sad, dad;
    uint16_t cnt, ctrl;
  } dma[4];
  uint32_t pad6[8];

  // Timers
  struct {
    uint16_t tm_cntl, tm_cnth;
  } tms[4];
  uint32_t pad7[4];

  // Serial regs
  uint16_t ser_regs[8];

  // Input
  uint16_t keyinput, keycnt;

  // More serial regs (and padding)
  uint16_t ser_regs2[102];

  // Interrupts and misc stuff
  uint16_t reg_ie;
  uint16_t reg_if;
  uint16_t waitcnt;
  uint16_t pad8;

  uint16_t master_ie;

  uint16_t endpad[251];

} t_iomap;


_Static_assert(sizeof(t_iomap) == 1024, "I/O map size mismatch");
_Static_assert(sizeof(t_spilled_region) <= MIN_SCRATCH_SPACE, "Reserved spilled area size is too small");

_Static_assert(offsetof(t_igmenu, tramp1_insts) == IGM_ENTRYPOINT_NOCHEATS_OFF, "Struct offset mismatch");
_Static_assert(offsetof(t_igmenu, tramp2_insts) == IGM_ENTRYPOINT_CHEATS_OFF, "Struct offset mismatch");

#endif


