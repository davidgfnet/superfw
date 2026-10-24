// Fuzzes the settings files parser (load_settings, settings.c): both
// settings.txt and ui-settings.txt contain the fuzz data.
#include "settings.h"
#include "fuzz.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  fz_data = data;
  fz_size = size;
  load_settings();
  return 0;
}
