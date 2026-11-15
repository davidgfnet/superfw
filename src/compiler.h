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

// On GBA, place this code on the IWRAM in ARM mode for speed.
// Word-aligned buffers are faster than unaligned ones.
#ifdef __GBA__
  #define ARM_CODE     __attribute__((target("arm")))
  #define IWRAM_CODE   __attribute__((section(".iwram_code"), long_call))
  #define IWRAM_RODATA __attribute__((section(".iwram_rodata")))
#else
  #define ARM_CODE
  #define IWRAM_CODE
  #define IWRAM_RODATA
#endif

#define NOINLINE __attribute__((noinline))
#define EXTERNAL __attribute__((used, externally_visible))
