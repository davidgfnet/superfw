/*
 * Cover-art / title-screen preview for the ROM browser.  See coverart.h.
 *
 * Reads "/IMGS/{c0}/{c1}/{CODE}.bmp" (120x80, 16bpp X1R5G5B5) directly off the SD
 * card, maps each pixel to a fixed 6x6x6 palette cube (MEM_PALETTE[20..235]) and
 * caches the resulting 8bpp image for fast per-frame blits.
 */
#include <string.h>
#include <stdbool.h>

#include "gbahw.h"
#include "fatfs/ff.h"
#include "common.h"
#include "nanoprintf.h"
#include "coverart.h"

#define COVER_DIR  "/IMGS"

// Big buffers go in EWRAM (.sbss); the default .bss lives in scarce IWRAM.
#define EWRAM_BSS  __attribute__((section(".sbss")))

static EWRAM_BSS __attribute__((aligned(4))) uint8_t cover_pix[COVER_W * COVER_H];
static EWRAM_BSS char cover_key[512];     // ROM path the current state belongs to
static bool     cover_have;               // a valid cover is loaded (.bss/IWRAM -> zeroed)
static uint16_t cube_pal[CUBE_NCOLORS];   // the fixed color cube (GBA BGR555)
static bool     cube_built;

// Build the 6x6x6 cube once. Each channel uses 6 evenly spread 5-bit levels.
static void build_cube(void) {
  static const uint8_t lvl[6] = { 0, 6, 12, 19, 25, 31 };
  for (unsigned r = 0; r < 6; r++)
    for (unsigned g = 0; g < 6; g++)
      for (unsigned b = 0; b < 6; b++)
        cube_pal[r * 36 + g * 6 + b] = (lvl[b] << 10) | (lvl[g] << 5) | lvl[r];
  cube_built = true;
}

// Map a BMP 16-bit pixel to its nearest cube index (already biased by base).
// The EZ-Flash-Omega pack stores pixels GBA-native (X1B5G5R5): red is the LOW
// 5 bits, blue the high 5 bits (not the standard X1R5G5B5 BMP layout).
static inline uint8_t rgb555_to_cube(unsigned v) {
  unsigned r = v & 0x1F, g = (v >> 5) & 0x1F, b = (v >> 10) & 0x1F;
  return CUBE_PAL_BASE + (((r * 6) >> 5) * 36 + ((g * 6) >> 5) * 6 + ((b * 6) >> 5));
}

static bool gcode_is_alnum(const uint8_t *c) {
  for (unsigned i = 0; i < 4; i++) {
    uint8_t ch = c[i];
    if (!((ch >= '0' && ch <= '9') ||
          (ch >= 'A' && ch <= 'Z') ||
          (ch >= 'a' && ch <= 'z')))
      return false;
  }
  return true;
}

static bool load_cover_file(const uint8_t gcode[4]) {
  char path[64];
  npf_snprintf(path, sizeof(path), "%s/%c/%c/%c%c%c%c.bmp",
               COVER_DIR, gcode[0], gcode[1],
               gcode[0], gcode[1], gcode[2], gcode[3]);

  FIL fd;
  if (FR_OK != f_open(&fd, path, FA_READ))
    return false;

  bool ok = false;
  UINT rd;
  uint8_t hdr[54];

  if (FR_OK == f_read(&fd, hdr, sizeof(hdr), &rd) && rd == sizeof(hdr) &&
      hdr[0] == 'B' && hdr[1] == 'M') {
    uint32_t dataoff = hdr[10] | (hdr[11] << 8) | (hdr[12] << 16) | (hdr[13] << 24);
    int32_t  width   = hdr[18] | (hdr[19] << 8) | (hdr[20] << 16) | (hdr[21] << 24);
    int32_t  rawh    = hdr[22] | (hdr[23] << 8) | (hdr[24] << 16) | (hdr[25] << 24);
    unsigned bpp     = hdr[28] | (hdr[29] << 8);
    bool topdown = rawh < 0;
    int32_t height = topdown ? -rawh : rawh;

    if (bpp == 16 && width > 0 && width <= COVER_W &&
        height > 0 && height <= COVER_H && FR_OK == f_lseek(&fd, dataoff)) {
      if (!cube_built)
        build_cube();

      // Pad letterbox (smaller images) with cube index 0 (= black).
      memset(cover_pix, CUBE_PAL_BASE, sizeof(cover_pix));

      unsigned rowbytes = ((unsigned)width * 2 + 3) & ~3u;   // 4-byte aligned rows
      uint8_t rowbuf[COVER_W * 2];
      ok = true;
      for (int sy = 0; sy < height; sy++) {
        if (FR_OK != f_read(&fd, rowbuf, rowbytes, &rd) || rd != rowbytes) {
          ok = false;
          break;
        }
        unsigned dy = topdown ? (unsigned)sy : (unsigned)(height - 1 - sy);
        uint8_t *dst = &cover_pix[dy * COVER_W];
        for (int x = 0; x < width; x++)
          dst[x] = rgb555_to_cube(rowbuf[x * 2] | (rowbuf[x * 2 + 1] << 8));
      }

      if (ok)
        dma_memcpy16(&MEM_PALETTE[CUBE_PAL_BASE], cube_pal, CUBE_NCOLORS);
    }
  }

  f_close(&fd);
  return ok;
}

void coverart_invalidate(void) {
  cover_key[0] = 0;
  cover_have = false;
}

void coverart_update(const char *rom_fullpath, uint32_t filesize, bool is_gba) {
  // No-op while the selection hasn't moved (avoids re-reading the SD card).
  if (0 == strncmp(cover_key, rom_fullpath, sizeof(cover_key) - 1))
    return;

  strncpy(cover_key, rom_fullpath, sizeof(cover_key) - 1);
  cover_key[sizeof(cover_key) - 1] = 0;
  cover_have = false;

  if (!is_gba)
    return;

  t_rom_header romh;
  if (0 != preload_gba_rom(rom_fullpath, filesize, &romh))
    return;

  if (gcode_is_alnum(romh.gcode))
    cover_have = load_cover_file(romh.gcode);
}

void coverart_update_gcode(const char *cachekey, const uint8_t gcode[4]) {
  if (0 == strncmp(cover_key, cachekey, sizeof(cover_key) - 1))
    return;

  strncpy(cover_key, cachekey, sizeof(cover_key) - 1);
  cover_key[sizeof(cover_key) - 1] = 0;
  cover_have = false;

  if (gcode_is_alnum(gcode))
    cover_have = load_cover_file(gcode);
}

bool coverart_available(void) {
  return cover_have;
}

void coverart_draw(volatile uint8_t *frame) {
  if (!cover_have)
    return;
  // Re-assert our palette every frame: the logo (info tab) shares the
  // MEM_PALETTE[20..235] range and may have overwritten the cube.
  dma_memcpy16(&MEM_PALETTE[CUBE_PAL_BASE], cube_pal, CUBE_NCOLORS);
  for (unsigned r = 0; r < COVER_H; r++)
    dma_memcpy16(&frame[(COVER_PANE_Y + r) * 240 + COVER_PANE_X],
                 &cover_pix[r * COVER_W], COVER_W / 2);
}
