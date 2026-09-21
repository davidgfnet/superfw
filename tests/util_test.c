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

#include "util.h"

#include "fatfs/ff.h"

unsigned mkdir_cnt = 0;
const char *expected_mkdirs[] = {
  "/path",
  "/foo",
  "/foo/bar",
  "/foo/bar/lol",
};
FRESULT f_mkdir (const TCHAR* path) {
  assert(mkdir_cnt < sizeof(expected_mkdirs) / sizeof(expected_mkdirs[0]));
  assert(!strcmp(path, expected_mkdirs[mkdir_cnt++]));
  return FR_OK;
}
FRESULT f_stat (const TCHAR* path, FILINFO* fno) {
  return FR_OK;
}

int main() {
  char tmp[1024];

  assert(0 == parseuint("0"));
  assert(1 == parseuint("1"));
  assert(123 == parseuint("123"));
  assert(4294967295 == parseuint("4294967295"));

  assert(!strcmp("", file_basename("")));
  assert(!strcmp("foo", file_basename("/foo")));
  assert(!strcmp("foo", file_basename("foo")));
  assert(!strcmp("test", file_basename("/foo/bar/lol/test")));

  assert(check_file_exists("/test"));
  assert(check_file_exists("/test/lol"));

  create_basepath(NULL);
  create_basepath("");
  create_basepath("/");
  create_basepath("/justafile");
  create_basepath("/path/justafile");
  create_basepath("/foo/bar/lol/test");

  file_dirname("/test/path1/path2/file", tmp);
  assert(!strcmp(tmp, "/test/path1/path2"));
  file_dirname("/", tmp);
  assert(!strcmp(tmp, ""));
  file_dirname("/file", tmp);
  assert(!strcmp(tmp, ""));

  strcpy(tmp, "/foo/bar/lol.txt");
  replace_extension(tmp, ".pdf");
  assert(!strcmp(tmp, "/foo/bar/lol.pdf"));

  strcpy(tmp, "/foo/bar/lol.txt");
  replace_extension(tmp, "");
  assert(!strcmp(tmp, "/foo/bar/lol"));

  strcpy(tmp, "/foo/bar/lol");
  replace_extension(tmp, ".doc");
  assert(!strcmp(tmp, "/foo/bar/lol.doc"));

  assert(!strcmp(find_extension("/foo/bar.lol"), "lol"));
  assert(find_extension("/foo/barlol") == NULL);
  assert(find_extension("/barlol") == NULL);
  assert(find_extension("foo") == NULL);
  assert(!strcmp(find_extension("/foo/bar."), ""));
  assert(!strcmp(find_extension("/foo/bar.lol/test.123"), "123"));
  assert(find_extension("/foo/bar.lol/beef") == NULL);

  human_size(tmp, sizeof(tmp), 0); assert(!strcmp(tmp, "1K"));
  human_size(tmp, sizeof(tmp), 100); assert(!strcmp(tmp, "1K"));
  human_size(tmp, sizeof(tmp), 1000); assert(!strcmp(tmp, "1K"));
  human_size(tmp, sizeof(tmp), 1024); assert(!strcmp(tmp, "1K"));
  human_size(tmp, sizeof(tmp), 2047); assert(!strcmp(tmp, "1K"));
  human_size(tmp, sizeof(tmp), 2048); assert(!strcmp(tmp, "2K"));
  human_size(tmp, sizeof(tmp), 1023*1024); assert(!strcmp(tmp, "1023K"));
  human_size(tmp, sizeof(tmp), 1024*1024); assert(!strcmp(tmp, "1M"));

  human_size_kb(tmp, sizeof(tmp), 0); assert(!strcmp(tmp, "<1MiB"));
  human_size_kb(tmp, sizeof(tmp), 100); assert(!strcmp(tmp, "<1MiB"));
  human_size_kb(tmp, sizeof(tmp), 1024); assert(!strcmp(tmp, "1.0MiB"));
  human_size_kb(tmp, sizeof(tmp), 1025); assert(!strcmp(tmp, "1.0MiB"));
  human_size_kb(tmp, sizeof(tmp), 1125); assert(!strcmp(tmp, "1.0MiB"));
  human_size_kb(tmp, sizeof(tmp), 1127); assert(!strcmp(tmp, "1.1MiB"));
  human_size_kb(tmp, sizeof(tmp), 1023*1024); assert(!strcmp(tmp, "1023.0MiB"));
  human_size_kb(tmp, sizeof(tmp), 1024*1024); assert(!strcmp(tmp, "1.0GiB"));

  // 2000-01-01 00:00:00 (ts: 946684800)
  const t_dec_date d1 = {.year = 0, .month = 1, .day = 1, .hour = 0, .min = 0, .sec = 0};
  assert(0 == date2timestamp(&d1));
  const t_dec_date d2 = {.year = 34, .month = 7, .day = 21, .hour = 14, .min = 12, .sec = 3};
  assert(1090419123 == date2timestamp(&d2));
  const t_dec_date d3 = {.year = 55, .month = 12, .day = 31, .hour = 23, .min = 59, .sec = 59};
  assert(1767225599 == date2timestamp(&d3));
  for (unsigned i = 0; i < 98; i++) {
    t_dec_date d = {.year = i, .month = 1, .day = 1, .hour = 0, .min = 0, .sec = 0};
    assert(86400 * ((i + 3) / 4) + 31536000 * i == date2timestamp(&d));
  }

  t_dec_date o;
  timestamp2date(0, &o);
  assert(!memcmp(&o, &d1, sizeof(d1)));
  timestamp2date(1090419123, &o);
  assert(!memcmp(&o, &d2, sizeof(d2)));
  timestamp2date(1767225599, &o);
  assert(!memcmp(&o, &d3, sizeof(d3)));

  // fixdate
  t_dec_date f1 = {.year = 100, .month = 1, .day = 1, .hour = 0, .min = 0, .sec = 0};
  fixdate(&f1); assert(f1.year == 0);
  t_dec_date f2 = {.year = -1, .month = 1, .day = 1, .hour = 0, .min = 0, .sec = 0};
  fixdate(&f2); assert(f2.year == 99);

  t_dec_date f3 = {.year = 0, .month = 1, .day = 1, .hour = 24, .min = 0, .sec = 0};
  fixdate(&f3); assert(f3.hour == 0);
  t_dec_date f4 = {.year = 0, .month = 1, .day = 1, .hour = -1, .min = 0, .sec = 0};
  fixdate(&f4); assert(f4.hour == 23);

  t_dec_date f5 = {.year = 0, .month = 1, .day = 1, .hour = 0, .min = 60, .sec = 0};
  fixdate(&f5); assert(f5.min == 0);
  t_dec_date f6 = {.year = 0, .month = 1, .day = 1, .hour = 0, .min = -1, .sec = 0};
  fixdate(&f6); assert(f6.min == 59);

  t_dec_date f7 = {.year = 0, .month = 1, .day = 1, .hour = 0, .min = 0, .sec = 60};
  fixdate(&f7); assert(f7.sec == 0);
  t_dec_date f8 = {.year = 0, .month = 1, .day = 1, .hour = 0, .min = 0, .sec = -1};
  fixdate(&f8); assert(f8.sec == 59);

  t_dec_date f9 = {.year = 0, .month = 0, .day = 1, .hour = 0, .min = 0, .sec = 0};
  fixdate(&f9); assert(f9.month == 12);
  t_dec_date f10 = {.year = 0, .month = 13, .day = 1, .hour = 0, .min = 0, .sec = 0};
  fixdate(&f10); assert(f10.month == 1);

  // day clamp, leap-year aware (year 0 leap, year 1 not)
  t_dec_date f11 = {.year = 0, .month = 2, .day = 30, .hour = 0, .min = 0, .sec = 0};
  fixdate(&f11); assert(f11.day == 1);
  t_dec_date f12 = {.year = 0, .month = 2, .day = 0, .hour = 0, .min = 0, .sec = 0};
  fixdate(&f12); assert(f12.day == 29);
  t_dec_date f13 = {.year = 1, .month = 2, .day = 0, .hour = 0, .min = 0, .sec = 0};
  fixdate(&f13); assert(f13.day == 28);
  t_dec_date f14 = {.year = 0, .month = 4, .day = 31, .hour = 0, .min = 0, .sec = 0};
  fixdate(&f14); assert(f14.day == 1);

  // memcpy32 / memset32 / memmove32
  // NB: buffers use uint32_t arrays to keep them 4-byte aligned, as required
  // by the forced 32-bit accesses in these functions (embedded, no byte loop).

  uint32_t mc_src[4] = {1, 2, 3, 4};
  uint32_t mc_dst[4] = {0, 0, 0, 0};
  memcpy32(mc_dst, mc_src, 16);
  assert(!memcmp(mc_dst, mc_src, 16));

  uint32_t mc_dst2[4] = {0xaa, 0xaa, 0xaa, 0xaa};
  memcpy32(mc_dst2, mc_src, 8);  // partial: only first 2 words
  assert(mc_dst2[0] == 1 && mc_dst2[1] == 2);
  assert(mc_dst2[2] == 0xaa && mc_dst2[3] == 0xaa);

  uint32_t ms_dst[4] = {0, 0, 0, 0};
  memset32(ms_dst, 0xdeadbeef, 16);
  assert(ms_dst[0] == 0xdeadbeef && ms_dst[1] == 0xdeadbeef &&
         ms_dst[2] == 0xdeadbeef && ms_dst[3] == 0xdeadbeef);

  uint32_t ms_dst2[4] = {0, 0, 0, 0};
  memset32(ms_dst2, 0x11111111, 8);  // partial: only first 2 words
  assert(ms_dst2[0] == 0x11111111 && ms_dst2[1] == 0x11111111);
  assert(ms_dst2[2] == 0 && ms_dst2[3] == 0);

  // memmove32 forward copy (dst < src, overlapping)
  uint32_t mv1[8] = {1, 2, 3, 4, 5, 6, 7, 8};
  memmove32(&mv1[0], &mv1[2], 24);  // shift left by 2 words
  uint32_t mv1_exp[8] = {3, 4, 5, 6, 7, 8, 7, 8};
  assert(!memcmp(mv1, mv1_exp, sizeof(mv1)));

  // memmove32 backward copy (dst > src, overlapping)
  uint32_t mv2[8] = {1, 2, 3, 4, 5, 6, 7, 8};
  memmove32(&mv2[2], &mv2[0], 24);  // shift right by 2 words
  uint32_t mv2_exp[8] = {1, 2, 1, 2, 3, 4, 5, 6};
  assert(!memcmp(mv2, mv2_exp, sizeof(mv2)));

  // memmove32 same pointer: no-op
  uint32_t mv3[4] = {1, 2, 3, 4};
  memmove32(mv3, mv3, 16);
  uint32_t mv3_exp[4] = {1, 2, 3, 4};
  assert(!memcmp(mv3, mv3_exp, sizeof(mv3)));

  // memmove32 non-overlapping
  uint32_t mv4_src[4] = {9, 8, 7, 6};
  uint32_t mv4_dst[4] = {0, 0, 0, 0};
  memmove32(mv4_dst, mv4_src, 16);
  assert(!memcmp(mv4_dst, mv4_src, 16));

  // memmove32 with non-word-multiple count: truncated down to nearest 4
  uint32_t mv5[4] = {1, 2, 3, 4};
  memmove32(&mv5[1], &mv5[0], 15);  // count -> 12, i.e. 3 words
  uint32_t mv5_exp[4] = {1, 1, 2, 3};
  assert(!memcmp(mv5, mv5_exp, sizeof(mv5)));
}


