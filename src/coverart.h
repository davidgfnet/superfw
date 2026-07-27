/*
 * Cover-art / title-screen preview for the ROM browser.
 *
 * Reads EZ-Flash-Omega style thumbnails directly from the SD card:
 *   /IMGS/{c0}/{c1}/{CODE}.bmp  (120x80, 16bpp X1R5G5B5 BMP, keyed by GBA game code)
 * Each pixel is mapped on the fly to a fixed 6x6x6 (216 color) palette cube that
 * lives in the free BG palette indices 20..235, and blitted into a bottom-right
 * pane of the Mode-4 menu framebuffer (EZ-Flash-Omega style).
 */
#ifndef __COVERART_H__
#define __COVERART_H__

#include <stdint.h>
#include <stdbool.h>

// Native thumbnail size (matches the EZ-Omega .bmp pack).
#define COVER_W          120
#define COVER_H          80

// Fixed 6x6x6 color cube, placed in the free BG palette range 20..235
// (theme=16..19, logo=1..15, IGM=240..244, selector=255 are left untouched).
#define CUBE_PAL_BASE    20
#define CUBE_NCOLORS     216

// Bottom-right pane, just above the y=144 footer bar.
#define COVER_PANE_X     (240 - COVER_W - 2)    // 118
#define COVER_PANE_Y     (144 - COVER_H - 2)    // 62

// Forget the cached cover (call when the directory listing is rebuilt or the
// feature is toggled off).
void coverart_invalidate(void);

// Ensure the cover for the currently selected ROM is loaded. Only touches the
// SD card when the selection actually changed, so it is cheap to call per frame.
// Pass is_gba=false (or an empty path) to clear the cover for non-ROM entries.
void coverart_update(const char *rom_fullpath, uint32_t filesize, bool is_gba);

// Like coverart_update but keyed directly by a stored 4-char game code (no ROM
// header read) -- used for NOR/flash games. `cachekey` is any stable string
// unique to the entry; the SD card is only touched when it changes.
void coverart_update_gcode(const char *cachekey, const uint8_t gcode[4]);

// Whether a cover is currently loaded and should be drawn.
bool coverart_available(void);

// Blit the loaded cover into the bottom-right pane of `frame`.
void coverart_draw(volatile uint8_t *frame);

#endif
