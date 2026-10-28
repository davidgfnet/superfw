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

typedef struct {
  uint8_t slen;        // String length (words)
  uint8_t codelen;     // Data payload length (double words)
  uint8_t enabled;
  uint8_t _pad;        // Helps with 16 bit writes when updating enabled.
  uint8_t data[];
} t_cheathdr;

typedef struct {
  t_cheathdr h;
  char title[256];
} t_cheathdr_ext;

// Longest cheat title: slen is 8 bits and the title is NUL terminated and
// padded to a word boundary (251 + 1 -> 252).
#define MAX_CHEAT_TITLE  251

typedef struct {
  uint8_t opcode;            // The codebreaker opcode (0 to 15)
  uint8_t blen;              // Number of bytes used by this cheat (usually 8, this struct)
  uint16_t value;            // Cheat value
  uint32_t address;          // Cheat address
} t_cheat_predec;

int open_read_cheats(uint8_t *buffer, unsigned buffersize, const char *fn);

