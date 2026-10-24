#!/usr/bin/env python3
# -*- coding: utf-8 -*-

# Tests for the language pack built by res/messages.py.
# Run from the repo root: python3 tests/messages_test.py
# The full pack test needs ./upkr.elf (make upkr.elf), otherwise it is skipped.

import importlib.util, os, struct, subprocess, sys, tempfile, unittest, zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
UPKR = os.path.join(ROOT, "upkr.elf")

argv, sys.argv = sys.argv, ["messages.py"]      # Do not run its command line part
spec = importlib.util.spec_from_file_location("messages", os.path.join(ROOT, "res", "messages.py"))
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)
sys.argv = argv

def blob_strings(blob, count):
  out = []
  for i in range(count):
    off = struct.unpack_from("<H", blob, 2 * i)[0]
    end = blob.index(b"\0", off)
    out.append(blob[off:end].decode("utf-8"))
  return out

class LanguagePack(unittest.TestCase):
  def setUp(self):
    self.strall = m.flatten()

  def check_lang(self, l, conds):
    expected = [v for k, v, c in m.lang_strings(self.strall, "main", l) if not c or c in conds]
    blob = m.lang_blob(self.strall, l, conds)
    self.assertEqual(len(blob) % 4, 0)
    self.assertEqual(blob_strings(blob, len(expected)), expected)
    self.assertLessEqual(len(blob), m.max_blob_size(conds))

  def test_all_languages(self):
    for conds in (set(), {"SUPPORT_NORGAMES"}):
      for l in m.PACK_LANGS:
        self.check_lang(l, conds)

  def test_translations_used(self):
    # Spot check: Portuguese strings come from pt.json, missing ones in English.
    pt = dict((k, v) for k, v, c in m.lang_strings(self.strall, "main", "pt"))
    en = dict((k, v) for k, v, c in m.lang_strings(self.strall, "main", "en"))
    self.assertEqual(pt["MSG_UIS_THEME"], m.load_lang("pt")["MSG_UIS_THEME"])
    self.assertNotEqual(pt["MSG_UIS_THEME"], en["MSG_UIS_THEME"])
    self.assertEqual(pt["MSG_EMPTY"], en["MSG_EMPTY"])    # null in pt.json

  def test_enum_matches(self):
    # Message count in the pack == entries of the C enum (without MSG_COUNT)
    for conds in (set(), {"SUPPORT_NORGAMES"}):
      n = len([1 for k, (v, c) in self.strall["main"].items() if not c or c in conds])
      blob = m.lang_blob(self.strall, "en", conds)
      self.assertEqual(struct.unpack_from("<H", blob, 0)[0], 2 * n)   # First string after the table

  @unittest.skipUnless(os.path.exists(UPKR), "needs ./upkr.elf (make upkr.elf)")
  def test_pack_roundtrip(self):
    for conds in (set(), {"SUPPORT_NORGAMES"}):
      pack = m.build_pack(conds, UPKR, 4)
      magic, ver, nlangs, nmsgs, _, rawsize, crc = struct.unpack_from("<IHHHHII", pack)
      self.assertEqual((magic, ver, nlangs), (m.PACK_MAGIC, m.PACK_VERSION, len(m.PACK_LANGS)))
      index = [struct.unpack_from("<II", pack, 20 + 8 * i) for i in range(nlangs)]
      stream = pack[20 + 8 * nlangs:]
      self.assertEqual(crc, zlib.crc32(stream))
      with tempfile.TemporaryDirectory() as tmp:
        cin, cout = os.path.join(tmp, "in"), os.path.join(tmp, "out")
        with open(cin, "wb") as fd:
          fd.write(stream)
        subprocess.run([UPKR, "-d", cin, cout], check=True, stdout=subprocess.DEVNULL)
        with open(cout, "rb") as fd:
          raw = fd.read()
      self.assertEqual(len(raw), rawsize)
      for l, (off, size) in zip(m.PACK_LANGS, index):
        self.assertEqual(off % 4, 0)
        self.assertEqual(raw[off:off + size], m.lang_blob(self.strall, l, conds))

if __name__ == "__main__":
  unittest.main(verbosity=2)
