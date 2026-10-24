// Fuzzes the cheat file parser (open_read_cheats, cheats.c).
#include <assert.h>
#include <string.h>
#include "cheats.h"
#include "fuzz.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  static uint32_t buf[32 * 1024 / 4];
  fz_data = data;
  fz_size = size;

  int ret = open_read_cheats((uint8_t*)buf, sizeof(buf), "x.cht");
  if (ret < 0)
    return 0;

  // The output must be a well formed list that fits in what was returned.
  assert((unsigned)ret <= sizeof(buf));
  const uint8_t *p = (const uint8_t*)buf;
  unsigned off = 4, n = buf[0];
  for (unsigned i = 0; i < n; i++) {
    const t_cheathdr *h = (const t_cheathdr*)&p[off];
    assert(off + sizeof(t_cheathdr) <= (unsigned)ret);
    assert(h->slen && !(h->slen & 3) && h->codelen);
    assert(memchr(h->data, 0, h->slen));               // Title is terminated
    off += sizeof(t_cheathdr) + h->slen + h->codelen;
    assert(off <= (unsigned)ret);
  }
  assert(off == (unsigned)ret);
  return 0;
}
