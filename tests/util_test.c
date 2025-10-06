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

  assert(!strcmp(find_extension("/foo/bar.lol"), ".lol"));
  assert(find_extension("/foo/barlol") == NULL);
  assert(find_extension("/barlol") == NULL);
  assert(find_extension("foo") == NULL);
  assert(!strcmp(find_extension("/foo/bar."), "."));
  assert(!strcmp(find_extension("/foo/bar.lol/test.123"), ".123"));
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

  // TODO test memcpy32 and memmove32
}


