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

// Support for various GBA emulators that require some minimal header/rom list.

#include <string.h>

#include "emu.h"
#include "util.h"

typedef struct {
  char title[32];
  uint32_t romsize;
  uint32_t flags1;
  uint32_t flags2;
  uint32_t padding;
} t_pocketnes_header;

unsigned pocket_nes_header(uint8_t *buffer, const char *fn, unsigned fs) {
  // Copy the base filename into the name buffer
  t_pocketnes_header hdr;
  const char *bname = file_basename(fn);

  memset(&hdr, 0, sizeof(hdr));
  memcpy(hdr.title, bname, sizeof(hdr.title) - 1);
  hdr.romsize = fs;
  memcpy32(buffer, &hdr, sizeof(hdr));

  return sizeof(hdr);
}

const t_emu_loader nes_loaders[] = {
  { "pocketnes",  pocket_nes_header },
  { NULL, NULL },
};

typedef struct {
  uint32_t ident;
  uint32_t romsize;
  uint32_t flags1;
  uint32_t flags2;
  uint32_t padding[4];
  char title[32];
} t_smsadv_header;

unsigned smsadvance_header(uint8_t *buffer, const char *fn, unsigned fs) {
  // Copy the base filename into the name buffer
  t_smsadv_header hdr;
  const char *bname = file_basename(fn);

  memset(&hdr, 0, sizeof(hdr));
  memcpy(hdr.title, bname, sizeof(hdr.title) - 1);
  hdr.romsize = fs;
  hdr.ident = 0x1A534D53;   // SMS + 0x1A (little endian)
  memcpy32(buffer, &hdr, sizeof(hdr));

  return sizeof(hdr);
}

typedef struct {
  uint8_t rid;
  uint8_t pad1[5];
  uint8_t flags;
  uint8_t pad2;
  uint8_t ggmode;
  uint8_t pad3[3];
  char title[28];
} t_drsms_header;

static unsigned drsms_header(uint8_t *buffer, const char *fn, unsigned fs, uint8_t mode) {
  t_drsms_header hdr = {
    .rid = 1,
    .pad1 = {0},
    .flags = 0,
    .pad2 = 0,
    .ggmode = mode,
    .pad3 = {0},
  };
  // Copy the base filename into the name buffer
  const char *bname = file_basename(fn);

  memcpy(hdr.title, bname, sizeof(hdr.title) - 1);
  memcpy32(buffer, &hdr, sizeof(hdr));

  return sizeof(hdr);
}

unsigned drsms_header_gg(uint8_t *buffer, const char *fn, unsigned fs) {
  return drsms_header(buffer, fn, fs, 1);
}

unsigned drsms_header_sms(uint8_t *buffer, const char *fn, unsigned fs) {
  return drsms_header(buffer, fn, fs, 0);
}

const t_emu_loader sms_loaders[] = {
  { "drsms", drsms_header_sms },
  { "smsadvance", smsadvance_header },
  { NULL, NULL },
};

const t_emu_loader gg_loaders[] = {
  { "drsms", drsms_header_gg },
  { "smsadvance", smsadvance_header },
  { NULL, NULL },
};

const t_emu_loader sg_loaders[] = {
  { "smsadvance", smsadvance_header },
  { NULL, NULL },
};

typedef struct {
  uint32_t ident;
  uint32_t romsize;
  uint32_t flags;
  uint32_t undef;
  uint32_t isbios;
  uint32_t padding[3];
  char title[32];
} t_wasabigba_header;

unsigned wasabigba_header(uint8_t *buffer, const char *fn, unsigned fs) {
  // Copy the base filename into the name buffer
  t_wasabigba_header hdr;
  const char *bname = file_basename(fn);

  memset(&hdr, 0, sizeof(hdr));
  memcpy(hdr.title, bname, sizeof(hdr.title) - 1);
  hdr.romsize = fs;
  hdr.ident = 0x1A565357;   // WSV + 0x1A (little endian)
  memcpy32(buffer, &hdr, sizeof(hdr));

  return sizeof(hdr);
}

const t_emu_loader sv_loaders[] = {
  { "wasabigba", wasabigba_header },
  { NULL, NULL },
};

typedef struct {
  uint32_t ident;
  uint32_t romsize;
  uint32_t flags;
  uint32_t undef;
  uint8_t isbios;
  uint8_t padding[15];
  char title[32];
} t_ngpgba_header;

unsigned ngpgba_header(uint8_t *buffer, const char *fn, unsigned fs) {
  // Copy the base filename into the name buffer
  t_ngpgba_header hdr;
  const char *bname = file_basename(fn);

  memset(&hdr, 0, sizeof(hdr));
  memcpy(hdr.title, bname, sizeof(hdr.title) - 1);
  hdr.romsize = fs;
  hdr.ident = 0x1A50474E;   // PGN + 0x1A (little endian)
  memcpy32(buffer, &hdr, sizeof(hdr));

  return sizeof(hdr);
}

const t_emu_loader ngc_loaders[] = {
  { "ngpgba", ngpgba_header },
  { NULL, NULL },
};

typedef struct {
  char title[32];
  uint32_t romsize;
  uint32_t flags;
  uint32_t spriteflags;
  uint32_t padding;
  // Fake header pretending to pass as a NES ROM
  uint32_t nes_sig;
  char padding2[12];
} t_pceadvance_header;

unsigned pceadvance_header(uint8_t *buffer, const char *fn, unsigned fs) {
  // Probably some bad copy paste made them parse the ROM as a NES ROM.
  // We add a "fake" header to the ROM.
  t_pceadvance_header hdr = {
    .title = "",
    .romsize = fs + 16,
    .flags = 0x4,            // Should be zero for Japanese ROMs? They work anyway?
    .spriteflags = 0,
    .padding = 0,
    .nes_sig = 0x1A53454E,
    .padding2 = "@          ",
  };
  // Copy the base filename into the name buffer
  const char *bname = file_basename(fn);
  memcpy(hdr.title, bname, sizeof(hdr.title) - 1);

  memcpy32(buffer, &hdr, sizeof(hdr));
  return sizeof(hdr);
}

const t_emu_loader pce_loaders[] = {
  { "pceadvance", pceadvance_header },
  { NULL, NULL },
};

const t_emu_loader gbc_loaders[] = {
  { "gbc-emu", NULL },
  { "vfs:GCEM", NULL },
  { NULL, NULL },
};

// Emulator loader table. Add entries here!

const t_emu_platform emu_platforms[] = {
  {"gb",  gbc_loaders},
  {"gbc", gbc_loaders},
  {"nes", nes_loaders},
  {"sms", sms_loaders},
  {"gg",  gg_loaders},
  {"sg",  sg_loaders},
  {"sv",  sv_loaders},
  {"ngc", ngc_loaders},
  {"pce", pce_loaders},
  {NULL, NULL},        // End marker!
};

