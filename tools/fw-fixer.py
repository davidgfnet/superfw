#!/usr/bin/env python
# -*- coding: utf-8 -*-

# Copyright 2024 David Guillen Fandos <david@davidgf.net>
# Fixes GBA header and patches checksums and other relevant header fields

import sys, hashlib, struct

fwimg = open(sys.argv[1], "rb").read()

# Pad the firmware to the next 512 byte block
fwimg += b'\xff' * (512 - (len(fwimg) % 512))

# Patch GBA fwimg with a fixed checksum
crc = sum(fwimg[0xA0:0xBD])
fwimg = fwimg[:0xBD] + ((-(0x19 + crc)) & 0xFF).to_bytes(1, "little") + fwimg[0xBE:]

# Insert firmware size into the fwimg as well (so we can properly check checksum)
fwimg = fwimg[:0xCC] + struct.pack("<I", len(fwimg)) + fwimg[0xD0:]

# Clear the checksum before calculating it
fwimg = fwimg[:0xE0] + (b'\x00' * 16) + fwimg[0xF0:]
fwimg = fwimg[:0xE0] + hashlib.sha256(fwimg).digest()[:16] + fwimg[0xF0:]

open(sys.argv[1], "r+b").write(fwimg)

