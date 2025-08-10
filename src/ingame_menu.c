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
#include "save.h"
#include "util.h"
#include "cheats.h"
#include "nanoprintf.h"
#include "fonts/font_render.h"
#include "menu_messages.h"
#include "res/logo.h"
#include "fatfs/ff.h"
#include "supercard_driver.h"
#include "res/icons-menu.h"
#include "ingame.h"

#include "directsave.h"

#define MIN(a,b) ((a) < (b) ? (a) : (b))
#define MAX(a,b) ((a) > (b) ? (a) : (b))

#define SAVESTATE_VERSION       0x00010000

// ASM functions and varibles:
extern unsigned has_rtc_support;
extern unsigned ingame_menu_lang;
extern uint32_t cheat_base_addr;
extern uint32_t menu_anim_speed;
extern uint16_t ingame_menu_palette[4];
extern uint32_t savefile_backups;                // Num of save backups to create
extern uint32_t scratch_base, scratch_size;      // Space to write snapshots (in memory)
extern uint32_t spill_addr;                      // Spill buffer that gets reloaded on IGM exit
extern char savefile_pattern[256];
extern char savestate_pattern[256];

void reset_game();
void reset_fw();
void fast_mem_cpy_256(void *dst, const void *src, unsigned count);
void fast_mem_clr_256(void *addr, uint32_t value, unsigned count);
void set_entrypoint_hook(bool process_cheats);
uint32_t *get_cheat_table();

#define MAX_DISK_SLOTS      5
#define MAX_MEM_SLOTS      32

#define FG_COLOR    16
#define BG_COLOR    17
#define HI_COLOR    18
#define SH_COLOR    19
#define ICON_PAL   128

#define THREEDOTS_WIDTH      9
#define ANIM_INITIAL_WAIT  128

#define SAVE_ICON            0    // Cannot save icon
#define DISK_ICON            1
#define DISK_ICON_DISABLED   2
#define MEM_ICON             3
#define MEM_ICON_DISABLED    4

#define MEM_VRAM_U8            (((volatile  uint8_t *) 0x06000000))
#define MEM_ROM_U8             (((volatile  uint8_t *) 0x08000000))
#define MEM_ROM_U16(off)       (((volatile  uint16_t *) (0x08000000 + (off))))

static unsigned submenu;
static unsigned copt;
static uint8_t rtc_values[6];
static struct {
  const char *msg;
  void (*callback)();
  unsigned opt;
} popup;
static unsigned franim = 0;

const uint8_t animspd_lut[] = {
  2,    //  8 pix/second
  3,    // 12 pix/second
  6,    // 24 pix/second
  8,    // 32 pix/second
  12,   // 48 pix/second
};

static bool diskst_init = false;
static int makepers = -1;
static int state_slot;
static int num_mem_savestates, num_dsk_savestates;
static uint8_t memslot_valid[MAX_MEM_SLOTS] = {0};
static uint8_t diskslot_valid[MAX_DISK_SLOTS] = {0};

void memory_set16(uint16_t *addr, uint16_t value, unsigned count) {
  while (count--)
    *addr++ = value;
}

void memory_copy16(uint16_t *addr, const uint16_t *src, unsigned count) {
  while (count--)
    *addr++ = *src++;
}

void memory_copy32(uint32_t *addr, const uint32_t *src, unsigned count) {
  while (count--)
    *addr++ = *src++;
}


// Save state management.

// Savestates take 388 KiB:
// VRAM(96KB) + IWRAM(32KB) + EWRAM(256KB) + PAL/OAM/IO(3KB) + regs... (1KB)
// CPU regs (16 regs) also include CPSR and SPSR (for each mode) and shadow regs.
#define SAVESTATE_SIZE_KB       388
_Static_assert(sizeof(t_savestate_snapshot) == SAVESTATE_SIZE_KB*1024, "Save state size is no bigger than 388KB");

static inline void* get_memslot_addr(unsigned slotnum) {
  return (void*)(scratch_base + ((slotnum * 388) << 10));
}

// The save state is a bit all over the place, since entering the menu only
// swaps some partial state (to save space and be faster).
void take_mem_snapshot(void *buffer) {
  // Memory layout in ingame.h

  const t_spilled_region *spill_ptr = (t_spilled_region*)spill_addr;
  t_savestate_snapshot *save_ptr = (t_savestate_snapshot*)buffer;

  // Copy the (partially) spilled buffers first.
  fast_mem_cpy_256(save_ptr->iwram, spill_ptr->low_iwram, sizeof(spill_ptr->low_iwram));
  fast_mem_cpy_256(save_ptr->ewram, spill_ptr->low_ewram, sizeof(spill_ptr->low_ewram));
  fast_mem_cpy_256(save_ptr->vram,  spill_ptr->low_vram,  sizeof(spill_ptr->low_vram));
  fast_mem_cpy_256(save_ptr->palette, spill_ptr->palette, sizeof(spill_ptr->palette));

  // Copy the remaining memory chunks (high segments)
  const uint8_t *IWRAM_BUF = (uint8_t*)0x03000000;
  fast_mem_cpy_256(&save_ptr->iwram[sizeof(spill_ptr->low_iwram)], &IWRAM_BUF[sizeof(spill_ptr->low_iwram)],
                   32*1024 - sizeof(spill_ptr->low_iwram));

  const uint8_t *EWRAM_BUF = (uint8_t*)0x02000000;
  fast_mem_cpy_256(&save_ptr->ewram[sizeof(spill_ptr->low_ewram)], &EWRAM_BUF[sizeof(spill_ptr->low_ewram)],
                   256*1024 - sizeof(spill_ptr->low_ewram));

  const uint8_t *VRAM_BUF = (uint8_t*)0x06000000;
  fast_mem_cpy_256(&save_ptr->vram[sizeof(spill_ptr->low_vram)], &VRAM_BUF[sizeof(spill_ptr->low_vram)],
                   96*1024 - sizeof(spill_ptr->low_vram));

  const uint8_t *OARAM_BUF = (uint8_t*)0x07000000;
  fast_mem_cpy_256(save_ptr->oamem, OARAM_BUF, sizeof(save_ptr->oamem));

  const uint8_t *IORAM_BUF = (uint8_t*)0x04000000;
  fast_mem_cpy_256(save_ptr->ioram, IORAM_BUF, sizeof(save_ptr->ioram));

  // Some I/O registers have been spilled to the spill area, we copy them too.
  t_iomap *siomap = (t_iomap*)save_ptr->ioram;
  siomap->dispcnt  = spill_ptr->dispcnt;
  siomap->dispstat = spill_ptr->dispstat;
  siomap->bldcnt   = spill_ptr->bldcnt;
  siomap->bldalpha = spill_ptr->bldalpha;
  siomap->soundcnt = spill_ptr->soundcnt;
  for (unsigned i = 0; i < 4; i++) {
    siomap->tms[i].tm_cntl = spill_ptr->tm_cnt[i];
    siomap->dma[i].ctrl    = spill_ptr->dma_cnt[i];
    siomap->bg_cnt[i]      = spill_ptr->bg_cnt[i];
  }

  memory_copy32(save_ptr->regs.cpu_regs, spill_ptr->cpu_regs, sizeof(save_ptr->regs.cpu_regs) / 4);

  save_ptr->regs.cpsr = spill_ptr->cpsr;

  memory_copy32(save_ptr->regs.irq_regs, spill_ptr->irq_regs, sizeof(save_ptr->regs.irq_regs) / 4);
  memory_copy32(save_ptr->regs.fiq_regs, spill_ptr->fiq_regs, sizeof(save_ptr->regs.fiq_regs) / 4);
  memory_copy32(save_ptr->regs.sup_regs, spill_ptr->sup_regs, sizeof(save_ptr->regs.sup_regs) / 4);
  memory_copy32(save_ptr->regs.abt_regs, spill_ptr->abt_regs, sizeof(save_ptr->regs.abt_regs) / 4);
  memory_copy32(save_ptr->regs.und_regs, spill_ptr->und_regs, sizeof(save_ptr->regs.und_regs) / 4);

  // Complete the state by clearing empty regions and completing the header
  memory_set16(save_ptr->header.pad, 0, sizeof(save_ptr->header.pad) / 2);
  memory_set16(save_ptr->regs.pad, 0, sizeof(save_ptr->regs.pad) / 2);
  save_ptr->header.signature[0] = SIGNATURE_A;
  save_ptr->header.signature[1] = SIGNATURE_B;
  save_ptr->header.signature[2] = SIGNATURE_C;
  save_ptr->header.version = SAVESTATE_VERSION;
}

bool write_rom_buffer(FIL *fd, const void *buffer, unsigned size, void *tmpbuf) {
  // If the IGM is loaded in the higher 16MB of ROM space, the spill buffer
  // and the SD driver cannot be mapped simultaneously. So we just use
  // a tmp buffer to copy/write stuff.
  // tmpbuf is 1024 bytes long, size is a multiple of 1024 bytes.

  const uint8_t* ptr = (uint8_t*)buffer;
  for (unsigned off = 0; off < size; off += 1024) {
    set_supercard_mode(MAPPED_SDRAM, true, false);   // Ensure we can read spill area.
    memory_copy32((uint32_t*)tmpbuf, (uint32_t*)&ptr[off], 1024 / 4);
    set_supercard_mode(MAPPED_SDRAM, true, true);   // So we can write to the SD card

    UINT wrbytes;
    if (FR_OK != f_write(fd, tmpbuf, 1024, &wrbytes) || wrbytes != 1024)
      return false;
  }

  return true;
}

// Same as above but we write directly to disk.
bool writefd_mem_snapshot(FIL *fd) {
  // Must write stuff in order, ideally in chunks multiple of 512 bytes.
  union {
    t_savestate_header header;
    t_savestate_regs regs;
    t_iomap iomap;
    uint8_t buf[1024];
  } tmp;
  _Static_assert(sizeof(tmp.header) == 512, "The header structure is 512 bytes in size");
  _Static_assert(sizeof(tmp.regs) == 512, "The regs structure is 512 bytes in size");
  _Static_assert(sizeof(tmp.iomap) == 1024, "The I/O structure is 1024 bytes in size");
  UINT wrbytes;
  const t_spilled_region *spill_ptr = (t_spilled_region*)spill_addr;

  memset(&tmp.header, 0, sizeof(tmp.header));
  tmp.header.signature[0] = SIGNATURE_A;
  tmp.header.signature[1] = SIGNATURE_B;
  tmp.header.signature[2] = SIGNATURE_C;
  tmp.header.version = SAVESTATE_VERSION;
  if (FR_OK != f_write(fd, &tmp.header, sizeof(tmp.header), &wrbytes) || wrbytes != sizeof(tmp.header))
    return false;

  set_supercard_mode(MAPPED_SDRAM, true, false);   // Ensure we can read spill area.
  memset(&tmp.regs, 0, sizeof(tmp.regs));
  tmp.regs.cpsr = spill_ptr->cpsr;
  memory_copy32(tmp.regs.cpu_regs, spill_ptr->cpu_regs, sizeof(tmp.regs.cpu_regs) / 4);
  memory_copy32(tmp.regs.irq_regs, spill_ptr->irq_regs, sizeof(tmp.regs.irq_regs) / 4);
  memory_copy32(tmp.regs.fiq_regs, spill_ptr->fiq_regs, sizeof(tmp.regs.fiq_regs) / 4);
  memory_copy32(tmp.regs.sup_regs, spill_ptr->sup_regs, sizeof(tmp.regs.sup_regs) / 4);
  memory_copy32(tmp.regs.abt_regs, spill_ptr->abt_regs, sizeof(tmp.regs.abt_regs) / 4);
  memory_copy32(tmp.regs.und_regs, spill_ptr->und_regs, sizeof(tmp.regs.und_regs) / 4);
  set_supercard_mode(MAPPED_SDRAM, true, true);   // So we can write to the SD card
  if (FR_OK != f_write(fd, &tmp.regs, sizeof(tmp.regs), &wrbytes) || wrbytes != sizeof(tmp.regs))
    return false;

  // Write the I/O RAM but patch in the spilled registers too.
  const uint32_t *IORAM_BUF = (uint32_t*)0x04000000;
  memory_copy32((uint32_t*)&tmp.iomap, IORAM_BUF, 1024 / 4);
  set_supercard_mode(MAPPED_SDRAM, true, false);   // Ensure we can read spill area.
  tmp.iomap.dispcnt  = spill_ptr->dispcnt;
  tmp.iomap.dispstat = spill_ptr->dispstat;
  tmp.iomap.bldcnt   = spill_ptr->bldcnt;
  tmp.iomap.bldalpha = spill_ptr->bldalpha;
  tmp.iomap.soundcnt = spill_ptr->soundcnt;
  for (unsigned i = 0; i < 4; i++) {
    tmp.iomap.tms[i].tm_cntl = spill_ptr->tm_cnt[i];
    tmp.iomap.dma[i].ctrl    = spill_ptr->dma_cnt[i];
    tmp.iomap.bg_cnt[i]      = spill_ptr->bg_cnt[i];
  }
  set_supercard_mode(MAPPED_SDRAM, true, true);   // So we can write to the SD card
  if (FR_OK != f_write(fd, &tmp.iomap, sizeof(tmp.iomap), &wrbytes) || wrbytes != sizeof(tmp.iomap))
    return false;

  if (!write_rom_buffer(fd, spill_ptr->palette, sizeof(spill_ptr->palette), tmp.buf))
    return false;

  const uint8_t *OARAM_BUF = (uint8_t*)0x07000000;
  if (FR_OK != f_write(fd, OARAM_BUF, 1024, &wrbytes) || wrbytes != 1024)
    return false;

  // VRAM, spilled, then actual data
  const uint8_t *VRAM_BUF = (uint8_t*)0x06000000;
  const unsigned highsize = 96*1024 - sizeof(spill_ptr->low_vram);

  if (!write_rom_buffer(fd, spill_ptr->low_vram, sizeof(spill_ptr->low_vram), tmp.buf))
    return false;
  if (FR_OK != f_write(fd, &VRAM_BUF[sizeof(spill_ptr->low_vram)], highsize, &wrbytes) || wrbytes != highsize)
    return false;

  // Same for IWRAM and EWRAM
  const uint8_t *IWRAM_BUF = (uint8_t*)0x03000000;
  const unsigned highsize2 = 32*1024 - sizeof(spill_ptr->low_iwram);
  if (!write_rom_buffer(fd, spill_ptr->low_iwram, sizeof(spill_ptr->low_iwram), tmp.buf))
    return false;
  if (FR_OK != f_write(fd, &IWRAM_BUF[sizeof(spill_ptr->low_iwram)], highsize2, &wrbytes) || wrbytes != highsize2)
    return false;

  const uint8_t *EWRAM_BUF = (uint8_t*)0x02000000;
  const unsigned highsize3 = 256*1024 - sizeof(spill_ptr->low_ewram);
  if (!write_rom_buffer(fd, spill_ptr->low_ewram, sizeof(spill_ptr->low_ewram), tmp.buf))
    return false;
  if (FR_OK != f_write(fd, &EWRAM_BUF[sizeof(spill_ptr->low_ewram)], highsize3, &wrbytes) || wrbytes != highsize3)
    return false;

  return true;
}

// Writes an in-memory state to disk. The format is the same, so a simple write is enough.
bool writefd_mem_snapshot_clone(FIL *fd, const void *buffer, unsigned size) {
  uint32_t tmp[1024/4];
  return write_rom_buffer(fd, buffer, size, tmp);
}


bool load_mem_snapshot(const void *buffer) {

  t_spilled_region *spill_ptr = (t_spilled_region*)spill_addr;
  const t_savestate_snapshot *save_ptr = (t_savestate_snapshot*)buffer;

  if (save_ptr->header.signature[0] != SIGNATURE_A ||
      save_ptr->header.signature[1] != SIGNATURE_B ||
      save_ptr->header.signature[2] != SIGNATURE_C)
    return false;

  if (save_ptr->header.version != SAVESTATE_VERSION)
    return false;

  // Copy the (partially) spilled buffers first.
  fast_mem_cpy_256(spill_ptr->low_iwram, save_ptr->iwram, sizeof(spill_ptr->low_iwram));
  fast_mem_cpy_256(spill_ptr->low_ewram, save_ptr->ewram, sizeof(spill_ptr->low_ewram));
  fast_mem_cpy_256(spill_ptr->low_vram,  save_ptr->vram,  sizeof(spill_ptr->low_vram));
  fast_mem_cpy_256(spill_ptr->palette, save_ptr->palette, sizeof(spill_ptr->palette));

  // Copy the remaining memory chunks (high segments)
  uint8_t *IWRAM_BUF = (uint8_t*)0x03000000;
  fast_mem_cpy_256(&IWRAM_BUF[sizeof(spill_ptr->low_iwram)], &save_ptr->iwram[sizeof(spill_ptr->low_iwram)],
                   32*1024 - sizeof(spill_ptr->low_iwram));

  uint8_t *EWRAM_BUF = (uint8_t*)0x02000000;
  fast_mem_cpy_256(&EWRAM_BUF[sizeof(spill_ptr->low_ewram)], &save_ptr->ewram[sizeof(spill_ptr->low_ewram)],
                   256*1024 - sizeof(spill_ptr->low_ewram));

  uint8_t *VRAM_BUF = (uint8_t*)0x06000000;
  fast_mem_cpy_256(&VRAM_BUF[sizeof(spill_ptr->low_vram)], &save_ptr->vram[sizeof(spill_ptr->low_vram)],
                   96*1024 - sizeof(spill_ptr->low_vram));

  uint8_t *OARAM_BUF = (uint8_t*)0x07000000;
  fast_mem_cpy_256(OARAM_BUF, save_ptr->oamem, sizeof(save_ptr->oamem));

  // Write spilled-area I/O regs so they can be restored at menu-exit point.
  const t_iomap *saved_io = (t_iomap*)save_ptr->ioram;
  spill_ptr->dispcnt  = saved_io->dispcnt;
  spill_ptr->dispstat = saved_io->dispstat;
  spill_ptr->bldcnt   = saved_io->bldcnt;
  spill_ptr->bldalpha = saved_io->bldalpha;
  spill_ptr->soundcnt = saved_io->soundcnt;
  for (unsigned i = 0; i < 4; i++) {
    spill_ptr->tm_cnt[i]  = saved_io->tms[i].tm_cntl;
    spill_ptr->dma_cnt[i] = saved_io->dma[i].ctrl;
    spill_ptr->bg_cnt[i]  = saved_io->bg_cnt[i];
  }

  // We cannot restore the full I/O space, many read only, write only and weird registers.
  // Let's restore them a bit more selectively.
  t_iomap *curr_ro_io = (t_iomap*)0x04000000;
  curr_ro_io->winin  = saved_io->winin;            // LCD registers (the rest are write only!)
  curr_ro_io->winout = saved_io->winout;
  curr_ro_io->sound1cnt   = saved_io->sound1cnt;   // Sound registers
  curr_ro_io->sound1cnt_x = saved_io->sound1cnt_x;
  curr_ro_io->sound2cnt_l = saved_io->sound2cnt_l;
  curr_ro_io->sound3cnt   = saved_io->sound3cnt;
  curr_ro_io->sound3cnt_x = saved_io->sound3cnt_x;
  curr_ro_io->sound4cnt_l = saved_io->sound4cnt_l;
  curr_ro_io->soundcnt_x  = saved_io->soundcnt_x;
  curr_ro_io->keycnt  = saved_io->keycnt;          // Input regs
  curr_ro_io->reg_ie  = saved_io->reg_ie;          // IRQ regs
  curr_ro_io->master_ie  = saved_io->master_ie;

  for (unsigned i = 0; i < 4; i++)                 // Timers
    curr_ro_io->tms[i].tm_cnth  = saved_io->tms[i].tm_cnth;

  // TODO: Restore SIO registers too?

  // Register restore
  memory_copy32(spill_ptr->cpu_regs, save_ptr->regs.cpu_regs, sizeof(save_ptr->regs.cpu_regs) / 4);
  spill_ptr->cpsr = save_ptr->regs.cpsr;

  memory_copy32(spill_ptr->irq_regs, save_ptr->regs.irq_regs, sizeof(save_ptr->regs.irq_regs) / 4);
  memory_copy32(spill_ptr->fiq_regs, save_ptr->regs.fiq_regs, sizeof(save_ptr->regs.fiq_regs) / 4);
  memory_copy32(spill_ptr->sup_regs, save_ptr->regs.sup_regs, sizeof(save_ptr->regs.sup_regs) / 4);
  memory_copy32(spill_ptr->abt_regs, save_ptr->regs.abt_regs, sizeof(save_ptr->regs.abt_regs) / 4);
  memory_copy32(spill_ptr->und_regs, save_ptr->regs.und_regs, sizeof(save_ptr->regs.und_regs) / 4);

  return true;
}


bool read_rom_buffer(FIL *fd, void *buffer, unsigned size, void *tmpbuf) {
  // Similar to write_rom_buffer, but just in the other direction.
  uint8_t* ptr = (uint8_t*)buffer;
  for (unsigned off = 0; off < size; off += 1024) {
    set_supercard_mode(MAPPED_SDRAM, true, true);   // So we can read from the SD card

    UINT rdbytes;
    if (FR_OK != f_read(fd, tmpbuf, 1024, &rdbytes) || rdbytes != 1024)
      return false;

    set_supercard_mode(MAPPED_SDRAM, true, false);   // Ensure we can write spill area.
    memory_copy32((uint32_t*)&ptr[off], (uint32_t*)tmpbuf, 1024 / 4);
  }

  set_supercard_mode(MAPPED_SDRAM, true, true);   // So we can read from the SD card
  return true;
}


bool readfd_mem_snapshot(FIL *fd) {

  t_spilled_region *spill_ptr = (t_spilled_region*)spill_addr;

  union {
    t_savestate_header header;
    t_savestate_regs regs;
    t_iomap iomap;
    uint8_t buf[1024];
  } tmp;
  _Static_assert(sizeof(tmp.header) == 512, "The header structure is 512 bytes in size");
  _Static_assert(sizeof(tmp.regs) == 512, "The regs structure is 512 bytes in size");
  _Static_assert(sizeof(tmp.iomap) == 1024, "The I/O structure is 1024 bytes in size");
  UINT rdbytes;

  if (FR_OK != f_read(fd, &tmp.header, sizeof(tmp.header), &rdbytes) || rdbytes != sizeof(tmp.header))
    return false;

  if (tmp.header.signature[0] != SIGNATURE_A ||
      tmp.header.signature[1] != SIGNATURE_B ||
      tmp.header.signature[2] != SIGNATURE_C)
    return false;

  if (tmp.header.version != SAVESTATE_VERSION)
    return false;

  if (FR_OK != f_read(fd, &tmp.regs, sizeof(tmp.regs), &rdbytes) || rdbytes != sizeof(tmp.regs))
    return false;

  set_supercard_mode(MAPPED_SDRAM, true, false);   // Ensure we can write spill area.
  spill_ptr->cpsr = tmp.regs.cpsr;
  memory_copy32(spill_ptr->cpu_regs, tmp.regs.cpu_regs, sizeof(tmp.regs.cpu_regs) / 4);
  memory_copy32(spill_ptr->irq_regs, tmp.regs.irq_regs, sizeof(tmp.regs.irq_regs) / 4);
  memory_copy32(spill_ptr->fiq_regs, tmp.regs.fiq_regs, sizeof(tmp.regs.fiq_regs) / 4);
  memory_copy32(spill_ptr->sup_regs, tmp.regs.sup_regs, sizeof(tmp.regs.sup_regs) / 4);
  memory_copy32(spill_ptr->abt_regs, tmp.regs.abt_regs, sizeof(tmp.regs.abt_regs) / 4);
  memory_copy32(spill_ptr->und_regs, tmp.regs.und_regs, sizeof(tmp.regs.und_regs) / 4);
  set_supercard_mode(MAPPED_SDRAM, true, true);   // So we can read from the SD card

  if (FR_OK != f_read(fd, &tmp.iomap, sizeof(tmp.iomap), &rdbytes) || rdbytes != sizeof(tmp.iomap))
    return false;

  set_supercard_mode(MAPPED_SDRAM, true, false);   // Ensure we can write spill area.
  spill_ptr->dispcnt  = tmp.iomap.dispcnt;
  spill_ptr->dispstat = tmp.iomap.dispstat;
  spill_ptr->bldcnt   = tmp.iomap.bldcnt;
  spill_ptr->bldalpha = tmp.iomap.bldalpha;
  spill_ptr->soundcnt = tmp.iomap.soundcnt;
  for (unsigned i = 0; i < 4; i++) {
    spill_ptr->tm_cnt[i]  = tmp.iomap.tms[i].tm_cntl;
    spill_ptr->dma_cnt[i] = tmp.iomap.dma[i].ctrl;
    spill_ptr->bg_cnt[i]  = tmp.iomap.bg_cnt[i];
  }

  t_iomap *curr_ro_io = (t_iomap*)0x04000000;
  curr_ro_io->winin  = tmp.iomap.winin;            // LCD registers (the rest are write only!)
  curr_ro_io->winout = tmp.iomap.winout;
  curr_ro_io->sound1cnt   = tmp.iomap.sound1cnt;   // Sound registers
  curr_ro_io->sound1cnt_x = tmp.iomap.sound1cnt_x;
  curr_ro_io->sound2cnt_l = tmp.iomap.sound2cnt_l;
  curr_ro_io->sound3cnt   = tmp.iomap.sound3cnt;
  curr_ro_io->sound3cnt_x = tmp.iomap.sound3cnt_x;
  curr_ro_io->sound4cnt_l = tmp.iomap.sound4cnt_l;
  curr_ro_io->soundcnt_x  = tmp.iomap.soundcnt_x;
  curr_ro_io->keycnt  = tmp.iomap.keycnt;          // Input regs
  curr_ro_io->reg_ie  = tmp.iomap.reg_ie;          // IRQ regs
  curr_ro_io->master_ie  = tmp.iomap.master_ie;

  for (unsigned i = 0; i < 4; i++)                 // Timers
    curr_ro_io->tms[i].tm_cnth  = tmp.iomap.tms[i].tm_cnth;

  if (!read_rom_buffer(fd, spill_ptr->palette, sizeof(spill_ptr->palette), tmp.buf))
    return false;

  // Use aux function for OAM/VRAM since they don't take byte writes nicely.
  uint8_t *OARAM_BUF = (uint8_t*)0x07000000;
  if (!read_rom_buffer(fd, OARAM_BUF, 1024, tmp.buf))
    return false;

  // VRAM, spilled, then actual data
  uint8_t *VRAM_BUF = (uint8_t*)0x06000000;
  const unsigned highsize = 96*1024 - sizeof(spill_ptr->low_vram);
  if (!read_rom_buffer(fd, spill_ptr->low_vram, sizeof(spill_ptr->low_vram), tmp.buf))
    return false;
  if (!read_rom_buffer(fd, &VRAM_BUF[sizeof(spill_ptr->low_vram)], highsize, tmp.buf))
    return false;

  // Same for IWRAM and EWRAM
  uint8_t *IWRAM_BUF = (uint8_t*)0x03000000;
  const unsigned highsize2 = 32*1024 - sizeof(spill_ptr->low_iwram);
  if (!read_rom_buffer(fd, spill_ptr->low_iwram, sizeof(spill_ptr->low_iwram), tmp.buf))
    return false;
  if (FR_OK != f_read(fd, &IWRAM_BUF[sizeof(spill_ptr->low_iwram)], highsize2, &rdbytes) || rdbytes != highsize2)
    return false;

  uint8_t *EWRAM_BUF = (uint8_t*)0x02000000;
  const unsigned highsize3 = 256*1024 - sizeof(spill_ptr->low_ewram);
  if (!read_rom_buffer(fd, spill_ptr->low_ewram, sizeof(spill_ptr->low_ewram), tmp.buf))
    return false;
  if (FR_OK != f_read(fd, &EWRAM_BUF[sizeof(spill_ptr->low_ewram)], highsize3, &rdbytes) || rdbytes != highsize3)
    return false;

  return true;
}

static void draw_hline(uint8_t *fb, unsigned x, unsigned y, unsigned w, uint16_t col) {
  memory_set16((uint16_t*)&fb[x + y * SCREEN_WIDTH], dup8(col), w / 2);
  memory_set16((uint16_t*)&fb[x + (y+1) * SCREEN_WIDTH], dup8(col), w / 2);
}
static void draw_vline(uint8_t *fb, unsigned x, unsigned y, unsigned h, uint16_t col) {
  for (unsigned i = 0; i < h; i++)
    *(uint16_t*)&fb[x + (y + i) * SCREEN_WIDTH] = dup8(col);
}

void draw_text(const char *t, uint8_t *fb, unsigned x, unsigned y, unsigned color) {
  uint8_t *basept = (uint8_t*)&fb[y * SCREEN_WIDTH + x];
  draw_text_idx8_bus16(t, basept, SCREEN_WIDTH, color);
}

static void draw_text_ovf(const char *t, volatile uint8_t *frame, unsigned x, unsigned y, unsigned maxw, unsigned color) {
  uint8_t *basept = (uint8_t*)&frame[y * SCREEN_WIDTH + x];
  unsigned twidth = font_width(t);
  if (twidth <= maxw)
    draw_text_idx8_bus16(t, basept, SCREEN_WIDTH, color);
  else {
    char tmpbuf[256];
    unsigned numchars = font_width_cap(t, maxw - THREEDOTS_WIDTH);
    memcpy(tmpbuf, t, numchars);
    memcpy(&tmpbuf[numchars], "...", 4);
    draw_text_idx8_bus16(tmpbuf, basept, SCREEN_WIDTH, color);
  }
}

static void draw_text_ovf_rotate(const char *t, volatile uint8_t *frame, unsigned x, unsigned y, unsigned maxw, unsigned color) {
  uint8_t *basept = (uint8_t*)&frame[y * SCREEN_WIDTH + x];
  unsigned twidth = font_width(t);
  if (twidth <= maxw)
    draw_text_idx8_bus16(t, basept, SCREEN_WIDTH, color);
  else {
    unsigned anim = franim > ANIM_INITIAL_WAIT ? (franim - ANIM_INITIAL_WAIT) >> 4 : 0;

    // Wrap around once the text end reaches the mid point aprox.
    char tmpbuf[540];
    strcpy(tmpbuf, t);
    strcat(tmpbuf, "      ");
    unsigned pixw = font_width(tmpbuf);
    if (anim > pixw)
      franim = ANIM_INITIAL_WAIT + ((anim - pixw) << 4);
    strcat(tmpbuf, t);

    draw_text_idx8_bus16_range(tmpbuf, basept, anim, maxw, SCREEN_WIDTH, color);
  }
}

void draw_text_center(const char *t, uint8_t *fb, unsigned x, unsigned y, unsigned color) {
  unsigned tw = font_width(t);
  unsigned cx = x - tw / 2;
  uint8_t *basept = (uint8_t*)&fb[y * SCREEN_WIDTH + cx];
  draw_text_idx8_bus16(t, basept, SCREEN_WIDTH, color);
}

void draw_popup(uint8_t *fb) {
  unsigned topy = popup.callback ? SCREEN_HEIGHT / 2 - 24 : SCREEN_HEIGHT / 2 - 16;
  unsigned boty = popup.callback ? SCREEN_HEIGHT / 2 + 24 : SCREEN_HEIGHT / 2 + 16;

  memory_set16((uint16_t*)&fb[SCREEN_WIDTH * topy], FG_COLOR | (FG_COLOR << 8), SCREEN_WIDTH * (boty - topy) / 2);
  draw_hline(fb, 0, topy, SCREEN_WIDTH, HI_COLOR);
  draw_hline(fb, 0, boty - 2, SCREEN_WIDTH, HI_COLOR);

  draw_text_center(popup.msg, fb, SCREEN_WIDTH/2, topy + 8, HI_COLOR);
  if (popup.callback) {
    draw_text_center(msgs[ingame_menu_lang][IMENU_QC1_YES], fb, SCREEN_WIDTH/3,   topy + 24, HI_COLOR);
    draw_text_center(msgs[ingame_menu_lang][IMENU_QC0_NO],  fb, SCREEN_WIDTH*2/3, topy + 24, HI_COLOR);
    unsigned cx = SCREEN_WIDTH / 3 * (2 - popup.opt) - font_width(msgs[ingame_menu_lang][IMENU_QC0_NO + popup.opt]) / 2;
    draw_text("⯈", fb, cx - 10, topy + 24, HI_COLOR);
  }
}

void draw_main_menu(uint8_t *fb, unsigned framen) {
  bool havess = num_mem_savestates || num_dsk_savestates;
  draw_text(msgs[ingame_menu_lang][IMENU_MAIN0_BACK_GAME],  fb, 30, 36 + 19*0, copt == 0 ? HI_COLOR : FG_COLOR);
  draw_text(msgs[ingame_menu_lang][IMENU_MAIN1_RESET],      fb, 30, 36 + 19*1, copt == 1 ? HI_COLOR : FG_COLOR);
  draw_text(msgs[ingame_menu_lang][IMENU_MAIN2_FLUSH_SAVE], fb, 30, 36 + 19*2, !savefile_pattern[0] ? SH_COLOR : (copt == 2 ? HI_COLOR : FG_COLOR));
  draw_text(msgs[ingame_menu_lang][IMENU_MAIN3_SSTATE],     fb, 30, 36 + 19*3, !havess ? SH_COLOR : (copt == 3 ? HI_COLOR : FG_COLOR));
  draw_text(msgs[ingame_menu_lang][IMENU_MAIN4_RTC],        fb, 30, 36 + 19*4, !has_rtc_support ? SH_COLOR : (copt == 4 ? HI_COLOR : FG_COLOR));
  draw_text(msgs[ingame_menu_lang][IMENU_MAIN5_CHEATS],     fb, 30, 36 + 19*5, !cheat_base_addr ? SH_COLOR : (copt == 5 ? HI_COLOR : FG_COLOR));

  draw_text("⯈", fb, 11, 36 + 19*copt, HI_COLOR);
}

void draw_reset_menu(uint8_t *fb, unsigned framen) {
  for (unsigned i = 0; i <= IMENU_RST2_DEVSKIP - IMENU_RST0_GAME; i++)
    draw_text(msgs[ingame_menu_lang][IMENU_RST0_GAME + i],  fb, 30, 36 + 19*i, copt == i ? HI_COLOR : FG_COLOR);
  draw_text(msgs[ingame_menu_lang][IMENU_GOBACK], fb, 30, 95, copt == 3 ? HI_COLOR : FG_COLOR);

  draw_text("⯈", fb, 11, 36 + 19*copt, HI_COLOR);
}

void draw_save_menu(uint8_t *fb, unsigned framen) {
  for (unsigned i = 0; i <= IMENU_SAVE2_RST - IMENU_SAVE0_OW; i++)
    draw_text(msgs[ingame_menu_lang][i + IMENU_SAVE0_OW],  fb, 30, 36 + 19*i, copt == i ? HI_COLOR : FG_COLOR);
  draw_text(msgs[ingame_menu_lang][IMENU_GOBACK], fb, 30, 96, copt == 3 ? HI_COLOR : FG_COLOR);

  draw_text("⯈", fb, 11, 36 + 19*copt, HI_COLOR);
}

void draw_rtc_menu(uint8_t *fb, unsigned framen) {
  unsigned hour = rtc_values[1];
  unsigned mins = rtc_values[2];
  unsigned days = rtc_values[3] + 1;
  unsigned mont = rtc_values[4] + 1;
  unsigned year = rtc_values[5];

  char thour[3] = {'0' + hour/10, '0' + hour%10, 0};
  char tmins[3] = {'0' + mins/10, '0' + mins%10, 0};
  char tdays[3] = {'0' + days/10, '0' + days%10, 0};
  char tmont[3] = {'0' + mont/10, '0' + mont%10, 0};
  char tyear[5] = {'2', '0', '0' + year/10, '0' + year%10, 0};

  draw_text(thour, fb,  40, 70, copt == 0 ? HI_COLOR : FG_COLOR);
  draw_text(":",   fb,  60, 70, FG_COLOR);
  draw_text(tmins, fb,  68, 70, copt == 1 ? HI_COLOR : FG_COLOR);
  draw_text(tdays, fb, 110, 70, copt == 2 ? HI_COLOR : FG_COLOR);
  draw_text("-",   fb, 130, 70, FG_COLOR);
  draw_text(tmont, fb, 140, 70, copt == 3 ? HI_COLOR : FG_COLOR);
  draw_text("-",   fb, 160, 70, FG_COLOR);
  draw_text(tyear, fb, 170, 70, copt == 4 ? HI_COLOR : FG_COLOR);

  draw_text_center(msgs[ingame_menu_lang][IMENU_UPDAT_RTC], fb, SCREEN_WIDTH/2, 120, copt == 5 ? HI_COLOR : FG_COLOR);
}

void draw_cheats_menu(uint8_t *fb, unsigned framen) {
  unsigned num_cheats = *(uint32_t*)cheat_base_addr;
  unsigned soff = (copt <= 2 || num_cheats <= 5)  ? 0 :
                  (copt >= num_cheats - 2)        ? num_cheats - 5 :
                   copt - 2;

  unsigned off = 4, numdisp = 0;
  for (unsigned i = 0; i < num_cheats && numdisp < 5; i++) {
    t_cheathdr *e = (t_cheathdr*)(((uint8_t*)cheat_base_addr) + off);
    off += sizeof(t_cheathdr) + e->slen + e->codelen;

    if (i >= soff) {
      draw_text(e->enabled ? "☑" : "☐", fb, 9, 40 + 20 *numdisp, copt == i ? HI_COLOR : FG_COLOR);
      if (copt == i)
        draw_text_ovf_rotate((char*)e->data, fb, 24, 40 + 20 *numdisp, 210, HI_COLOR);
      else
        draw_text_ovf((char*)e->data, fb, 24, 40 + 20 *numdisp, 210, FG_COLOR);
      numdisp++;
    }
  }
}

// Walks over the active cheats, produces a cheat table and updates
// the IRQ hook accordingly.
bool update_cheat_table() {
  unsigned num_cheats = *(uint32_t*)cheat_base_addr;
  unsigned off = 4, numenabled = 0;
  uint32_t *tptr = get_cheat_table();

  for (unsigned i = 0; i < num_cheats && numenabled < 63; i++) {
    t_cheathdr *e = (t_cheathdr*)(((uint8_t*)cheat_base_addr) + off);
    off += sizeof(t_cheathdr) + e->slen + e->codelen;
    if (e->enabled) {
      *tptr++ = (uintptr_t)&e->data[e->slen];
      numenabled++;
    }
  }
  *tptr = 0;    // End of list marker

  return numenabled > 0;
}

static void draw_icon(uint8_t *fb, unsigned iconn, unsigned x, unsigned y) {
  for (unsigned i = 0 ; i < 16; i++)
    memory_copy16((uint16_t*)&fb[x + (y + i) * SCREEN_WIDTH], (uint16_t*)&menu_icons[iconn][i][0], 8);
}

void draw_states_menu(uint8_t *fb, unsigned framen) {
  char tmp[32];
  int max_state = makepers >= 0 ? 0 : num_mem_savestates;

  for (int o = -2; o <= 2; o++) {
    int sln = state_slot + o;
    if (sln >= max_state || sln < -num_dsk_savestates)
      continue;

    unsigned xpoint = SCREEN_WIDTH / 2 + (o * 40) - 8;
    draw_hline(fb, xpoint - 6, 58, 28, o ? FG_COLOR : HI_COLOR);
    draw_hline(fb, xpoint - 6, 84, 28, o ? FG_COLOR : HI_COLOR);
    draw_vline(fb, xpoint - 7, 58, 28, o ? FG_COLOR : HI_COLOR);
    draw_vline(fb, xpoint +22, 58, 28, o ? FG_COLOR : HI_COLOR);
    unsigned iconn = sln >= 0 ? (memslot_valid[sln] ? MEM_ICON : MEM_ICON_DISABLED) :
                                (diskslot_valid[-sln - 1] ? DISK_ICON : DISK_ICON_DISABLED);

    draw_icon(fb, iconn, xpoint, 64);
  }
  if (state_slot < max_state - 3)
    draw_text("⯈", fb, SCREEN_WIDTH - 20, 64, FG_COLOR);
  if (state_slot >= -num_dsk_savestates + 3)
    draw_text("⯇", fb, 12, 64, FG_COLOR);

  if (state_slot < 0) {
    npf_snprintf(tmp, sizeof(tmp), msgs[ingame_menu_lang][IMENU_SSTATE_PN], -state_slot);
    draw_text_center(tmp,  fb, SCREEN_WIDTH / 2, 34, FG_COLOR);

    if (makepers >= 0) {
      copt = copt & 1;
      draw_text_center(msgs[ingame_menu_lang][IMENU_MAKEPER], fb, SCREEN_WIDTH / 2, 95 + 18*0, copt == 0 ? HI_COLOR : FG_COLOR);
      draw_text_center(msgs[ingame_menu_lang][IMENU_CANCEL],  fb, SCREEN_WIDTH / 2, 95 + 18*1, copt == 1 ? HI_COLOR : FG_COLOR);
    } else {
      draw_text_center(msgs[ingame_menu_lang][IMENU_SSTATEP0_SAVE],  fb, SCREEN_WIDTH / 2, 95 + 18*0, copt == 0 ? HI_COLOR : FG_COLOR);
      draw_text_center(msgs[ingame_menu_lang][IMENU_SSTATEP1_LOAD],  fb, SCREEN_WIDTH / 2, 95 + 18*1,
                       (copt == 1 ? HI_COLOR : (diskslot_valid[-state_slot - 1] ? FG_COLOR : SH_COLOR)));
      draw_text_center(msgs[ingame_menu_lang][IMENU_SSTATEP2_DEL],   fb, SCREEN_WIDTH / 2, 95 + 18*2,
                       (copt == 2 ? HI_COLOR : (diskslot_valid[-state_slot - 1] ? FG_COLOR : SH_COLOR)));
    }
  } else {
    npf_snprintf(tmp, sizeof(tmp), msgs[ingame_menu_lang][IMENU_SSTATE_QN], state_slot + 1);
    draw_text_center(tmp,  fb, SCREEN_WIDTH / 2, 34, FG_COLOR);

    draw_text_center(msgs[ingame_menu_lang][IMENU_SSTATEQ0_SAVE],  fb, SCREEN_WIDTH / 2, 95 + 18*0, copt == 0 ? HI_COLOR : FG_COLOR);
    draw_text_center(msgs[ingame_menu_lang][IMENU_SSTATEQ1_LOAD],  fb, SCREEN_WIDTH / 2, 95 + 18*1,
                     (copt == 1 ? HI_COLOR : (memslot_valid[state_slot] ? FG_COLOR : SH_COLOR)));
    draw_text_center(msgs[ingame_menu_lang][IMENU_SSTATEQ2_WRITE], fb, SCREEN_WIDTH / 2, 95 + 18*2,
                     (copt == 2 ? HI_COLOR : (memslot_valid[state_slot] ? FG_COLOR : SH_COLOR)));
  }
}


void rtc_fix() {
  // Correct any out of range values
  rtc_values[1] %= 24;    // Hour
  rtc_values[2] %= 60;    // Min
  rtc_values[3] %= 31;    // Day
  rtc_values[4] %= 12;    // Month
  rtc_values[5] %= 100;   // Year
}

enum { MenuMain = 0, MenuReset = 1, MenuSave = 2, MenuSState = 3, MenuRTC = 4, MenuCheats = 5 };
typedef void(*menu_draw_fn)(uint8_t *fb, unsigned framen);
typedef void(*menu_key_fn)(uint16_t keyp);
typedef bool(*menu_action_fn)();
typedef unsigned (*menu_getoptcnt_fn)();

bool action_resume_game() {
  return true;
}

bool action_reset_game() {
  reset_game();
  return false;
}
bool action_reset_fw() {
  reset_fw();
  return false;
}
bool action_reset_fw_nosave() {
  // Skip saving on reboot!
  program_sram_dump(NULL, 0);
  // Go ahead and reboot to flash.
  reset_fw();
  return false;
}

bool action_save_menu() {
  // If no file saving pattern has been filled it, saving is disabled
  if (!savefile_pattern[0])
    return false;

  submenu = MenuSave;
  copt = 0;
  return false;
}

bool action_sstate_menu() {
  bool havess = num_mem_savestates || num_dsk_savestates;
  if (havess) {
    if (num_dsk_savestates && !diskst_init) {
      // Check if the files actually exist
      for (unsigned i = 0; i < num_dsk_savestates; i++) {
        char tmp[256];
        npf_snprintf(tmp, sizeof(tmp), "%s.%d.state", savestate_pattern, i + 1);
        diskslot_valid[i] = check_file_exists(tmp);
      }
      diskst_init = true;
    }

    makepers = -1;
    submenu = MenuSState;
    copt = 0;
  }
  return false;
}

bool action_reset_menu() {
  submenu = MenuReset;
  copt = 0;
  return false;
}

bool action_cheats_menu() {
  if (cheat_base_addr) {
    submenu = MenuCheats;
    copt = 0;
  }
  return false;
}

bool action_rtc_menu() {
  if (has_rtc_support) {
    submenu = MenuRTC;
    copt = 0;
  }
  return false;
}

void create_paths(const char *fn) {
  char dirp[256];
  file_dirname(fn, dirp);
  f_mkdir(dirp);
}

bool action_save_overw() {
  // Just overwrites the .sav file with our data.
  char finalfn[256];
  strcpy(finalfn, savefile_pattern);
  strcat(finalfn, ".sav");
  create_paths(finalfn);     // Just in case it doesn't exist.
  if (write_save_sram(finalfn))
    popup.msg = msgs[ingame_menu_lang][IMENU_MSG_SAVEC];
  else
    popup.msg = msgs[ingame_menu_lang][IMENU_MSG_SAVEERR];
  submenu = MenuMain;
  return false;
}

bool action_save_backup() {
  create_paths(savefile_pattern);     // Just in case it doesn't exist.

  // Write save generating a backup, honoring the backup_count.
  unsigned bc = MAX(savefile_backups, 1);
  if (write_save_sram_rotate(savefile_pattern, bc))
    popup.msg = msgs[ingame_menu_lang][IMENU_MSG_SAVEC];
  else
    popup.msg = msgs[ingame_menu_lang][IMENU_MSG_SAVEERR];

  return false;
}

bool action_save_reset() {
  // Write the .sav file
  action_save_overw();
  // Do not write any file on reboot (it's done already!)
  program_sram_dump(NULL, 0);
  // Go ahead and reboot to flash.
  reset_fw();
  return false;
}

bool cheat_active_action() {
  // We access the ROM-mapped data, need full access to the ROM.
  set_supercard_mode(MAPPED_SDRAM, true, false);

  unsigned off = 4;
  for (unsigned i = 0; i < copt; i++) {
    t_cheathdr *e = (t_cheathdr*)(((uint8_t*)cheat_base_addr) + off);
    off += sizeof(t_cheathdr) + e->slen + e->codelen;
  }

  t_cheathdr *e = (t_cheathdr*)(((uint8_t*)cheat_base_addr) + off);
  e->enabled ^= 1;

  return false;
}

bool action_menu_back() {
  submenu = MenuMain;
  copt = 0;
  return false;
}

void save_memstate() {
  set_supercard_mode(MAPPED_SDRAM, true, false);
  take_mem_snapshot(get_memslot_addr(state_slot));
  memslot_valid[state_slot] = 1;
  popup.msg = msgs[ingame_menu_lang][IMENU_WSAV_OK];
}

// Saves a disk state, capable of "cloning" an in-memory state.
void save_diskstate() {
  set_supercard_mode(MAPPED_SDRAM, true, true);

  FIL fd;
  char fn[256];
  npf_snprintf(fn, sizeof(fn), "%s.%d.state", savestate_pattern, -state_slot);
  create_paths(fn);
  if (FR_OK == f_open(&fd, fn, FA_WRITE | FA_CREATE_ALWAYS)) {
    bool success = (makepers >= 0) ? writefd_mem_snapshot_clone(&fd, get_memslot_addr(makepers), sizeof(t_savestate_snapshot))
                                   : writefd_mem_snapshot(&fd);
    if (success) {
      popup.msg = msgs[ingame_menu_lang][IMENU_WSTAF_OK];
      diskslot_valid[-state_slot - 1] = 1;
    } else {
      popup.msg = msgs[ingame_menu_lang][IMENU_WSTAF_ERR];
    }
    f_close(&fd);
  } else {
    popup.msg = msgs[ingame_menu_lang][IMENU_WSTAF_ERR];
  }

  if (makepers >= 0)
    state_slot = makepers;
  makepers = -1;
}

bool state_save() {
  if (makepers >= 0) {
    // Copy the memory slot into this persistent slot.
    if (diskslot_valid[-state_slot - 1]) {
      popup.msg = msgs[ingame_menu_lang][IMENU_ST_OVER];
      popup.callback = save_diskstate;
    }
    else
      save_diskstate();
  } else {
    if (state_slot >= 0) {
      if (memslot_valid[state_slot]) {
        popup.msg = msgs[ingame_menu_lang][IMENU_ST_OVER];
        popup.callback = save_memstate;
      }
      else
        save_memstate();
    } else {
      if (diskslot_valid[-state_slot - 1]) {
        popup.msg = msgs[ingame_menu_lang][IMENU_ST_OVER];
        popup.callback = save_diskstate;
      }
      else
        save_diskstate();
    }
  }
  return false;
}

bool state_load() {
  if (makepers >= 0) {
    state_slot = makepers;     // Switch back to the original slot.
    makepers = -1;
  } else {
    if (state_slot >= 0 && memslot_valid[state_slot]) {
      set_supercard_mode(MAPPED_SDRAM, true, false);
      bool success = load_mem_snapshot(get_memslot_addr(state_slot));
      popup.msg = msgs[ingame_menu_lang][success ? IMENU_QLD_OK : IMENU_QLD_ERR];
    }
    else if (state_slot < 0 && diskslot_valid[-state_slot - 1]) {
      FIL fd;
      char fn[256];
      npf_snprintf(fn, sizeof(fn), "%s.%d.state", savestate_pattern, -state_slot);
      if (FR_OK == f_open(&fd, fn, FA_READ)) {
        bool success = readfd_mem_snapshot(&fd);
        popup.msg = msgs[ingame_menu_lang][success ? IMENU_QLD_OK : IMENU_PLD_ERR];
      }
      else
        popup.msg = msgs[ingame_menu_lang][IMENU_WSTAR_ERR];
    }
  }
  return false;
}

void del_diskstate() {
  set_supercard_mode(MAPPED_SDRAM, true, true);
  char tmp[256];
  npf_snprintf(tmp, sizeof(tmp), "%s.%d.state", savestate_pattern, -state_slot - 1);
  f_unlink(tmp);
  diskslot_valid[-state_slot - 1] = 0;
}

// Deletes persistent slots or converts a slot into persistent.
bool state_special() {
  if (makepers < 0) {
    if (state_slot >= 0) {
      if (memslot_valid[state_slot])
        makepers = state_slot;
    } else {
      if (diskslot_valid[-state_slot - 1]) {
        popup.msg = msgs[ingame_menu_lang][IMENU_ST_DEL];
        popup.callback = del_diskstate;
      }
    }
  }
  return false;
}

void sstkey(uint16_t keyp) {
  if (keyp & KEY_BUTTLEFT)
    state_slot--;
  if (keyp & KEY_BUTTRIGHT)
    state_slot++;
  if (keyp & KEY_BUTTL)
    state_slot -= 5;
  if (keyp & KEY_BUTTR)
    state_slot += 5;

  int max_state = (makepers >= 0) ? 0 : num_mem_savestates;

  if (state_slot >= max_state)
    state_slot = max_state - 1;
  else if (state_slot < -num_dsk_savestates)
    state_slot = -num_dsk_savestates;
}

void rtckey(uint16_t keyp) {
  static const uint8_t rtc_decv[] = { 23, 59, 30, 11, 99 };

  if (copt < 5) {
    if (keyp & KEY_BUTTUP)
      rtc_values[copt + 1]++;
    if (keyp & KEY_BUTTDOWN)
      rtc_values[copt + 1] += rtc_decv[copt];

    rtc_fix();
  }
}

bool action_write_rtc() {
  // Write to the GPIO buffer, assuming R/W mode!
  *MEM_ROM_U16(0xC4) = rtc_values[0] | (rtc_values[1] << 8);
  *MEM_ROM_U16(0xC6) = rtc_values[2] | (rtc_values[3] << 8);
  *MEM_ROM_U16(0xC8) = rtc_values[4] | (rtc_values[5] << 8);
  submenu = MenuMain;
  copt = 0;

  popup.msg = msgs[ingame_menu_lang][IMENU_MSG_RTCWR];

  return false;
}

const menu_action_fn mainacts[] = {
  action_resume_game,
  action_reset_menu,
  action_save_menu,
  action_sstate_menu,
  action_rtc_menu,
  action_cheats_menu,
};
const menu_action_fn resetacts[] = {
  action_reset_game,
  action_reset_fw,
  action_reset_fw_nosave,
  action_menu_back,
};
const menu_action_fn saveacts[] = {
  action_save_overw,
  action_save_backup,
  action_save_reset,
  action_menu_back,
};
const menu_action_fn statesacts[] = {
  state_save,
  state_load,
  state_special,
};
const menu_action_fn rtcacts[] = {
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  action_write_rtc,  // Apply RTC time.
};

const menu_action_fn cheatsacts[] = {
  cheat_active_action,
};

unsigned cheats_cnt() {
  return *(uint32_t*)cheat_base_addr;
}

typedef struct {
  const menu_draw_fn draw_fn;          // Draw function
  const menu_action_fn *actions;       // Action callback for A button
  const menu_key_fn key_fn;            // Key press function
  const unsigned opt_count;            // Total option count
  const menu_getoptcnt_fn optcnt_cb;   // Total option count (callback)
  bool vertical;                       // Menu can be vert/hor
} t_menu_def;

const t_menu_def menudata [] = {
  { draw_main_menu,   mainacts,   NULL,   6, NULL,  true },
  { draw_reset_menu,  resetacts,  NULL,   4, NULL,  true },
  { draw_save_menu,   saveacts,   NULL,   4, NULL,  true },
  { draw_states_menu, statesacts, sstkey, 3, NULL,  true },
  { draw_rtc_menu,    rtcacts,    rtckey, 6, NULL, false },
  { draw_cheats_menu, cheatsacts, NULL,   0, cheats_cnt,  true },
};

void setup_video_frame() {
  // Setup video mode
  REG_DISPCNT = 0x404;   // Mode 4, BG2 no OBJs
  REG_BGxCNT(2) = 0x80;  // 256 color mode
  REG_BLDCNT = 0;        // No effects
  REG_BGxHOFS(2) = 0;
  REG_BGxVOFS(2) = 0;

  REG_BG2PA = 0x100;
  REG_BG2PD = 0x100;
  REG_BG2PB = 0;
  REG_BG2PC = 0;
  REG_BG2X = 0;
  REG_BG2Y = 0;

  // Setup palettes
  memory_copy16((uint16_t*)&MEM_PALETTE[1], logo_pal, sizeof(logo_pal) >> 1);
  MEM_PALETTE[FG_COLOR] = ingame_menu_palette[0];
  MEM_PALETTE[BG_COLOR] = ingame_menu_palette[1];
  MEM_PALETTE[HI_COLOR] = ingame_menu_palette[2];
  MEM_PALETTE[SH_COLOR] = ingame_menu_palette[3];

  memory_copy16((uint16_t*)&MEM_PALETTE[ICON_PAL], menu_icons_pal, sizeof(menu_icons_pal) >> 1);
  MEM_PALETTE[ICON_PAL] = MEM_PALETTE[BG_COLOR]; // Transparent color to BG color

  // Clear two frames with BG color
  fast_mem_clr_256((uint16_t*)MEM_VRAM_U8, dup16(dup8(BG_COLOR)), SCREEN_WIDTH * SCREEN_HEIGHT * 2);
}

void ingame_menu_blocked(uint32_t *use_cheats_hook) {
  setup_video_frame();

  // Display frame 0 (empty)
  REG_DISPCNT = (REG_DISPCNT & ~0x10);

  uint8_t *fb = (uint8_t*)&MEM_VRAM_U8[0xA000];
  fast_mem_clr_256((uint16_t*)fb, dup16(dup8(BG_COLOR)), SCREEN_WIDTH * SCREEN_HEIGHT);
  render_logo((uint16_t*)fb, SCREEN_WIDTH / 2, 20, 2);

  // Render the "no-save" icon in the corner, if the menu is rendered during a save operation.
  const unsigned SAVE_ICON_X = (SCREEN_WIDTH - 64) / 2;
  const unsigned SAVE_ICON_Y = (SCREEN_HEIGHT - 64) / 2;

  for (unsigned i = 0; i < 16; i++) {
    for (unsigned j = 0; j < 16; j++) {
      uint16_t val = menu_icons[SAVE_ICON][i][j];
      for (unsigned k = 0; k < 4; k++)
        for (unsigned l = 0; l < 4; l++)
          *(uint16_t*)&fb[SAVE_ICON_X + SCREEN_WIDTH * (i*4 + l + SAVE_ICON_Y) + (j*4) + k] = dup8(val);
    }
  }

  // Show an user message
  draw_text_center(msgs[ingame_menu_lang][IMENU_SAVING_BLOCKED], fb, SCREEN_WIDTH / 2, SCREEN_HEIGHT - 32, HI_COLOR);

  // Wait for VBlank (with some leeway)
  while ((REG_VCOUNT & ~7) != 160);
  // Display frame 1, just rendered
  REG_DISPCNT = (REG_DISPCNT | 0x10);

  // Wait for the user to press any button
  uint16_t pk = 0xFFFF;
  while (1) {
    uint16_t k = ~REG_KEYINPUT;
    uint16_t pressed = k & ~pk;
    pk = k;

    // Wait for VBlank (with some leeway)
    while ((REG_VCOUNT & ~7) != 160);

    if (pressed & (KEY_BUTTA | KEY_BUTTB))
      break;
  }
}


void ingame_menu_loop(uint32_t *use_cheats_hook) {
  setup_video_frame();

  unsigned framen = 0;

  num_mem_savestates = MIN(MAX_MEM_SLOTS, (scratch_size >> 10) / SAVESTATE_SIZE_KB);
  num_dsk_savestates = savestate_pattern[0] ? MAX_DISK_SLOTS : 0;

  // Read RTC values
  memcpy(rtc_values, (uint8_t*)&MEM_ROM_U8[0xC4], sizeof(rtc_values));

  // Lazy intialization of the SD card, to avoid blocking the menu
  FATFS fs;
  f_mount(&fs, "0:", 0);   // Does not actually mount stuff nor access the card

  uint16_t pk = 0xFFFF;    // Ensure we capture keys completely (avoid bouncing).
  copt = 0;
  submenu = 0;
  state_slot = num_mem_savestates ? 0 : -1;
  memset(&popup, 0, sizeof(popup));

  while (1) {
    // Render a new frame into the back-buffer
    framen ^= 1;
    // Clear the screen & draw logo at the top
    uint8_t *fb = (uint8_t*)&MEM_VRAM_U8[0xA000 * framen];
    fast_mem_clr_256((uint16_t*)fb, dup16(dup8(BG_COLOR)), SCREEN_WIDTH * SCREEN_HEIGHT);
    render_logo((uint16_t*)fb, SCREEN_WIDTH / 2, 20, 2);

    // Render the current menu
    menudata[submenu].draw_fn(fb, framen);

    // Render Popup message if available
    if (popup.msg)
      draw_popup(fb);

    // Process input
    uint16_t k = ~REG_KEYINPUT;
    uint16_t pressed = k & ~pk;
    pk = k;
    if (popup.msg) {
      if (pressed & (KEY_BUTTA | KEY_BUTTB)) {
        void (*cb)() = popup.opt ? popup.callback : NULL;
        memset(&popup, 0, sizeof(popup));

        if ((pressed & KEY_BUTTA) && cb) {
          set_supercard_mode(MAPPED_SDRAM, true, true);
          cb();
          set_supercard_mode(MAPPED_SDRAM, true, false);
        }
      }
      else if (pressed & (KEY_BUTTLEFT | KEY_BUTTRIGHT))
        popup.opt ^= 1;
    } else {
      if (submenu != 0 && pressed & KEY_BUTTB) {
        submenu = 0;
        copt = 0;
      }
      else if (pressed & KEY_BUTTA) {
        unsigned cbnum = menudata[submenu].opt_count ? copt : 0;

        set_supercard_mode(MAPPED_SDRAM, true, true);
        bool retn = menudata[submenu].actions[cbnum]();
        set_supercard_mode(MAPPED_SDRAM, true, false);
        if (retn)
          break;
      }
      else {
        unsigned dec_but = menudata[submenu].vertical ? KEY_BUTTUP   : KEY_BUTTLEFT;
        unsigned inc_but = menudata[submenu].vertical ? KEY_BUTTDOWN : KEY_BUTTRIGHT;
        unsigned opcnt = menudata[submenu].opt_count ?: menudata[submenu].optcnt_cb();
        if (pressed & dec_but) {
          copt = (copt + opcnt - 1) % opcnt;
          franim = 0;
        }
        else if (pressed & inc_but) {
          copt = (copt + 1) % opcnt;
          franim = 0;
        }
        else
          franim += animspd_lut[menu_anim_speed] << 2;
      }

      // Process any input if necessary
      if (menudata[submenu].key_fn)
        menudata[submenu].key_fn(pressed);
    }

    // Wait for VBlank (with some leeway)
    while ((REG_VCOUNT & ~7) != 160);
    // Flip frame
    REG_DISPCNT = (REG_DISPCNT & ~0x10) | (framen << 4);
  }

  // Unmount the device, ensure everything is in order
  set_supercard_mode(MAPPED_SDRAM, true, true);
  f_unmount("0:");
  set_supercard_mode(MAPPED_SDRAM, true, false);

  if (cheat_base_addr)
    *use_cheats_hook = update_cheat_table();
}

