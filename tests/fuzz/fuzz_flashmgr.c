// Fuzzes the NOR (flash) game list loader (flashmgr_load, flash_mgr.c). The
// fuzz data plays the flash metadata area.
#include <assert.h>
#include <string.h>
#include "common.h"
#include "flash_mgr.h"
#include "fuzz.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  static t_reg_entry_max nd;
  fz_data = data;
  fz_size = size;

  if (flashmgr_load(ROM_FLASHMETA_ADDR, FLASH_METADATA_SIZE, (t_reg_entry*)&nd)) {
    assert(nd.gamecnt <= FLASHG_MAXFN_CNT);
    for (unsigned i = 0; i < nd.gamecnt; i++) {
      const t_flash_game_entry *e = &nd.games[i];
      const char *end = memchr(e->game_name, 0, sizeof(e->game_name));
      assert(end && e->bnoffset <= end - e->game_name);
      for (unsigned j = 0; j < MAX_GAME_BLOCKS; j++)
        assert(e->blkmap[j] < NOR_BLOCK_COUNT);
    }
  }
  return 0;
}
