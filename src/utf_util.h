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

#include <stdint.h>

// Decodes the pointed string and returns the number of bytes it takes.
unsigned utf8_chlen(const char *s);

// Returns the number of unicode characters in an encoded utf-8 buffer.
unsigned utf8_strlen(const char *s);

// Decodes a utf-8 character into its u32 respresentation
uint32_t utf8_decode(const char *s);

// Convert string to searchable structures
void sortable_utf8_u16(const char *s8, uint16_t *s16);

// Maps a code point to its sorting order (transliterated, case insensitive)
unsigned unicodeorder(unsigned cp);

