#!/usr/bin/env python3
# -*- coding: utf-8 -*-

# Worst case stack usage of the firmware, from GCC's call graph info.
#
# Build the C files with -fstack-usage -fcallgraph-info=su (see the
# "stack-check" Makefile target), then run this on the folder with the .ci
# files. It walks the call graph from the entry point and adds up the stack
# frames of the deepest call chain:
#  - Functions without info (assembly, libc) count as UNKNOWN_FUNC bytes,
#    except the ones in KNOWN_ASM.
#  - Indirect calls (function pointers) count as the deepest chain of the
#    functions that are never called directly (callbacks: progress screens,
#    sort comparators, printf output...), at least UNKNOWN_FUNC (assembly).
#  - Recursion and dynamic (alloca/VLA) frames are reported.
# Fails if the worst case is over --limit bytes.
#
# Usage: stack-check.py [--limit BYTES] [--entry main] <dir with .ci files>

import argparse, os, re, sys

UNKNOWN_FUNC = 64
KNOWN_ASM = {
  "upkr_unpack16": 384 + 48,    # Probability table + saved registers
  "upkr_unpack": 384 + 48,
}
# Functions whose indirect calls only reach assembly routines (UNKNOWN_FUNC):
# the SD driver picks sc_read/write_sectors_w0/w1 (supercard_io.S) from a table.
ASM_INDIRECT = {"sdcard_read_blocks", "sdcard_write_blocks"}

NODE_RE = re.compile(r'node:\s*\{\s*title:\s*"([^"]+)"\s*label:\s*"([^"]*)"')
EDGE_RE = re.compile(r'edge:\s*\{\s*sourcename:\s*"([^"]+)"\s*targetname:\s*"([^"]+)"')
SIZE_RE = re.compile(r'(\d+) bytes \(([^)]*)\)')

def plain(title):
  return title.split(":")[-1]

def load(folder):
  nodes, edges = {}, {}
  for fn in sorted(os.listdir(folder)):
    if not fn.endswith(".ci"):
      continue
    text = open(os.path.join(folder, fn), encoding="utf-8", errors="replace").read()
    for title, label in NODE_RE.findall(text):
      m = SIZE_RE.search(label)
      if m:
        nodes[title] = (int(m.group(1)), m.group(2))
    for src, dst in EDGE_RE.findall(text):
      edges.setdefault(src, []).append(dst)
  return nodes, edges

def main():
  ap = argparse.ArgumentParser()
  ap.add_argument("folder")
  ap.add_argument("--entry", default="main")
  ap.add_argument("--limit", type=int, default=16 * 1024)   # __STACK_USR_SIZE_MIN__
  args = ap.parse_args()

  nodes, edges = load(args.folder)
  if not nodes:
    sys.exit("No call graph info (.ci files) found in %s" % args.folder)

  byname = {}
  for t in nodes:
    byname.setdefault(plain(t), []).append(t)

  def resolve(t):
    if t in nodes:
      return t
    cands = byname.get(plain(t), [])
    return cands[0] if len(cands) == 1 else None

  warnings = set()
  memo = {}

  def depth(t, stack, indirect_cost):
    # Returns (bytes, chain) for the deepest chain starting at function t.
    if t in memo:
      return memo[t]
    if t in stack:
      warnings.add("recursion: " + " -> ".join(plain(x) for x in stack[stack.index(t):] + [t]))
      return (0, [])
    own, kind = nodes[t]
    if "dynamic" in kind and "bounded" not in kind:
      warnings.add("dynamic stack frame: %s (%s)" % (plain(t), kind))
    best = (0, [])
    for dst in edges.get(t, []):
      if "indirect" in dst and plain(t) in ASM_INDIRECT:
        cand = (UNKNOWN_FUNC, ["<indirect call to assembly>"])
      elif "indirect" in dst:
        cand = (indirect_cost, ["<indirect call>"])
      else:
        r = resolve(dst)
        if r:
          cand = depth(r, stack + [t], indirect_cost)
        else:
          cost = KNOWN_ASM.get(plain(dst), UNKNOWN_FUNC)
          cand = (cost, ["%s (%s)" % (plain(dst), "asm" if plain(dst) in KNOWN_ASM else "no info")])
      if cand[0] > best[0]:
        best = cand
    res = (own + best[0], ["%s %d" % (plain(t), own)] + best[1])
    memo[t] = res
    return res

  entry = resolve(args.entry)
  if not entry:
    sys.exit("Entry point %s not found" % args.entry)

  # Functions nobody calls directly: the possible targets of indirect calls.
  called = set()
  for src, dsts in edges.items():
    for d in dsts:
      r = resolve(d)
      if r and r != src:
        called.add(r)
  callbacks = [t for t in nodes if t not in called and t != entry]

  # First pass: indirect calls cost nothing, to find the deepest callback.
  worst_cb = max([depth(t, [], 0) + (t,) for t in callbacks] or [(0, [], None)])
  indirect_cost = max(worst_cb[0], UNKNOWN_FUNC)
  # Second pass: indirect calls can reach that callback.
  memo.clear()
  total, chain = depth(entry, [], indirect_cost)

  print("Worst case stack from %s: %d bytes (limit %d)" % (args.entry, total, args.limit))
  print("  " + "\n  ".join(chain))
  if worst_cb[2]:
    print("Indirect calls counted as %d bytes (deepest callback: %s)" % (indirect_cost, plain(worst_cb[2])))
  for w in sorted(warnings):
    print("warning:", w)
  print("Biggest frames:")
  for t, (b, k) in sorted(nodes.items(), key=lambda x: -x[1][0])[:8]:
    print("  %6d  %s (%s)" % (b, plain(t), k))
  if total > args.limit:
    sys.exit("Stack usage over the limit!")

if __name__ == "__main__":
  main()
