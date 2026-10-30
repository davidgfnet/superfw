// Fuzzes the text file line reader (file_read_lines, fileutil.c), used for
// favorites.txt, playstats.txt and titles.txt on the SD card.
#include <assert.h>
#include <string.h>
#include "util.h"
#include "fuzz.h"

static unsigned total;

static void line_cb(char *line, void *usr) {
  size_t len = strlen(line);
  assert(len < FILE_LINE_MAX);
  assert(!memchr(line, '\n', len));
  total += len + 1;
  (*(unsigned*)usr)++;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  fz_data = data;
  fz_size = size;
  unsigned n = 0;
  total = 0;
  assert(file_read_lines("list.txt", line_cb, &n));
  assert(total <= size + 1);    // Every byte is in at most one line
  return 0;
}
