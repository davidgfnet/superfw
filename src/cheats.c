/*
 * Copyright (C) 2025 David Guillen Fandos <david@davidgf.net>
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

#include "cheats.h"
#include "util.h"
#include "fatfs/ff.h"

#pragma GCC optimize ("Os")

typedef struct {
  uint32_t addr;
  uint16_t value;
  uint16_t _pad;
} t_cheat_raw;

_Static_assert(sizeof(t_cheat_raw) == sizeof(t_cheat_predec), "in-place predecode");

// Preprocessing cheats, includes a proper header and pre-formats some payloads.
bool predecode_cheats(t_cheat_raw *codes, unsigned cnt) {
  for (unsigned i = 0; i < cnt; i++) {
    const uint8_t op2 = ((codes[i].addr >> 28) & 0xF) * 2;    // Opcode scaled by 2
    const uint32_t addr28 = codes[i].addr & 0xFFFFFFF;
    const uint16_t val16 = codes[i].value;

    // Some opcodes carry extra payload chunks.
    const uint16_t pllen = (op2 == 4 * 2) ? 1 :
                           (op2 == 5 * 2) ? (val16 + 2) / 3 :
                            0;
    if (pllen >= 255 / 8 || i + pllen >= cnt)
      return false;

    const t_cheat_predec h = {
      .opcode = op2,
      .blen = (1 + pllen) * 8,
      .value = val16,
      .address = addr28,
    };

    // Overwrite buffer with the new format
    memcpy(&codes[i], &h, sizeof(h));

    if (h.opcode == 5 * 2) {
      // This is an invalid cheat code
      if (!val16)
        return false;

      // Extra buffer. We perform some endianess conversion here.
      for (unsigned j = 1; j <= pllen; j++) {
        codes[i+j].addr  = __builtin_bswap32(codes[i+j].addr);
        codes[i+j].value = __builtin_bswap16(codes[i+j].value);
      }
    }

    i += pllen;
  }
  return true;
}

static bool parse_hex(const char *s, uint32_t *val, unsigned nibcnt) {
  unsigned r = 0;
  for (unsigned i = 0; i < nibcnt; i++) {
    if (!s[i])
      return false;    // String ended prematurely

    if (s[i] >= '0' && s[i] <= '9')
      r = (r << 4) | (s[i] - '0');
    else if (s[i] >= 'a' && s[i] <= 'f')
      r = (r << 4) | (s[i] - 'a' + 10);
    else if (s[i] >= 'A' && s[i] <= 'F')
      r = (r << 4) | (s[i] - 'A' + 10);
    else
      return false;
  }
  *val = r;
  return true;
}

// Parses RAW codes into a raw cheat buffer
int parse_cheat_codes(const char *s, t_cheat_raw *codes) {
  // Codes are in the format:
  // 0123ABCD+67EF 125634AB+78CD ....
  // We parse them assuming that the separators are space or plus. We admit multiple separators.
  unsigned cnt = 0;

  while (*s == ' ' || *s == '+') s++;      // Skip initial spaces

  while (*s) {
    uint32_t addr, val;
    if (!parse_hex(s, &addr, 8))
      return -1;

    s += 8;  // Consume the hex32
    while (*s == ' ' || *s == '+') s++;      // Skip separators
    if (!*s)
      return -1;   // The code is truncated, abort.

    if (!parse_hex(s, &val, 4))
      return -1;
    s += 4;  // Consume the hex16

    // A full code (addr+val) has been parsed, just write it raw into the buffer.
    codes->addr = addr;
    codes->value = val;
    codes++;
    cnt++;

    while (*s == ' ' || *s == '+') s++;      // Skip trailing separators
  }
  return cnt;
}


// Reads a cheat file into a temp buffer (usually in SDRAM) and returns the size in bytes
// Returns -1 if the file is not found or the cheats cannot be loaded.
// Cheats are limited to 512 byte lines (~42 codes per line, or 36 depending how packed they are).
int open_read_cheats(uint8_t *buffer, unsigned buffsize, const char *fn) {
  FIL fd;
  FRESULT res = f_open(&fd, fn, FA_READ);
  if (res != FR_OK)
    return -1;

  bool parse_name = true;
  unsigned bcount = 0;
  char tmp[1024 + 4];

  t_cheathdr_ext chdr;
  uint32_t *bufhd = (uint32_t*)buffer;
  *bufhd = 0;
  unsigned bufsz = 4;

  // Parse the file line by line, using a temp buffer.
  while (1) {
    if (bcount <= 512) {
      UINT rdbytes;
      if (FR_OK != f_read(&fd, &tmp[bcount], 512, &rdbytes))
        return -1;
      bcount += rdbytes;
      if (!bcount)
        break;
      tmp[bcount] = 0;
    }

    // Attempt to parse the next line.
    char *p = strchr(tmp, '\n');
    if (!p) 
      p = strchr(tmp, '\0');

    // Reject any cheats with lines over 512 bytes!
    if (p - tmp > 512)
      return -1;

    *p = 0;        // Add the string end char.

    // Skip leading characters.
    char *s = tmp;
    while (*s == ' ' || *s == '\t')
      s++;

    // Skip empty lines!
    if (*s != 0) {
      if (bufsz + 1024 > buffsize)
        return -1;

      // Fill entry, string or cheat codes.
      if (parse_name) {
        // Fill title and header.
        strbcpy(chdr.title, s, sizeof(chdr.title));
        chdr.h.slen = (strlen(chdr.title) + 1 + 3) / 4;  // Word count
        chdr.h.enabled = 0;
        chdr.h.codelen = 0;
      } else {
        // Parse the cheat codes (in hex), and generate the respective code.
        t_cheat_raw codes[42+1];  // Enough codes for a 512 byte cheat line.
        memset(codes, 0, sizeof(codes));
        int numcodes = parse_cheat_codes(tmp, codes);
        if (numcodes < 0)
          return -1;
        // Process the raw codes and format them into t_cheat_predec (predecoded opcode)
        if (!predecode_cheats(codes, numcodes))
          return -1;
        // Each code takes 8 bytes
        chdr.h.codelen = numcodes + 1;

        { // Copy the data to the actual buffer.
          unsigned pheadl = sizeof(t_cheathdr) + chdr.h.slen * 4;
          memcpy32(&buffer[bufsz], &chdr, pheadl);
          memcpy32(&buffer[bufsz + pheadl], codes, chdr.h.codelen * 8);

          bufsz += pheadl + chdr.h.codelen * 8;
          (*bufhd)++;
        }
      }

      parse_name = !parse_name;
    }

    // Consume bytes
    unsigned cnt = strlen(tmp) + 1;
    if (bcount < cnt)
      bcount = 0;
    else {
      memmove(&tmp[0], &tmp[cnt], bcount - cnt + 1);
      bcount -= cnt;
    }
  }

  f_close(&fd);
  return bufsz;
}


