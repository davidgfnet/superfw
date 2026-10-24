// FatFS name handling with SuperFW's configuration (code page 437, UTF-8 API),
// on a small FAT12 volume built in memory.
//
// Issue #84: a Chinese Windows stores short folder names (up to 4 Chinese
// characters) as a short name only, in its own code page (GBK), with no long
// name. FatFS shows those bytes as code page 437 characters, and when opening
// the name it upper-cased the "accented" 437 characters (ExCvt), so bytes like
// 0xA1/0xA2 (GBK trail bytes) no longer matched what is on the card: the folder
// could not be opened.

#include <assert.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include "fatfs/ff.h"
#include "fatfs/diskio.h"

#define SECTORS 2880
static uint8_t disk[SECTORS * 512];

// RAM disk.
DSTATUS disk_initialize(BYTE pdrv) { return 0; }
DSTATUS disk_status(BYTE pdrv) { return 0; }
DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count) {
  memcpy(buff, &disk[sector * 512], count * 512);
  return RES_OK;
}
DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count) {
  memcpy(&disk[sector * 512], buff, count * 512);
  return RES_OK;
}
DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff) {
  if (cmd == GET_SECTOR_COUNT)
    *(LBA_t*)buff = SECTORS;
  else if (cmd == GET_BLOCK_SIZE)
    *(DWORD*)buff = 1;
  return RES_OK;
}

static void put16(uint8_t *p, unsigned v) { p[0] = v; p[1] = v >> 8; }

static void dirent(uint8_t *e, const char *name11, uint8_t attr, unsigned cluster, unsigned size) {
  memcpy(e, name11, 11);
  e[11] = attr;
  put16(&e[26], cluster);
  put16(&e[28], size); put16(&e[30], size >> 16);
}

// 1.44 MB FAT12: 1 reserved sector, 2 FATs of 9 sectors, 224 root entries
// (14 sectors), data from sector 33, one sector per cluster.
static void make_volume(const char *dirname11) {
  memset(disk, 0, sizeof(disk));
  uint8_t *bs = disk;
  bs[0] = 0xEB; bs[1] = 0x3C; bs[2] = 0x90;
  memcpy(&bs[3], "MSDOS5.0", 8);
  put16(&bs[11], 512); bs[13] = 1; put16(&bs[14], 1); bs[16] = 2;
  put16(&bs[17], 224); put16(&bs[19], SECTORS); bs[21] = 0xF0;
  put16(&bs[22], 9); put16(&bs[24], 18); put16(&bs[26], 2);
  bs[38] = 0x29;
  memcpy(&bs[43], "NO NAME    FAT12   ", 19);
  bs[510] = 0x55; bs[511] = 0xAA;

  // Clusters 2 (the folder) and 3 (the file) are one cluster long.
  static const uint8_t fat[] = { 0xF0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
  memcpy(&disk[1 * 512], fat, sizeof(fat));
  memcpy(&disk[10 * 512], fat, sizeof(fat));

  // Root dir: the folder, with a short name only (no long name entries).
  dirent(&disk[19 * 512], dirname11, AM_DIR, 2, 0);

  // The folder: ".", "..", GAME.GBA.
  dirent(&disk[33 * 512 + 0],  ".          ", AM_DIR, 2, 0);
  dirent(&disk[33 * 512 + 32], "..         ", AM_DIR, 0, 0);
  dirent(&disk[33 * 512 + 64], "GAME    GBA", AM_ARC, 3, 4);
  memcpy(&disk[34 * 512], "ABCD", 4);
}

// Lists the root, then opens the folder and the game in it by the listed name.
static void check_folder(const char *dirname11) {
  make_volume(dirname11);

  FATFS fs;
  assert(f_mount(&fs, "0:", 1) == FR_OK);

  DIR d;
  FILINFO fi;
  assert(f_opendir(&d, "/") == FR_OK);
  assert(f_readdir(&d, &fi) == FR_OK);
  assert(fi.fname[0] && (fi.fattrib & AM_DIR));
  assert(strcmp(fi.fname, "?"));           // Issue #84: names too long for FF_SFN_BUF
  f_closedir(&d);

  char path[600];
  snprintf(path, sizeof(path), "/%s", fi.fname);
  assert(f_opendir(&d, path) == FR_OK);      // Issue #84: FR_NO_PATH
  f_closedir(&d);

  snprintf(path, sizeof(path), "/%s/game.gba", fi.fname);   // ASCII stays case insensitive
  FIL f;
  char buf[8] = {0};
  UINT rd;
  assert(f_open(&f, path, FA_READ) == FR_OK);
  assert(f_read(&f, buf, sizeof(buf), &rd) == FR_OK && rd == 4 && !memcmp(buf, "ABCD", 4));
  f_close(&f);

  f_mount(NULL, "0:", 0);
}

// New files still get an upper case SFN (as stock FatFS writes it).
static void check_create(void) {
  make_volume("GAMES      ");
  FATFS fs;
  FIL f;
  assert(f_mount(&fs, "0:", 1) == FR_OK);
  assert(f_open(&f, "/caf\xC3\xA9.txt", FA_CREATE_NEW | FA_WRITE) == FR_OK);   // "café.txt"
  f_close(&f);

  // Find its SFN in the root dir: no lower case 'é' (0x82 in code page 437).
  bool found = false;
  for (unsigned i = 0; i < 224; i++) {
    const uint8_t *e = &disk[19 * 512 + i * 32];
    if (e[0] && e[11] != 0x0F && (e[11] & AM_ARC)) {
      found = true;
      for (unsigned j = 0; j < 11; j++)
        assert(e[j] != 0x82);
    }
  }
  assert(found);

  assert(f_open(&f, "/caf\xC3\xA9.txt", FA_READ) == FR_OK);   // By its name
  f_close(&f);
  assert(f_open(&f, "/CAF\xC3\x89.TXT", FA_READ) == FR_OK);   // "CAFÉ.TXT": long names ignore case
  f_close(&f);
  f_mount(NULL, "0:", 0);
}

int main() {
  check_create();
  // GBK "啊阿" (B0A1 B0A2): A1 and A2 are lower case letters in code page 437.
  check_folder("\xB0\xA1\xB0\xA2       ");
  // GBK "口袋妖怪" (BFDA B4FC D1FD B9D6): no such bytes, always worked.
  check_folder("\xBF\xDA\xB4\xFC\xD1\xFD\xB9\xD6   ");
  // Plain ASCII and code page 437 accented upper case names still work.
  check_folder("GAMES      ");
  check_folder("\x90TUDES     ");    // "ÉTUDES"

  printf("fatfs_names_test OK\n");
  return 0;
}
