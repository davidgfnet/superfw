
const uint8_t logo_img[7][31] = {
 {2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,1,1,1,1,1,1,1,1,1,1,1},
 {2,2,0,0,2,0,2,0,2,0,0,2,2,0,0,2,0,0,2,2,1,0,0,0,1,0,1,1,1,0,1},
 {2,0,2,2,2,0,2,0,2,0,2,0,2,0,2,2,0,2,0,2,1,0,1,1,1,0,1,1,1,0,1},
 {2,0,0,0,2,0,2,0,2,0,2,0,2,0,0,2,0,2,0,2,1,0,0,1,1,0,1,0,1,0,1},
 {2,2,2,0,2,0,2,0,2,0,0,2,2,0,2,2,0,0,2,2,1,0,1,1,1,0,1,0,1,0,1},
 {2,0,0,2,2,2,0,0,2,0,2,2,2,0,0,2,0,2,0,2,1,0,1,1,1,1,0,1,0,1,1},
 {2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,1,1,1,1,1,1,1,1,1,1,1},
};
const uint16_t logo_pal[3] = {
  0x7fff,0x0013,0x0000
};

static inline void init_logo_palette(volatile uint16_t *pal) {
  for (unsigned i = 0; i < 3; i++)
    pal[i] = logo_pal[i];
}

// Scale must be multiple of 2
static void render_logo(volatile uint16_t *frame, unsigned x, unsigned y, unsigned scale) {
  // Render logo in the center, serves as loading screen
  const unsigned logow = sizeof(logo_img[0])/sizeof(logo_img[0][0]);
  const unsigned logoh = sizeof(logo_img)/sizeof(logo_img[0]);
  const unsigned logoy = y - (logoh*scale) / 2;
  const unsigned logox = x - (logow*scale) / 2;
  for (unsigned i = 0; i < logoh; i++) {
    for (unsigned j = 0; j < logow; j++) {
      for (unsigned m = 0; m < scale; m++)
      for (unsigned n = 0; n < scale; n+=2)
        frame[((logoy + i*scale + m) * SCREEN_WIDTH + logox + j*scale + n)/2] = dup8(logo_img[i][j] + 1);
    }
  }
}

