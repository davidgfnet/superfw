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

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "gbahw.h"
#include "util.h"
#include "settings.h"
#include "supercard_driver.h"
#include "fatfs/ff.h"
#include "common.h"
#include "save.h"
#include "nanoprintf.h"

#define SRAM_BASE_U8        ((volatile uint8_t *)(0x0E000000))
#define SRAM_BANK_SIZE      ( 64*1024)
#define SRAM_CHIP_SIZE      (128*1024)

// SuperChis maps SRAM bank separatedly (not shared with read/write bit)
#ifdef SUPERCHIS_IO
  #define SRAM_MAP_BANK(bank) sram_superchis_bank(bank)
#else
  #define SRAM_MAP_BANK(bank) set_supercard_mode(MAPPED_SDRAM, bank ? true : false, true)
#endif

bool load_save_sram(const char *savefn) {
  FIL fd;
  FRESULT res = f_open(&fd, savefn, FA_READ);
  if (res != FR_OK)
    return false;

  // Proceed to load the file, up to 128KB
  erase_sram();

  uint8_t buf[4*1024];
  for (unsigned i = 0; i < SRAM_CHIP_SIZE; i += sizeof(buf)) {
    UINT rdbytes = 0;
    if (FR_OK != f_read(&fd, buf, sizeof(buf), &rdbytes)) {
      f_close(&fd);
      return false;
    }

    volatile uint8_t *sram_ptr = &SRAM_BASE_U8[i % SRAM_BANK_SIZE];
    unsigned bank = i / SRAM_BANK_SIZE;
    SRAM_MAP_BANK(bank);
    for (unsigned j = 0; j < rdbytes; j++)
      *sram_ptr++ = buf[j];
    SRAM_MAP_BANK(0);

    if (rdbytes < sizeof(buf))
      break;   // EOF
  }

  f_close(&fd);
  return true;
}

bool wipe_sav_file(const char *fn) {
  FIL fd;
  FRESULT res = f_open(&fd, fn, FA_WRITE | FA_CREATE_ALWAYS);
  if (res != FR_OK)
    return false;

  uint16_t tmpbuf[4096/2];
  dma_memset16(tmpbuf, 0xFFFF, sizeof(tmpbuf) / 2);

  for (unsigned i = 0; i < SRAM_CHIP_SIZE; i += sizeof(tmpbuf)) {
    UINT wrbytes = 0;
    res = f_write(&fd, tmpbuf, sizeof(tmpbuf), &wrbytes);
    if (res != FR_OK) {
      f_close(&fd);
      return false;
    }
  }
  f_close(&fd);

  return true;
}

bool write_save_sram(const char *fn) {
  FIL fd;
  FRESULT res = f_open(&fd, fn, FA_WRITE | FA_CREATE_ALWAYS);
  if (res != FR_OK)
    return false;

  for (unsigned i = 0; i < SRAM_CHIP_SIZE; i += 1024) {
    UINT wrbytes = 0;
    uint8_t tmpbuf[1024];
    // Read SRAM using byte access
    volatile uint8_t *sram_ptr = &SRAM_BASE_U8[i % SRAM_BANK_SIZE];
    unsigned bank = i / SRAM_BANK_SIZE;
    SRAM_MAP_BANK(bank);
    for (unsigned j = 0; j < 1024; j++)
      tmpbuf[j] = sram_ptr[j];
    SRAM_MAP_BANK(0);

    res = f_write(&fd, tmpbuf, sizeof(tmpbuf), &wrbytes);
    if (res != FR_OK) {
      f_close(&fd);
      return false;
    }
  }
  f_close(&fd);

  return true;
}

bool compare_save_sram(const char *fn) {
  FIL fd;
  FRESULT res = f_open(&fd, fn, FA_READ);
  if (res != FR_OK)
    return false;

  bool mism = false;
  for (unsigned i = 0; i < SRAM_CHIP_SIZE && !mism; i += 1024) {
    UINT rdbytes = 0;
    uint8_t tmpbuf[1024];
    res = f_read(&fd, tmpbuf, sizeof(tmpbuf), &rdbytes);
    if (res != FR_OK || rdbytes == 0) {
      f_close(&fd);
      return false;
    }

    // Read SRAM and compare read values
    volatile uint8_t *sram_ptr = &SRAM_BASE_U8[i % SRAM_BANK_SIZE];
    unsigned bank = i / SRAM_BANK_SIZE;
    SRAM_MAP_BANK(bank);
    for (unsigned j = 0; j < 1024; j++)
      if (tmpbuf[j] != sram_ptr[j])
        mism = true;
    SRAM_MAP_BANK(0);
  }
  f_close(&fd);

  return !mism;
}

// Performs file rotation. Assumes that a ".tmp.sav" file exists.
// - Unlink (N_backup+1) if present
// - Rename i to i+1
// - Rename .sav to .1.sav
// - Rename .tmp.sav to .sav
// - Unlink i-(N+1) if present
bool rotate_savefile(const char *templ_fn, unsigned max_backups) {
  char tmpfn[MAX_FN_LEN], dstfn[MAX_FN_LEN];

  // Backup renaming, f_rename doesn't like existing dest files tho.
  if (max_backups) {
    npf_snprintf(tmpfn, sizeof(tmpfn), "%s.%u.sav", templ_fn, max_backups+1);
    f_unlink(tmpfn);
    for (unsigned i = max_backups; i >= 1; i--) {
      npf_snprintf(dstfn, sizeof(dstfn), "%s.%u.sav", templ_fn, i+1);
      npf_snprintf(tmpfn, sizeof(tmpfn), "%s.%u.sav", templ_fn, i);
      f_rename(tmpfn, dstfn);
    }
  }

  // Rename the .sav to .1.sav
  npf_snprintf(dstfn, sizeof(dstfn), "%s.1.sav", templ_fn);
  npf_snprintf(tmpfn, sizeof(tmpfn), "%s.sav", templ_fn);
  f_rename(tmpfn, dstfn);

  // Rename the .tmp.sav file to .sav
  npf_snprintf(dstfn, sizeof(dstfn), "%s.sav", templ_fn);
  npf_snprintf(tmpfn, sizeof(tmpfn), "%s.tmp.sav", templ_fn);
  f_rename(tmpfn, dstfn);

  // Attempt to remove any overflowing backup file.
  npf_snprintf(tmpfn, sizeof(tmpfn), "%s.%u.sav", templ_fn, max_backups+1);
  f_unlink(tmpfn);

  return true;
}

// Performs a write to SD and rotates (renames) files as backup goes (see above).
bool write_save_sram_rotate(const char *templ_fn, unsigned max_backups) {
  char tmpfn[MAX_FN_LEN];
  strcpy(tmpfn, templ_fn);
  strcat(tmpfn, ".tmp.sav");
  if (!write_save_sram(tmpfn))
    return false;

  return rotate_savefile(templ_fn, max_backups);
}


// Writes a save game from SRAM using a pending file sentinel as input.
unsigned flush_pending_sram() {
  FIL fd;
  FRESULT res = f_open(&fd, PENDING_SAVE_FILEPATH, FA_READ);
  if (res != FR_OK)
    return ERR_SAVE_FLUSH_NOSENTINEL;

  // The file contains the save filename template, plus options.
  UINT rdbytes = 0;
  char content[512];
  if (FR_OK != f_read(&fd, content, sizeof(content) - 1, &rdbytes)) {
    f_close(&fd);
    return ERR_SAVE_FLUSH_NOSENTINEL;
  }
  content[rdbytes] = 0;

  // Separate options using NULL.
  unsigned l = strlen(content);
  for (unsigned i = 0; i < l; i++)
    if (content[i] == '\n')
      content[i] = 0;

  // Extract the filename and options
  const char *savefn = content;
  const char *bkpn = NULL;
  for (unsigned i = strlen(content) + 1; i < l + 1; ) {
    if (!strncmp(&content[i], "backup_count=", 13))
      bkpn = &content[i + 13];
    i += strlen(content) + 1;
  }

  // Parse options.
  unsigned backup_num = 0;
  if (bkpn)
    backup_num = parseuint(bkpn);

  // Validate the filename! Should start with "/". Let the FatFS check it too.
  if (savefn[0] != '/')
    return ERR_SAVE_FLUSH_NOSENTINEL;

  // Check if the save file exists and contains the same data.
  {
    char tmpfn[MAX_FN_LEN];
    strcpy(tmpfn, savefn);
    strcat(tmpfn, ".sav");
    // Do not write nor rotate backups if the SRAM did not change!
    if (compare_save_sram(tmpfn))
      return 0;
  }

  // Create the base dir (since in some cases like /SAVES/ it won't exist).
  create_basepath(savefn);

  // Use the rotate function to ensure we write this properly.
  if (!write_save_sram_rotate(savefn, backup_num))
    return ERR_SAVE_FLUSH_WRITEFAIL;

  return 0;
}

// Writes/Clears a sentinel file to indicate that SRAM must be dumped and
// stored during the next boot, to preserve the current game save storage.
// The file contains one or more lines: filename template (without .sav),
// options (ie. backup_count=N)
bool program_sram_dump(const char *save_filename, unsigned backup_cnt) {
  if (!save_filename) {
    if (check_file_exists(PENDING_SAVE_FILEPATH)) {
      if (FR_OK != f_unlink(PENDING_SAVE_FILEPATH))
        return false;
    }
  } else {
    // Create the directory (just in case it doesn't exist
    f_mkdir(SUPERFW_DIR);
    // Make it hidden
    f_chmod(SUPERFW_DIR, AM_HID, AM_HID);

    // Write filename along with backup count.
    char content[512];
    npf_snprintf(content, sizeof(content), "%s\nbackup_count=%u", save_filename, backup_cnt);

    FIL fd;
    if (FR_OK != f_open(&fd, PENDING_SAVE_FILEPATH, FA_WRITE | FA_CREATE_ALWAYS))
      return false;
    UINT wrbytes;
    FRESULT res = f_write(&fd, content, strlen(content), &wrbytes);
    f_close(&fd);

    return FR_OK == res && wrbytes == strlen(content);
  }
  return true;
}

// Erases the SRAM (using ones since it seems to be the most common mem type)
void erase_sram() {
  // Erase both 64KB banks (ensure we leave on bank 1, for write-enable)
  for (unsigned bankn = 0; bankn < 2; bankn++) {
    SRAM_MAP_BANK(bankn);
    for (unsigned i = 0; i < SRAM_BANK_SIZE; i++)
      SRAM_BASE_U8[i] = 0xFF;
  }
}

bool file_is_contiguous(const char *fn, LBA_t *lba) {
  FIL fd;
  if (FR_OK != f_open(&fd, fn, FA_READ))
    return false;

  int iscont = 0;
  if (FR_OK != test_contiguous_file(&fd, &iscont) || !iscont)
    return false;

  if (lba)
    *lba = fd.obj.fs->database + fd.obj.fs->csize * (fd.obj.sclust - 2);

  f_close(&fd);
  return true;
}

// Creates a copy, or an empty FF file (contiguous)
bool copy_save_contiguous_file(const char *fn, const char *dest, unsigned size) {
  // Ensure the out path exists, create it!
  create_basepath(dest);

  FIL foutput;
  if (FR_OK != f_open(&foutput, dest, FA_WRITE | FA_CREATE_ALWAYS))
    return false;

  if (FR_OK != f_expand(&foutput, size, 1)) {
    f_close(&foutput);
    return false;
  }

  uint8_t buffer[1024*2];
  if (fn) {
    // File copy, block by block. Pad to "size" with ones.
    FIL finput;
    if (FR_OK != f_open(&finput, fn, FA_READ))
      return false;

    for (unsigned i = 0; i < size; i += sizeof(buffer)) {
      UINT rdbytes;
      unsigned toread = MIN(sizeof(buffer), size - i);
      if (FR_OK != f_read(&finput, buffer, toread, &rdbytes)) {
        f_close(&foutput);
        f_close(&finput);
        return false;
      }

      // Pad or clear the buffer if the input file wasn't enough!
      if (rdbytes < sizeof(buffer))
        memset(&buffer[rdbytes], 0xFF, sizeof(buffer) - rdbytes);

      UINT wrbytes;
      unsigned towrite = MIN(sizeof(buffer), size - i);
      if (FR_OK != f_write(&foutput, buffer, towrite, &wrbytes) || wrbytes != towrite) {
        f_close(&foutput);
        f_close(&finput);
        return false;
      }
    }

    f_close(&foutput);
    f_close(&finput);
  } else {
    // Just write some empty data!
    memset(buffer, 0xFF, sizeof(buffer));

    for (unsigned i = 0; i < size; i += sizeof(buffer)) {
      UINT wrbytes;
      unsigned towrite = MIN(sizeof(buffer), size - i);
      if (FR_OK != f_write(&foutput, buffer, towrite, &wrbytes) || wrbytes != towrite) {
        f_close(&foutput);
        return false;
      }
    }
    f_close(&foutput);
  }

  return true;
}

__attribute__((noinline))
unsigned prepare_sram_based_savegame(t_sram_load_policy loadp, t_sram_save_policy savep, const char *savefn) {
  // Process the loading part first:
  if (loadp == SaveLoadSav) {
    if (!load_save_sram(savefn))
      return ERR_SAVE_BADSAVE;
  }
  else if (loadp == SaveLoadReset)
    erase_sram();

  // Now proceed to program / de-program the saving on reboot aspect.
  if (savep == SaveReboot) {
    // Program auto-save on reboot, write basename to the config file
    char savetmpl[MAX_FN_LEN];
    sram_template_filename_calc(savefn, "", savetmpl);
    if (!program_sram_dump(savetmpl, backup_sram_default))
      return ERR_SAVE_CANTWRITE;
  }
  else { /* Save disabled */
    if (!program_sram_dump(NULL, 0))   // Remove sentinel file, no save on reboot.
      return ERR_SAVE_CANTWRITE;
  }

  return 0;
}

__attribute__((noinline))
unsigned prepare_savegame(t_sram_load_policy loadp, t_sram_save_policy savep, EnumSavetype stype, t_dirsave_info *dsinfo, const char *savefn) {
  // Branch on the two main saving modes: DirectSave and SRAM-based saving.
  if (savep == SaveDirect) {
    if (loadp == SaveLoadDisable)
      return ERR_SAVE_BADARG;         // This option is invalid, this should never happen.

    if (!program_sram_dump(NULL, 0))   // Remove sentinel file, no save on reboot!
      return ERR_SAVE_CANTWRITE;

    unsigned ssize = savetype_size(stype);
    dsinfo->save_size = ssize;
    // Proceed to perform a backup of the save file if necessary. We rewrite the save
    // in any case. That way we can do backup rotation, and we guarantee it's contiguous.
    // If backups are disabled and the file was already contiguous, we do some unnecessary work.

    char tmpfilen[MAX_FN_LEN];
    strcpy(tmpfilen, savefn);
    replace_extension(tmpfilen, ".tmp.sav");

    if (loadp == SaveLoadReset) {
      if (!copy_save_contiguous_file(NULL, tmpfilen, ssize))
        return ERR_SAVE_CANTWRITE;
    } else {
      if (!copy_save_contiguous_file(savefn, tmpfilen, ssize))
        return ERR_SAVE_CANTWRITE;
    }

    // Proceed to rotate (if at all) the backup.
    strcpy(tmpfilen, savefn);
    replace_extension(tmpfilen, "");
    if (!rotate_savefile(tmpfilen, backup_sram_default))
      return ERR_SAVE_CANTWRITE;

    // For EEPROM based DirSave we only flush, so the save needs to be loaded
    if (stype == SaveTypeEEPROM4K || stype == SaveTypeEEPROM64K) {
      // Proceed to load the current save, to SRAM. Or clear it all if necessary.
      if (loadp == SaveLoadSav) {
        if (!load_save_sram(savefn))
          return ERR_SAVE_BADSAVE;
      }
      else if (loadp == SaveLoadReset)
        erase_sram();
    }

    // Proceed with the saving magic. Validate that the file is contiguous to ensure
    // we do not bork the FAT partition (even tho we know it is!). Calculate the LBA
    // and return it along with the save size.
    LBA_t lba;
    if (!file_is_contiguous(savefn, &lba))
      return ERR_SAVE_CANTALLOC;

    dsinfo->sector_lba = lba;
  } else {
    // SRAM-based (on reboot/manual) saving!

    return prepare_sram_based_savegame(loadp, savep, savefn);
  }

  return 0;
}

