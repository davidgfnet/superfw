#!/usr/bin/env python
# -*- coding: utf-8 -*-

# Copyright 2024 David Guillen Fandos <david@davidgf.net>

# Testing tool to patch games with the in-game menu payload (for testing!)
# It works like this:
#  - Get a ROM, an ingame-menu payload and a font pack.
#  - Patch the font pack and in-game menu, with required fields.
#  - Patch the ROM entry point and add some ROM power-of-two padding.
#
# Note: the ROM must be already patched for IRQ handler magic.

import os, sys, argparse, struct

parser = argparse.ArgumentParser(prog='igm-test-patcher')
parser.add_argument('--rom', dest='rom', required=True, help='ROM path')
parser.add_argument('--payload', dest='payload', required=True, help='payload path (to the in game menu bin)')
parser.add_argument('--fontpack', dest='fontpack', required=True, help='FontPack file path')
parser.add_argument('--output', dest='out', required=True, help='Output ROM file')
args = parser.parse_args()

HOTKEY = 0x00F7    # L+R+START

def RGB2GBA(c):
  return ((c & 0xF80000) >> 19) | ((c & 0x00F800) >>  6) | ((c & 0x0000F8) <<  7)

rom = open(args.rom, "rb").read()
pload = open(args.payload, "rb").read()
fpack = open(args.fontpack, "rb").read()

# Pad font pack (should be multiple of 4 already ...)
while len(fpack) % 4:
  fpack += b'\x00'

# Extract the entry point from the ROM by reading the first instruction
assert rom[0x3] == 0xEA  # It's always an unconditional branch!
start_addr = (struct.unpack("<I", rom[0:4])[0] & 0xFFFFFF) * 4 + 8 + 0x08000000

# Replace inst with a branch to the payload
pload_entry = len(rom) + len(fpack)
start_inst = struct.pack("<I", 0xEA000000 | ((pload_entry >> 2) - 2))

# We place the menu right after the ROM, font pack first.
fpack_addr = 0x08000000 + len(rom)

# Proceed to patch the payload by patching its header.
hdr = struct.pack("<IIIIIIHHHH",
  start_addr,                  # entry point
  0,                           # pload size (ignore)
  HOTKEY,                      # hotkey combo
  0,                           # lang
  fpack_addr,                  # Font pack address
  0,                           # Cheat base addr
  RGB2GBA(0xeca551),           # menu palette
  RGB2GBA(0xbda27b),
  RGB2GBA(0x000000),
  0)

# Pack it all!
outrom = start_inst + rom[4:] + fpack + pload[:15*4] + hdr + pload[15*4 + len(hdr):]

# Padd it to power of two?
# TODO

open(args.out, "wb").write(outrom)

