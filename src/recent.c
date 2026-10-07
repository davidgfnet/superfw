/*
 * Copyright (C) 2026 David Guillen Fandos <david@davidgf.net>
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

#include "recent.h"

#include "common.h"
#include "compiler.h"
#include "gbahw.h"
#include "util.h"
#include "fatfs/ff.h"

#pragma GCC optimize ("Os")

NOINLINE bool recent_flush(const t_rentry *rentries, unsigned rcount) {
  WRITE_LOG("Flushing recently played games (%d entries)", rcount);

  // Flush to disk!
  FIL fo;
  if (FR_OK != f_open(&fo, RECENT_FILEPATH, FA_WRITE | FA_CREATE_ALWAYS))
    return false;

  // Write stuff to disk. Use a 1KiB buffer and flush as full blocks fill.
  unsigned coff = 0;
  char tmpbuf[1024];
  tmpbuf[0] = 0;

  for (unsigned i = 0; i < rcount; i++) {
    unsigned fnlen = strlen(rentries[i].fpath);
    if (rentries[i].flags & FLAG_RECENT_NOR) {
      memcpy(&tmpbuf[coff], "nor:", 4);
      coff += 4;
    }
    memcpy(&tmpbuf[coff], rentries[i].fpath, fnlen);
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

NOINLINE unsigned insert_recent_fn(t_rentry *rentries, unsigned rcount, const char *fn, unsigned flags) {
  WRITE_LOG("Adding/bumping recently played game: '%s' [%x]", fn, flags);

  for (unsigned i = 0; i < rcount; i++) {
    if (rentries[i].flags == flags && !strcmp(rentries[i].fpath, fn)) {
      // Found a matching file, move it to position 0, unless it's there already.
      if (i) {
        t_rentry tmp;
        memcpy32(&tmp, &rentries[i], sizeof(tmp));   // Copy entry to tmp
        memmove32(&rentries[1], &rentries[0], i * sizeof(rentries[0]));
        memcpy32(&rentries[0], &tmp, sizeof(tmp));
      }
      return rcount;
    }
  }

  // Not in the list, push all items back and insert it in the first position
  if (rcount) {
    unsigned movecnt = MIN(rcount, RECENT_MAXFN_CNT - 1);
    memmove32(&rentries[1], &rentries[0], movecnt * sizeof(rentries[0]));
  }

  const char *pbn = file_basename(fn);
  rentries[0].fname_offset = pbn - fn;
  rentries[0].flags = flags;
  memcpy32(rentries[0].fpath, fn, strlen(fn) + 1);
  // The oldest entry falls off the end when the list is full.
  return MIN(rcount + 1, RECENT_MAXFN_CNT);
}

NOINLINE unsigned delete_recent(t_rentry *rentries, unsigned rcount, unsigned entry_num) {
  if (entry_num >= rcount)
    return rcount;

  if (entry_num + 1 < rcount)
    memmove32(&rentries[entry_num], &rentries[entry_num + 1],
              (rcount - (entry_num + 1)) * sizeof(rentries[0]));

  return rcount - 1;
}

NOINLINE unsigned recent_load(const char *fpath, t_rentry *rentries) {
  FIL fi;
  if (FR_OK != f_open(&fi, fpath, FA_READ))
    return 0;

  // Read data block by block. Each line is a path (or "nor:" and a path),
  // lines that can't be one (too long) are skipped.
  char tmp[1024 + 4];
  unsigned bcount = 0, nentries = 0;
  bool eof = false, skipping = false;
  while (nentries < RECENT_MAXFN_CNT) {
    if (bcount <= 512 && !eof) {
      UINT rdbytes;
      if (FR_OK != f_read(&fi, &tmp[bcount], 512, &rdbytes))
        break;
      eof = rdbytes < 512;
      bcount += rdbytes;
    }

    if (!bcount)
      break;

    // Find the end of the line. Without a newline in the buffer, it is
    // either the last line of the file or a line too long to be a path,
    // which is skipped up to its newline (that can come in later blocks).
    char *nl = memchr(tmp, '\n', bcount);
    unsigned len = nl ? (unsigned)(nl - tmp) : bcount;
    unsigned cnt = nl ? len + 1 : bcount;      // Bytes consumed

    if (!nl && !eof)
      skipping = true;
    else if (skipping)
      skipping = false;                        // End of a skipped line
    else {
      tmp[len] = 0;
      if (len && tmp[len - 1] == '\r')
        tmp[--len] = 0;                        // Edited on Windows
      bool nor = len >= 4 && !memcmp(tmp, "nor:", 4);
      const char *path = nor ? &tmp[4] : tmp;
      unsigned plen = nor ? len - 4 : len;
      // Skip empty lines, and paths that don't fit an entry.
      if (plen && plen < MAX_FN_LEN) {
        rentries[nentries].flags = nor ? FLAG_RECENT_NOR : 0;
        dma_memcpy16(rentries[nentries].fpath, path, (plen + 2) / 2);
        rentries[nentries].fname_offset = file_basename(rentries[nentries].fpath) - rentries[nentries].fpath;
        nentries++;
      }
    }

    // Consume the bytes
    memmove(&tmp[0], &tmp[cnt], bcount - cnt);
    bcount -= cnt;
  }

  WRITE_LOG("Loaded recently played games. %d entries found", nentries);

  f_close(&fi);
  return nentries;
}


