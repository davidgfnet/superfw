// Fake FatFS and hardware functions for the fuzz targets (host builds).
// Every file "exists" and contains the bytes given by the fuzzer; writes fail.

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "fatfs/ff.h"
#include "common.h"
#include "flash.h"
#include "fuzz.h"

const uint8_t *fz_data = NULL;
size_t fz_size = 0;

typedef struct {
  const uint8_t *d;
  size_t n, pos;
} t_fakefil;
_Static_assert(sizeof(t_fakefil) <= sizeof(FIL), "fake FIL must fit in FIL");

FRESULT f_open(FIL *fp, const TCHAR *path, BYTE mode) {
  (void)path;
  if (mode & (FA_WRITE | FA_CREATE_ALWAYS | FA_CREATE_NEW | FA_OPEN_ALWAYS))
    return FR_DENIED;
  t_fakefil *f = (t_fakefil*)fp;
  f->d = fz_data;
  f->n = fz_size;
  f->pos = 0;
  return FR_OK;
}

FRESULT f_read(FIL *fp, void *buff, UINT btr, UINT *br) {
  t_fakefil *f = (t_fakefil*)fp;
  size_t left = f->n - f->pos;
  size_t cnt = btr < left ? btr : left;
  memcpy(buff, &f->d[f->pos], cnt);
  f->pos += cnt;
  *br = cnt;
  return FR_OK;
}

FRESULT f_lseek(FIL *fp, FSIZE_t ofs) {
  t_fakefil *f = (t_fakefil*)fp;
  f->pos = ofs < f->n ? ofs : f->n;
  return FR_OK;
}

FRESULT f_close(FIL *fp) { (void)fp; return FR_OK; }
FRESULT f_write(FIL *fp, const void *buff, UINT btw, UINT *bw) { (void)fp; (void)buff; (void)btw; *bw = 0; return FR_DENIED; }
FRESULT f_stat(const TCHAR *path, FILINFO *fno) { (void)path; (void)fno; return FR_NO_FILE; }
FRESULT f_unlink(const TCHAR *path) { (void)path; return FR_DENIED; }
FRESULT f_mkdir(const TCHAR *path) { (void)path; return FR_DENIED; }
FRESULT f_rename(const TCHAR *a, const TCHAR *b) { (void)a; (void)b; return FR_DENIED; }
FRESULT f_chmod(const TCHAR *path, BYTE attr, BYTE mask) { (void)path; (void)attr; (void)mask; return FR_DENIED; }

// GBA hardware helpers used by the parsers
void dma_memcpy16(volatile void *dst, const void *src, uint16_t count) { memcpy((void*)dst, src, count * 2); }
void dma_memcpy32(volatile void *dst, const void *src, uint16_t count) { memcpy((void*)dst, src, count * 4); }
void dma_memset16(volatile void *ptr, uint16_t value, uint16_t count) {
  uint16_t *p = (uint16_t*)ptr;
  while (count--)
    *p++ = value;
}
void set_supercard_mode(unsigned mapped_area, bool write_access, bool sdcard_interface) {
  (void)mapped_area; (void)write_access; (void)sdcard_interface;
}

// Flash: reads come from the fuzz data, as if it was the metadata area.
void flash_read(uint32_t baseaddr, uint8_t *buf, unsigned size) {
  uint32_t off = baseaddr - ROM_FLASHMETA_ADDR;
  for (unsigned i = 0; i < size; i++)
    buf[i] = (off + i < fz_size) ? fz_data[off + i] : 0;
}
bool flash_erase_sectors(uint32_t baseaddr, unsigned sectsize, unsigned sectcount) {
  (void)baseaddr; (void)sectsize; (void)sectcount; return false;
}
bool flash_program(uint32_t baseaddr, const uint8_t *buf, unsigned size) {
  (void)baseaddr; (void)buf; (void)size; return false;
}
bool flash_program_buffered(uint32_t baseaddr, const uint8_t *buf, unsigned size, unsigned bufsize) {
  (void)baseaddr; (void)buf; (void)size; (void)bufsize; return false;
}
bool flash_verify(uint32_t baseaddr, const uint8_t *buf, unsigned size) {
  (void)baseaddr; (void)buf; (void)size; return false;
}
bool flash_check_erased(uintptr_t addr, unsigned size) { (void)addr; (void)size; return false; }
bool flash_erase_sector(uintptr_t addr) { (void)addr; return false; }
// No flash chip detected: writes give up early, reads still parse the data.
t_flash_info flashinfo;

// From menu.c (used by settings.c)
unsigned lang_lookup(uint16_t code) { (void)code; return 0; }
uint16_t lang_getcode() { return 0x6e65; }
