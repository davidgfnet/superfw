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

  // Look up the game of the first index entry, so the entry code runs too.
  uint8_t gamecode[5];
  memcpy(gamecode, &db[1024], sizeof(gamecode));
  if (patchmem_lookup(gamecode, db, &p))
    assert(p.wcnt_ops + p.save_ops + p.irqh_ops + p.rtc_ops <= MAX_PATCH_OPS);
  return 0;
}
