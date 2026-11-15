#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Generates perfect hash table and lookup machinery from signature header file

Allowed signatures:

  const uint16_t <name>_sig[] = { 0xb5b0, 0xb0aa, ... };  // halfwords, 0 = wildcard
  const uint32_t <name>_sig[] = { 0x04000204, ... };      // words
  const char     <name>_sig[] = "SRAM_V11";               // string, word aligned in ROM

Uses the first word of each signature as a hash key. Signatures sharing a first word share
a slot, named by their common prefix, ie. flash_v2_verify_sig + flash_v3_verify_sig -> FLASH_V2V3_VERIFY_SIG.

Each ROM word is hashed with N chained ARM ops (t = t OP (t SHIFT n)), ideally 1 for speed.
The result is AND maskd and used to load one entry from a small IWRAM table. If the entry
matches the word, it means this is a valid signature and SIGSLOT(key) is the slot (case label) to check.

Usage: gensighash.py src/save_signatures.h [--max-slots N] [--max-ops 1|2]
"""
import argparse, itertools, re, sys

M32 = 0xFFFFFFFF
SHIFTS = {
  "lsl": lambda x, n: (x << n) & M32,
  "lsr": lambda x, n: x >> n,
  "ror": lambda x, n: ((x >> n) | (x << (32 - n))) & M32,
  # asr doesn't give us much since it replicates the same bit.
}
OPS = {   # Rd = Rn OP shifted (rsb: shifted - Rn). orr/and/bic are worse than eor.
  "eor": ("^", lambda x, y: x ^ y),
  "add": ("+", lambda x, y: (x + y) & M32),
  "sub": ("-", lambda x, y: (x - y) & M32),
}
STAGES = list(itertools.product(OPS, SHIFTS, range(1, 32)))   # (op, shift, amount)

# Parses header signatures
def parse_signatures(path):
  """Returns a list of (name, first_word) for every signature declared in the header."""
  src = open(path).read()
  # Drop comments, keep string literals (strict: no escapes or quotes inside them).
  src = re.sub(r'"[^"\\\n]*"|//[^\n]*|/\*.*?\*/',
               lambda m: m[0] if m[0].startswith('"') else " ", src, flags=re.S)
  decl = re.compile(r'const\s+(uint16_t|uint32_t|char)\s+(\w+)\s*\[\s*\]\s*=\s*(\{[^}]*\}|"[^"]*")\s*;')
  sigs = []
  for m in decl.finditer(src):
    ctype, name, init = m.groups()
    err = lambda msg: sys.exit(f"{path}: {name}: {msg}")
    if not name.endswith("_sig"):
      err("signature names must end in _sig")
    if ctype == "char":
      if not init.startswith('"'):
        err("char signatures must be a string literal")
      text = init[1:-1].encode("ascii")
      if len(text) < 4:
        err("strings need at least 4 characters (one full word)")
      first = int.from_bytes(text[:4], "little")
    else:
      if not init.startswith("{"):
        err("integer signatures must be a { ... } list")
      vals = [v.strip() for v in init[1:-1].split(",") if v.strip()]
      if not all(re.fullmatch(r"0[xX][0-9a-fA-F]+|\d+", v) for v in vals):
        err("only plain hex/decimal values are allowed")
      vals = [int(v, 0) for v in vals]
      bits = 16 if ctype == "uint16_t" else 32
      if any(v >> bits for v in vals):
        err(f"value does not fit in {ctype}")
      first = vals[0] | vals[1] << 16 if bits == 16 and len(vals) >= 2 else vals[0] if vals else 0
      if (bits == 16 and (len(vals) < 2 or 0 in vals[:2])) or (bits == 32 and (not vals or vals[0] == 0)):
        err("the first word must be fully specified (no 0 wildcards)")
    sigs.append((name, first))
  # Anything else declared const is a typo or an unsupported form: refuse it.
  ndecl = len(re.findall(r"^\s*const\b", src, flags=re.M))
  if ndecl != len(sigs):
    sys.exit(f"{path}: {ndecl - len(sigs)} const declaration(s) not in a supported signature form")
  if not sigs:
    sys.exit(f"{path}: no signatures found")
  return sigs

def group_name(names):
  """Slot label: common prefix + sorted concatenated differing middles + common suffix
  (by _-separated tokens). flash_v2_verify_sig + flash_v3_verify_sig -> FLASH_V2V3_VERIFY_SIG"""
  toks = [n.split("_") for n in names]
  if len(toks) == 1:
    return names[0].upper()
  p = 0
  while all(len(t) > p for t in toks) and len({t[p] for t in toks}) == 1:
    p += 1
  s = 0
  while all(len(t) > p + s for t in toks) and len({t[-1 - s] for t in toks}) == 1:
    s += 1
  mids = "".join(sorted("".join(t[p:len(t) - s]) for t in toks))
  return "_".join(toks[0][:p] + [mids] + toks[0][len(toks[0]) - s:]).upper()


# Emulates the ARM instructions to generate the hash value
def calc_hash(stages, w):
  for op, sh, n in stages:
    w = OPS[op][1](w, SHIFTS[sh](w, n))
  return w

# Given a mask of N bits to mask return all possible positions operand 2
# shifts. Increments are even, but with a mask of 7 or less bits you can do 1.
def index_positions(n):
  return range(0, 33 - n, 1 if n < 8 else 2)

# Find the cheapest possible hash (fewer nops, smaller table size)
def find_hash(keys, max_ops, max_slots):
  min_table_bits = (len(keys) - 1).bit_length()  # ceil(log2(key count - 1))
  # Keep adding ops, start with one
  for nops in range(1, max_ops + 1):
    # Start with the smallest possible table, up to the max allowed.
    for bits in range(min_table_bits, max_slots.bit_length()):
      mask = (1 << bits) - 1
      # Try all possible operations and shift types.
      for stages in itertools.product(STAGES, repeat=nops):
        t = [calc_hash(stages, k) for k in keys]
        # We try differnt bit positions for the final AND masking.
        # We require that the AND operation can fit in a single AND ARM inst.
        for s in index_positions(bits):
          # checks if the hash is perfect
          if len({(x >> s) & mask for x in t}) == len(keys):
            return stages, s, bits
  return None

def hash_code(stages, s, bits):
  lines = ["k = (k %s (op%s(k, %d)));" % (OPS[op][0], sh, n) for op, sh, n in stages]
  lines.append("k = k & 0x%08x;" % (((1 << bits) - 1) << s))
  return lines

def emit(groups, stages, s, bits):
  mask = ((1 << bits) - 1) << s
  slotnum = lambda w: (calc_hash(stages, w) & mask) >> s

  # Empty table entries are usually zero, must be a value that can't fall in that slot tho.
  tbl = [next(v for v in itertools.count() if slotnum(v) != i) for i in range(1 << bits)]
  assert all(slotnum(tbl[i]) != i for i in range(len(tbl)))

  # Fill slots with the matching words
  for g in groups:
    tbl[slotnum(g[1])] = g[1]

  o = ["// Autogenerated by tools/gensighash.py, do not edit.", "",
       "#ifndef __SIGHASH_H__", "#define __SIGHASH_H__", "",
       "#define SIGSLOT(key)  ((key) >> %d)" % s, ""]
  for label, w, names in groups:
    o.append("#define %30s %4d   // 0x%08x: %s" % (label, slotnum(w), w, ', '.join(names)))
  o += ["", 'static const uint32_t sighash_tbl[] IWRAM_RODATA = {']
  for i in range(0, len(tbl), 4):
    o.append("  " + " ".join("0x%08x," % tbl[j] for j in range(i, i + 4)))
  o += ["};", "",
        "#define oplsl(x, y) ((x) << (y))", "#define oplsr(x, y) ((x) >> (y))",
        "#define opror(x, y) (((x) >> (y)) | ((x) << (32 - (y))))", "",
        "static inline uint32_t sighash_lookup(uint32_t k, uint32_t *key) {" ]
  for ln in hash_code(stages, s, bits):
    o += ['  ' + ln]
  o += ["  *key = k;", "  return sighash_tbl[SIGSLOT(k)];", "}", "", "#endif", ""]

  assert all(tbl[slotnum(w)] == w for _, w, _ in groups)
  return "\n".join(o)


def main():
  ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
  ap.add_argument("signatures", help="signature header to parse (e.g. src/save_signatures.h)")
  ap.add_argument("--max-slots", type=int, default=128, help="largest table allowed (power of two)")
  ap.add_argument("--max-ops", type=int, default=2)
  args = ap.parse_args()

  if args.max_slots > 256:
    sys.exit("max_slots can't be larger than 256 with the current implementation!")

  # Builds a word -> [signatures] map by parsing all signatures.
  byword = {}
  for name, w in parse_signatures(args.signatures):
    byword.setdefault(w, []).append(name)

  # Groups signatures in case they share the same first word. (sig, word, [signatures])
  groups = [(group_name(names), w, names) for w, names in byword.items()]
  if len({g[0] for g in groups}) != len(groups):
    sys.exit("signature name collision!")

  # Finds the most efficient hash function. It returns a descr of the stages, shift and mask amounts
  impl = find_hash([g[1] for g in groups], args.max_ops, args.max_slots)
  if impl is None:
    sys.exit("no perfect hash found: raise --max-slots or --max-ops")

  stages, s, bits = impl
  hdr = emit(groups, stages, s, bits)
  sys.stdout.write(hdr)

if __name__ == "__main__":
  main()

