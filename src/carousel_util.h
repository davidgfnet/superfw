/*
 * Copyright (C) 2026 SuperFW contributors
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

// Hardware independent helpers for the carousel tab (unit tested on the host).

#ifndef __CAROUSEL_UTIL_H__
#define __CAROUSEL_UTIL_H__

#include <stdbool.h>
#include <stdint.h>

#include "config.h"

// Cover files: /.superfw/covers/<rom file name>.cov or <game code>.cov
#define COVER_MAGIC         0x56434653     // "SFCV"
#define COVER_BIG           80             // Selected cover (pixels, square)
#define COVER_SMALL         48             // Neighbour covers
#define COVER_PAL_BASE      20             // 6x6x6 color cube at [20..235]
#define COVER_PAL_SIZE     216
#define COVER_BG_INDEX_V1   17             // Letterbox pixels (menu background)
#define COVER_BG_INDEX_V2  236             // Letterbox pixels (carousel background)

typedef struct {
  uint32_t magic;           // COVER_MAGIC
  uint8_t version;          // 1 or 2
  uint8_t big_size;         // COVER_BIG
  uint8_t small_size;       // COVER_SMALL
  uint8_t palette;          // 0: fixed color cube at COVER_PAL_BASE
  uint16_t tint;            // v2: background tint (BGR555, bit 15 set if valid)
  uint16_t reserved[3];
  // Followed by big_size^2 and small_size^2 palette index bytes.
} t_cover_header;
_Static_assert (sizeof(t_cover_header) == 16, "t_cover_header must be 16 bytes");

#define COVER_TINT_VALID    0x8000

// Validates a cover header. Returns its tint (BGR555) or -1 if it has none.
// Returns -2 if the header is not valid.
int cover_header_check(const t_cover_header *h);

// Mixes two BGR555 colors: weight/16 of b, the rest of a.
uint16_t rgb555_mix(uint16_t a, uint16_t b, unsigned weight);

// Size of a cover drawn at a distance (pixels) from the center: COVER_BIG at
// the center, COVER_SMALL at "spacing" and beyond. Always a multiple of 4.
unsigned cover_size_at(unsigned dist, unsigned spacing);

// Display title from a ROM file name: no extension, no "(USA)"/"[!]" tags and
// no 4 digit scene release number ("1190 - "). out must hold MAX_FN_LEN bytes.
void carousel_title(const char *fname, char *out);

// Compares two file names for sorting (case insensitive, accents folded).
int carousel_namecmp(const char *a, const char *b);

// Sort key (folded first character) used for the letter jumps.
unsigned carousel_letter_key(const char *fname);

// FNV-1a hash, used to detect changes in the game list.
#define CAROUSEL_HASH_INIT  0x811C9DC5U
uint32_t carousel_hash(uint32_t h, const void *data, unsigned len);

#endif
