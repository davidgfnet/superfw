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

#ifndef _FONT_RENDER_H_
#define _FONT_RENDER_H_

// Size (in bytes) of the font graphs DB
unsigned font_block_size();

// Returns the number of unicode characters in an encoded utf-8 buffer.
unsigned utf8_strlen(const char *s);

// Calculates the number of width pixels that the string needs to render.
unsigned font_width(const char *s);

// Calculates how many characters (returned as utf-8 byte count) can be
// rendered in a framebuffer that's at least max_width in size.
unsigned font_width_cap(const char *s, unsigned max_width);
unsigned font_width_lcap(const char *s, unsigned max_width);
// Same but using a space limited verison (char 0x20)
unsigned font_width_cap_space(const char *s, unsigned max_width, unsigned *outwidth);

// Renders some text in a framebuffer (8bit color)
void draw_text_idx8(const char *s, uint8_t *buffer, unsigned pitch, uint8_t color);

// Same as above but with 16bit bus support (for GBA VRAM-like buffers)
void draw_text_idx8_bus16(const char *s, uint8_t *buffer, unsigned pitch, uint8_t color);
void draw_text_idx8_bus16_range(const char *s, uint8_t *buffer, unsigned skip, unsigned maxcols, unsigned pitch, uint8_t color);

#endif

