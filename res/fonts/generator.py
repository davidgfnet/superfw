#!/usr/bin/env python3
# -*- coding: utf-8 -*-

# Copyright (C) 2024 David Guillen Fandos <david@davidgf.net>
#
# This program is free software: you can redistribute it and/or
# modify it under the terms of the GNU General Public License as
# published by the Free Software Foundation, either version 3 of the
# License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see
# <http://www.gnu.org/licenses/>.


# Parses UNSCII font hex files and generates font asset databases.
#
# The database format contains a header and a list of blocks, followed by
# character (pixel) data for font rendering. The format looks like:
#
# Header:
#  bytes: ['F', 'O']
#  byte:  version (set to 1)
#  byte:  number of blocks
#  int32: file/database size in bytes
#
# Block index entry:
#  int32: start character unicode point
#  int32: end character unicode point
#  int32: flags
#      bit0: set if font is 16pixel fixed width, 0 if it's variable
#      bit31: requires multi-char composition (for now only hangul supported)
#  int32: offset (bytes, after the index)
#
# Pixel data is represented as a succession of uint16 that represent a 1bpp
# column image. For fixed width fonts (16px wide) there are a fixed number of
# 16 columns (32 bytes in total) drawn left to right. For variable width font
# there is an initial lookup table (kind of an index) that contains:
#
# N * uint16 index (N is the number of characters in the block)
#   Each uint16 encode 3 bits of char width [1...8] and a 13 bit offset.
# K * uint16 data block (where K is an arbitrary number, up to N*8).
#   The data represents columns just as fixed width fonts. The index offset
#   points to an entry in the data block. Data can de deduplicated.

import os, sys, math, argparse, struct

DEF_BLOCKS = "ascii,latin,latin-a,latin-b,greek,cyrilic"
SPACE_PIXELS = 4    # Must be at least 1!

parser = argparse.ArgumentParser(prog='efontgen')
parser.add_argument('--font-files', dest='fontfiles', nargs='+', help='List of font files, order matters')
parser.add_argument('--font-blocks', dest='blocks', type=str, default=DEF_BLOCKS, help='Comma separated list of font blocks')
parser.add_argument('--output', dest='out', required=True, help='Output header file that contains font data')
parser.add_argument('--debug-png', dest='dbgpng', type=str, default=None, help='Debug font PNG file')
args = parser.parse_args()

def atrim(l):
  while l and l[0] == 0:
    l = l[1:]
  while l and l[-1] == 0:
    l = l[:-1]
  return l

def chash(l):
  ret = 0
  for x in l:
    ret = (ret << 16) | x
  return ret

# Block lists
blocks = {
  "ascii":    (     0,   0x7F,  8),
  "latin":    (  0x80,   0xFF,  8),   # Most european latin-based languages
  "latin-a":  ( 0x100,  0x17F,  8),
  "latin-b":  ( 0x180,  0x24F,  8),
  "greek":    ( 0x370,  0x3FF,  8),   # greek
  "cyrilic":  ( 0x400,  0x4FF,  8),   # russian/ukr/bul...
  "hangul-j": (0x1100, 0x11FF, 16),   # Hangul Jamo Characters
  "check":    (0x2610, 0x2611,  8),   # Symbols (ticks)
  "arrows":   (0x2BC5, 0x2BC8,  8),   # Symbols (arrows)
  "arrows2":  (0x25B8, 0x25B8,  8),   # Right arrow small
  "cjk-sym":  (0x3000, 0x3009, 16),   # CJK Symbols and Punctuation
  "hiragana": (0x3040, 0x309F, 16),   # Hiragana block
  "katakana": (0x30A0, 0x30FF, 16),   # Katakana block
  "hangul":   (0xAC00, 0xD7A3, 16),   # Hangul Syllables (pre-composed)
  "cjk-uni":  (0x4E00, 0x9FEF, 16),   # Unified CJK Ideographs
  "fixwidth": (0xFF01, 0xFF20, 16),   # Full-width characters

  # Composable char blocks
  "hangul-part": (0x20000, 0x200CC, 16, ["hangul"]),   # Full-width hangul composite syllable.
}

def convchars(hexc):
  assert (len(hexc) in [32, 64])
  ret = [y for y in bytes.fromhex(hexc)]
  if len(ret) == 32:   # 16px wide char
    ret = [ret[i+1] | (ret[i] << 8) for i in range(0, 32, 2)]
  return ret

# Load all characters, guess their sizes and map them into a proper format
fonthex = []
for f in args.fontfiles:
  fonthex += [x.split(":") for x in open(f, "r").read().strip().split("\n")]
charshp = {int(x[0], 16):len(x[1]) // 4 for x in fonthex}
fontmap = {int(x[0], 16):convchars(x[1]) for x in fonthex}


# Calculate the block sizes and ranges, total character count, etc.
pickedblks = set(args.blocks.split(","))
allblocks = sorted([ blocks[x] for x in pickedblks ])

# Merge consecutive blocks, optimization
charblocks = []
for b in allblocks:
  if charblocks and len(charblocks) < 4 and charblocks[-1][1] + 1 == b[0] and charblocks[-1][2] == b[2]:
    charblocks[-1] = (charblocks[-1][0], b[1], b[2], b[3] if len(b) >= 4 else None)
  else:
    charblocks.append((b[0], b[1], b[2], b[3] if len(b) >= 4 else None))

# Force/Override sizes as specified in blocks. We do this since we do not support
# mixing 8-wide and 16-wide chars. It seems only some charsets like Hiragana have issues really.
for bs, be, bsize, *_ in blocks.values():
  for cn in range(bs, be+1):
    charshp[cn] = bsize

# Convert font map to be horizontal
vfontmap = {}
for n, data in fontmap.items():
  cwide = charshp[n]
  e = [0] * cwide
  for r, d in enumerate(data):
    for i in range(cwide):
      if d & (1 << (cwide-1-i)):
        e[i] |= (1 << r)
  vfontmap[n] = e

print("Total number of characters in database", len(vfontmap))


oblocks = []
for startchar, endchar, charwidth, _ in charblocks:
  numchrs = endchar - startchar + 1
  print("Character block", hex(startchar), "-", hex(endchar), charwidth, "wide")
  charws = [len(vfontmap[cn]) for cn in range(startchar, endchar + 1) if cn in vfontmap]
  assert(len(set(charws)) == 1)

  if charwidth == 8:
    # Take the chance to dedup
    chard8 = []
    index8 = []
    for cn in range(startchar, endchar + 1):
      if cn not in vfontmap:
        index8.append((-1, -1))   # Char does not exist
      else:
        chdata = vfontmap[cn]
        assert len(chdata) == 8
        chdata = atrim(chdata)            # Remove space front/back
        if len(chdata) == 0:
          chdata = [0]*SPACE_PIXELS       # Insert a space if the char is empty
        colsw = len(chdata)
        assert (colsw > 0 and colsw <= 8)
        d = b"".join(struct.pack("<H", x) for x in chdata)
        if d in chard8:
          index8.append((colsw, chard8.index(d)))   # Reuse existing char
        else:
          index8.append((colsw, len(chard8)))
          chard8.append(d)                 # Add new char

    offtbl = []
    chard8p = b""
    for e in chard8:
      offtbl.append(len(chard8p) // 2)
      chard8p += e

    print(len(index8), "characters created, with", len(chard8), "unique chars and", sum(len(x) for x in chard8), "bytes")
    # Encode index using 16 bits, 3 for char width, 13 for offset, can encode up to 8192 chars per (merged) block.
    assert all(x[1] < 8192 and x[0] <= 8 for x in index8)
    index8 = [ 0xFFFF if x[0] < 0 else ((x[0] - 1) << 13) | (offtbl[x[1]]) for x in index8 ]
    # Pack index and data contiguously (pad to word boundary)
    d = b"".join(struct.pack("<H", x) for x in index8) + chard8p
    oblocks.append(d + (b"\x00" * (4 - (len(d) % 4))))

  else:
    data16 = b""
    for cn in range(startchar, endchar + 1):
      if cn not in vfontmap:
        # TODO Embedded some character!
        chdata = [0xFFFF] * 16
      else:
        chdata = vfontmap[cn]
      assert len(chdata) == 16
      data16 += b"".join(struct.pack("<H", x) for x in chdata)
    oblocks.append(data16)
    print(len(data16) // 32, "characters created for 16x16 char block", len(data16), "bytes")

with open(args.out, "wb") as ofd:
  # Expand dependant blocks
  extrasets = {}
  for startchar, endchar, charwidth, compblk in charblocks:
    if compblk:
      for dset in compblk:
        extrasets[dset] = None
    assert not (set(extrasets.keys()) & pickedblks)

  # Header plus charblock index
  numhdrs = len(oblocks) + len(extrasets)
  i, off = 0, 0
  blksize = sum(len(b) for b in oblocks) + 16*numhdrs + 8
  ofd.write(struct.pack("<ccBBI", b"F", b"O", 1, numhdrs, blksize))
  for startchar, endchar, charwidth, compblk in charblocks:
    flags = 1 if charwidth == 16 else 0

    if compblk:
      for dset in compblk:
        assert extrasets[dset] is None
        extrasets[dset] = off

    ofd.write(struct.pack("<IIII", startchar, endchar, flags, off))
    off += len(oblocks[i])
    i += 1

  # Pulls extra blocks from composite blocks
  for eblk, baseoff in extrasets.items():
    startchar, endchar, charwidth = blocks[eblk]
    flags = 1 if charwidth == 16 else 0
    flags |= 0x80000000
    ofd.write(struct.pack("<IIII", startchar, endchar, flags, baseoff))

  # Write data for each block type!
  for blk in oblocks:
    ofd.write(blk)


if args.dbgpng:
  allchars = sum((list(range(x[0], x[1]+1)) for x in charblocks), [])
  finalmap = {cn : vfontmap.get(cn, []) for cn in allchars}

  print("Number of characters to output", len(finalmap))
  print("Number of non-empty characters", sum(1 if x else 0 for x in finalmap.values()))
  print("Number of character columns", sum(len(x) for x in finalmap.values()))

  from PIL import Image
  startchar = min(finalmap.keys())
  endchar = max(finalmap.keys())
  ncols = int(math.sqrt(endchar-startchar+1))
  nrows = math.ceil((endchar-startchar+1) / ncols)

  imw, imh = 16*ncols, 16*nrows
  im = Image.new(mode="RGB", size=(imw, imh))
  numch = imw * imh // 128

  for cn in range(startchar, startchar + numch):
    cnpos = cn - startchar
    bx, by = (cnpos % ncols) * 16, (cnpos // ncols) * 16
    if cn not in finalmap:
      continue

    bgcol = 255 if (bx ^ by) & 16 else 190
    for i in range(bx, bx+16):
      for j in range(by, by+16):
        im.putpixel((i, j), (bgcol, bgcol, bgcol))

    for i, d in enumerate(finalmap[cn]):
      for j in range(16):
        if d & (1 << j):
          im.putpixel((bx + i, by + j), (0, 0, 0))

  im.save(args.dbgpng)

