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

// Save function signatures, direct copy from:
// https://github.com/davidgfnet/gba-patch-gen/blob/master/tools/save-finder.py

const uint16_t eeprom_v1_read_sig[] = {
  0xb5b0,   // push    {r4, r5, r7, lr}
  0xb0aa,   // sub     sp, #168
  0x466f,   // mov     r7, sp
  0x6079,   // str     r1, [r7, #4]
  0x1c39,   // adds    r1, r7, #0
  0x8008,   // strh    r0, [r1, #0]
  0x1c38,   // adds    r0, r7, #0
  0x8801,   // ldrh    r1, [r0, #0]
  0x293f,   // cmp     r1, #63 @ 0x3f
  0xd903,   // bls.n
  0x4800,   // ldr     r0, [pc, #0]
};
const uint16_t eeprom_v2_read_sig[] = {
  0xb570,   // push    {r4, r5, r6, lr}
  0xb0a2,   // sub     sp, #136
  0x1c0d,   // adds    r5, r1, #0
  0x0400,   // lsls    r0, r0, #16
  0x0c03,   // lsrs    r3, r0, #16
  0x4803,   // ldr     r0, [pc, #12]
  0x6800,   // ldr     r0, [r0, #0]
  0x8880,   // ldrh    r0, [r0, #4]
  0x4283,   // cmp     r3, r0
  0xd305,   // bcc.n
  0x4801,   // ldr     r0, [pc, #4]
};

const uint16_t eeprom_v1_write_sig[] = {
  0xb580,   // push    {r7, lr}
  0xb0aa,   // sub     sp, #168
  0x466f,   // mov     r7, sp
  0x6079,   // str     r1, [r7, #4]
  0x1c39,   // adds    r1, r7, #0
  0x8008,   // strh    r0, [r1, #0]
  0x1c38,   // adds    r0, r7, #0
  0x8801,   // ldrh    r1, [r0, #0]
  0x293f,   // cmp     r1, #63
};
const uint16_t eeprom_v2_write_sig[] = {
  0xb530,   // push    {r4, r5, lr}
  0xb0a9,   // sub     sp, #164
  0x1c0d,   // adds    r5, r1, #0
  0x0400,   // lsls    r0, r0, #16
  0x0c04,   // lsrs    r4, r0, #16
  0x4803,   // ldr     r0, [pc, #12]
  0x6800,   // ldr     r0, [r0, #0]
  0x8880,   // ldrh    r0, [r0, #4]
  0x4284,   // cmp     r4, r0
};
const uint16_t eeprom_v3_write_sig[] = {
  0xb5f0,   // push    {r4, r5, r6, r7, lr}
  0xb0ac,   // sub     sp, #176
  0x1c0d,   // adds    r5, r1, #0
  0x0400,   // lsls    r0, r0, #16
  0x0c01,   // lsrs    r1, r0, #16
  0x0612,   // lsls    r2, r2, #24
  0x0e17,   // lsrs    r7, r2, #24
  0x4803,   // ldr     r0, [pc, #12]
  0x6800,   // ldr     r0, [r0, #0]
  0x8880,   // ldrh    r0, [r0, #4]
  0x4281,   // cmp     r1, r0
};
const uint16_t eeprom_v4_write_sig[] = {
  0xb5f0,   // push    {r4, r5, r6, r7, lr}
  0x4647,   // mov     r7, r8
  0xb480,   // push    {r7}
  0xb0ac,   // sub     sp, #176
  0x1c0e,   // adds    r6, r1, #0
  0x0400,   // lsls    r0, r0, #16
  0x0c05,   // lsrs    r5, r0, #16
  0x0612,   // lsls    r2, r2, #24
  0x0e12,   // lsrs    r2, r2, #24
  0x4690,   // mov     r8, r2
  0x4803,   // ldr     r0, [pc, #12]
  0x6800,   // ldr     r0, [r0, #0]
  0x8880,   // ldrh    r0, [r0, #4]
  0x4285,   // cmp     r5, r0
};

const uint16_t flash_v1_read_sig[] = {
  0xb590,          // push  {r4, r7, lr}
  0xb0a9,          // sub   sp, #0xa4
  0x466f,          // mov   r7, sp
  0x6079,          // str   r1, [r7, #4]
  0x60ba,          // str   r2, [r7, #8]
  0x60fb,          // str   r3, [r7, #12]
  0x1c39,          // adds  r1, r7, #0
  0x8008,          // strh  r0, [r1, #0]
};
const uint16_t flash_v2_read_sig[] = {
  0xb5f0,          // push  {r4, r5, r6, r7, lr}
  0xb0a0,          // sub   sp, #0x80
  0x1c0d,          // adds  r5, r1, #0
  0x1c16,          // adds  r6, r2, #0
  0x1c1f,          // adds  r7, r3, #0
  0x0400,          // lsls  r0, r0, #16
  0x0c04,          // lsrs  r4, r0, #16
  0x4a08,          // ldr   r2, [pc, #32]
  0x8810,          // ldrh  r0, [r2, #0]
  0x4908,          // ldr   r1, [pc, #32]
  0x4008,          // ands  r0, r1
  0x2103,          // movs  r1, #3
  0x4308,          // orrs  r0, r1
};
const uint16_t flash_v3_read_sig[] = {
  0xb5f0,          // push {r4, r5, r6, r7, lr}
  0xb0a0,          // sub  sp, #0x80
  0x1c0d,          // adds r5, r1, #0
  0x1c16,          // adds r6, r2, #0
  0x1c1f,          // adds r7, r3, #0
  0x0403,          // lsls r3, r0, #0x10
  0x0c1c,          // lsrs r4, r3, #0x10
  0x4a0f,          // ldr  r2, [pc, #0x3c]
  0x8810,          // ldrh r0, [r2]
  0x490f,          // ldr  r1, [pc, #0x3c]
  0x4008,          // ands r0, r1
};

const uint16_t flash_v1_ident_sig[] = {
  0xb590,          // push  {r4, r7, lr}
  0xb093,          // sub   sp, #0x4c
  0x466f,          // mov   r7, sp
  0x1d39,          // adds  r1, r7, #4
  0x1c08,          // adds  r0, r1, #0
  0xf000, 0x0000,  // bl    off
  0x1d38,          // adds  r0, r7, #4
};
const uint16_t flash_v2_ident_sig[] = {
  0xb530,          // push  {r4, r5, lr}
  0xb091,          // sub   sp, #0x44
  0x4668,          // mov   r0, sp
  0xf000, 0x0000,  // bl    off
  0x466d,          // mov   r5, sp
  0x3501,          // adds  r5, #1
  0x4a06,          // ldr   r2, [pc, #24]
};

const uint16_t flash_v1_verify_sig[] = {
  0xb590,          // push    {r4, r7, lr}
  0xb0c9,          // sub     sp, #292
  0x466f,          // mov     r7, sp
  0x6079,          // str     r1, [r7, #4]
  0x1c39,          // adds    r1, r7, #0
  0x8008,          // strh    r0, [r1, #0]
  0x0000,          // ldr     r0, [pc, #X]
  0x0000,          // ldr     r1, [pc, #X]
  0x880a,          // ldrh    r2, [r1, #0]
  0x0000,          // ldr     r3, [pc, #X]
  0x1c11,          // adds    r1, r2, #0
  0x4019,          // ands    r1, r3
  0x1c0a,          // adds    r2, r1, #0
  0x2303,          // movs    r3, #3
  0x1c11,          // adds    r1, r2, #0
};
const uint16_t flash_v2_verify_sig[] = {
  0xb530,          // push {r4, r5, lr}
  0xb0c0,          // sub sp, #256
  0x1c0d,          // adds r5, r1, #0
  0x0400,          // lsls r0, r0, #16
  0x0c04,          // lsrs r4, r0, #16
  0x0000,          // ldr  r2, [pc, X]
  0x8810,          // ldrh r0, [r2, #0]
  0x0000,          // ldr  r1, [pc, X]
  0x4008,          // ands r0, r1
  0x2103,          // movs r1, #3
  0x4308,          // orrs r0, r1
  0x8010,          // strh r0, [r2, #0]
  0x0000,          // ldr  r0, [pc, X]
  0x2001,          // movs r0, #1
  0x4043,          // eors r3, r0
  0x466a,          // mov  r2, sp
};
const uint16_t flash_v3_verify_sig[] = {
  0xb530,          // push {r4, r5, lr}
  0xb0c0,          // sub sp, #256
  0x1c0d,          // adds r5, r1, #0
  0x0403,          // lsls r3, r0, #16
  0x0c1c,          // lsrs r4, r3, #16
  0x0000,          // ldr  r2, [pc, #X]
  0x8810,          // ldrh r0, [r2, #0]
  0x0000,          // ldr  r1, [pc, #X]
  0x4008,          // ands r0, r1
  0x2103,          // movs r1, #3
  0x4308,          // orrs r0, r1
  0x8010,          // strh r0, [r2, #0]
  0x0000,          // ldr  r0, [pc, #X]
  0x6800,          // ldr  r0, [r0, #0]
  0x6801,          // ldr  r1, [r0, #0]
  0x2080,          // movs r0, #128
  0x0280,          // lsls r0, r0, #10
  0x4281,          // cmp  r1, r0
  0x0000,          // bne.n OFF
  0x0d18,          // lsrs r0, r3, #20
  0x0600,          // lsls r0, r0, #24
  0x0e00,          // lsrs r0, r0, #24
};

const uint16_t siirtc_probe_sig[] = {
  0xb580,       // push {r7, lr}
  0xb084,       // sub sp, #16
  0x466f,       // mov r7, sp
  0x1d39,       // adds r1, r7, #4
  0x1c08,       // adds r0, r1, #0
  0xf000, 0x00, // bl off
  0x0601,       // lsls r1, r0, #24
  0x0e08,       // lsrs r0, r1, #24
  0x2800,       // cmp r0, #0
  0x0000,       // bne.n off
  0x2000,       // movs    r0, #0
};

const uint16_t siirtc_reset_sig[] = {
  0xb580,       // push {r7, lr}
  0xb084,       // sub sp, #16
  0x466f,       // mov r7, sp
  0x4803,       // ldr r0, [pc, #12]
  0x7801,       // ldrb r1, [r0, #0]
  0x2901,       // cmp r1, #1
  0x0000,       // bne.n off
  0x2000,       // movs r0, #0
};

const uint16_t siirtc_getstatus_sig[] = {
  0xb590,       // push {r4, r7, lr}
  0xb082,       // sub sp, #8
  0x466f,       // mov r7, sp
  0x6038,       // str r0, [r7, #0]
  0x4802,       // ldr r0, [pc, #8]
  0x7801,       // ldrb r1, [r0, #0]
  0x2901,       // cmp r1, #1
  0x0000,       // bne.n off
  0x2000,       // movs r0, #0
  0x0000,       // b.n off
  0x00, 0x00,   // [pool data]
  0x0000,       // ldr r0, [pc, #X]
  0x2101,       // movs r1, #1
  0x7001,       // strb r1, [r0, #0]
  0x0000,       // ldr r0, [pc, #X]
  0x2101,       // movs r1, #1
  0x8001,       // strh r1, [r0, #0]
  0x0000,       // ldr r0, [pc, #X]
  0x2105,       // movs r1, #5
  0x8001,       // strh r1, [r0, #0]
  0x0000,       // ldr r0, [pc, #X]
  0x2107,       // movs r1, #7
  0x8001,       // strh r1, [r0, #0]
};

const uint16_t siirtc_getdatetime_sig[] = {
  0xb580,       // push {r7, lr}
  0xb082,       // sub sp, #8
  0x466f,       // mov r7, sp
  0x6038,       // str r0, [r7, #0]
  0x4802,       // ldr r0, [pc, #8]
  0x7801,       // ldrb r1, [r0, #0]
  0x2901,       // cmp r1, #1
  0x0000,       // bne.n off
  0x2000,       // movs r0, #0
  0x0000,       // b.n off
  0x00, 0x00,   // [pool data]
  0x0000,       // ldr r0, [pc, #X]
  0x2101,       // movs r1, #1
  0x7001,       // strb r1, [r0, #0]
  0x0000,       // ldr r0, [pc, #X]
  0x2101,       // movs r1, #1
  0x8001,       // strh r1, [r0, #0]
  0x0000,       // ldr r0, [pc, #X]
  0x2105,       // movs r1, #5
  0x8001,       // strh r1, [r0, #0]
  0x0000,       // ldr r0, [pc, #X]
  0x2107,       // movs r1, #7
  0x8001,       // strh r1, [r0, #0]
  0x2065,       // movs r0, #101   # This distinguishes set/get
};

// Single word signatures: constants loaded via LDR rX, [pc, #imm]
const uint32_t waitcnt_sig[] = { 0x04000204 };     // WAITCNT register address
const uint32_t irqhaddr_sig[] = { 0x03007FFC };    // IRQ handler pointer address

// Save/RTC library ID strings (word aligned in ROM)
const char str_sram_v11_sig[]  = "SRAM_V11";
const char str_sram_fv_sig[]   = "SRAM_F_V";
const char str_eeprom_v_sig[]  = "EEPROM_V";
const char str_flash_v1_sig[]  = "FLASH_V1";
const char str_flash_512_sig[] = "FLASH512";
const char str_flash_1m_sig[]  = "FLASH1M_";
const char str_siirtc_v_sig[]  = "SIIRTC_V";


typedef struct {
  uint32_t program_sector_fnptr;
  uint32_t erase_chip_fnptr;
  uint32_t erase_sector_fnptr;
  uint32_t wait_flash_write_fnptr;
  uint32_t timeout_lut_ptr;

  uint32_t flash_size;     // Should be 65536, 131072
  uint32_t sector_size;    // Should be 128 / 4096
  uint8_t  shift_amount;   // Should be 7 or 12 (for 2^7 or 2^12)
  uint8_t  zero_pad1;
  uint16_t sector_count;
  uint16_t top_value;
  uint16_t zero_pad2;

  uint16_t ws[2];
  uint16_t device_id;
} t_flash_setup_info_v1;

typedef struct {
  uint32_t program_byte_fnptr;
  t_flash_setup_info_v1 v1;
} t_flash_setup_info_v2;


