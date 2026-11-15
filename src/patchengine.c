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

// Patch engine
// This function tries to generate a patchset for a given ROM, finding:
//  - WAITCNT patch locations (for slow ROM/SDRAM)
//  - Save patches: Identify save type and patch SDK functions.
//  - IRQ handlers: Find IRQ installs and hook and redirect them.

// Heavily inspired on the patch tool at:
// https://github.com/davidgfnet/gba-patch-gen/blob/master/tools/save-finder.py
//
// Attempts to be as fast as possible by running on IWRAM and matching at
// word level, it still probably takes ~10-20 cycles per ROM byte.

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "compiler.h"
#include "common.h"
#include "util.h"
#include "fatfs/ff.h"
#include "patchengine.h"
#include "sighash.h"

#define WAITCNT_VALUE_EXACT  0x04000204
#define IRQHADDR_VALUE       0x03007FFC

// RTC word signatures
#define RTC_V_WORD0          0x52494953    // "SIIR"
#define RTC_V_WORD1          0x565f4354    // "TC_V"

// Savetype signatures (strings)
#define SRAM_V_WORD0         0x4D415253    // "SRAM"
#define SRAM_V_WORD1         0x3131565F    // "_V11"
#define SRAM_F_WORD1         0x565f465f    // "_F_V"
#define EEPROM_V_WORD0       0x52504545    // "EEPR"
#define EEPROM_V_WORD1       0x565F4D4F    // "OM_V"
#define FLASH_V_WORD0        0x53414C46    // "FLAS"
#define FLASH_V_WORD1        0x31565F48    // "H_V1"
#define FLASH512_WORD1       0x32313548    // "H512"
#define FLASH1M_WORD1        0x5F4D3148    // "H1M_"

#define GUESS_SRAM           (1 << 0)
#define GUESS_EEPROM         (1 << 1)
#define GUESS_FLASH          (1 << 2)
#define GUESS_FLASH64        (1 << 3)
#define GUESS_FLASH128       (1 << 4)

// Savetype function signatures.
#include "save_signatures.h"

#define THUMB_LDR_BACKOFF    256      // 8bit imm (scaled by 4 really)
#define ARM_LDR_BACKOFF     1024      // 12bit imm (not scaled)

#define OPC_WR_BUF      0x0
#define OPC_NOP_THUMB   0x1
#define OPC_NOP_ARM     0x2
#define OPC_COPY_BYTE   0x3
#define OPC_COPY_WORD   0x4
#define OPC_PATCH_FN    0x5
#define OPC_RTC_HD      0x7
#define OPC_EEPROM_HD   0x8
#define OPC_FLASH_HD    0x9

#define FUNC_RET0_THUMB   0

#define EEPROM_RD_HNDLR   0
#define EEPROM_WR_HNDLR   1

#define FLASH_READ_HNDLR  0
#define FLASH_CLRC_HNDLR  1
#define FLASH_CLRS_HNDLR  2
#define FLASH_WRTS_HNDLR  3
#define FLASH_WRBT_HNDLR  4
#define FLASH_IDEN_HNDLR  6   // Invalid patch, just a placeholder!
#define FLASH_VERF_HNDLR  7   // Invalid patch, just a placeholder!

#define RTC_PROBE_HNDLR   0
#define RTC_RESET_HNDLR   1
#define RTC_STSRD_HNDLR   2
#define RTC_GETTD_HNDLR   3

ARM_CODE IWRAM_CODE NOINLINE
static bool match_sig_prefix(const uint32_t *p, const uint16_t *sig, unsigned sigsize) {
  const uint16_t *p16 = (uint16_t*)p;
  for (unsigned i = 0; i < sigsize / 2; i++) {
    if (sig[i] && p16[i] != sig[i])
      return false;
  }
  return true;
}

// Start and target expressed in half-word offsets
static bool find_thumb_ldrpc(const uint16_t *rom, unsigned start, unsigned target) {
  for (unsigned i = start; i < target; i++) {
    unsigned opc = rom[i] >> 11;
    if (opc == 0x09) {
      // LDR rX, [pc + imm8*4]
      unsigned imm8 = rom[i] & 0xFF;
      unsigned tgtaddr = (i & ~1) + imm8 * 2 + 2;
      // Check for target (addr + off == target)
      if (tgtaddr == target)
        return true;
    }
  }
  return false;
}

// Start and target expressed in word offsets
static bool find_arm_ldrpc(const uint32_t *rom, unsigned start, unsigned target) {
  for (unsigned i = start; i < target; i++) {
    unsigned opc = (rom[i] >> 20) & 0xFF;
    unsigned rn  = (rom[i] >> 16) & 0x0F;
    if (opc == 0x59 && rn == 15) {
      // LDR rX, [pc + imm]
      unsigned imm12 = rom[i] & 0xFFF;
      if ((imm12 & 3) == 0) {    // Aligned load from pool
        unsigned tgtaddr = i + (imm12 >> 2) + 2;
        // Check for target (addr + off == target)
        if (tgtaddr == target)
          return true;
      }
    }
  }
  return false;
}

// Matches a string signature at a ROM position.
static inline bool match_str(const uint32_t *p, const char *str, unsigned len) {
  const uint8_t *p8 = (uint8_t*)p;
  for (unsigned i = 0; i < len; i++)
    if (p8[i] != (uint8_t)str[i])
      return false;
  return true;
}
#define MATCH_SIG(sig)  match_sig_prefix(&rom[i], sig, sizeof(sig))
#define MATCH_STR(str)  match_str(&rom[i], str, sizeof(str) - 1)

NOINLINE ARM_CODE
static void push_save_handler(t_patch *patch, unsigned savetype, unsigned hndltype, uint32_t addr) {
  memmove(&patch->op[patch->wcnt_ops+patch->save_ops+1],
          &patch->op[patch->wcnt_ops+patch->save_ops], (patch->irqh_ops + patch->rtc_ops) * 4);
  patch->op[patch->wcnt_ops + patch->save_ops++] = addr | (savetype << 28) | (hndltype << 25);
}

NOINLINE ARM_CODE
static void push_rtc_handler(t_patch *patch, unsigned hndltype, uint32_t addr) {
  patch->op[patch->wcnt_ops + patch->save_ops + patch->irqh_ops + patch->rtc_ops++] = addr | (OPC_RTC_HD << 28) | (hndltype << 25);
}

static inline bool isromaddr(uint32_t addr) {
  return (addr >> 24) == 8 || (addr >> 24) == 9;
}

static inline bool isromramaddr(uint32_t addr) {
  return (addr >> 24) == 8 || (addr >> 24) == 9 || (addr >> 24) == 2 || (addr >> 24) == 3;
}

static inline bool valid_flashid(uint16_t did) {
  static const uint16_t idtbl[] = {
    0x0000, 0x3D1F, 0xD4BF, 0x1B32,
    0x1CC2, 0x09C2, 0x1362 };
  for (unsigned i = 0; i < sizeof(idtbl) / sizeof(idtbl[0]); i++)
    if (idtbl[i] == did)
      return true;
  return false;
}

static inline bool isflash128k(uint16_t did) {
  return did == 0x09C2 || did == 0x1362;
}

// Flash info checks for structure recognition
// (Carefully chosen order to discard false positives faster)
static inline bool seems_flashinfo(const t_flash_setup_info_v1 *st) {
  return isromaddr(st->program_sector_fnptr) &&
         st->zero_pad2 == 0 && st->zero_pad1 == 0 &&
         (st->flash_size == 64*1024 || st->flash_size == 128*1024) &&
         (st->sector_size == 128 || st->sector_size == 4096) &&
         (st->shift_amount == 7 || st->shift_amount == 12) &&
         st->ws[0] < 4 && st->ws[1] < 4 &&
         isromaddr(st->erase_chip_fnptr) &&
         isromaddr(st->erase_sector_fnptr) &&
         isromaddr(st->wait_flash_write_fnptr) &&
         isromramaddr(st->timeout_lut_ptr);
}

static inline bool flashinfo_validsize(const t_flash_setup_info_v1 *st) {
  return st->flash_size == st->sector_count * st->sector_size;
}

static void filter_save_ops(t_patch *p, unsigned optype) {
  // Filter save ops to match the specified type.
  for (unsigned i = 0; i < p->save_ops; i++)
    if ((p->op[p->wcnt_ops + i] >> 28) != optype) {
      memmove(&p->op[p->wcnt_ops + i], &p->op[p->wcnt_ops + i + 1],
              (p->save_ops - 1 - i + p->irqh_ops + p->rtc_ops) * 4);
      p->save_ops--;
      i--;
    }
}

void patchengine_init(t_patch_builder *patchb, unsigned filesize) {
  memset(patchb, 0, sizeof(*patchb));

  patchb->filesize = filesize;

  // Since we mostly patch WAITCNT constants, generate a program that zeroes out a 32 bit word
  patchb->p.prgs[0].length = 4;
  // For IRQ writes, we overwrite with the redirected address.
  uint32_t handleraddr = 0x03007FF4;
  patchb->p.prgs[1].length = sizeof(handleraddr);
  memcpy(patchb->p.prgs[1].data, &handleraddr, sizeof(handleraddr));

  // Flash Ident Stub functions (64 and 128KB flavours)
  const uint16_t flash64_stub[] = {
    0x201c,  // movs r0, #0x1c     - (Macronix 64KB flash device)
    0x0200,  // lsls r0, r0, #8
    0x30c2,  // adds r0, #0xc2
    0x4770,  // bx lr
  };
  const uint16_t flash128_stub[] = {
    0x2009,  // movs r0, #0x09     - Macronix 128KB flash device
    0x0200,  // lsls r0, r0, #8
    0x30c2,  // adds r0, #0xc2
    0x4770,  // bx lr
  };
  patchb->p.prgs[2].length = sizeof(flash64_stub);
  memcpy(patchb->p.prgs[2].data, flash64_stub, sizeof(flash64_stub));
  patchb->p.prgs[3].length = sizeof(flash128_stub);
  memcpy(patchb->p.prgs[3].data, flash128_stub, sizeof(flash128_stub));
}

void patchengine_finalize(t_patch_builder *patchb) {
  t_patch *p = &patchb->p;
  if (patchb->save_type_guess == 0 && p->save_ops == 0)
    p->save_mode = SaveTypeNone;         // No saving strings nor signatures found!
  else if (patchb->save_type_guess == GUESS_SRAM) {
    p->save_mode = SaveTypeSRAM;
    filter_save_ops(p, 0xF);             // Clear all save opcodes!
  }
  else if (patchb->save_type_guess == GUESS_EEPROM) {
    p->save_mode = SaveTypeEEPROM64K;
    filter_save_ops(p, OPC_EEPROM_HD);   // Clear other opcodes but EEPROM
  }
  else if (patchb->save_type_guess == GUESS_FLASH ||
           patchb->save_type_guess == GUESS_FLASH64 ||
           patchb->save_type_guess == GUESS_FLASH128) {
    // Guess flash size.
    p->save_mode = (patchb->save_type_guess == GUESS_FLASH128) ? SaveTypeFlash1024K
                                                               : SaveTypeFlash512K;
    filter_save_ops(p, OPC_FLASH_HD);    // Filter all opcodes but FLASH
  }
  else if (p->save_ops == 0 && (patchb->save_type_guess & GUESS_SRAM)) {
    // Could not find any signatures, but SRAM looks promising.
    p->save_mode = SaveTypeSRAM;
  }
  else {
    // Multiple types, or not a clue, fallback to SRAM
    p->save_mode = SaveTypeSRAM;
    filter_save_ops(p, 0xF);             // Clear all save opcodes!
  }

  // Process any FLASH_IDEN_HNDLR handlers into program patches.
  // These stub out the ident function to return some hardcoded device ID.
  for (unsigned i = 0; i < p->save_ops; i++) {
    if ((p->op[p->wcnt_ops + i] >> 28) == OPC_FLASH_HD) {
      unsigned subop = ((p->op[p->wcnt_ops + i] >> 25) & 7);
      if (subop == FLASH_IDEN_HNDLR) {
        // Replace the ident function with one of the two possible stubs.
        unsigned num = (patchb->save_type_guess == GUESS_FLASH128) ? 3 : 2;
        p->op[p->wcnt_ops + i] = (p->op[p->wcnt_ops + i] & 0x1FFFFFF)
                                 | (OPC_WR_BUF << 28) | (num << 25);
      }
      else if (subop == FLASH_VERF_HNDLR) {
        // Patch function with return 0 (thumb)
        p->op[p->wcnt_ops + i] = (p->op[p->wcnt_ops + i] & 0x1FFFFFF)
                                 | (OPC_PATCH_FN << 28) | (FUNC_RET0_THUMB << 25);
      }
    }
  }

  // Clear all the ops at the end (if any)
  for (unsigned i = p->wcnt_ops + p->save_ops + p->irqh_ops + p->rtc_ops; i < MAX_PATCH_OPS; i++)
    p->op[i] = 0;

  // If the rom is big, annotate the trailing data.
  if (patchb->filesize >= MAX_ROM_SIZE_IGM && patchb->ldatacnt >= 4096) {
    // Calculate address and hole size, round up!
    uint32_t saddr = patchb->filesize - patchb->ldatacnt;
    uint32_t eaddr = patchb->filesize;

    uint32_t saddrr = ROUND_UP2(saddr, 1024);
    uint32_t eaddrr = eaddr & ~1023U;
    uint32_t size = eaddrr - saddrr;

    patchb->p.hole_addr = saddrr;
    patchb->p.hole_size = size;
  }
}

// Checks whether the word at rom[i] is loaded by some LDR rX, [PC + off] (Thumb or ARM).
ARM_CODE IWRAM_CODE NOINLINE
static bool ldr_loads_word(const uint32_t *rom, unsigned i) {
  unsigned start_pos_thumb = i < THUMB_LDR_BACKOFF ? 0 : i - THUMB_LDR_BACKOFF;
  unsigned start_pos_arm   = i < ARM_LDR_BACKOFF   ? 0 : i - ARM_LDR_BACKOFF;
  return find_thumb_ldrpc((const uint16_t*)rom, start_pos_thumb * 2, i * 2) ||
         find_arm_ldrpc(rom, start_pos_arm, i);
}

// Generates a patch set from a given ROM.
// Walks a ROM (or chunk of a ROM) and looks for certain constants to calculate
// WAITCNT, IRQ and save patches. Generate a patch structure out of it.
ARM_CODE IWRAM_CODE NOINLINE
bool patchengine_process_rom(const uint32_t *rom, unsigned romsize, t_patch_builder *patchb, void(*progresscb)(unsigned)) {
  const unsigned nwords = romsize / sizeof(uint32_t);
  t_patch *patch = &patchb->p;
  uint32_t ldata = 0, ldatacnt = 0;

  for (unsigned blk = 0; blk < nwords; blk += 32*1024) {
    unsigned blksize = MIN(nwords - blk, 32*1024);
    progresscb(blk);

    for (unsigned off = 0; off < blksize; off++) {
      const uint32_t i = blk + off;
      const uint32_t addr = patchb->rom_offset + i * 4;
      const uint32_t romword = rom[i];

      // Count the number of identical words
      if (ldata == romword)
        ldatacnt += 4;
      else {
        ldata = romword;
        ldatacnt = 0;
      }

      // Calculate the quick hash, lookup the table and see if we have the right word.
      uint32_t key;
      if (sighash_lookup(romword, &key) == romword) {
        switch (SIGSLOT(key)) {

        case WAITCNT_SIG:
          // Identify WAITCNT constants, validate them by finding LDR instructions
          if (ldr_loads_word(rom, i)) {
            // This constant seems to be used by an LDR rX, [PC + off], most likely
            // a WAITCNT update. We just patch the constant even tho it's not great
            memmove(&patch->op[patch->wcnt_ops+1], &patch->op[patch->wcnt_ops],
                    (patch->save_ops + patch->irqh_ops + patch->rtc_ops) * 4);
            patch->op[patch->wcnt_ops++] = addr | (OPC_WR_BUF << 28) | (0 << 25);
          }
          break;

        // Identify IRQ handle address, so we can find IRQ hook set.
        case IRQHADDR_SIG:
          if (ldr_loads_word(rom, i)) {
            // This constant seems to be used by an LDR rX, [PC + off], most likely
            // an IRQ handler write. We just patch the constant to point to the reserved area.
            patch->op[patch->wcnt_ops + patch->save_ops + patch->irqh_ops++] = addr | (OPC_WR_BUF << 28) | (1 << 25);
          }
          break;

        // Matches signatures that guess flash and rtc types
        case STR_SRAM_FVV11_SIG:
          if (MATCH_STR(str_sram_v11_sig) || MATCH_STR(str_sram_fv_sig))
            patchb->save_type_guess |= GUESS_SRAM;
          break;
        case STR_EEPROM_V_SIG:
          if (MATCH_STR(str_eeprom_v_sig))
            patchb->save_type_guess |= GUESS_EEPROM;
          break;
        case STR_FLASH_1M512V1_SIG:
          if (MATCH_STR(str_flash_v1_sig))
            patchb->save_type_guess |= GUESS_FLASH;
          else if (MATCH_STR(str_flash_512_sig))
            patchb->save_type_guess |= GUESS_FLASH64;
          else if (MATCH_STR(str_flash_1m_sig))
            patchb->save_type_guess |= GUESS_FLASH128;
          break;
        case STR_SIIRTC_V_SIG:
          if (MATCH_STR(str_siirtc_v_sig))
            patchb->rtc_guess = true;
          break;

        // EEPROM functions
        case EEPROM_V1_READ_SIG:
          if (MATCH_SIG(eeprom_v1_read_sig))
            push_save_handler(patch, OPC_EEPROM_HD, EEPROM_RD_HNDLR, addr);
          break;
        case EEPROM_V2_READ_SIG:
          if (MATCH_SIG(eeprom_v2_read_sig))
            push_save_handler(patch, OPC_EEPROM_HD, EEPROM_RD_HNDLR, addr);
          break;
        case EEPROM_V1_WRITE_SIG:
          if (MATCH_SIG(eeprom_v1_write_sig))
            push_save_handler(patch, OPC_EEPROM_HD, EEPROM_WR_HNDLR, addr);
          break;
        case EEPROM_V2_WRITE_SIG:
          if (MATCH_SIG(eeprom_v2_write_sig))
            push_save_handler(patch, OPC_EEPROM_HD, EEPROM_WR_HNDLR, addr);
          break;
        case EEPROM_V3_WRITE_SIG:
          if (MATCH_SIG(eeprom_v3_write_sig))
            push_save_handler(patch, OPC_EEPROM_HD, EEPROM_WR_HNDLR, addr);
          break;
        case EEPROM_V4_WRITE_SIG:
          if (MATCH_SIG(eeprom_v4_write_sig))
            push_save_handler(patch, OPC_EEPROM_HD, EEPROM_WR_HNDLR, addr);
          break;

        // Flash functions
        case FLASH_V1_READ_SIG:
          if (MATCH_SIG(flash_v1_read_sig))
            push_save_handler(patch, OPC_FLASH_HD, FLASH_READ_HNDLR, addr);
          break;
        case FLASH_V2V3_READ_SIG:
          if (MATCH_SIG(flash_v2_read_sig))
            push_save_handler(patch, OPC_FLASH_HD, FLASH_READ_HNDLR, addr);
          if (MATCH_SIG(flash_v3_read_sig))
            push_save_handler(patch, OPC_FLASH_HD, FLASH_READ_HNDLR, addr);
          break;
        case FLASH_V1_IDENT_SIG:
          if (MATCH_SIG(flash_v1_ident_sig))
            push_save_handler(patch, OPC_FLASH_HD, FLASH_IDEN_HNDLR, addr);
          break;
        case FLASH_V2_IDENT_SIG:
          if (MATCH_SIG(flash_v2_ident_sig))
            push_save_handler(patch, OPC_FLASH_HD, FLASH_IDEN_HNDLR, addr);
          break;
        case FLASH_V1_VERIFY_SIG:
          if (MATCH_SIG(flash_v1_verify_sig))
            push_save_handler(patch, OPC_FLASH_HD, FLASH_VERF_HNDLR, addr);
          break;
        case FLASH_V2V3_VERIFY_SIG:
          if (MATCH_SIG(flash_v2_verify_sig))
            push_save_handler(patch, OPC_FLASH_HD, FLASH_VERF_HNDLR, addr);
          if (MATCH_SIG(flash_v3_verify_sig))
            push_save_handler(patch, OPC_FLASH_HD, FLASH_VERF_HNDLR, addr);
          break;

        // RTC functions
        case SIIRTC_PROBERESET_SIG:
          if (MATCH_SIG(siirtc_probe_sig))
            push_rtc_handler(patch, RTC_PROBE_HNDLR, addr);
          if (MATCH_SIG(siirtc_reset_sig))
            push_rtc_handler(patch, RTC_RESET_HNDLR, addr);
          break;
        case SIIRTC_GETSTATUS_SIG:
          if (MATCH_SIG(siirtc_getstatus_sig))
            push_rtc_handler(patch, RTC_STSRD_HNDLR, addr);
          break;
        case SIIRTC_GETDATETIME_SIG:
          if (MATCH_SIG(siirtc_getdatetime_sig))
            push_rtc_handler(patch, RTC_GETTD_HNDLR, addr);
          break;
        }
      }
      else {
        // Try to match FLASH setup info data structure (word aligned)
        const t_flash_setup_info_v1 *info1 = (t_flash_setup_info_v1*)&rom[i];
        const t_flash_setup_info_v2 *info2 = (t_flash_setup_info_v2*)&rom[i];
        if (isromaddr(info2->program_byte_fnptr) && seems_flashinfo(&info2->v1)) {
          // Validate Device ID and check that sizes make sense.
          if (flashinfo_validsize(&info2->v1) && valid_flashid(info2->v1.device_id)) {
            // Extract handler info from the table.
            push_save_handler(patch, OPC_FLASH_HD, FLASH_CLRC_HNDLR, 0x1FFFFFE & info2->v1.erase_chip_fnptr);
            push_save_handler(patch, OPC_FLASH_HD, FLASH_CLRS_HNDLR, 0x1FFFFFE & info2->v1.erase_sector_fnptr);
            push_save_handler(patch, OPC_FLASH_HD, FLASH_WRTS_HNDLR, 0x1FFFFFE & info2->v1.program_sector_fnptr);
            push_save_handler(patch, OPC_FLASH_HD, FLASH_WRBT_HNDLR, 0x1FFFFFE & info2->program_byte_fnptr);
            if (info2->v1.device_id) {
              if (isflash128k(info2->v1.device_id))
                patchb->flash128cnt++;
              else
                patchb->flash64cnt++;
            }
          }
        }
        else if (seems_flashinfo(info1)) {
          if (flashinfo_validsize(info1) && valid_flashid(info1->device_id)) {
            push_save_handler(patch, OPC_FLASH_HD, FLASH_CLRC_HNDLR, 0x1FFFFFE & info1->erase_chip_fnptr);
            push_save_handler(patch, OPC_FLASH_HD, FLASH_CLRS_HNDLR, 0x1FFFFFE & info1->erase_sector_fnptr);
            push_save_handler(patch, OPC_FLASH_HD, FLASH_WRTS_HNDLR, 0x1FFFFFE & info1->program_sector_fnptr);
            if (info1->device_id) {
              if (isflash128k(info1->device_id))
                patchb->flash128cnt++;
              else
                patchb->flash64cnt++;
            }
          }
        }
      }
    }
  }

  patchb->rom_offset += romsize;
  patchb->ldata = ldata;
  patchb->ldatacnt = ldatacnt;

  return true;
}

// Generates a patch buffer (for a file) so that it can be loaded later.
int serialize_patch(const t_patch *patch, uint8_t *buffer) {
  // Write the patch into the buffer.
  memcpy(&buffer[0], "SUPERFWPATCHV01", 16);
  buffer[16] = patch->wcnt_ops;
  buffer[17] = patch->save_ops;
  buffer[18] = patch->save_mode;
  buffer[19] = patch->irqh_ops;
  buffer[20] = patch->rtc_ops;
  buffer[21] = 0;
  buffer[22] = (patch->hole_size >> 10) & 0xFF;
  buffer[23] = patch->hole_size >> 18;
  buffer[24] = (patch->hole_addr >> 10) & 0xFF;
  buffer[25] = patch->hole_addr >> 18;
  buffer[26] = 0;
  buffer[27] = 0;
  buffer[28] = 0;
  buffer[29] = 0;
  buffer[30] = 0;
  buffer[31] = 0;
  buffer += 32;

  memcpy(buffer, patch->prgs, sizeof(patch->prgs));
  buffer += sizeof(patch->prgs);
  memcpy(buffer, patch->op, sizeof(patch->op));

  return 32 + sizeof(patch->op) + sizeof(patch->prgs);
}

// Loads a patch from a buffer.
bool unserialize_patch(const uint8_t *buffer, unsigned size, t_patch *patch) {
  // Check header and size
  if (size != 32 + sizeof(patch->op) + sizeof(patch->prgs))
    return false;
  if (memcmp(&buffer[0], "SUPERFWPATCHV01", 16))
    return false;

  patch->wcnt_ops = buffer[16];
  patch->save_ops = buffer[17];
  patch->save_mode = buffer[18];
  patch->irqh_ops = buffer[19];
  patch->rtc_ops = buffer[20];
  patch->hole_size = (buffer[22] | (buffer[23] << 8)) << 10;
  patch->hole_addr = (buffer[24] | (buffer[25] << 8)) << 10;
  buffer += 32;

  memcpy(patch->prgs, buffer, sizeof(patch->prgs));
  buffer += sizeof(patch->prgs);
  memcpy(patch->op, buffer, sizeof(patch->op));

  return patch->wcnt_ops + patch->save_ops + patch->irqh_ops + patch->rtc_ops <= MAX_PATCH_OPS;
}

bool load_rom_patches(const char *romfn, t_patch *patches) {
  // Look for .patch files next to the ROM.
  char tmp[MAX_FN_LEN];
  strcpy(tmp, romfn);
  replace_extension(tmp, ".patch");

  // Try to open the file and process its content.
  FIL fd;
  if (FR_OK != f_open(&fd, tmp, FA_READ))
    return false;

  uint8_t buf[1024];
  UINT rdbytes;
  if (FR_OK != f_read(&fd, buf, sizeof(buf), &rdbytes))
    return false;

  return unserialize_patch(buf, rdbytes, patches);
}

bool load_cached_patches(const char *romfn, t_patch *patches) {
  char tmp[MAX_FN_LEN];
  const char *p = file_basename(romfn);
  strcpy(tmp, PATCHDB_PATH);
  strcat(tmp, p);

  // Replace the extension of the ROM with something more fitting.
  replace_extension(tmp, ".patch");

  // Try to open the file and process its content.
  FIL fd;
  if (FR_OK != f_open(&fd, tmp, FA_READ))
    return false;

  uint8_t buf[1024];
  UINT rdbytes;
  if (FR_OK != f_read(&fd, buf, sizeof(buf), &rdbytes))
    return false;

  return unserialize_patch(buf, rdbytes, patches);
}

bool write_patches_cache(const char *romfn, const t_patch *patches) {
  char tmp[MAX_FN_LEN];
  const char *p = file_basename(romfn);
  strcpy(tmp, PATCHDB_PATH);
  strcat(tmp, p);

  // Replace the extension of the ROM with something more fitting.
  replace_extension(tmp, ".patch");

  // Attempt to create dirs, should they not exist
  f_mkdir(SUPERFW_DIR);
  f_mkdir(PATCHDB_PATH);
  // Delete any existing patch file
  f_unlink(tmp);

  // Try to open the file and process its content.
  FIL fd;
  if (FR_OK != f_open(&fd, tmp, FA_WRITE | FA_CREATE_ALWAYS))
    return false;

  uint8_t buf[1024];
  int fs = serialize_patch(patches, buf);

  UINT wrbytes;
  if (FR_OK != f_write(&fd, buf, fs, &wrbytes)) {
    f_close(&fd);
    f_unlink(tmp);
    return false;
  }

  f_close(&fd);
  return wrbytes == fs;
}


