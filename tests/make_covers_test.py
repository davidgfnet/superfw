#!/usr/bin/env python3
# -*- coding: utf-8 -*-

# Tests for tools/make-covers.py and tools/carousel-preview.py (no network).
# Run: python3 tests/make_covers_test.py

import importlib.util, io, json, os, struct, sys, tempfile, unittest, zipfile, zlib

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.join(os.path.dirname(HERE), "tools")

def load(name, fn):
  spec = importlib.util.spec_from_file_location(name, os.path.join(TOOLS, fn))
  mod = importlib.util.module_from_spec(spec)
  spec.loader.exec_module(mod)
  return mod

mc = load("make_covers", "make-covers.py")
pv = load("carousel_preview", "carousel-preview.py")

from PIL import Image

def gba_rom(path, code, title="TEST", good_checksum=True):
  h = bytearray(0xC0)
  h[0:4] = b"\x2e\x00\x00\xea"
  h[0xA0:0xAC] = title.encode().ljust(12, b"\0")
  h[0xAC:0xB0] = code.encode()
  h[0xB2] = 0x96
  h[0xBD] = (-(sum(h[0xA0:0xBD]) + 0x19)) & 0xFF
  if not good_checksum:
    h[0xBD] ^= 0xFF
  with open(path, "wb") as fd:
    fd.write(h)

BOXARTS = [
  "Pokemon - Emerald Version (USA, Europe)",
  "Pokemon - Ruby Version (USA)",
  "Pokemon - Ruby Version (Europe)",
  "Metroid Fusion (USA)",
  "Metroid Fusion (Japan)",
  "Legend of Zelda, The - The Minish Cap (USA)",
  "Dragon Ball Z - The Legacy of Goku II International (Japan)",
  "2 Games in 1 - Dragon Ball Z - The Legacy of Goku I _ II (USA)",
  "Dragon Ball GT - Transformation (USA)",
  "Super Mario Advance 4 - Super Mario Bros. 3 (USA)",
]

class NameMatching(unittest.TestCase):
  @classmethod
  def setUpClass(cls):
    # A cached (fresh) index, so ThumbIndex never touches the network.
    cls.tmp = tempfile.TemporaryDirectory()
    tree = [["Named_Boxarts/%s.png" % n, "100644"] for n in BOXARTS]
    with open(os.path.join(cls.tmp.name, "REPO.json"), "w", encoding="utf-8") as fd:
      json.dump(tree, fd)
    cls.idx = mc.ThumbIndex(cls.tmp.name)

  @classmethod
  def tearDownClass(cls):
    cls.tmp.cleanup()

  def match(self, romname):
    m = self.idx.find("REPO", romname)
    return m[0] if m else None

  def test_norm(self):
    self.assertEqual(mc.norm("1234 - Legend of Zelda, The - The Minish Cap (U)(Trashman)"),
                     "legendofzeldaminishcap")
    self.assertEqual(mc.norm("Lilo & Stitch"), mc.norm("Lilo _ Stitch (USA)"))

  def test_exact_and_normalized(self):
    self.assertEqual(self.match("Metroid Fusion (USA).gba"), "Metroid Fusion (USA)")
    self.assertEqual(self.match("Metroid Fusion (U).gba"), "Metroid Fusion (USA)")
    self.assertEqual(self.match("1190 - Super Mario Advance 4 - Super Mario Bros 3 (E)(Menace).gba"),
                     "Super Mario Advance 4 - Super Mario Bros. 3 (USA)")

  def test_region_preference(self):
    self.assertEqual(self.match("Pokemon Ruby.gba"), "Pokemon - Ruby Version (USA)")

  def test_prefix_and_suffix(self):
    # The suffix match (closer) must win over "Goku II International".
    self.assertEqual(self.match("Dragon Ball Z - The Legacy of Goku I & II (USA).gba"),
                     "2 Games in 1 - Dragon Ball Z - The Legacy of Goku I _ II (USA)")

  def test_fuzzy(self):
    self.assertEqual(self.match("Dragonball GT - Transformation # GBA.GBA"),
                     "Dragon Ball GT - Transformation (USA)")

  def test_no_match(self):
    self.assertIsNone(self.match("My Homebrew Game.gba"))

DAT = '''clrmamepro (
\tname "Test"
)

game (
\tname "Metroid Fusion (USA)"
\tregion "USA"
\tserial "AMTE"
\trom ( name "Metroid Fusion (USA).gba" size 1024 crc %08X md5 0 sha1 0 serial "AMTE" )
)
game (
\tname "Metroid Fusion (Japan)"
\tserial "AMTJ"
\trom ( name "Metroid Fusion (Japan).gba" size 1024 crc 00000001 serial "AMTJ" )
)
game (
\tname "Some NES Game (USA)"
\trom ( name "Some NES Game (USA).nes" size 32 crc %08X )
)
'''

class Identify(unittest.TestCase):
  def setUp(self):
    self.tmp = tempfile.TemporaryDirectory()
    self.gba = os.path.join(self.tmp.name, "renamed.gba")
    with open(self.gba, "wb") as fd:
      fd.write(b"0123456789abcdef")
    self.nes_body = bytes(range(32))
    self.nes = os.path.join(self.tmp.name, "game.nes")
    with open(self.nes, "wb") as fd:
      fd.write(b"NES\x1a" + bytes(12) + self.nes_body)   # 16 byte iNES header
    text = DAT % (zlib.crc32(b"0123456789abcdef"), zlib.crc32(self.nes_body))
    # Cached DATs (fresh), so nothing is downloaded.
    for repo in ("GBA", "NES"):
      with open(os.path.join(self.tmp.name, "nointro-%s.dat" % repo), "w", encoding="utf-8") as fd:
        fd.write(text)
    self.ni = mc.NoIntro(self.tmp.name)

  def tearDown(self):
    self.tmp.cleanup()

  def test_single_titles(self):
    single = self.ni.single_titles("GBA")
    self.assertIn(mc.norm("Some NES Game"), single)
    self.assertNotIn(mc.norm("Metroid Fusion"), single)    # USA and Japan
    self.assertEqual(self.ni.size("GBA", "Metroid Fusion (USA)"), 1024)

  def test_parse(self):
    bycrc, byserial = mc.parse_dat(DAT % (5, 6))
    self.assertEqual(bycrc[5], "Metroid Fusion (USA)")
    self.assertEqual(bycrc[1], "Metroid Fusion (Japan)")
    self.assertEqual(bycrc[6], "Some NES Game (USA)")
    self.assertEqual(byserial["AMTE"], ["Metroid Fusion (USA)"])

  def test_by_checksum(self):
    self.assertEqual(self.ni.identify("GBA", self.gba), ("Metroid Fusion (USA)", "checksum"))

  def test_headerless_checksum(self):
    self.assertEqual(self.ni.identify("NES", self.nes), ("Some NES Game (USA)", "checksum"))

  def test_by_game_code(self):
    with open(self.gba, "wb") as fd:
      fd.write(b"modified rom....")
    self.assertEqual(self.ni.identify("GBA", self.gba, "AMTJ"), ("Metroid Fusion (Japan)", "game code"))
    self.assertEqual(self.ni.identify("GBA", self.gba, "ZZZZ"), (None, None))

  def test_crc_cache(self):
    self.ni.identify("GBA", self.gba)
    self.ni.save()
    again = mc.NoIntro(self.tmp.name)
    self.assertIn(os.path.abspath(self.gba), again.crcs)

def png_bytes(color):
  buf = io.BytesIO()
  Image.new("RGB", (40, 40), color).save(buf, "PNG")
  return buf.getvalue()

# A libretro cheat file with usable and unusable codes (like Emerald's).
RETRO_CHT = '''cheats = 8

cheat0_desc = "Master Code"
cheat0_code = "D8BAE4D9+4864DCE5+A86CDBA5+19BA49B3"
cheat0_enable = false

cheat1_desc = "Max Money"
cheat1_code = "82000568+423F+8200056A+000F"
cheat1_enable = false

cheat2_desc = "No Random Battle"
cheat2_code = "320375D4+0000"
cheat2_enable = false

cheat3_desc = "Action Replay code"
cheat3_code = "00000000+18059EB6+0000E038+00000000"
cheat3_enable = false

cheat4_desc = "Encrypted"
cheat4_code = "A745569AA9B6+FD228345FE5F"
cheat4_enable = false

cheat5_desc = "With hook"
cheat5_code = "1000ABCD+0007+3203A000+0001"
cheat5_enable = false

cheat6_desc = "Patch code"
cheat6_code = "520330CC+0004+04DDF608+0000+12345678+0000+83001000+00FF"
cheat6_enable = false

cheat7_desc = "Patch code, data missing"
cheat7_code = "520330CC+0004+04DDF608+0000"
cheat7_enable = false
'''

class RetroConversion(unittest.TestCase):
  def test_convert(self):
    out = mc.convert_retro_cheats(RETRO_CHT)
    self.assertEqual(out, "Max Money\n82000568+423F 8200056A+000F\n"
                          "No Random Battle\n320375D4+0000\n"
                          "With hook\n3203A000+0001\n"
                          "Patch code\n520330CC+0004 04DDF608+0000 12345678+0000 83001000+00FF\n")

  def test_find(self):
    retro = mc.RetroCheats(tempfile.mkdtemp())
    retro.keyed = [(n, mc.retro_name_key(n)) for n in sorted([
      "Pokemon - Emerald Version (USA, Europe) (Code Breaker).cht",
      "Pokemon - Ruby Version (Code Breaker)(USA).cht",
      "Pokemon Pinball - Ruby _ Sapphire (World) (Code Breaker).cht",
      "Metroid Fusion (World) (Code Breaker).cht",
      "Golden Sun (USA, Europe).cht",
      "Pokemon - Leaf Green Version (V1.1)(USA)(Code Breaker).cht",
      "Mario _ Luigi - Superstar Saga (USA, Europe) (Gameshark).cht"])]
    for official, want in (
        ("Pokemon - Emerald Version (USA, Europe)", "Pokemon - Emerald Version (USA, Europe) (Code Breaker).cht"),
        ("Pokemon - Ruby Version (USA, Europe)", "Pokemon - Ruby Version (Code Breaker)(USA).cht"),
        ("Pokemon - Ruby Version (USA, Europe) (Rev 1)", None),
        ("Pokemon Pinball - Ruby & Sapphire (USA)", "Pokemon Pinball - Ruby _ Sapphire (World) (Code Breaker).cht"),
        ("Metroid Fusion (USA)", "Metroid Fusion (World) (Code Breaker).cht"),
        ("Metroid Fusion (Japan)", None),
        ("Golden Sun (USA, Europe)", "Golden Sun (USA, Europe).cht"),
        ("Golden Sun (Germany)", None),
        ("Pokemon - LeafGreen Version (USA, Europe) (Rev 1)", "Pokemon - Leaf Green Version (V1.1)(USA)(Code Breaker).cht"),
        ("Pokemon - Leaf Green Version (USA) (Rev 1)", "Pokemon - Leaf Green Version (V1.1)(USA)(Code Breaker).cht"),
        ("Mario & Luigi - Superstar Saga (USA, Australia)", None)):
      self.assertEqual(retro.find(official), want, official)

  def test_names(self):
    self.assertEqual(mc.retro_name_key("Pokemon - Feuerrote Edition (G).cht")[1], {"germany"})
    self.assertEqual(mc.retro_name_key("Some Game (UE) (Code Breaker).cht")[1], {"usa", "europe"})
    self.assertEqual(mc.retro_name_key("Some Game (Unknown) (Code Breaker).cht")[1], set())
    self.assertEqual(mc.retro_name_key("Some Game (USA) (Code Breaker).cht")[1], {"usa"})
    keyed = [(n, mc.retro_name_key(n)) for n in (
      "Kuru Kuru Kururin (Europe) (Code Breaker).cht",
      "Mega Man Battle Network 4 - Blue Moon (USA).cht",
      "Mega Man Battle Network (Code Breaker).cht")]
    # Spaces, punctuation and "Mega Man -" vs "Mega Man" do not matter
    self.assertEqual(mc.match_cheat_name("Kurukuru Kururin (Europe)", keyed), keyed[0][0])
    self.assertEqual(mc.match_cheat_name("Mega Man - Battle Network 4 - Blue Moon (USA)", keyed), keyed[1][0])
    # No region in the file name: only for games with a single release
    mmbn = "Mega Man - Battle Network (USA)"
    self.assertIsNone(mc.match_cheat_name(mmbn, keyed))
    self.assertEqual(mc.match_cheat_name(mmbn, keyed, {mc.norm("Mega Man - Battle Network")}), keyed[2][0])

  def test_superfw_format(self):
    # Your own files in SuperFW's format go through the same checks.
    text = "\ufeffMine\n00000000+0001 82000000+0002\n\nEncrypted\n9000ABCD+0001\nOk\n32000000+0001\n"
    self.assertEqual(mc.convert_cheat_file(text), "Mine\n82000000+0002\nOk\n32000000+0001\n")
    self.assertEqual(mc.convert_cheat_file(RETRO_CHT), mc.convert_retro_cheats(RETRO_CHT))
    self.assertIsNone(mc.convert_cheat_file("Bad\n9000ABCD+0001\n"))

  def test_firmware_limits(self):
    # 36 codes fit a 512 byte line, 37 do not (the firmware would reject the file)
    code = "82000000+0001"
    self.assertIsNotNone(mc.cheat_entry("x", "+".join([code] * 36)))
    self.assertIsNone(mc.cheat_entry("x", "+".join([code] * 37)))
    # Patch codes with 31+ data codes are rejected by the firmware too
    self.assertIsNone(mc.cheat_entry("x", "52000000+005D " + " ".join(["12345678+1234"] * 31)))
    # Long titles are cut (on a character boundary)
    title = mc.cheat_entry("\u00e9" * 300, code).split("\n")[0]
    self.assertEqual(len(title.encode("utf-8")), 250)

  def test_nothing_usable(self):
    self.assertIsNone(mc.convert_retro_cheats('cheat0_desc = "x"\ncheat0_code = "9000ABCD+0001"\n'))
    self.assertIsNone(mc.convert_retro_cheats('cheat0_desc = "x"\ncheat0_code = "4300ABCD+0001"\n'))

class EndToEnd(unittest.TestCase):
  # Runs the whole tool on a fake SD card with the network replaced.
  def setUp(self):
    self.tmp = tempfile.TemporaryDirectory()
    self.sd = os.path.join(self.tmp.name, "sd")
    self.cache = os.path.join(self.tmp.name, "cache")
    os.makedirs(os.path.join(self.sd, "GBA", "Sub"))
    os.makedirs(self.cache)
    gba_rom(os.path.join(self.sd, "GBA", "Sub", "renamed rom.gba"), "AMTE")
    with open(os.path.join(self.sd, "GBA", "Pokemon Ruby.gba"), "wb") as fd:
      fd.write(b"\0" * 64)
    repo = mc.SYSTEMS["gba"]
    tree = [["Named_Boxarts/%s.png" % n, "100644"] for n in BOXARTS]
    tree.append(["Named_Boxarts/Linked (USA).png", "120000"])
    with open(os.path.join(self.cache, repo + ".json"), "w", encoding="utf-8") as fd:
      json.dump(tree, fd)
    with open(os.path.join(self.cache, "nointro-%s.dat" % repo), "w", encoding="utf-8") as fd:
      fd.write(DAT % (1, 2))
    self.requests = []
    cheats = io.BytesIO()
    with zipfile.ZipFile(cheats, "w") as z:
      z.writestr("AMTE-00.cht", "Infinite missiles\n82000000+0063\n")
      z.writestr("BPEE-00.cht", "Other game\n82000000+0001\n")
    def fake_get(url, retries=3):
      self.requests.append(url)
      if url == mc.CHEATS_RELEASE:
        return json.dumps({"tag_name": "v1", "assets": [
          {"name": "superfw-cheats.zip", "browser_download_url": "https://x/superfw-cheats.zip"}]}).encode()
      if url.endswith("superfw-cheats.zip"):
        return cheats.getvalue()
      if url == mc.RETRO_CHEATS_LIST:
        return json.dumps([{"name": "Metroid Fusion (Japan) (Code Breaker).cht"}]).encode()
      if url.startswith(mc.RETRO_CHEATS_RAW):
        return RETRO_CHT.encode()
      if url == mc.FIRMWARE_RELEASE:
        return json.dumps({"tag_name": "v9", "assets": [
          {"name": "superfw-%s.fw" % b, "browser_download_url": "https://x/superfw-%s.fw" % b}
          for b in mc.BOARDS]}).encode()
      if url.endswith(".fw"):
        return url.rsplit("/", 1)[1].encode()
      if "Linked" in url:
        return b"Metroid Fusion (USA).png"      # Symlinks are served as text
      return png_bytes((200, 40, 40))
    self.real_get, mc.http_get = mc.http_get, fake_get

  def tearDown(self):
    mc.http_get = self.real_get
    self.tmp.cleanup()

  def run_tool(self, *extra):
    argv = sys.argv
    sys.argv = ["make-covers.py", self.sd, "--cache", self.cache] + list(extra)
    try:
      mc.main()
    finally:
      sys.argv = argv
    return sorted(os.listdir(os.path.join(self.sd, mc.COVERS_DIR)))

  def test_covers_written(self):
    covers = self.run_tool()
    # GBA game code cover for the identified ROM, file name cover for the other.
    self.assertEqual(covers, ["AMTE.cov", "Pokemon Ruby.gba.cov"])
    for c in covers:
      with open(os.path.join(self.sd, mc.COVERS_DIR, c), "rb") as fd:
        big, small, tint = mc.read_cover(fd.read())
      self.assertIsNotNone(tint)
    # Already present covers are skipped on the next run.
    n = len(self.requests)
    self.run_tool()
    self.assertEqual(len(self.requests), n)

  def test_cheats(self):
    self.run_tool()
    fn = os.path.join(self.sd, mc.CHEATS_DIR, "AMTE-00.cht")
    with open(fn, encoding="utf-8") as fd:
      self.assertIn("Infinite missiles", fd.read())
    # Only the games on the card get cheats.
    self.assertEqual(os.listdir(os.path.join(self.sd, mc.CHEATS_DIR)), ["AMTE-00.cht"])
    # Edited cheat files are kept.
    with open(fn, "w", encoding="utf-8") as fd:
      fd.write("Mine\n82000000+0001\n")
    self.run_tool()
    with open(fn, encoding="utf-8") as fd:
      self.assertEqual(fd.read(), "Mine\n82000000+0001\n")

  def test_cheats_offline(self):
    self.run_tool()                       # Downloads and caches the pack
    os.remove(os.path.join(self.sd, mc.CHEATS_DIR, "AMTE-00.cht"))
    real = mc.http_get
    def offline(url, retries=3):
      raise OSError("no network")
    mc.http_get = offline
    try:
      self.run_tool()
    finally:
      mc.http_get = real
    self.assertTrue(os.path.exists(os.path.join(self.sd, mc.CHEATS_DIR, "AMTE-00.cht")))

  def test_cheats_from_libretro(self):
    # Not in the SuperFW database: converted from libretro's (No-Intro name).
    gba_rom(os.path.join(self.sd, "GBA", "jp.gba"), "AMTJ")
    self.run_tool()
    with open(os.path.join(self.sd, mc.CHEATS_DIR, "AMTJ-00.cht"), encoding="utf-8") as fd:
      self.assertEqual(fd.read(), mc.convert_retro_cheats(RETRO_CHT))

  def run_events(self, *extra):
    _, args = mc.make_args([self.sd, "--cache", self.cache] + list(extra))
    events = []
    mc.make_covers(args, lambda ev, **kw: events.append((ev, kw)))
    return {kw["romname"]: kw for ev, kw in events if ev == "cheats"}

  def test_cheats_hack(self):
    # Only the game code matches, and it is bigger and named otherwise: a hack,
    # the game's cheats are not used. The report says why.
    gba_rom(os.path.join(self.sd, "GBA", "Metroid Hack Deluxe.gba"), "AMTJ")
    with open(os.path.join(self.sd, "GBA", "Metroid Hack Deluxe.gba"), "ab") as fd:
      fd.write(bytes(2048))
    ev = self.run_events()
    self.assertFalse(os.path.exists(os.path.join(self.sd, mc.CHEATS_DIR, "AMTJ-00.cht")))
    self.assertEqual(ev["Metroid Hack Deluxe.gba"]["short"], "looks like a hack")
    self.assertEqual(ev["renamed rom.gba"]["source"], "SuperFW database")

  def test_cheats_own_folder(self):
    own = os.path.join(self.tmp.name, "mine")
    os.makedirs(own)
    gba_rom(os.path.join(self.sd, "GBA", "jp.gba"), "AMTJ")
    with open(os.path.join(own, "jp.cht"), "w", encoding="utf-8") as fd:
      fd.write(RETRO_CHT)                          # RetroArch format, named like the ROM
    with open(os.path.join(own, "AMTE-00.cht"), "w", encoding="utf-8") as fd:
      fd.write("Mine\n00000000+0001 82000000+0002\n")   # SuperFW format, by game code
    ev = self.run_events("--cheats", own)
    with open(os.path.join(self.sd, mc.CHEATS_DIR, "AMTJ-00.cht"), encoding="utf-8") as fd:
      self.assertEqual(fd.read(), mc.convert_retro_cheats(RETRO_CHT))
    with open(os.path.join(self.sd, mc.CHEATS_DIR, "AMTE-00.cht"), encoding="utf-8") as fd:
      self.assertEqual(fd.read(), "Mine\n82000000+0002\n")    # Before the SuperFW database
    self.assertEqual(ev["jp.gba"]["source"], "your folder (jp.cht)")
    # Your folder works without downloads too
    os.remove(os.path.join(self.sd, mc.CHEATS_DIR, "AMTJ-00.cht"))
    self.run_events("--cheats", own, "--no-download")
    self.assertTrue(os.path.exists(os.path.join(self.sd, mc.CHEATS_DIR, "AMTJ-00.cht")))

  def test_no_cheats(self):
    self.run_tool("--no-cheats")
    self.assertFalse(os.path.exists(os.path.join(self.sd, mc.CHEATS_DIR)))

  def test_firmware(self):
    self.run_tool("--firmware", "chis")
    with open(os.path.join(self.sd, "superfw-chis.fw"), "rb") as fd:
      self.assertEqual(fd.read(), b"superfw-chis.fw")
    self.assertFalse(os.path.exists(os.path.join(self.sd, "superfw-sd.fw")))

  def test_symlink(self):
    img = mc.ThumbIndex(self.cache).download(mc.SYSTEMS["gba"], "Named_Boxarts/Linked (USA).png", True)
    self.assertEqual(img.size, (40, 40))
    self.assertTrue(self.requests[-1].endswith("Metroid%20Fusion%20%28USA%29.png"))

  def test_progress_events(self):
    # What the window shows: one "rom" event per ROM, with its result.
    events = []
    def report(event, **kw):
      events.append((event, kw))
    parser, args = mc.make_args([self.sd, "--cache", self.cache])
    mc.make_covers(args, report)
    self.assertEqual(events[0], ("start", {"total": 2}))
    roms = {kw["romname"]: kw for e, kw in events if e == "rom"}
    self.assertEqual(roms["renamed rom.gba"]["status"], "made")
    self.assertTrue(roms["renamed rom.gba"]["outfn"].endswith("AMTE.cov"))
    self.assertEqual(roms["Pokemon Ruby.gba"]["status"], "made")
    self.assertEqual(events[-1][0], "done")
    # Second run: both present, with the cover file to preview.
    events.clear()
    mc.make_covers(args, report)
    for e, kw in events:
      if e == "rom":
        self.assertEqual(kw["status"], "present")
        self.assertTrue(os.path.exists(kw["outfn"]))

  def test_stop(self):
    events = []
    parser, args = mc.make_args([self.sd, "--cache", self.cache])
    mc.make_covers(args, lambda e, **kw: events.append(e), cancelled=lambda: True)
    self.assertNotIn("rom", events)
    self.assertFalse(os.path.exists(os.path.join(self.sd, mc.COVERS_DIR)))

class GameCode(unittest.TestCase):
  def test_codes(self):
    with tempfile.TemporaryDirectory() as d:
      p = os.path.join(d, "rom.gba")
      gba_rom(p, "AMTE")
      self.assertEqual(mc.gba_gamecode(p), "AMTE")
      gba_rom(p, "AMTE", good_checksum=False)
      self.assertIsNone(mc.gba_gamecode(p))
      gba_rom(p, "amte")
      self.assertIsNone(mc.gba_gamecode(p))
      with open(p, "wb") as fd:
        fd.write(b"\0" * 100)            # Too short
      self.assertIsNone(mc.gba_gamecode(p))

class CoverFormat(unittest.TestCase):
  def test_roundtrip(self):
    # A wide red image: letterboxed top/bottom, red tint.
    img = Image.new("RGB", (160, 80), (220, 30, 30))
    data = mc.cover_bytes(img)
    self.assertEqual(len(data), 16 + mc.BIG * mc.BIG + mc.SMALL * mc.SMALL)
    magic, ver, big, small, pal, tint = struct.unpack_from("<IBBBBH6x", data)
    self.assertEqual((magic, ver, big, small, pal), (mc.COVER_MAGIC, 2, 80, 48, 0))
    self.assertTrue(tint & mc.TINT_VALID)

    bigimg, smallimg, t = mc.read_cover(data)
    r, g, b = t & 31, (t >> 5) & 31, (t >> 10) & 31
    self.assertGreater(r, 20)
    self.assertLess(g, 10)
    self.assertLess(b, 10)

    # Letterbox rows use the carousel background, the image the color cube.
    self.assertTrue(all(p == mc.BG_INDEX for p in bigimg[:mc.BIG * 10]))
    middle = bigimg[mc.BIG * 40:mc.BIG * 41]
    self.assertTrue(all(mc.PAL_BASE <= p < mc.PAL_BASE + 216 for p in middle))

  def test_preview(self):
    # Blue tall image: the sides are the tint (blue), the middle the image.
    data = mc.cover_bytes(Image.new("RGB", (40, 80), (0, 0, 255)))
    img = mc.cover_preview(data)
    self.assertEqual(img.size, (mc.BIG * 2, mc.BIG * 2))
    r, g, b = img.getpixel((0, 80))
    self.assertTrue(b > 200 and r < 40 and g < 40)
    self.assertEqual(img.getpixel((80, 80)), (0, 0, 255))

  def test_v1_files(self):
    data = struct.pack("<IBBBB8x", mc.COVER_MAGIC, 1, 80, 48, 0) + bytes(80 * 80 + 48 * 48)
    self.assertIsNone(mc.read_cover(data)[2])

  def test_bad_files(self):
    good = mc.cover_bytes(Image.new("RGB", (10, 10), (0, 0, 255)))
    with self.assertRaises(ValueError):
      mc.read_cover(good[:-1])
    with self.assertRaises(ValueError):
      mc.read_cover(b"XXXX" + good[4:])

  def test_palette(self):
    # Must match menu_init(): 6 levels per channel, r + 6g + 36b.
    self.assertEqual(len(mc.PALETTE), 216)
    self.assertEqual(mc.PALETTE[0], (0, 0, 0))
    self.assertEqual(mc.PALETTE[215], (255, 255, 255))
    self.assertEqual(mc.PALETTE[5], (255, 0, 0))
    self.assertEqual(mc.PALETTE[30], (0, 255, 0))

class PreviewParity(unittest.TestCase):
  # Same expectations as tests/carousel_util_test.c
  def test_titles(self):
    cases = {
      "Pokemon - Emerald Version (USA, Europe).gba": "Pokemon - Emerald Version",
      "1190 - Super Mario Advance 4 - Super Mario Bros 3 (E)(Menace).gba":
        "Super Mario Advance 4 - Super Mario Bros 3",
      "007 - NightFire (USA, Europe).gba": "007 - NightFire",
      "(Hack) My Game.gba": "(Hack) My Game",
    }
    for fn, title in cases.items():
      self.assertEqual(pv.carousel_title(fn), title)

  def test_sizes_and_mix(self):
    self.assertEqual(pv.cover_size_at(0), 80)
    self.assertEqual(pv.cover_size_at(76), 48)
    self.assertTrue(all(pv.cover_size_at(d) % 4 == 0 for d in range(100)))
    self.assertEqual(pv.rgb555_mix(0, 0x7FFF, 8), 16 | (16 << 5) | (16 << 10))

if __name__ == "__main__":
  unittest.main(verbosity=2)
