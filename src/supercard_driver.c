/*
 * Copyright (C) 2024 David Guillen Fandos <david@davidgf.net>
 *
 * This program is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see
 * <http://www.gnu.org/licenses/>.
 */

// Documentation can be found at several places, including:
// http://elm-chan.org/docs/mmc/mmc_e.html
// https://www.davidgf.net/2011/07/05/usb_sd_reader/index.html

#include <stdio.h>

#include "gbahw.h"
#include "supercard_driver.h"
#include "crc.h"

extern bool isgba;
extern bool slowsd;

// If init is diabled, any driver variables are externally defined (and provided by some other object)
// #define NO_SUPERCARD_INIT

// Using the second mirror with the fastest WS gives us ~20% faster load times.
#ifndef SC_FAST_ROM_MIRROR
  #define SC_FAST_ROM_MIRROR false
#endif

static inline bool use_fast_mirror() {
  // Use it only when in GBA mode and if fast-load is enabled.
  #ifdef SUPERCARD_LITE_IO
    return false;              // SC lite can't use the faster waitstates.
  #else
    return isgba && !slowsd;
  #endif
}

typedef int (*t_rdsec_fn)(uint8_t *buffer, unsigned count);
typedef int (*t_wrsec_fn)(const uint8_t *buffer, unsigned count);

int sc_read_sectors_w0(uint8_t *buffer, unsigned count);
int sc_read_sectors_w1(uint8_t *buffer, unsigned count);
int sc_write_sectors_w0(const uint8_t *buffer, unsigned count);
int sc_write_sectors_w1(const uint8_t *buffer, unsigned );

const static t_rdsec_fn sc_read_sectors[2] = {
  &sc_read_sectors_w0, &sc_read_sectors_w1
};

const static t_wrsec_fn sc_write_sectors[2] = {
  &sc_write_sectors_w0, &sc_write_sectors_w1
};

// Util functions (implemented in ASM)
void send_empty_clocks(unsigned count);
bool wait_sdcard_idle(unsigned timeout);
bool wait_dat0_idle(unsigned timeout);
bool receive_sdcard_response(uint8_t *buffer, unsigned maxsize, unsigned timeout);
void send_sdcard_commandbuf(const uint8_t *buffer, unsigned maxsize);

// We use the 0x8-9 mapping here, since there is little performance difference
// when sending/receiving commands.
#define SC_MIRROR_BASE           0x08000000

#define REG_SC_MODE_REG_ADDR     0x09FFFFFE
#define REG_SD_MODE              (*(volatile uint16_t*)(REG_SC_MODE_REG_ADDR))

#define MODESWITCH_MAGIC         0xA55A
#define NORMAPPING_MAGIC         0xA558

#define MAX_WRITE_RETRIES        2            // Try up to 3 times to write a block.
#define MAX_REINIT_RETRIES       9            // Try up to 10 to re-init the card.

#define CMD_WAIT_IDLE            0x800000     // Number of iterations to wait for card ready
#define CMD_WAIT_RESP             0x60000     // Aprox ~100ms as command timeout
#define CMD_WAIT_DATA            0x800000     // Aprox ~1s as data timeout
#define WAIT_READY_COUNT             4096     // Aprox 50-200ms, should take less
#define WAIT_READY_WRITE         0x200000     // Around 500ms.

#define OCR_CCS         0x40000000
#define OCR_NBUSY       0x80000000
#define OCR_V30         0x00040000   // Work at 3.0-3.1V

#define SD_STATUS_READYDATA   0x0100

#define SD_MAX_RESP       8
#define SD_R1_RESP        6    // CMD + resp(32bit) + crc7
#define SD_MAX_RESP_BUF  20


#define SD_CMD0         0   // Reset/Go Idle command
#define SD_CMD8         8   // Identify V2 cards
#define SD_CMD2         2   // Send CID register (card info)
#define SD_CMD3         3   // Send RCA register (card addr)
#define SD_CMD7         7   // Change to transfer mode
#define SD_CMD9         9   // Send CSD register (card status)
#define SD_CMD12       12   // Stop reading (multiple blocks)
#define SD_CMD13       13   // Send status
#define SD_CMD16       16   // Set block length
#define SD_CMD18       18   // Read multiple blocks
#define SD_CMD24       24   // Write single block
#define SD_CMD25       25   // Write multiple blocks
#define SD_CMD55       55
#define SD_ACMD6        6   // Bus width change
#define SD_ACMD23      23   // Set write block erase count (pre-erase)
#define SD_ACMD41      41   // Card ident

typedef struct {
  uint8_t cmdresp;
  uint8_t manufacturer;
  uint16_t appid;
  char prodname[5];
  uint8_t prodrev;
  uint8_t prodserial[4];
  uint8_t mdate[2];
  uint8_t crc;
} t_card_cid;

#ifdef NO_SUPERCARD_INIT
  bool sc_issdhc();
  uint16_t sc_rca();
#else
  static bool drv_issdhc = false;
  static uint16_t drv_rca = 0;

  bool sc_issdhc() { return drv_issdhc; }
  uint16_t sc_rca() { return drv_rca; }
#endif

void write_supercard_mode(uint16_t modebits) {
  // Write magic value and then the mode value (twice) to trigger the mode change.
  // Using asm to ensure we place a proper memory barrier.
  asm volatile (
    "strh %1, [%0]\n"
    "strh %1, [%0]\n"
    "strh %2, [%0]\n"
    "strh %2, [%0]\n"
    :: "l"(REG_SC_MODE_REG_ADDR),
       "l"(MODESWITCH_MAGIC),
       "l"(modebits)
    : "memory");
}

void set_supercard_normap(const uint8_t *blks) {
  // Push blocks in order via magic register
  for (unsigned i = 0; i < 8; i++) {
    asm volatile (
      "strh %1, [%0]\n"
      "strh %1, [%0]\n"
      "strh %2, [%0]\n"
      "strh %2, [%0]\n"
      :: "l"(REG_SC_MODE_REG_ADDR),
         "l"(NORMAPPING_MAGIC),
         "l"((unsigned int)blks[i])
      : "memory");
  }
}

void set_supercard_mode(unsigned mapped_area, bool write_access, bool sdcard_interface) {
  // Bit0: Controls SDRAM vs internal Flash mapping
  // Bit1: Controls whether the SD card interface is mapped into the ROM addresspace.
  // Bit2: Controls read-only/write access. Doubles as SRAM bank selector!
  uint16_t value = mapped_area | (sdcard_interface ? 0x2 : 0x0) | (write_access ? 0x4 : 0x0);

  write_supercard_mode(value);
}

// Waits for idle and sends a command. Does nothing afterwards
static bool send_sdcard_command_raw(uint8_t cmd, uint32_t arg) {
  // Send the command bit banging it via the CMD interface.
  uint8_t buf[6] = { 0x40 | cmd, arg >> 24, arg >> 16, arg >> 8, arg, 0 };
  buf[5] = crc7(buf, 5);

  // Wait before sending any command, until the line is not pulled down.
  if (!wait_sdcard_idle(CMD_WAIT_IDLE))
    return false;

  // Send the buffer out
  send_sdcard_commandbuf(buf, sizeof(buf));

  return true;
}

// Sends a command and receives a response. Does *not* clock afterwards.
static bool send_sdcard_command_noclock(uint8_t cmd, uint32_t arg, uint8_t *resp, unsigned respsize) {
  // Wait for Idle and send the command
  if (!send_sdcard_command_raw(cmd, arg))
    return false;

  // Wait for response, and retrieve a response to the command.
  return receive_sdcard_response(resp, respsize, CMD_WAIT_RESP);
}

// Like send_sdcard_command_noclock but it inject a bunch of clocks after.
// (some commands require spacing between them, usually 8 clocks at least).
static bool send_sdcard_command(uint8_t cmd, uint32_t arg, uint8_t *resp, unsigned respsize) {
  bool ret = send_sdcard_command_noclock(cmd, arg, resp, respsize);

  // Allow for some idle time before next command (minimum is 8).
  send_empty_clocks(32);

  return ret;
}

static bool send_get_status(uint16_t *status) {
  uint8_t resp[6];
  if (!send_sdcard_command(SD_CMD13, sc_rca() << 16, resp, sizeof(resp)))
    return false;

  if (status)
    *status = (resp[1] << 8) | resp[2];
  return true;
}

#ifndef NO_SUPERCARD_INIT

// Sends the command and pushes some clocks, but ignores any response.
static bool send_sdcard_command_nowait(uint8_t cmd, uint32_t arg) {
  // Wait for Idle and send the command
  bool ret = send_sdcard_command_raw(cmd, arg);

  // Inject some empty clocks, ensure cards process the previous command
  // and that any response (if one exists) is clocked out as well.
  send_empty_clocks(256);

  return ret;
}

static bool send_sdcard_reset() {
  // Send the command bit banging it via the CMD interface.
  uint8_t buf[6] = { 0x40 | SD_CMD0, 0, 0, 0, 0, 0 };
  buf[5] = crc7(buf, 5);

  // Wait before sending any command, until the line is not pulled down.
  if (!wait_sdcard_idle(CMD_WAIT_IDLE))
    return false;

  // Send the buffer out
  send_sdcard_commandbuf(buf, sizeof(buf));

  // Allow some reset time for the card, won't respond
  // ("the switching period is within 8 clocks after the end bit of CMD0")
  send_empty_clocks(4096);

  return true;
}

// Re-init the SD card. This just initializes the driver variables/state and
// the card is assume to be init/ready.
unsigned sdcard_reinit() {
  uint8_t resp[20];

  // Pipe out any weirdness left in the previous state?
  send_empty_clocks(64);

  // Move the card into standby mode.
  // We assme that the card might be in transfer state (or standby).
  // Resend it a couple of times, since there is no response ...
  for (unsigned i = 0; i < 3; i++)
    send_sdcard_command_nowait(SD_CMD7, 0);

  // Get a new RCA, try a few times just in case
  for (unsigned i = 0; i < 1+MAX_REINIT_RETRIES; i++) {
    if (send_sdcard_command(SD_CMD3, 0, resp, SD_MAX_RESP)) {
      if (resp[0] == SD_CMD3) {
        drv_rca = (resp[1] << 8) | resp[2];
        break;
      }
    }
    if (i == MAX_REINIT_RETRIES)
      return SD_ERR_BAD_INIT;
  }

  // Get CSD register, see if we have a SDSC or an SDHC/XC.
  for (unsigned i = 0; i < 1+MAX_REINIT_RETRIES; i++) {
    if (!send_sdcard_command(SD_CMD9, drv_rca << 16, resp, 20))
      return SD_ERR_BAD_CAP;

    if (resp[0] == 0x3F) {  // Check for reserved token seq.
      uint32_t csd_ver = resp[1] >> 6;
      drv_issdhc = (csd_ver == 1);
      break;
    }

    if (i == MAX_REINIT_RETRIES)
      return SD_ERR_BAD_CAP;
  }

  // Select card, move back into Transfer mode.
  if (!send_sdcard_command(SD_CMD7, drv_rca << 16, NULL, SD_MAX_RESP))
    return SD_ERR_BAD_MODEXCH;

  return 0;
}

// SD card init, as per "Card Initialization and Identification Flow (SD mode)"
unsigned sdcard_init(t_card_info *info) {
  uint8_t resp[SD_MAX_RESP_BUF];

  // Wait for card to be internally initialized. As per spec:
  // Initialization delay: The maximum of 1 msec, 74 clock cycles and supply ramp up time
  send_empty_clocks(4096);  // ~1msec (assuming ~4 clocks per clock)

  // Start by sending a CMD0 to ensure the card goes into IDLE state.
  if (!send_sdcard_reset())
    return SD_ERR_NO_STARTUP;

  // Try CMD8, see if the card is a v2.0
  // It is recommended to use ‘10101010b’ for the ‘check pattern’.
  bool cmd8_ok = false;
  if (send_sdcard_command(SD_CMD8, 0xAA | 0x100, resp, SD_MAX_RESP)) {
    // There was a response, validate it.
    cmd8_ok = (resp[0] == SD_CMD8 && resp[4] == 0xAA && resp[3]);
  }

  // Send ACMD41 now regardless of the version.
  uint32_t ocrreq = OCR_V30 | (cmd8_ok ? OCR_CCS : 0);   // Request SDHC if it implements V2
  for (unsigned i = 0; i < WAIT_READY_COUNT; i++) {
    if (!send_sdcard_command(SD_CMD55, 0, NULL, SD_MAX_RESP))
      return SD_ERR_BAD_IDENT;

    if (!send_sdcard_command(SD_ACMD41, ocrreq, resp, SD_MAX_RESP))
      return SD_ERR_BAD_IDENT;

    uint32_t ocr = (resp[1] << 24) | (resp[2] << 16) | (resp[3] << 8) | resp[4];
    if (ocr & OCR_NBUSY) {
      drv_issdhc = cmd8_ok && (ocr & OCR_CCS);   // It is a V2 card with SDHC support!
      break;    // Stop polling
    }
  }

  // Run identification phase now.
  if (!send_sdcard_command(SD_CMD2, 0, resp, SD_MAX_RESP_BUF))
    return SD_ERR_BAD_INIT;

  if (info) {
    const t_card_cid *cid = (t_card_cid*)resp;
    // Save SD CID info as card info.
    info->manufacturer = cid->manufacturer;
    info->oemid = cid->appid;
  }

  drv_rca = 0;
  for (unsigned i = 0; i < CMD_WAIT_IDLE; i++) {
    if (!send_sdcard_command(SD_CMD3, 0, resp, SD_MAX_RESP))
      return SD_ERR_BAD_INIT;

    uint32_t response = (resp[1] << 24) | (resp[2] << 16) | (resp[3] << 8) | resp[4];
    uint32_t cstate = ((response >> 9) & 0xF);
    if (cstate != 0x3) {  // Not in Standby mode
      drv_rca = (resp[1] << 8) | resp[2];
      break;
    }
  }

  if (!drv_rca)
    return SD_ERR_BAD_INIT;

  // Read card capabilities (~17 bytes):
  // Until the end of Card Identification Mode the host shall remain at fOD frequency because
  // some cards may have operating frequency restrictions during the card identification mode.
  if (!send_sdcard_command(SD_CMD9, drv_rca << 16, resp, SD_MAX_RESP_BUF))
    return SD_ERR_BAD_CAP;

  // Extract some data from the CSD register.
  if (info) {
    unsigned csd_ver = resp[1] >> 6;
    if (csd_ver == 0) {
      unsigned c_size = ((resp[8] << 10) | (resp[9] << 2) | (resp[10] >> 6)) & 0xFFF;
      unsigned s_mult = ((resp[11] << 1) | (resp[12] >> 7)) & 0x7;
      unsigned readbl = resp[6] & 0xF;

      unsigned block_len = (1 << readbl);
      unsigned multiplier = (4 << s_mult);
      unsigned blocknr = (c_size + 1) * multiplier;

      info->block_cnt = (blocknr * block_len) >> 9;    // Expressed in 512 byte units
      info->sdhc = false;
    } else {
      // c_size represents units of 512KBytes.
      unsigned c_size = ((resp[8] << 16) | (resp[9] << 8) | (resp[10])) & 0x3FFFFF;
      info->block_cnt = (c_size+1) << 10;   // Multiply by 1K to get number of blocks
      info->sdhc = true;
    }
  }

  // Select card, move into Transfer mode (only certain commands can be issued after).
  if (!send_sdcard_command(SD_CMD7, drv_rca << 16, NULL, SD_MAX_RESP))
    return SD_ERR_BAD_MODEXCH;

  // Request 4 bit mode, since it is much faster
  if (!send_sdcard_command(SD_CMD55, drv_rca << 16, NULL, SD_MAX_RESP))
    return SD_ERR_BAD_BUSSEL;
  if (!send_sdcard_command(SD_ACMD6, 0x2, NULL, SD_MAX_RESP))  // b10 means 4 bits, b00 means 1 bit
    return SD_ERR_BAD_BUSSEL;

  // Set block size to be 512, not necessary for SDHC cards?
  if (!send_sdcard_command(SD_CMD16, 512, NULL, SD_MAX_RESP))
    return SD_ERR_BAD_BUSSEL;

  return 0;
}

#endif

unsigned sdcard_read_blocks(uint8_t *buffer, uint32_t blocknum, unsigned blkcnt) {
  uint8_t resp[4];
  if (!send_sdcard_command_noclock(SD_CMD18, sc_issdhc() ? blocknum : blocknum * 512, resp, sizeof(resp)))
    return SD_ERR_BADREAD;

  // Read all data using the asm routine for speed.
  if (sc_read_sectors[SC_FAST_ROM_MIRROR ? 1 : 0](buffer, blkcnt))
    return SD_ERR_BADREAD;

  if (!send_sdcard_command(SD_CMD12, 0, NULL, SD_MAX_RESP))
    return SD_ERR_BADREAD;
  return 0;
}

unsigned sdcard_write_blocks(const uint8_t *buffer, uint32_t blocknum, unsigned blkcnt) {
  // Send a write intent / clear command, for faster writes. Do not take errors
  // too seriously, this is "optional" really.
  #ifdef SD_PREERASE_BLOCKS_WRITE
  if (send_sdcard_command(SD_CMD55, sc_rca() << 16, NULL, SD_MAX_RESP))
    send_sdcard_command(SD_ACMD23, blkcnt, NULL, SD_MAX_RESP);
  #endif

  // Perform a block write. The ASM function handles it all (CRC and all).
  for (unsigned j = 0; j < 1+MAX_WRITE_RETRIES; j++) {
    if (!send_sdcard_command_noclock(SD_CMD25, sc_issdhc() ? blocknum : blocknum * 512, NULL, SD_R1_RESP))
      return SD_ERR_BADWRITE;

    bool wr_ok = !sc_write_sectors[SC_FAST_ROM_MIRROR ? 1 : 0](buffer, blkcnt);

    // Send CMD12 to signal the end of the write sequence!
    if (!send_sdcard_command(SD_CMD12, 0, NULL, SD_MAX_RESP))
      return SD_ERR_BADWRITE;

    // CMD12 pulls DAT0 low while busy (usually writing). Wait for !busy.
    wait_dat0_idle(WAIT_READY_WRITE);

    if (wr_ok) {
      // Check the status/error code reported by CMD13, should be zero.
      uint16_t cardst = 0xFFFF;
      if (!send_get_status(&cardst))
        return SD_ERR_BADWRITE;

      if (cardst == 0)
        return 0;     // Write seq successful!
    }
  }

  return SD_ERR_BADWRITE;
}

