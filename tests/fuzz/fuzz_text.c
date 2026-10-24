// Fuzzes the utf-8 and name helpers used on file names and text read from the
// SD card (utf_util.c, carousel_util.c). Inputs are NUL terminated strings of
// up to 255 bytes, like file names.
#include <assert.h>
#include <string.h>
#include "utf_util.h"
#include "carousel_util.h"
#include "util.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  char a[MAX_FN_LEN], b[MAX_FN_LEN], out[MAX_FN_LEN];
  uint16_t s16[MAX_FN_LEN * 2];

  // Split the input in two names.
  size_t half = size / 2;
  size_t la = half < MAX_FN_LEN - 1 ? half : MAX_FN_LEN - 1;
  size_t lb = (size - half) < MAX_FN_LEN - 1 ? (size - half) : MAX_FN_LEN - 1;
  memcpy(a, data, la);
  a[la] = 0;
  memcpy(b, &data[half], lb);
  b[lb] = 0;

  assert(utf8_strlen(a) <= strlen(a));
  sortable_utf8_u16(a, s16);
  carousel_namecmp(a, b);
  carousel_letter_key(a);
  carousel_title(a, out);
  assert(strlen(out) <= strlen(a));
  path_join(out, a, b, "/");
  rom_path_ok(a);
  return 0;
}
