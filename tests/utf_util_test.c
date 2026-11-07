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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.   See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see
 * <http://www.gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "utf_util.h"


int main() {
  assert(1 == utf8_chlen("f"));
  assert(2 == utf8_chlen("ç"));
  assert(3 == utf8_chlen("㐀"));
  assert(4 == utf8_chlen("😀"));

  assert(3 == utf8_strlen("foo"));
  assert(5 == utf8_strlen("Barça"));
  assert(1 == utf8_strlen("㐀"));
  assert(1 == utf8_strlen("㐁"));
  assert(2 == utf8_strlen("㐀㐁"));
  assert(4 == utf8_strlen("s㐀㐁a"));
  assert(1 == utf8_strlen("😀"));
  assert(4 == utf8_strlen("a😀😀a"));

  assert('f'  == utf8_decode("f"));
  assert(0xE7 == utf8_decode("ç"));
  assert(0x3400 == utf8_decode("㐀"));
  assert(0x1f600 == utf8_decode("😀"));

  // Malformed/cut sequences count as one byte and never skip the NUL.
  assert(1 == utf8_chlen("\xC3"));
  assert(1 == utf8_chlen("\xE2\x82"));
  assert(1 == utf8_chlen("\xF0\x9F\x98"));
  assert(1 == utf8_chlen("\xC3" "A"));
  assert(0xFFFD == utf8_decode("\xC3"));
  assert(0xFFFD == utf8_decode("\xF0\x9F"));
  assert(3 == utf8_strlen("a\xE2\x82"));
  assert(2 == utf8_strlen("\xFF\xFF"));

  uint16_t out[4];

  sortable_utf8_u16("f", out);
  assert(out[0] == 'f' && out[1] == 0);

  sortable_utf8_u16("F", out);
  assert(out[0] == 'f' && out[1] == 0);

  const char *tst[] = {"Á", "á", "À", "à", "Ä", "ä", "Â", "â", "Ã", "ã", "Ā", "Ă", "ā"};
  for (unsigned i = 0; i < sizeof(tst)/sizeof(tst[0]); i++) {
    sortable_utf8_u16(tst[i], out);
    assert(out[0] == 'a' && out[1] == 0);
  }
}


