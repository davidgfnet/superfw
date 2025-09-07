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

// Minimal C implementations for compactness. These functions should not
// be in the critical path of any useful stuff.

#include <stddef.h>

#include "compiler.h"

#ifndef BUILTIN_PREFIX
#define BUILTIN_PREFIX
#endif

#define CONCAT2(a, b) a ## b
#define CONCAT(a, b) CONCAT2(a, b)
#define FNAME(x) CONCAT(BUILTIN_PREFIX, x)

NOINLINE
int FNAME(strcmp)(const char *a, const char *b) {
  while (*a && (*a == *b)) {
    a++;
    b++;
  }
  return *a - *b;
}

NOINLINE
int FNAME(strncmp)(const char *a, const char *b, unsigned n) {
  while (n && *a && (*a == *b)) {
    n--; a++, b++;
  }
  if (!n)
    return 0;
  return *a - *b;
}

NOINLINE
char *FNAME(strchr)(const char *s, int c) {
  do {
    if (*s == (char)c)
      return (char *)s;
  } while (*s++);
  return 0;
}

NOINLINE
char *FNAME(strrchr)(const char *s, int c) {
  const char *last = 0;
  do {
    if (*s == (char)c)
      last = s;
  } while (*s++);
  return (char *)last;
}

NOINLINE
char *FNAME(strcat)(char *dest, const char *src) {
  char *p = dest;
  while (*p)
    p++;
  while ((*p++ = *src++))
    {}

  return dest;
}

NOINLINE
size_t FNAME(strlen)(const char *s) {
  size_t ret = 0;
  while (*s++)
    ret++;
  return ret;
}


