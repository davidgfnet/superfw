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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see
 * <http://www.gnu.org/licenses/>.
 */

#include "utf_util.h"

// Decodes the pointed string and returns the number of bytes it takes.
unsigned utf8_chlen(const char *s) {
  uint8_t fchar = (uint8_t)*s;

  if ((fchar & 0x80) == 0x00)
    return 1;
  else if ((fchar & 0xE0) == 0xC0)
    return 2;
  else if ((fchar & 0xF0) == 0xE0)
    return 3;

  return 4;
}

// Returns the number of unicode characters in an encoded utf-8 buffer.
unsigned utf8_strlen(const char *s) {
  unsigned ret = 0;
  while (*s) {
    s += utf8_chlen(s);
    ret++;
  }
  return ret;
}

// Decodes a utf-8 character into its u32 respresentation
uint32_t utf8_decode(const char *s) {
  const uint8_t *uchar = (uint8_t*)s;

  if ((*uchar & 0x80) == 0x00)
    return *uchar;
  else if ((*uchar & 0xE0) == 0xC0)
    return ((uchar[0] & 0x1F) << 6) |
           ((uchar[1] & 0x3F));
  else if ((*uchar & 0xF0) == 0xE0)
    return ((uchar[0] & 0x1F) << 12) |
           ((uchar[1] & 0x3F) << 6)  |
           ((uchar[2] & 0x3F));

  return ((uchar[0] & 0x07) << 18) |
         ((uchar[1] & 0x3F) << 12) |
         ((uchar[2] & 0x3F) << 6)  |
         ((uchar[3] & 0x3F));
}


// Dirt & cheap aproximation to unicode transliteration + "tolower".
// Converts codepoints to ASCII chars when it makes sense, and also converts
// some characters to their lowercase counterparts.
// This is not exhaustive nor it tries to be.
unsigned unicodeorder(unsigned cp) {
  // Transliteration for the [0xC0, 0xE0) range, same for [0xE0, 0xFF]
  static const char transl_ls[] = {
    'a', 'a', 'a', 'a', 'a', 'a', 'a', 'c',
    'e', 'e', 'e', 'e', 'i', 'i', 'i', 'i',
    'd', 'n', 'o', 'o', 'o', 'o', 'o', 'o',
    'o', 'u', 'u', 'u', 'u', 'y', 't', 's',
  };
  // Transliterates Latin Ext A [0x100, 0x180), note that the LSB indicates
  // whether the character is upper/lower case usually.
  static const char transl_lA[] = {
    'a', 'a', 'a', 'c', 'c', 'c', 'c', 'd',
    'd', 'e', 'e', 'e', 'e', 'e', 'g', 'g',
    'g', 'g', 'h', 'h', 'i', 'i', 'i', 'i',
    'i', 'i', 'j', 'k', 'k', 'l', 'l', 'l',
    'l', 'l', 'n', 'n', 'n', 'n', 'o', 'o',
    'o', 'o', 'r', 'r', 'r', 's', 's', 's',
    's', 't', 't', 't', 'u', 'u', 'u', 'u',
    'u', 'u', 'w', 'y', 'z', 'z', 'z', 'z',
  };

  // Transliterates Latin Ext B [0x180, 0x200)
  // This range is mapped directly since it's quite irregular.
  static const char transl_lB[] = {
    'b','b','b','b','b','b','o','c',  'c','d','d','d','d','d','e','e',
    'e','f','f','g','g','h','i','i',  'k','k','l','l','m','n','n','o',
    'o','o','o','o','p','p','r','s',  's','s','s','t','t','t','t','u',
    'u','u','v','y','y','z','z','z',  'z','z','z','2','5','5','t','w',
    '|','|','|','!','d','d','d','l',  'l','l','n','n','n','a','a','i',
    'i','o','o','u','u','u','u','u',  'u','u','u','u','u','e','a','a',
    'a','a','a','a','g','g','g','g',  'k','k','o','o','o','o','z','z',
    'j','d','d','d','g','g','h','w',  'n','n','a','a','a','a','o','o',
  };

  switch (cp >> 8) {
  case 0:          // ASCII/latin
    if (cp >= 'A' && cp <= 'Z')
      return cp + 'a' - 'A';
    else if (cp >= 0xC0)
      return transl_ls[cp & 0x1F];  // Latin supplement accents/diacritics are transliterated
    return cp;

  case 1:          // Latin extended A + B
    if (cp < 0x180)
      return transl_lA[(cp - 0x100) >> 1];
    return transl_lB[cp - 0x180];

  default:
    return cp;
  };
}

// Given an utf-8 encoded string, produces an utf-16-like encoded string that
// can be used for sorting (ie. lexicographic sorting).
// Our utf-6-like encoding is just utf-8 but using 16 bit (prefix encoding)
void sortable_utf8_u16(const char *s8, uint16_t *s16) {
  while (*s8) {
    uint32_t code = utf8_decode(s8);
    code = unicodeorder(code);

    if (code < 0x8000)
      *s16++ = code;
    else {
      *s16++ = (code & 0x1FFFFF) >> 15;
      *s16++ = (code & 0x7FFF);
    }

    s8 += utf8_chlen(s8);
  }
  *s16 = 0;
}

