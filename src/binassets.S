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

// Binary assets (uncompressed) used in GBA mode
// These are kept here and compressed along with the firmware due to their
// small size and better compression ratio when kept toghether (due to dupe code).

#define DEF_ASSET(name, file)   \
  .global name;                 \
  .balign 4;                    \
  name:                         \
  .incbin file;                 \
  name##_end:                   \
  .balign 4;                    \
  .balign 4;                    \
  name##_size:                  \
    .word (name##_end - name);  \
  .global name##_size;

.text
.balign 4

// In-game menu assets
DEF_ASSET(ingame_menu_payload, "ingamemenu.payload")

// Direct save payload
DEF_ASSET(directsave_payload, "directsave.payload")

// DLDI payload (builtin!)
DEF_ASSET(dldi_payload, "superfw.dldi.payload")

// IGM trampoline gadget for NOR flashed games
#ifdef SUPPORT_NORGAMES
  DEF_ASSET(ingame_trampoline_payload, "ingame_trampoline.payload")
#endif


