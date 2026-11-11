// Fuzzes the patch parsers: .patch files (unserialize_patch, patchengine.c)
// and the patch database lookup (patchmem_lookup, patcher.c), which can be
// loaded from the SD card.
#include <assert.h>
#include <string.h>
#include "common.h"
#include "patchengine.h"
#include "fuzz.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  static uint8_t db[PATCHDB_MAX_SIZE];
  t_patch p;

  if (unserialize_patch(data, size, &p))
    assert(p.wcnt_ops + p.save_ops + p.irqh_ops + p.rtc_ops <= MAX_PATCH_OPS);

  // As a database: the area is PATCHDB_MAX_SIZE bytes, the rest is zeros.
  size_t n = size < sizeof(db) ? size : sizeof(db);
  memcpy(db, data, n);
  memset(&db[n], 0, sizeof(db) - n);

  // Look up the first game of the first index group, so the entry code runs
  // too: group = code0, code1, version, count; then code2, code3 per game.
  uint32_t idxcnt;
  memcpy(&idxcnt, &db[12], sizeof(idxcnt));
  size_t idx2 = 1024 + (size_t)(idxcnt % (PATCHDB_MAX_SIZE / 4)) * 4;
  uint8_t gamecode[5] = { db[1024], db[1025], 0, 0, db[1026] };
  if (idx2 + 2 <= sizeof(db))
    memcpy(&gamecode[2], &db[idx2], 2);
  if (patchmem_lookup(gamecode, db, &p))
    assert(p.wcnt_ops + p.save_ops + p.irqh_ops + p.rtc_ops <= MAX_PATCH_OPS);
  return 0;
}
