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
#include <string.h>

#include "common.h"

typedef struct {
  char fn[4];
  uint32_t size;
  uint8_t payload[];
} vf_header;

const void *get_vfile_ptr(const char *fname) {
  const vf_header *ptr = (vf_header*)ROM_ASSETS_U8;
  while (ptr->size) {
    if (!memcmp(ptr->fn, fname, sizeof(ptr->fn)))
      return ptr->payload;
    ptr = (vf_header*)&ptr->payload[ROUND_UP2(ptr->size, 4)];
  }

  return NULL;
}

int get_vfile_size(const char *fname) {
  const vf_header *ptr = (vf_header*)ROM_ASSETS_U8;
  while (ptr->size) {
    if (!memcmp(ptr->fn, fname, sizeof(ptr->fn)))
      return (int)ptr->size;
    ptr = (vf_header*)&ptr->payload[ROUND_UP2(ptr->size, 4)];
  }

  return -1;
}

