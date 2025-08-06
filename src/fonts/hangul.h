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

// Unifont hangul character composition.
// To save on unnecessary full hangul font, we simply ship the Unifont
// pre-rendered jamo blocks and render them as needed.

#define CHOSEONG_START         1
#define JUNGSEONG_START      115
#define JONGSEONG_START      178

#define CHOSEONG_VARCNT        6
#define JUNGSEONG_VARCNT       3

// Given a hangul unicode point, return up to three glyphs to render (combine)
// to form the required character.
static unsigned hangul_glyphs(uint32_t basecode, unsigned *cho, unsigned *jung, unsigned *jong) {
  int cho_num = basecode / (28 * 21);      // 19 possible choseong
  int jung_num = (basecode / 28) % 21;     // 21 possible jungseong
  int jong_num = (basecode % 28) - 1;      // 27 possible jongseong + no jongseong (-1)

  // The choseong variation to use, depends on the jungseong.
  const static uint8_t vchoseong[21] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1,
    2, 2, 2, 1, 4, 5, 5, 5,
    4, 1, 2, 0,
  };
  unsigned chovar = vchoseong[jung_num];
  // And also on whether there's a jongseong or not.
  if (jong_num >= 0 && chovar < 3)
    chovar += 3;

  // Only three jungseong variations (no jongseong, jongseong and nieun).
  unsigned jungvar = jong_num == 3 ? 2 :
                     jong_num >= 0 ? 1 : 0;

  // Output the relevant glyphs.
  *cho = CHOSEONG_START + cho_num * CHOSEONG_VARCNT + chovar;
  *jung = JUNGSEONG_START + jung_num * JUNGSEONG_VARCNT + jungvar;

  if (jong_num < 0)
    return 2;

  *jong = JONGSEONG_START + jong_num;
  return 3;
}

