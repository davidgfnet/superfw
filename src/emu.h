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

#ifndef __EMU__H__
#define __EMU__H__

#include <stdint.h>

typedef unsigned(*t_loader_handler)(uint8_t *buffer, const char *fn, unsigned fs);

typedef struct {
  const char *emu_name;                // Emulator (file) name
  const t_loader_handler hndlr;        // Handler function that loads any necessary header.
} t_emu_loader;

typedef struct {
  const char *extension;               // File extension
  const t_emu_loader *loaders;         // List of loaders
} t_emu_platform;

const t_emu_loader * get_emu_info(const char *ext);

// Emulated systems, grouped by emulator (ie. gb and gbc share one). Group of
// an extension (-1 if it is not emulated), and its name ("GB/GBC").
int emu_group(const char *ext);
void emu_group_name(int group, char *out, unsigned size);

#endif
