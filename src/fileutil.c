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

#include "fatfs/ff.h"
#include "util.h"

#pragma GCC optimize ("Os")

bool check_file_exists(const char *fn) {
  FILINFO info;
  FRESULT res = f_stat(fn, &info);
  return res == FR_OK;
}

// Creates the path for a given file name.
void create_basepath(const char *fn) {
  if (!fn || !*fn)
    return;        // Empty path

  char tmp[FF_MAX_LFN];
  if (strlen(fn) >= sizeof(tmp))
    return;        // Too long for FatFS anyway
  strcpy(tmp, fn);

  // Iteratively attempt to create dirs, will fail if the dir already exists.
  unsigned off = 1;   // Skip first char (if it's a "/" we don't care)
  while (true) {
    char *p = strchr(&tmp[off], '/');
    if (!p)
      return;          // All done!

    *p = 0;            // Temporarily truncate the char
    f_mkdir(tmp);      // Create the dir until here
    *p = '/';

    off = p - tmp + 1; // Advance pointer
  }
}


bool file_read_lines(const char *fn, void (*cb)(char *line, void *usr), void *usr) {
  FIL fd;
  if (FR_OK != f_open(&fd, fn, FA_READ))
    return false;

  char buf[FILE_LINE_MAX + 1];
  unsigned len = 0;
  bool skip = false;          // Dropping the rest of a too long line
  while (true) {
    UINT rdbytes = 0;
    if (FR_OK != f_read(&fd, &buf[len], FILE_LINE_MAX - len, &rdbytes))
      break;
    len += rdbytes;
    buf[len] = 0;

    unsigned start = 0;
    for (unsigned i = 0; i < len; i++) {
      if (buf[i] != '\n')
        continue;
      buf[i] = 0;
      if (i > start && buf[i - 1] == '\r')
        buf[i - 1] = 0;
      if (!skip)
        cb(&buf[start], usr);
      skip = false;
      start = i + 1;
    }

    if (!rdbytes) {           // End of file: a last line without a line break
      if (start < len && !skip) {
        if (buf[len - 1] == '\r')
          buf[len - 1] = 0;
        cb(&buf[start], usr);
      }
      break;
    }
    if (!start && len == FILE_LINE_MAX) {
      skip = true;            // A full buffer without a line break
      len = 0;
    } else {
      memmove(buf, &buf[start], len - start);
      len -= start;
    }
  }
  f_close(&fd);
  return true;
}
