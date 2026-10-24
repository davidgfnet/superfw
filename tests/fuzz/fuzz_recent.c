// Fuzzes the recently played list parser (recent_load, recent.c).
#include <assert.h>
#include <string.h>
#include "recent.h"
#include "fuzz.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  static t_rentry entries[RECENT_MAXFN_CNT];
  fz_data = data;
  fz_size = size;

  memset(entries, 0xA5, sizeof(entries));      // Catch unterminated paths
  unsigned n = recent_load("recent.txt", entries);
  assert(n <= RECENT_MAXFN_CNT);
  for (unsigned i = 0; i < n; i++) {
    const char *end = memchr(entries[i].fpath, 0, sizeof(entries[i].fpath));
    assert(end);
    assert(entries[i].fname_offset <= (unsigned)(end - entries[i].fpath));
  }
  return 0;
}
