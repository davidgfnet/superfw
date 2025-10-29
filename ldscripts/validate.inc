
#define FUNC_SIZE_CHECK(fn, size)                                \
  ASSERT(fn##_end - fn <= size, "Patch function too long");      \

#define FLASH_SIZE_CHECK(bn, size)      \
  FUNC_SIZE_CHECK(bn##_sram64k,size)    \
  FUNC_SIZE_CHECK(bn##_sram128k,size)   \
  FUNC_SIZE_CHECK(bn##_directsave,size)

#define EEPROM_SIZE_CHECK(bn, size)     \
  FUNC_SIZE_CHECK(bn##_sram64k,size)    \
  FUNC_SIZE_CHECK(bn##_directsave,size)

/* Check patch function sizes, to ensure we do not overflow. */
FLASH_SIZE_CHECK(patch_flash_read,           118)
FLASH_SIZE_CHECK(patch_flash_erase_device,    90)
FLASH_SIZE_CHECK(patch_flash_erase_sector,   110)
FLASH_SIZE_CHECK(patch_flash_write_sector,   100)
FLASH_SIZE_CHECK(patch_flash_write_byte,     148)

EEPROM_SIZE_CHECK(patch_eeprom_read,          168)
EEPROM_SIZE_CHECK(patch_eeprom_write,         204)

FUNC_SIZE_CHECK(patch_rtc_probe,       214)
FUNC_SIZE_CHECK(patch_rtc_reset,       132)
FUNC_SIZE_CHECK(patch_rtc_gettimedate, 176)
FUNC_SIZE_CHECK(patch_rtc_getstatus,   204)

