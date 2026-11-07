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

#include <string.h>

#include "carousel_util.h"
#include "utf_util.h"
#include "util.h"

int cover_header_check(const t_cover_header *h) {
  if (h->magic != COVER_MAGIC || h->big_size != COVER_BIG ||
      h->small_size != COVER_SMALL || h->palette != 0)
    return -2;
  if (h->version == 1)
    return -1;
  if (h->version == 2)
    return (h->tint & COVER_TINT_VALID) ? (h->tint & 0x7FFF) : -1;
  return -2;
}

uint16_t rgb555_mix(uint16_t a, uint16_t b, unsigned weight) {
  uint16_t ret = 0;
  for (unsigned sh = 0; sh < 15; sh += 5) {
    unsigned ca = (a >> sh) & 31, cb = (b >> sh) & 31;
    ret |= ((ca * (16 - weight) + cb * weight + 8) / 16) << sh;
  }
  return ret;
}

unsigned cover_size_at(unsigned dist, unsigned spacing) {
  if (dist >= spacing)
    return COVER_SMALL;
  unsigned sz = COVER_BIG - (COVER_BIG - COVER_SMALL) * dist / spacing;
  return sz & ~3U;     // Multiple of 4: keeps the left edge 16-bit aligned
}

void carousel_title(const char *fname, char *out) {
  unsigned i = 0;
  for (; i < MAX_FN_LEN - 1 && fname[i]; i++)
    out[i] = fname[i];
  out[i] = 0;
  replace_extension(out, "");

  // Hide "(USA, Europe)" / "[!]" style tags, they make most titles too long.
  for (char *p = &out[1]; out[0] && *p; p++)
    if ((*p == '(' || *p == '[') && p[-1] == ' ') {
      p[-1] = 0;
      break;
    }

  // Remove scene release numbers: "1190 - Super Mario Advance 4". Always 4
  // digits, so titles like "007 - NightFire" are left alone.
  unsigned n = 0;
  while (n < 5 && out[n] >= '0' && out[n] <= '9')
    n++;
  if (n == 4 && !strncmp(&out[n], " - ", 3) && out[n + 3])
    memmove(out, &out[n + 3], strlen(&out[n + 3]) + 1);
}

int carousel_namecmp(const char *a, const char *b) {
  while (*a && *b) {
    unsigned ca = unicodeorder(utf8_decode(a));
    unsigned cb = unicodeorder(utf8_decode(b));
    if (ca != cb)
      return ca < cb ? -1 : 1;
    a += utf8_chlen(a);
    b += utf8_chlen(b);
  }
  return (*a ? 1 : 0) - (*b ? 1 : 0);
}

unsigned carousel_letter_key(const char *fname) {
  return *fname ? unicodeorder(utf8_decode(fname)) : 0;
}

uint32_t carousel_hash(uint32_t h, const void *data, unsigned len) {
  const uint8_t *p = (const uint8_t*)data;
  for (unsigned i = 0; i < len; i++) {
    h ^= p[i];
    h *= 0x01000193U;
  }
  return h;
}
