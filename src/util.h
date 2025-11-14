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

#ifndef _UTIL_H_
#define _UTIL_H_

// Performs a heap sort of a word-aligned array, with elements being exactly
// `size` words in size.
void heapsort4(
  void *vbase, unsigned nmemb, unsigned size,
  int (*compar)(const void *, const void *)
);

// String misc routines
const char *file_basename(const char *fullpath);
void file_dirname(const char *fullpath, char *dirname);
void replace_extension(char *fn, const char *newext);
const char *find_extension(const char *s);

unsigned parseuint(const char *s);
void human_size(char *s, unsigned ml, uint32_t sz);
void human_size_kb(char *s, unsigned ml, uint32_t sz);

// Date conversion/management routines.
typedef struct {
  int8_t year, month, day, hour, min, sec;
} t_dec_date;
uint32_t date2timestamp(const t_dec_date *d);
void timestamp2date(uint32_t ts, t_dec_date *out);
void fixdate(t_dec_date *d);

// Just checks that a file exists.
bool check_file_exists(const char *fn);

// Creates a path to a file (recursively if needed)
void create_basepath(const char *fn);

// Parses a LE 32 bit unsigned int
static inline uint32_t parse32le(const uint8_t *d) {
  return (d[0]) | (d[1] << 8) | (d[2] << 16) | (d[3] << 24);
}

// Aligned copying routines (for SDRAM and similar RAMs)
void memcpy32(void *restrict dst, const void *restrict src, unsigned count);

// Aligned memory move. Support any overlap
void memmove32(void *dst, void *src, unsigned count);

// Aligned memory set.
void memset32(void *dst, uint32_t value, unsigned count);

#endif

