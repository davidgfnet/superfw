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

#include "utf_util.h"
#include "nanoprintf.h"

const char *file_basename(const char *fullpath) {
  const char * ret = strrchr(fullpath, '/');
  if (!ret)
    return fullpath;
  return &ret[1];
}

void file_dirname(const char *fullpath, char *dirname) {
  strcpy(dirname, fullpath);
  char *p = strrchr(dirname, '/');
  if (p)
    *p = 0;
}

// Returns a pointer to the "." for a file name extension.
const char *find_extension(const char *s) {
  const char *p = &s[strlen(s)];
  while (p != s) {
    if (*p == '/')
      return NULL;   // Has no extension!
    else if (*p == '.')
      return p;
    p--;
  }
  return NULL;     // Has no extension, nor a path
}

void replace_extension(char *fn, const char *newext) {
  // Change or append the extension (if it has none)
  const char *p = find_extension(fn);
  if (p)
    *(char*)p = 0;    // Truncate file at extension

  // Just append .sav since we could not replace the extension.
  strcat(fn, newext);
}

unsigned parseuint(const char *s) {
  unsigned ret = 0;
  while (*s)
    ret = ret * 10 + (*s++ - '0');

  return ret;
}

void human_size(char *s, unsigned ml, uint32_t sz) {
  if (sz < 1024)
    memcpy(s, "1K", 3);
  else if (sz < 1024*1024)
    npf_snprintf(s, ml, "%uK", (unsigned int)(sz >> 10));
  else
    npf_snprintf(s, ml, "%uM", (unsigned int)(sz >> 20));
}

void human_size_kb(char *s, unsigned ml, uint32_t sz) {
  if (sz < 1024)
    memcpy(s, "<1MiB", 6);
  else if (sz < 1024*1024)
    npf_snprintf(s, ml, "%u.%uMiB", (unsigned int)(sz >> 10), (unsigned int)((sz & 0x3ff) * 10) >> 10);
  else
    npf_snprintf(s, ml, "%u.%uGiB", (unsigned int)(sz >> 20), (unsigned int)((sz & 0xfffff) * 10) >> 20);
}

void memcpy32(void *restrict dst, const void *restrict src, unsigned count) {
  uint32_t *dst32 = (uint32_t*)dst;
  uint32_t *src32 = (uint32_t*)src;
  for (unsigned i = 0; i < count; i+=4)
    *dst32++ = *src32++;
}

void memset32(void * dst, uint32_t value, unsigned count) {
  uint32_t *dst32 = (uint32_t*)dst;
  for (unsigned i = 0; i < count; i+=4)
    *dst32++ = value;
}

void memmove32(void *dst, void *src, unsigned count) {
  // Move forward/backwards depending on the pointer overlap.
  if (dst == src)
    return;

  // Round up to uint32 sized.
  count = count & ~3U;

  if ((uintptr_t)dst < (uintptr_t)src) {
    // Copy regularly, the dest buffer is *before* the src one.
    uint32_t *dst32 = (uint32_t*)dst;
    uint32_t *src32 = (uint32_t*)src;
    for (unsigned i = 0; i < count; i+=4)
      *dst32++ = *src32++;
  } else {
    // Copy backwards, otherwise we destroy the src buffer.
    uint32_t *dst32 = (uint32_t*)(((uint8_t*)dst) + count);
    uint32_t *src32 = (uint32_t*)(((uint8_t*)src) + count);
    for (unsigned i = 0; i < count; i+=4)
      *(--dst32) = *(--src32);
  }
}


