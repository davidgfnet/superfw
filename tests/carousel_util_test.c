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

#include "carousel_util.h"
#include "util.h"

#include "fatfs/ff.h"

// util.c dependencies, not used by these tests.
FRESULT f_mkdir (const TCHAR* path) {
  return FR_OK;
}
FRESULT f_open (FIL* fp, const TCHAR* path, BYTE mode) { return FR_NO_FILE; }
FRESULT f_read (FIL* fp, void* buff, UINT btr, UINT* br) { *br = 0; return FR_OK; }
FRESULT f_close (FIL* fp) { return FR_OK; }
FRESULT f_stat (const TCHAR* path, FILINFO* fno) {
  return FR_OK;
}

static void check_title(const char *fname, const char *expected) {
  char out[MAX_FN_LEN];
  carousel_title(fname, out);
  if (strcmp(out, expected)) {
    printf("carousel_title('%s') = '%s', expected '%s'\n", fname, out, expected);
    abort();
  }
}

static void test_cover_header() {
  t_cover_header h = {
    .magic = COVER_MAGIC, .version = 1, .big_size = COVER_BIG,
    .small_size = COVER_SMALL, .palette = 0, .tint = 0,
  };
  assert(cover_header_check(&h) == -1);         // v1: valid, no tint

  h.version = 2;
  assert(cover_header_check(&h) == -1);         // v2 without tint
  h.tint = COVER_TINT_VALID | 0x1234;
  assert(cover_header_check(&h) == 0x1234);
  h.tint = COVER_TINT_VALID;
  assert(cover_header_check(&h) == 0);          // Black is a valid tint

  h.version = 3;
  assert(cover_header_check(&h) == -2);
  h.version = 2;
  h.magic = 0x12345678;
  assert(cover_header_check(&h) == -2);
  h.magic = COVER_MAGIC;
  h.big_size = 64;
  assert(cover_header_check(&h) == -2);
  h.big_size = COVER_BIG;
  h.small_size = 32;
  assert(cover_header_check(&h) == -2);
  h.small_size = COVER_SMALL;
  h.palette = 1;
  assert(cover_header_check(&h) == -2);
}

static void test_mix() {
  assert(rgb555_mix(0x0000, 0x7FFF, 0) == 0x0000);
  assert(rgb555_mix(0x0000, 0x7FFF, 16) == 0x7FFF);
  assert(rgb555_mix(0x1234, 0x5678, 0) == 0x1234);
  // Half way between 0 and 31 rounds to 16, per channel.
  assert(rgb555_mix(0x0000, 0x7FFF, 8) == (16 | (16 << 5) | (16 << 10)));
  // Channels do not leak into each other.
  assert(rgb555_mix(0x001F, 0x7C00, 8) == (16 | (16 << 10)));
}

static void test_sizes() {
  assert(cover_size_at(0, 76) == COVER_BIG);
  assert(cover_size_at(76, 76) == COVER_SMALL);
  assert(cover_size_at(500, 76) == COVER_SMALL);
  unsigned prev = COVER_BIG;
  for (unsigned d = 0; d < 100; d++) {
    unsigned sz = cover_size_at(d, 76);
    assert(sz % 4 == 0);                         // Keeps 16-bit alignment
    assert(sz <= prev && sz >= COVER_SMALL && sz <= COVER_BIG);
    prev = sz;
  }
}

static void test_titles() {
  check_title("Pokemon - Emerald Version (USA, Europe).gba", "Pokemon - Emerald Version");
  check_title("Metroid Fusion (U).gba", "Metroid Fusion");
  check_title("Golden Sun [!].gba", "Golden Sun");
  check_title("1190 - Super Mario Advance 4 - Super Mario Bros 3 (E)(Menace).gba",
              "Super Mario Advance 4 - Super Mario Bros 3");
  check_title("0004 - Golden Sun (U)(Mode7).gba", "Golden Sun");
  check_title("007 - NightFire (USA, Europe).gba", "007 - NightFire");   // Not a scene number
  check_title("1942 (USA, Europe).gbc", "1942");
  check_title("1234 - .gba", "1234 - ");          // Nothing left after the number
  check_title("(Hack) My Game.gba", "(Hack) My Game");
  check_title("Tetris.gb", "Tetris");
  check_title("NoExtension", "NoExtension");
  check_title("", "");
}

static void test_sorting() {
  assert(carousel_namecmp("abc", "abd") < 0);
  assert(carousel_namecmp("abd", "abc") > 0);
  assert(carousel_namecmp("ABC", "abc") == 0);
  assert(carousel_namecmp("abc", "abcd") < 0);
  assert(carousel_namecmp("abcd", "abc") > 0);
  assert(carousel_namecmp("", "") == 0);
  assert(carousel_namecmp("\xc3\xa9" "cran", "ecran") == 0);   // é sorts as e
  assert(carousel_namecmp("Zelda", "advance") > 0);

  assert(carousel_letter_key("Metroid") == carousel_letter_key("metroid"));
  assert(carousel_letter_key("Metroid") == carousel_letter_key("Mario"));
  assert(carousel_letter_key("Metroid") != carousel_letter_key("Zelda"));
  assert(carousel_letter_key("") == 0);
}

static void test_paths() {
  char out[MAX_FN_LEN], longname[MAX_FN_LEN + 16];

  assert(path_join(out, "/GBA/", "game.gba", ""));
  assert(!strcmp(out, "/GBA/game.gba"));
  assert(path_join(out, "/GBA/", "sub", "/"));
  assert(!strcmp(out, "/GBA/sub/"));

  // Exactly MAX_FN_LEN - 1 characters fit, one more does not.
  memset(longname, 'a', sizeof(longname));
  longname[MAX_FN_LEN - 1 - 5] = 0;
  assert(path_join(out, "/GBA/", longname, ""));
  assert(strlen(out) == MAX_FN_LEN - 1);
  longname[MAX_FN_LEN - 1 - 5] = 'a';
  longname[MAX_FN_LEN - 5] = 0;
  strcpy(out, "untouched");
  assert(!path_join(out, "/GBA/", longname, ""));
  assert(!strcmp(out, "untouched"));

  assert(rom_path_ok("/GBA/game.gba"));
  memset(longname, 'b', sizeof(longname));
  longname[0] = '/';
  longname[MAX_ROM_BASENAME_LEN + 1] = 0;             // Basename of MAX_ROM_BASENAME_LEN
  assert(rom_path_ok(longname));
  longname[MAX_ROM_BASENAME_LEN + 1] = 'b';
  longname[MAX_ROM_BASENAME_LEN + 2] = 0;
  assert(!rom_path_ok(longname));                     // Basename too long
}

static void test_hash() {
  // FNV-1a reference values
  assert(carousel_hash(CAROUSEL_HASH_INIT, "", 0) == 0x811C9DC5U);
  assert(carousel_hash(CAROUSEL_HASH_INIT, "a", 1) == 0xE40C292CU);
  assert(carousel_hash(CAROUSEL_HASH_INIT, "foobar", 6) == 0xBF9CF968U);
  // Incremental hashing matches hashing all at once.
  uint32_t h = carousel_hash(CAROUSEL_HASH_INIT, "foo", 3);
  assert(carousel_hash(h, "bar", 3) == 0xBF9CF968U);
}

int main() {
  test_cover_header();
  test_mix();
  test_sizes();
  test_titles();
  test_sorting();
  test_paths();
  test_hash();
  printf("carousel_util tests OK\n");
  return 0;
}
