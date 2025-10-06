#!/usr/bin/env python
# -*- coding: utf-8 -*-

# Copyright 2025 David Guillen Fandos <david@davidgf.net>

# NOR manager testing tool. Generates flash metadata (as in a ROM) given a bunch
# of ROMs, to append it to a flash image (ie. reload NOR games)

import os, sys, argparse, struct
from functools import reduce

parser = argparse.ArgumentParser(prog='nor-manager')
parser.add_argument('--fwimage', dest='fwimage', required=True, help='ROM firmware image path')
parser.add_argument('--roms', dest='roms', nargs='+', help='ROM files to append')
parser.add_argument('--output', dest='out', required=True, help='Output image file')
args = parser.parse_args()

fwimage = open(args.fwimage, "rb").read()

# Pad the image to 2MiB
fwimage += (b'\xff' * ((2 << 20) - len(fwimage)))


# List all roms, we take the file basename as it's simpler.
meta = b''
romdata = b''
for fn in args.roms:
  bn = os.path.basename(fn).encode("utf-8")
  data = open(fn, "rb").read()
  topad = (4*1024*1024 - (len(data) % (4*1024*1024))) % (4*1024*1024)
  data += (b"\xff" * topad)

  numblks = (len(data)) // (4*1024*1024)
  baseblk = len(romdata) // (4*1024*1024) + 1
  blkmap = [baseblk + i if i < numblks else 0 for i in range(8)]
  meta += data[0xAC:0xB0] + struct.pack("<BBBB", data[0xBC], numblks, 0, 0)
  meta += struct.pack("<BBBBBBBB", *blkmap)
  meta += bn + (b"\x00" * (256 - len(bn)))

  # Pad ROM to 4MiB bondary
  romdata += data

# Generate header
crc = reduce(lambda a, b: a ^ b, struct.unpack('<'+'I'*(len(meta)//4), meta))

hdr = struct.pack("<III", 0x6A7E60D1, crc ^ len(args.roms), len(args.roms))
hdr += struct.pack("<I", 0) * 32  # Block balancing info
hdr += meta

# Pad metadata too
hdr += (b"\xff" * (2*1024*1024 - len(hdr)))

open(args.out, "wb").write(fwimage + hdr + romdata)

