#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# SPDX-License-Identifier: GPL-3.0-or-later (like the rest of SuperFW)

# Generates cover art files for the SuperFW carousel tab.
#
# Scans an SD card (or any folder) for game ROMs and writes one cover file per
# ROM at <sd>/.superfw/covers/<rom file name>.cov (ie. "Metroid Fusion.gba.cov").
#
# Cover images come from (in this order):
#  1. --images DIR: an image named like the ROM, with or without the ROM
#     extension (ie. "Metroid Fusion.png" or "Metroid Fusion.gba.png").
#  2. The libretro-thumbnails box art collection (downloaded). The ROM is
#     identified exactly by its checksum (or GBA game code) using the No-Intro
#     game lists, then the box art is looked up by that official name. If that
#     fails, by the ROM file name.
#
# Cover file format (all little endian):
#   uint32  magic "SFCV"
#   uint8   version (2)
#   uint8   big size (80): selected cover, square
#   uint8   small size (48): neighbour covers, square
#   uint8   palette (0): fixed 6x6x6 color cube at palette index 20
#   uint16  background tint (BGR555, bit 15 set if valid)
#   6 bytes reserved (zero)
#   big*big bytes, then small*small bytes: 8bpp palette indices, row-major.
#   Index 236 is the carousel background color (used for letterboxing).
#   (Version 1 files have no tint and use index 17, the menu background.)
#
# Usage:
#   python make-covers.py E:\
#   python make-covers.py E:\ --images C:\my-covers --force
#   python make-covers.py --gui
#
# --gui opens a window to pick the SD card and see the progress, the box art
# picked for each game and a preview of each cover as the cart shows it. The
# packaged .exe (and the macOS / Linux builds) open it when started without
# arguments, ie. double-clicked.
#
# Requires Pillow (pip install pillow).

import argparse, difflib, glob, io, json, os, re, struct, sys, time, zipfile, zlib
import urllib.parse, urllib.request

try:
  from PIL import Image
except ImportError:
  sys.exit("Pillow is required: pip install pillow")

VERSION = "1.4.0"            # Of the tool (shown in the window and --version)

COVER_MAGIC = 0x56434653
COVER_VERSION = 2
BIG, SMALL = 80, 48
PAL_BASE = 20
BG_INDEX = 236
TINT_VALID = 0x8000

COVERS_DIR = os.path.join(".superfw", "covers")
CHEATS_DIR = os.path.join(".superfw", "cheats")

# The SuperFW cheat database: .cht files in the firmware's format, named
# <game code>-<version>.cht like the firmware looks for them.
CHEATS_RELEASE = "https://api.github.com/repos/davidgfnet/superfw-cheats/releases/latest"
# Games not in it: libretro's (RetroArch) Code Breaker cheats, converted.
RETRO_CHEATS_LIST = ("https://api.github.com/repos/libretro/libretro-database/contents/cht/"
                     "Nintendo%20-%20Game%20Boy%20Advance")
RETRO_CHEATS_RAW = ("https://raw.githubusercontent.com/libretro/libretro-database/master/cht/"
                    "Nintendo%20-%20Game%20Boy%20Advance/")
# Firmware releases (--firmware): superfw-<board>.fw files.
FIRMWARE_RELEASE = "https://api.github.com/repos/davidgfnet/superfw/releases/latest"
BOARDS = ("chis", "sd", "lite")
IMAGE_EXTS = (".png", ".jpg", ".jpeg", ".webp", ".bmp", ".gif")

# ROM extension -> libretro-thumbnails repository. Keep in sync with the
# firmware's supported extensions (gba + emulator extensions in src/emu.c).
SYSTEMS = {
  "gba": "Nintendo_-_Game_Boy_Advance",
  "gb":  "Nintendo_-_Game_Boy",
  "gbc": "Nintendo_-_Game_Boy_Color",
  "nes": "Nintendo_-_Nintendo_Entertainment_System",
  "sms": "Sega_-_Master_System_-_Mark_III",
  "gg":  "Sega_-_Game_Gear",
  "sg":  "Sega_-_SG-1000",
  "sv":  "Watara_-_Supervision",
  "ngc": "SNK_-_Neo_Geo_Pocket_Color",
  "pce": "NEC_-_PC_Engine_-_TurboGrafx_16",
}

THUMBS_API = "https://api.github.com/repos/libretro-thumbnails/%s/git/trees/master?recursive=1"
THUMBS_RAW = "https://raw.githubusercontent.com/libretro-thumbnails/%s/master/%s"
USER_AGENT = "superfw-make-covers/1"

# Region preference when several box arts match the same game.
REGION_ORDER = ["usa", "world", "europe", "japan"]

# Fixed palette, must match menu_init() in src/menu.c:
#   value5 = c * 31 / 5 (integer division), index = r + 6*g + 36*b
def build_palette():
  pal = []
  for i in range(216):
    r, g, b = i % 6, (i // 6) % 6, i // 36
    rgb = []
    for c in (r, g, b):
      v5 = c * 31 // 5
      rgb.append((v5 << 3) | (v5 >> 2))   # What the GBA actually shows
    pal.append(tuple(rgb))
  return pal

PALETTE = build_palette()

def palette_image():
  flat = []
  for c in PALETTE:
    flat.extend(c)
  # Pad with copies of color 0, so any index >= 216 is the same color as 0.
  flat.extend(PALETTE[0] * (256 - len(PALETTE)))
  img = Image.new("P", (1, 1))
  img.putpalette(flat)
  return img

PAL_IMG = palette_image()

def convert(img, size):
  # Fit the image in a size x size square (keeping the aspect ratio), then
  # map it to the fixed palette with dithering. Uncovered area -> BG_INDEX.
  img = img.convert("RGBA")
  scale = min(size / img.width, size / img.height)
  w = max(1, round(img.width * scale))
  h = max(1, round(img.height * scale))
  img = img.resize((w, h), Image.LANCZOS)

  canvas = Image.new("RGBA", (size, size), (0, 0, 0, 0))
  canvas.paste(img, ((size - w) // 2, (size - h) // 2))

  rgb = Image.new("RGB", (size, size), (0, 0, 0))
  rgb.paste(canvas, mask=canvas.getchannel("A"))
  q = rgb.quantize(palette=PAL_IMG, dither=Image.Dither.FLOYDSTEINBERG)

  idx = q.tobytes()
  alpha = canvas.getchannel("A").tobytes()
  out = bytearray(size * size)
  for i in range(size * size):
    if alpha[i] < 128:
      out[i] = BG_INDEX
    else:
      p = idx[i]
      out[i] = PAL_BASE + (p if p < 216 else 0)
  return bytes(out)

def tint_color(img):
  # Main color of a cover (BGR555 | TINT_VALID): the most common of its few
  # dominant colors, preferring saturated ones over greys, black and white.
  import colorsys
  small = img.convert("RGBA").resize((32, 32), Image.BILINEAR)
  rgb = Image.new("RGB", small.size, (0, 0, 0))
  rgb.paste(small, mask=small.getchannel("A"))
  q = rgb.quantize(colors=8)
  pal = q.getpalette()
  best, best_score = None, -1.0
  for count, i in q.getcolors():
    r, g, b = pal[i * 3:i * 3 + 3]
    _, sat, val = colorsys.rgb_to_hsv(r / 255, g / 255, b / 255)
    score = count * (0.15 + sat) * (0.3 if val < 0.2 or val > 0.95 else 1.0)
    if score > best_score:
      best, best_score = (r, g, b), score
  r, g, b = best
  return TINT_VALID | (r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10)

def cover_bytes(img):
  hdr = struct.pack("<IBBBBH6x", COVER_MAGIC, COVER_VERSION, BIG, SMALL, 0, tint_color(img))
  return hdr + convert(img, BIG) + convert(img, SMALL)

PNG_KEEP = (b"IHDR", b"PLTE", b"tRNS", b"IDAT", b"IEND")

def open_image(data):
  # Opens image bytes. PNGs keep only the chunks needed for the picture: some
  # box arts have broken optional chunks (ie. iCCP with a bad CRC) that make
  # Pillow reject the whole file, while browsers just ignore them.
  if data[:8] == b"\x89PNG\r\n\x1a\n":
    out, i = bytearray(data[:8]), 8
    while i + 8 <= len(data):
      n, cid = struct.unpack(">I4s", data[i:i + 8])
      if cid in PNG_KEEP:
        out += data[i:i + 12 + n]
      i += 12 + n
    data = bytes(out)
  return Image.open(io.BytesIO(data))

def read_cover(data):
  # Decodes a .cov file: returns (big, small, tint). Images are palette index
  # bytes, tint is BGR555 or None. Mirrors cover_header_check() in the firmware.
  magic, ver, big, small, pal, tint = struct.unpack_from("<IBBBBH6x", data)
  if magic != COVER_MAGIC or ver not in (1, 2) or (big, small, pal) != (BIG, SMALL, 0):
    raise ValueError("not a valid cover file")
  if len(data) != 16 + big * big + small * small:
    raise ValueError("bad cover file size")
  imgs = []
  off = 16
  for sz in (big, small):
    imgs.append(data[off:off + sz * sz])
    off += sz * sz
  valid = ver == 2 and (tint & TINT_VALID)
  return imgs[0], imgs[1], (tint & 0x7FFF) if valid else None

# ---- Name matching -------------------------------------------------------

def norm(name):
  # "1234 - Legend of Zelda, The - The Minish Cap (U)(Trashman)" -> "legendofzeldaminishcap"
  n = name.lower()
  n = re.sub(r"^\d{3,4}\s*-\s*", "", n)          # Scene release numbers
  n = re.sub(r"\([^)]*\)|\[[^\]]*\]", " ", n)     # (Region) [tags]
  n = re.sub(r"\bthe\b", " ", n)
  n = n.replace("&", " and ").replace("_", " ")   # libretro uses _ for &
  n = re.sub(r"\band\b", " ", n)
  return re.sub(r"[^a-z0-9]", "", n)

def region_rank(name):
  tags = " ".join(re.findall(r"\(([^)]*)\)", name)).lower()
  for i, r in enumerate(REGION_ORDER):
    if r in tags:
      return i
  return len(REGION_ORDER)

def libretro_name(name):
  # libretro-thumbnails replaces these characters with "_" in file names.
  return re.sub(r'[&*/:`<>?\\|"]', "_", name)

def http_get(url, retries=3):
  req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
  for attempt in range(retries):
    try:
      with urllib.request.urlopen(req, timeout=30) as r:
        return r.read()
    except Exception:
      if attempt == retries - 1:
        raise
      time.sleep(2)

class ThumbIndex:
  def __init__(self, cache_dir):
    self.cache_dir = cache_dir
    self.repos = {}

  def boxarts(self, repo):
    # Returns {normalized name: [(full name, path, is_symlink)]} for a repo.
    if repo in self.repos:
      return self.repos[repo]

    cachefn = os.path.join(self.cache_dir, repo + ".json")
    tree = None
    if os.path.exists(cachefn) and time.time() - os.path.getmtime(cachefn) < 7 * 86400:
      with open(cachefn, "r", encoding="utf-8") as fd:
        tree = json.load(fd)
    else:
      print("Downloading box art index for %s ..." % repo)
      data = json.loads(http_get(THUMBS_API % repo))
      if data.get("truncated"):
        print("  warning: index is truncated, some games may not be found")
      tree = [[e["path"], e["mode"]] for e in data["tree"]
              if e["path"].startswith("Named_Boxarts/") and e["path"].endswith(".png")]
      os.makedirs(self.cache_dir, exist_ok=True)
      with open(cachefn, "w", encoding="utf-8") as fd:
        json.dump(tree, fd)

    idx = {}
    for path, mode in tree:
      full = path[len("Named_Boxarts/"):-len(".png")]
      idx.setdefault(norm(full), []).append((full, path, mode == "120000"))
    for k in idx:
      idx[k].sort(key=lambda e: (region_rank(e[0]), e[0]))
    self.repos[repo] = idx
    return idx

  def find(self, repo, romname):
    # Returns (box art name, path, is_symlink, score) or None
    idx = self.boxarts(repo)
    base = os.path.splitext(romname)[0]

    # Exact No-Intro name first, then normalized, then fuzzy.
    exact = libretro_name(base)
    for entries in idx.values():
      for e in entries:
        if e[0] == exact:
          return e + (1.0,)

    key = norm(base)
    if not key:
      return None
    if key in idx:
      return idx[key][0] + (1.0,)

    # "Pokemon Ruby" -> "Pokemon - Ruby Version": shortest name that starts with it.
    # "Legacy of Goku I & II" -> "2 Games in 1 - Legacy of Goku I & II": shortest that ends with it.
    # Both kinds compete, the closest (shortest) name wins.
    longer = [k for k in idx if len(k) - len(key) <= max(8, len(key) // 2) and
              ((len(key) >= 6 and k.startswith(key)) or (len(key) >= 10 and k.endswith(key)))]
    if longer:
      best = min(longer, key=len)
      return idx[best][0] + (len(key) / len(best),)

    close = difflib.get_close_matches(key, list(idx.keys()), n=1, cutoff=0.82)
    if close:
      score = difflib.SequenceMatcher(None, key, close[0]).ratio()
      return idx[close[0]][0] + (score,)
    return None

  def download(self, repo, path, is_symlink):
    data = http_get(THUMBS_RAW % (repo, urllib.parse.quote(path)))
    # Symlinks are served as a text file containing the relative target.
    for _ in range(4):
      if data[:8] == b"\x89PNG\r\n\x1a\n" or not is_symlink:
        break
      target = data.decode("utf-8").strip()
      path = os.path.normpath(os.path.join(os.path.dirname(path), target)).replace("\\", "/")
      data = http_get(THUMBS_RAW % (repo, urllib.parse.quote(path)))
    return open_image(data)

# ---- Exact identification (No-Intro DATs) ------------------------------------

NOINTRO_DAT = "https://raw.githubusercontent.com/libretro/libretro-database/master/metadat/no-intro/%s.dat"

def parse_dat(text):
  # clrmamepro DAT -> ({crc32: game name}, {serial: [game names]})
  bycrc, byserial = {}, {}
  for block in re.findall(r'^game \((.*?)^\)', text, re.M | re.S):
    m = re.search(r'^\s*name "((?:[^"\\]|\\.)*)"', block, re.M)
    if not m:
      continue
    name = m.group(1)
    for crc in re.findall(r'\brom \(.*?\bcrc ([0-9A-Fa-f]{8})', block):
      bycrc.setdefault(int(crc, 16), name)
    s = re.search(r'^\s*serial "([^"]+)"', block, re.M)
    if s:
      byserial.setdefault(s.group(1), []).append(name)
  return bycrc, byserial

def dat_sizes(text):
  # clrmamepro DAT -> {game name: size of its first ROM}
  sizes = {}
  for block in re.findall(r'^game \((.*?)^\)', text, re.M | re.S):
    m = re.search(r'^\s*name "((?:[^"\\]|\\.)*)"', block, re.M)
    z = re.search(r'\brom \(.*?\bsize ([0-9]+)', block)
    if m and z:
      sizes[m.group(1)] = int(z.group(1))
  return sizes

def rom_crcs(path):
  # CRC32 of the whole file, plus the CRC32 without the header if it has an
  # iNES (16 bytes) or copier (512 bytes) header: No-Intro DATs list ROMs
  # without them for some systems.
  with open(path, "rb") as fd:
    first = fd.read(4)
  skip = 16 if first == b"NES\x1a" else 512 if os.path.getsize(path) % 1024 == 512 else 0

  crc_all = crc_body = pos = 0
  with open(path, "rb") as fd:
    while True:
      chunk = fd.read(1 << 20)
      if not chunk:
        break
      crc_all = zlib.crc32(chunk, crc_all)
      if skip:
        crc_body = zlib.crc32(chunk[max(0, skip - pos):], crc_body)
      pos += len(chunk)
  return [crc_all, crc_body] if skip else [crc_all]

class NoIntro:
  def __init__(self, cache_dir):
    self.cache_dir = cache_dir
    self.dats = {}
    self.sizes = {}
    self.singles = {}
    self.crcfn = os.path.join(cache_dir, "rom-crcs.json")
    try:
      with open(self.crcfn, "r", encoding="utf-8") as fd:
        self.crcs = json.load(fd)
    except (OSError, ValueError):
      self.crcs = {}

  def dat(self, repo):
    if repo not in self.dats:
      datname = repo.replace("_-_", " - ").replace("_", " ")
      cachefn = os.path.join(self.cache_dir, "nointro-" + repo + ".dat")
      if not (os.path.exists(cachefn) and time.time() - os.path.getmtime(cachefn) < 7 * 86400):
        print("Downloading game list for %s ..." % datname)
        data = http_get(NOINTRO_DAT % urllib.parse.quote(datname))
        os.makedirs(self.cache_dir, exist_ok=True)
        with open(cachefn, "wb") as fd:
          fd.write(data)
      with open(cachefn, "r", encoding="utf-8", errors="replace") as fd:
        text = fd.read()
      self.dats[repo] = parse_dat(text)
      self.sizes[repo] = dat_sizes(text)
    return self.dats[repo]

  def size(self, repo, name):
    self.dat(repo)
    return self.sizes[repo].get(name)

  def single_titles(self, repo):
    # Titles (as retro_name_key() makes them) with a single release (region
    # and revision) in the list: for cheat files that name no region.
    if repo not in self.singles:
      bycrc, byserial = self.dat(repo)
      releases = {}
      for name in set(bycrc.values()) | {n for ns in byserial.values() for n in ns}:
        t, r, v, _ = retro_name_key(name)
        releases.setdefault(t, set()).add((frozenset(r), v))
      self.singles[repo] = {t for t, rel in releases.items() if len(rel) == 1}
    return self.singles[repo]

  def crcs_of(self, path):
    # Reading big ROMs from an SD card is slow: remember them by size and date.
    st = os.stat(path)
    key = os.path.abspath(path)
    c = self.crcs.get(key)
    if c and c[0] == st.st_size and c[1] == int(st.st_mtime):
      return c[2]
    crcs = rom_crcs(path)
    self.crcs[key] = [st.st_size, int(st.st_mtime), crcs]
    return crcs

  def save(self):
    try:
      os.makedirs(self.cache_dir, exist_ok=True)
      with open(self.crcfn, "w", encoding="utf-8") as fd:
        json.dump(self.crcs, fd)
    except OSError:
      pass

  def identify(self, repo, path, gamecode=None):
    # Returns (No-Intro name, how) or (None, None)
    bycrc, byserial = self.dat(repo)
    for crc in self.crcs_of(path):
      if crc in bycrc:
        return bycrc[crc], "checksum"
    if gamecode and gamecode in byserial:
      names = sorted(byserial[gamecode], key=lambda n: (region_rank(n), len(n)))
      return names[0], "game code"
    return None, None

# ---- Main ----------------------------------------------------------------

def find_roms(root):
  for dirpath, dirnames, filenames in os.walk(root):
    # Skip SuperFW's own dir and system/hidden dirs
    dirnames[:] = [d for d in dirnames if not d.startswith(".") and d != "System Volume Information"]
    for fn in sorted(filenames):
      ext = os.path.splitext(fn)[1][1:].lower()
      if ext in SYSTEMS and not fn.startswith("."):
        yield os.path.join(dirpath, fn), fn, ext

def gba_header(path):
  # Game code (ie. "BPEE") and version of a valid GBA ROM header, or
  # (None, None). Must match the checks in carousel_open_cover() (src/menu.c).
  try:
    with open(path, "rb") as fd:
      h = fd.read(0xC0)
  except OSError:
    return None, None
  if len(h) < 0xC0 or h[0xB2] != 0x96:
    return None, None
  if (-(sum(h[0xA0:0xBD]) + 0x19)) & 0xFF != h[0xBD]:
    return None, None  # Bad header checksum
  code = h[0xAC:0xB0].decode("ascii", "replace")
  return (code, h[0xBC]) if re.fullmatch(r"[A-Z0-9]{4}", code) else (None, None)

def gba_gamecode(path):
  return gba_header(path)[0]

# ---- Cheats and firmware ---------------------------------------------------

class CheatPack:
  # The SuperFW cheat database (github.com/davidgfnet/superfw-cheats), cached.
  # Without internet the last downloaded version is used.
  def __init__(self, cache_dir):
    self.cache_dir = cache_dir
    self.files = None

  def load(self):
    if self.files is not None:
      return self.files
    try:
      rel = json.loads(http_get(CHEATS_RELEASE))
      asset = next(a for a in rel["assets"] if a["name"].endswith(".zip"))
      fn = os.path.join(self.cache_dir, "superfw-cheats-%s.zip" % re.sub(r"[^\w.-]", "_", rel["tag_name"]))
      if not os.path.exists(fn):
        data = http_get(asset["browser_download_url"])
        os.makedirs(self.cache_dir, exist_ok=True)
        with open(fn + ".tmp", "wb") as fd:
          fd.write(data)
        os.replace(fn + ".tmp", fn)
    except Exception:
      cached = sorted(glob.glob(os.path.join(self.cache_dir, "superfw-cheats-*.zip")), key=os.path.getmtime)
      if not cached:
        raise
      fn = cached[-1]
    with zipfile.ZipFile(fn) as z:
      self.files = {os.path.basename(n).lower(): z.read(n) for n in z.namelist()
                    if n.lower().endswith(".cht")}
    return self.files

  def get(self, code, version):
    return self.load().get(cheat_name(code, version).lower())

  def others(self, code, version):
    # Other versions of the game it has cheats for (ie. "BPRE-00")
    want = cheat_name(code, version).lower()
    return ["SuperFW database " + n[:-4].upper() for n in sorted(self.load())
            if n.startswith(code.lower() + "-") and n != want]

# GoodTools region letters, used by some libretro cheat file names ("(U)").
GOODTOOLS_REGIONS = {"u": "usa", "e": "europe", "j": "japan", "g": "germany", "f": "france",
                     "s": "spain", "i": "italy", "a": "australia", "w": "world"}

def retro_name_key(name):
  # (title, regions, revision, cheat device) of a No-Intro, libretro or cheat
  # file name: "Pokemon - Ruby Version (Code Breaker)(USA).cht" -> ("pokemon
  # rubyversion", {"usa"}, "", "code breaker"). Titles are compared like cover
  # names (norm(): case, punctuation, "The", "&" and libretro's "_" ignored).
  name = re.sub(r"\.cht$", "", name, flags=re.I)
  title = norm(name.split(" (")[0])
  regions, rev, device = None, "", ""
  for tag in re.findall(r"\(([^)]*)\)", name[len(name.split(" (")[0]):]):
    low = tag.lower().strip()
    if low.replace(" ", "") in ("codebreaker", "gameshark", "actionreplay", "codebreaker,gameshark"):
      device = low
    elif low.startswith("rev "):
      rev = low
    elif re.match(r"v1\.[0-9]$", low):
      rev = "" if low == "v1.0" else "rev " + low[3:]     # libretro's "(V1.1)"
    elif regions is None and low not in ("virtual console", "unknown"):
      if re.fullmatch(r"[uejgfsiaw]{1,2}", low):
        regions = set(GOODTOOLS_REGIONS[c] for c in low)
      else:
        regions = set(r.strip() for r in low.split(","))
  return title, regions or set(), rev, device

def match_cheat_name(official, keyed, single=()):
  # The cheat file (keyed: [(name, retro_name_key(name))]) for a No-Intro
  # name: same title and revision, and a shared region. "(World)" files are
  # taken for USA releases only (Code Breaker codes are made for those, other
  # regions have other addresses), files without a region only for games with
  # a single release (single: titles from NoIntro.single_titles()).
  title, regions, rev, _ = retro_name_key(official)
  best = None
  for name, (t, r, v, dev) in keyed:
    if t != title or v != rev or dev.replace(" ", "") not in ("codebreaker", ""):
      continue
    if not (r & regions or ("world" in r and "usa" in regions) or (not r and title in single)):
      continue
    if best is None or (dev and not best[1]):
      best = (name, dev)          # Prefer the files marked as Code Breaker
  return best[0] if best else None

class RetroCheats:
  # libretro's GBA cheat files, named (mostly) after the No-Intro names. The
  # list is cached for when there is no internet.
  def __init__(self, cache_dir):
    self.cache_dir = cache_dir
    self.keyed = None

  def load(self):
    if self.keyed is None:
      fn = os.path.join(self.cache_dir, "libretro-gba-cheats.json")
      try:
        names = [e["name"] for e in json.loads(http_get(RETRO_CHEATS_LIST)) if e["name"].endswith(".cht")]
        os.makedirs(self.cache_dir, exist_ok=True)
        with open(fn, "w", encoding="utf-8") as fd:
          json.dump(names, fd)
      except Exception:
        with open(fn, encoding="utf-8") as fd:
          names = json.load(fd)
      self.keyed = [(n, retro_name_key(n)) for n in sorted(names)]
    return self.keyed

  def find(self, official, single=()):
    return match_cheat_name(official, self.load(), single)

  def others(self, official):
    # Its files for the same title (other regions or revisions)
    title = retro_name_key(official)[0]
    return ["libretro " + n + ("" if k[1] else " (names no region, and the game has several)")
            for n, k in self.load() if k[0] == title]

  def download(self, name):
    return http_get(RETRO_CHEATS_RAW + urllib.parse.quote(name)).decode("utf-8", "replace")

  def get(self, official, single=()):
    name = self.find(official, single)
    return self.download(name) if name else None

class OwnCheats:
  # --cheats DIR: your own cheat files (RetroArch or SuperFW .cht), named like
  # the ROM, like the game (its No-Intro name, as libretro does) or
  # <game code>-<version>.cht.
  def __init__(self, folder):
    self.files = {}
    for dirpath, _, filenames in os.walk(folder):
      for fn in sorted(filenames):
        if fn.lower().endswith(".cht"):
          self.files.setdefault(fn[:-4].casefold(), os.path.join(dirpath, fn))
    self.keyed = [(stem, retro_name_key(stem)) for stem in sorted(self.files)]

  def find(self, paths, code, version, official, single=()):
    stems = [os.path.splitext(os.path.basename(p))[0].casefold() for p in paths]
    stems.append(cheat_name(code, version)[:-4].casefold())
    for stem in stems:
      if stem in self.files:
        return self.files[stem]
    stem = match_cheat_name(official, self.keyed, single) if official else None
    return self.files[stem] if stem else None

# libretro cheats -> SuperFW, with the rules of scripts/conv-retro.py in
# github.com/davidgfnet/superfw-cheats: only Code Breaker codes ("AAAAAAAA+VVVV"
# or "AAAAAAAAVVVV"), opcode 9 (encrypted) rejected, addresses only in RAM and
# I/O, and no master/hook codes (opcodes 0 and 1: SuperFW runs cheats on VBlank).
# Patch codes (opcode 5) count halfwords, 3 per data code, as the firmware,
# mGBA and VBA-M read them (conv-retro.py counts bytes).
CB_RANGES = ((0x02000000, 0x0203FFFF), (0x03000000, 0x03007FFF), (0x04000000, 0x040003FF),
             (0x05000000, 0x050003FF), (0x06000000, 0x06017FFF), (0x07000000, 0x070003FF))
# The firmware rejects the whole file for a line over 512 bytes, or a patch
# code with 31+ data codes (open_read_cheats(), predecode_cheats()).
CB_MAX_LINE = 512
CB_MAX_TITLE = 250    # Bytes (it keeps 255), cut at a utf-8 character boundary
CB_MAX_PATCH = 30

def cb_patch_codes(value):
  return (value + 2) // 3

def cb_decode(s):
  if any(ch not in "0123456789ABCDEFabcdef +:" for ch in s):
    return None
  toks = s.replace("+", " ").replace(":", " ").split()
  lens = sorted(set(len(t) for t in toks))
  if lens == [4, 8]:
    if len(toks) % 2 or any(len(t) != (8, 4)[i % 2] for i, t in enumerate(toks)):
      return None
    return [(int(toks[i], 16), int(toks[i + 1], 16)) for i in range(0, len(toks), 2)]
  if lens == [12]:
    return [(int(t[:8], 16), int(t[8:], 16)) for t in toks]
  return None

def cb_valid(codes):
  i = 0
  while i < len(codes):
    addr, value = codes[i]
    i += 1
    op = addr >> 28
    if op == 9:
      return False
    if op == 4:
      if i + 1 > len(codes):
        return False       # The firmware would reject the whole file
      i += 1
    elif op == 5:
      n = cb_patch_codes(value)
      if not value or n > CB_MAX_PATCH or i + n > len(codes):
        return False
      i += n
    if op not in (0, 1, 13) and not any(lo <= (addr & 0x0FFFFFFF) <= hi for lo, hi in CB_RANGES):
      return False
  return True

def cb_drop_master(codes):
  out, i = [], 0
  while i < len(codes):
    op = codes[i][0] >> 28
    n = 1
    if op in (0, 1):
      i += 1
      continue
    if op == 4:
      n = 2
    elif op == 5:
      n = 1 + cb_patch_codes(codes[i][1])
    out.extend(codes[i:i + n])
    i += n
  return out

def cheat_entry(desc, codestr):
  # One cheat in SuperFW's format ("title\ncodes\n"), or None if its codes
  # cannot be used.
  desc = desc.strip().encode("utf-8")[:CB_MAX_TITLE].decode("utf-8", "ignore").strip()
  codes = cb_decode(codestr.strip())
  if not desc or not codes or not cb_valid(codes):
    return None
  codes = cb_drop_master(codes)
  line = " ".join("%08X+%04X" % c for c in codes)
  if not codes or len(line) > CB_MAX_LINE:
    return None
  return "%s\n%s\n" % (desc, line)

def convert_retro_cheats(text):
  # A libretro .cht file -> SuperFW .cht text ("title\ncodes" per cheat), or
  # None if none of its cheats can be used.
  out, desc = [], None
  for line in text.splitlines():
    line = line.strip()
    m = re.match(r'cheat[0-9]+_desc\s*=\s*"(.*)"', line)
    if m:
      desc = m.group(1)
      continue
    m = re.match(r'cheat[0-9]+_code\s*=\s*"([^"]+)"', line)
    if m and desc:
      entry = cheat_entry(desc, m.group(1))
      if entry:
        out.append(entry)
      desc = None
  return "".join(out) if out else None

def convert_superfw_cheats(text):
  # A .cht in SuperFW's own format (a title line, then its codes line) -> the
  # cheats SuperFW can use, or None.
  lines = [l.strip() for l in text.splitlines() if l.strip()]
  out = [cheat_entry(t, c) for t, c in zip(lines[0::2], lines[1::2])]
  return "".join(e for e in out if e) or None

def convert_cheat_file(text):
  # Your own .cht file, in RetroArch's or SuperFW's format.
  text = text.lstrip("\ufeff")
  if re.search(r"^\s*cheat[0-9]+_code\s*=", text, re.M):
    return convert_retro_cheats(text)
  return convert_superfw_cheats(text)

def name_fits(official, romname):
  # Whether a ROM file name looks like the game's name.
  a, b = norm(official), norm(os.path.splitext(romname)[0])
  return bool(a and b) and (a in b or b in a or difflib.SequenceMatcher(None, a, b).ratio() >= 0.8)

def looks_like_hack(nointro, repo, path, official, how):
  # Matched only by its game code, bigger than that game and named otherwise:
  # most likely a hack (ie. Pokemon Unbound, a 32 MiB FireRed), whose memory
  # addresses differ, so the game's cheats would write to the wrong places.
  if how != "game code" or not official:
    return False
  size = nointro.size(repo, official)
  try:
    bigger = bool(size) and os.path.getsize(path) > size
  except OSError:
    return False
  return bigger and not name_fits(official, os.path.basename(path))

def cheat_name(code, version):
  return "%s-%02x.cht" % (code, version)    # Like prepare_gba_cheats() (src/menu.c)

def add_cheats(args, games, report, nointro):
  # Writes the cheats of the GBA games on the card ({(code, version): [ROM
  # paths]}) that are not there yet, from (in this order) your --cheats
  # folder, the SuperFW cheat database or libretro's, converted. Existing files
  # are kept: they may have been edited. One "cheats" event per ROM says where
  # its cheats came from, or why it has none.
  outdir = os.path.join(args.sdroot, CHEATS_DIR)
  repo = SYSTEMS["gba"]
  own = OwnCheats(args.cheats) if args.cheats else None
  pack, retro = CheatPack(args.cache), RetroCheats(args.cache)
  lists = {}              # Downloaded (or cached) cheat lists: only once, when needed
  def online():
    if not lists and not args.no_download:
      lists["ok"] = True
      for src, what in ((pack, "the SuperFW cheat database"), (retro, "libretro's cheat list")):
        try:
          src.load()
        except Exception as e:
          report("log", text="Could not get %s: %s" % (what, e))
          lists["ok"] = False
    return lists.get("ok", False)
  counts = dict.fromkeys(("own", "pack", "retro", "present", "none"), 0)
  for (code, version), paths in sorted(games.items()):
    dest = os.path.join(outdir, cheat_name(code, version))
    if os.path.exists(dest):
      counts["present"] += 1
      for p in paths:
        report("cheats", romname=os.path.basename(p), source="already on the card", reason=None, short=None)
      continue
    # The ROM to go by: a known dump first, else one that is not a hack.
    found = []
    for p in paths:
      try:
        official, how = nointro.identify(repo, p, code)
      except Exception:
        official, how = None, None
      found.append((p, official, how))
    pick = (next((f for f in found if f[2] == "checksum"), None) or
            next((f for f in found if f[1] and not looks_like_hack(nointro, repo, *f)), None))
    hack = pick is None and any(f[1] for f in found)
    official = pick[1] if pick else None
    try:
      if not pick:
        names = set(nointro.dat(repo)[0].values())
        named = next((os.path.splitext(os.path.basename(p))[0] for p in paths
                      if os.path.splitext(os.path.basename(p))[0] in names), None)
        if named:                   # Named exactly like a game of the list
          official, hack = named, False
      single = nointro.single_titles(repo) if official else ()
    except Exception:               # No game list (no internet, nothing cached)
      single = ()

    data, source, reason = None, None, None
    if own:
      fn = own.find(paths, code, version, official, single)
      if fn:
        with open(fn, encoding="utf-8", errors="replace") as fd:
          text = convert_cheat_file(fd.read())
        if text:
          data, source = text.encode("utf-8"), "your folder (%s)" % os.path.basename(fn)
          counts["own"] += 1
        else:
          reason = ("%s has no cheats SuperFW can use" % os.path.basename(fn), "no usable codes")
    if data is None and hack:
      reason = ("only its game code matches %s, but it is bigger and named otherwise: it looks like a "
                "hack, whose memory addresses differ" % next(f[1] for f in found if f[1]), "looks like a hack")
    elif data is None and online():
      try:
        data = pack.get(code, version)
        if data is not None:
          source = "SuperFW database"
          counts["pack"] += 1
        elif not official:
          reason = ("not recognised (not in the No-Intro game list: a hack, a translation or a bad dump?)",
                    "not recognised")
        else:
          name = retro.find(official, single)
          text = convert_retro_cheats(retro.download(name)) if name else None
          if text:
            data, source = text.encode("utf-8"), "libretro (%s)" % name
            counts["retro"] += 1
          elif name:
            reason = ("libretro's %s only has codes SuperFW cannot use (encrypted or master codes)" % name,
                      "no usable codes")
          else:
            others = pack.others(code, version) + retro.others(official)
            if others:
              reason = ("cheats only exist for other versions (another region or revision has other memory "
                        "addresses) or an unknown one: %s" % "; ".join(others), "only for other versions")
            else:
              reason = ("not in the SuperFW database nor in libretro's cheats", "none in the databases")
      except Exception as e:
        reason = ("could not download its cheats: %s" % e, "download failed")
    elif data is None and reason is None:
      reason = ("; ".join(([] if not own else ["not in your cheats folder"]) +
                          ([] if args.no_download else ["the cheat lists could not be downloaded"])),
                "not found")

    if data is None:
      counts["none"] += 1
      for p in paths:
        report("cheats", romname=os.path.basename(p), source=None, reason=reason[0], short=reason[1])
      continue
    if not args.dry_run:
      os.makedirs(outdir, exist_ok=True)
      with open(dest, "wb") as fd:
        fd.write(data)
    for p in paths:
      report("cheats", romname=os.path.basename(p), source=source, reason=None, short=None)
  report("log", text="Cheats: %d %s (%d from your folder, %d from the SuperFW database, %d converted from "
         "libretro), %d already there, %d games without cheats" %
         (counts["own"] + counts["pack"] + counts["retro"], "to add" if args.dry_run else "added",
          counts["own"], counts["pack"], counts["retro"], counts["present"], counts["none"]))

def copy_firmware(args, report):
  # The latest release's firmware for the board, to the card root (flash it
  # from SuperFW's SD browser).
  rel = json.loads(http_get(FIRMWARE_RELEASE))
  name = "superfw-%s.fw" % args.firmware
  asset = next((a for a in rel["assets"] if a["name"] == name), None)
  if not asset:
    report("log", text="The latest release (%s) has no %s" % (rel.get("tag_name"), name))
    return
  if not args.dry_run:
    data = http_get(asset["browser_download_url"])
    with open(os.path.join(args.sdroot, name), "wb") as fd:
      fd.write(data)
  report("log", text="Firmware %s (%s) %s the card: flash it from SuperFW's SD browser." %
         (name, rel["tag_name"], "would be copied to" if args.dry_run else "copied to"))

def find_local_image(images_dir, romname):
  if not images_dir:
    return None
  base = os.path.splitext(romname)[0]
  for stem in (romname, base):
    for ext in IMAGE_EXTS:
      p = os.path.join(images_dir, stem + ext)
      if os.path.isfile(p):
        return p
  return None

def default_sdroot():
  # Packaged as an .exe (PyInstaller): use the folder the .exe sits in, so it
  # can be copied to the SD card root and double-clicked.
  if getattr(sys, "frozen", False):
    d = os.path.dirname(os.path.abspath(sys.executable))
    # macOS .app bundle: <folder>/make-covers.app/Contents/MacOS/make-covers
    if d.endswith(os.path.join(".app", "Contents", "MacOS")):
      d = os.path.dirname(os.path.dirname(os.path.dirname(d)))
    return d
  return None

def cover_preview(data, scale=2):
  # What the carousel shows for a .cov file: the big cover on its background
  # (tint color, or the default carousel background), as an RGB image.
  big, small, tint = read_cover(data)
  bg = (40, 40, 48)
  if tint is not None:
    bg = tuple((((tint >> s) & 31) << 3) | (((tint >> s) & 31) >> 2) for s in (0, 5, 10))
  pal = []
  for i in range(256):
    if PAL_BASE <= i < PAL_BASE + 216:
      pal.extend(PALETTE[i - PAL_BASE])
    else:
      pal.extend(bg)
  img = Image.frombytes("P", (BIG, BIG), big)
  img.putpalette(pal)
  img = img.convert("RGB")
  return img.resize((BIG * scale, BIG * scale), Image.NEAREST)

def make_args(argv=None):
  parser = argparse.ArgumentParser(description="Generate SuperFW carousel cover art")
  parser.add_argument("sdroot", nargs="?", default=default_sdroot(),
                      help="SD card root (ie. E:\\) or a folder with ROMs (default for the .exe: its own folder)")
  parser.add_argument("--output", default=None, help="Covers output dir (default: <sdroot>/.superfw/covers)")
  parser.add_argument("--images", default=None, help="Folder with your own cover images, named like the ROMs")
  parser.add_argument("--no-download", action="store_true", help="Only use --images, do not download box art")
  parser.add_argument("--force", action="store_true", help="Regenerate covers that already exist")
  parser.add_argument("--dry-run", action="store_true", help="Only show what would be done")
  parser.add_argument("--no-checksum", action="store_true",
                      help="Do not identify ROMs by checksum (only by file name)")
  parser.add_argument("--cache", default=os.path.join(os.path.expanduser("~"), ".superfw-covers-cache"),
                      help="Where to cache the downloaded box art index")
  parser.add_argument("--no-cheats", action="store_true",
                      help="Do not add the cheats of the GBA games (SuperFW cheat database)")
  parser.add_argument("--cheats", default=None, metavar="DIR",
                      help="Folder with your own cheat files (RetroArch or SuperFW .cht), named like the "
                           "ROMs or the games; used before the downloaded ones")
  parser.add_argument("--firmware", choices=BOARDS, default=None,
                      help="Also copy the latest SuperFW firmware for this board to the card")
  parser.add_argument("--gui", action="store_true", help="Open the window (default when started without arguments)")
  parser.add_argument("--version", action="version", version="make-covers " + VERSION)
  return parser, parser.parse_args(argv)

def print_report(event, **kw):
  # Command line output of make_covers().
  if event == "log":
    print(kw["text"])
  elif event == "rom":
    if kw["status"] == "missing":
      print("NO COVER   %s" % kw["romname"])
    elif kw["status"] in ("made", "would make"):
      print("%s %s  <-  %s  [%s]" % ("WOULD MAKE" if kw["status"] == "would make" else "MADE      ",
                                      kw["romname"], kw["source"], os.path.basename(kw["outfn"])))
  elif event == "cheats":
    if kw["reason"]:
      print("NO CHEATS  %s: %s" % (kw["romname"], kw["reason"]))
    elif kw["source"] != "already on the card":
      print("CHEATS     %s  <-  %s" % (kw["romname"], kw["source"]))
  elif event == "done":
    print()
    print("Covers %s: %d, already present: %d, not found: %d" %
          ("to make" if kw["dry_run"] else "made", kw["made"], kw["skipped"], len(kw["missing"])))
    if kw["missing"]:
      print("For games without a cover, put an image named like the ROM in a folder")
      print("and run again with --images <folder>.")

def make_covers(args, report=print_report, cancelled=lambda: False):
  # Makes the covers for every ROM under args.sdroot. Progress goes to
  # report(event, ...): "log" (text), "start" (total), "rom" (one per ROM, with
  # its status: made, would make, present, missing) and "done" (summary).
  outdir = args.output or os.path.join(args.sdroot, COVERS_DIR)
  thumbs = ThumbIndex(args.cache)
  nointro = NoIntro(args.cache)
  roms = list(find_roms(args.sdroot))
  report("start", total=len(roms))

  made, skipped, missing = 0, 0, []
  done = set()
  codes_used = set()      # Game codes that got a cover file in this run
  cheat_games = {}        # (game code, version) -> paths of the GBA games, for the cheats
  for n, (path, romname, ext) in enumerate(roms):
    if cancelled():
      report("log", text="Stopped.")
      break
    info = dict(index=n, total=len(roms), romname=romname, path=path)
    if romname in done:
      report("rom", status="duplicate", source=None, outfn=None, **info)
      continue      # Same file name in another folder shares the cover
    done.add(romname)

    local = find_local_image(args.images, romname)
    namefn = os.path.join(outdir, romname + ".cov")
    code, version = gba_header(path) if ext == "gba" else (None, None)
    if code:
      cheat_games.setdefault((code, version), []).append(path)
    codefn = os.path.join(outdir, code + ".cov") if code else None

    if not args.force and (os.path.exists(namefn) or (codefn and not local and os.path.exists(codefn))):
      skipped += 1
      report("rom", status="present", source=None,
             outfn=namefn if os.path.exists(namefn) else codefn, **info)
      continue

    # GBA covers are named after the game code (ie. BPEE.cov), so they keep
    # working if the ROM is renamed. Your own images, and a second ROM with the
    # same code (ie. a ROM hack), use the ROM file name instead.
    use_code = bool(codefn) and not local and code not in codes_used
    outfn = codefn if use_code else namefn

    img, source = None, None
    try:
      if local:
        with open(local, "rb") as fd:
          img, source = open_image(fd.read()), local
      elif not args.no_download:
        repo = SYSTEMS[ext]
        m, how = None, None
        if not args.no_checksum:
          try:
            official, how = nointro.identify(repo, path, code)
          except Exception as e:
            report("log", text="  could not identify %s by checksum: %s" % (romname, e))
            official = None
          if official:
            m = thumbs.find(repo, official + ".x")
        if m:
          how = "%s match" % how if m[3] >= 1.0 else "%s match, similar box art" % how
        else:
          m = thumbs.find(repo, romname)
          how = "name match %d%%" % round(m[3] * 100) if m else None
        if m:
          name, tpath, islink, score = m
          source = "%s (%s)" % (name, how)
          if not args.dry_run:
            img = thumbs.download(repo, tpath, islink)
    except Exception as e:
      report("log", text="  error getting cover for %s: %s" % (romname, e))
      img, source = None, None

    if not source:
      missing.append(romname)
      report("rom", status="missing", source=None, outfn=None, **info)
      continue

    if not args.dry_run:
      os.makedirs(outdir, exist_ok=True)
      data = cover_bytes(img)
      with open(outfn, "wb") as fd:
        fd.write(data)
    if use_code:
      codes_used.add(code)
    made += 1
    report("rom", status="would make" if args.dry_run else "made", source=source, outfn=outfn, **info)

  nointro.save()
  if not cancelled() and cheat_games and not args.no_cheats and (args.cheats or not args.no_download):
    try:
      add_cheats(args, cheat_games, report, nointro)
    except Exception as e:
      report("log", text="Could not add the cheats: %s" % e)
  if not cancelled() and not args.no_download:
    if args.firmware:
      try:
        copy_firmware(args, report)
      except Exception as e:
        report("log", text="Could not get the firmware: %s" % e)
  report("done", made=made, skipped=skipped, missing=missing, dry_run=args.dry_run)

def main():
  parser, args = make_args()
  if not args.sdroot:
    parser.error("the SD card root is required, ie. make-covers.py E:\\")
  if not os.path.isdir(args.sdroot):
    sys.exit("Not a directory: %s" % args.sdroot)
  make_covers(args)

# ---- Window ----------------------------------------------------------------

def sd_card_candidates():
  # Folders that look like SD cards: removable drives on Windows, mounted
  # volumes on macOS and Linux.
  found = []
  if sys.platform == "win32":
    import ctypes, string
    mask = ctypes.windll.kernel32.GetLogicalDrives()
    for i, letter in enumerate(string.ascii_uppercase):
      root = letter + ":\\"
      if mask & (1 << i) and ctypes.windll.kernel32.GetDriveTypeW(root) == 2:   # DRIVE_REMOVABLE
        found.append(root)
  else:
    user = os.environ.get("USER", "")
    for base in ("/Volumes", "/media/" + user, "/run/media/" + user, "/media"):
      try:
        for d in sorted(os.listdir(base)):
          p = os.path.join(base, d)
          if os.path.isdir(p) and p not in found and d != user and not os.path.islink(p):
            found.append(p)
      except OSError:
        pass
  return found

def hide_own_console():
  # The Windows .exe is a console program (for command line use). When it was
  # double-clicked the console is its own: hide it while the window is open.
  if sys.platform != "win32" or not getattr(sys, "frozen", False):
    return
  try:
    import ctypes
    k32 = ctypes.windll.kernel32
    procs = (ctypes.c_uint * 4)()
    if k32.GetConsoleProcessList(procs, 4) <= 2:     # This program (and its bootloader) only
      ctypes.windll.user32.ShowWindow(k32.GetConsoleWindow(), 0)
  except Exception:
    pass

STATUS_TEXT = {"made": "Made", "would make": "Would make", "present": "Already there",
               "missing": "Not found", "duplicate": "Same name as another"}

def gui(args):
  import queue, threading
  import tkinter as tk
  from tkinter import ttk, filedialog, messagebox
  from PIL import ImageTk

  if sys.platform == "win32":
    try:
      import ctypes
      ctypes.windll.shcore.SetProcessDpiAwareness(1)   # Sharp text on scaled screens
    except Exception:
      pass
  root = tk.Tk()
  root.title("SuperFW cover maker " + VERSION)
  import tkinter.font
  linesp = tkinter.font.nametofont("TkDefaultFont").metrics("linespace")
  ttk.Style(root).configure("Treeview", rowheight=int(linesp * 1.4))
  root.minsize(720, 460)
  events = queue.Queue()
  stop = threading.Event()
  state = {"worker": None, "rows": {}, "photo": None, "names": {}, "why": {}}

  frm = ttk.Frame(root, padding=10)
  frm.pack(fill="both", expand=True)
  frm.columnconfigure(1, weight=1)
  frm.rowconfigure(7, weight=1)

  ttk.Label(frm, text="SD card:").grid(row=0, column=0, sticky="w")
  sdvar = tk.StringVar(value=args.sdroot or "")
  cands = sd_card_candidates()
  if not sdvar.get() and cands:
    sdvar.set(cands[0])
  sdbox = ttk.Combobox(frm, textvariable=sdvar, values=cands)
  sdbox.grid(row=0, column=1, sticky="ew", padx=6)
  def pick_sd():
    d = filedialog.askdirectory(title="SD card or folder with ROMs", initialdir=sdvar.get() or None)
    if d:
      sdvar.set(os.path.normpath(d))
  ttk.Button(frm, text="Browse...", command=pick_sd).grid(row=0, column=2)

  ttk.Label(frm, text="Own images:").grid(row=1, column=0, sticky="w", pady=(6, 0))
  imgvar = tk.StringVar(value=args.images or "")
  ttk.Entry(frm, textvariable=imgvar).grid(row=1, column=1, sticky="ew", padx=6, pady=(6, 0))
  def pick_images():
    d = filedialog.askdirectory(title="Folder with cover images named like the ROMs")
    if d:
      imgvar.set(os.path.normpath(d))
  ttk.Button(frm, text="Browse...", command=pick_images).grid(row=1, column=2, pady=(6, 0))

  ttk.Label(frm, text="Own cheats:").grid(row=2, column=0, sticky="w", pady=(6, 0))
  chtvar = tk.StringVar(value=args.cheats or "")
  ttk.Entry(frm, textvariable=chtvar).grid(row=2, column=1, sticky="ew", padx=6, pady=(6, 0))
  def pick_cheats():
    d = filedialog.askdirectory(title="Folder with your own cheat files (.cht)")
    if d:
      chtvar.set(os.path.normpath(d))
  ttk.Button(frm, text="Browse...", command=pick_cheats).grid(row=2, column=2, pady=(6, 0))

  opts = ttk.Frame(frm)
  opts.grid(row=3, column=0, columnspan=3, sticky="w", pady=6)
  dlvar = tk.BooleanVar(value=not args.no_download)
  ckvar = tk.BooleanVar(value=not args.no_checksum)
  forcevar = tk.BooleanVar(value=args.force)
  ttk.Checkbutton(opts, text="Download box art", variable=dlvar).pack(side="left")
  ttk.Checkbutton(opts, text="Identify games by checksum", variable=ckvar).pack(side="left", padx=12)
  ttk.Checkbutton(opts, text="Redo existing covers", variable=forcevar).pack(side="left")

  opts2 = ttk.Frame(frm)
  opts2.grid(row=4, column=0, columnspan=3, sticky="w", pady=(0, 6))
  cheatvar = tk.BooleanVar(value=not args.no_cheats)
  ttk.Checkbutton(opts2, text="Add cheats (GBA games)", variable=cheatvar).pack(side="left")
  fwnames = ["Don't copy firmware", "SuperChis", "SuperCard SD", "SuperCard Lite"]
  fwvar = tk.StringVar(value=fwnames[BOARDS.index(args.firmware) + 1] if args.firmware else fwnames[0])
  ttk.Label(opts2, text="Firmware:").pack(side="left", padx=(16, 4))
  ttk.Combobox(opts2, textvariable=fwvar, values=fwnames, state="readonly", width=20).pack(side="left")

  bar = ttk.Frame(frm)
  bar.grid(row=5, column=0, columnspan=3, sticky="ew")
  bar.columnconfigure(1, weight=1)
  gobtn = ttk.Button(bar, text="Make covers")
  gobtn.grid(row=0, column=0)
  prog = ttk.Progressbar(bar, mode="determinate")
  prog.grid(row=0, column=1, sticky="ew", padx=8)
  status = ttk.Label(frm, text="Pick the SD card and press Make covers.")
  status.grid(row=6, column=0, columnspan=3, sticky="w", pady=(4, 6))

  body = ttk.Frame(frm)
  body.grid(row=7, column=0, columnspan=3, sticky="nsew")
  body.columnconfigure(0, weight=1)
  body.rowconfigure(0, weight=1)
  tree = ttk.Treeview(body, columns=("result", "source", "cheats"), show="tree headings", selectmode="browse")
  tree.heading("#0", text="Game")
  tree.heading("result", text="Result")
  tree.heading("source", text="Box art used")
  tree.heading("cheats", text="Cheats")
  tree.column("#0", width=linesp * 12)
  tree.column("result", width=linesp * 7, stretch=False)
  tree.column("source", width=linesp * 13)
  tree.column("cheats", width=linesp * 10)
  tree.grid(row=0, column=0, sticky="nsew")
  sb = ttk.Scrollbar(body, orient="vertical", command=tree.yview)
  sb.grid(row=0, column=1, sticky="ns")
  tree.configure(yscrollcommand=sb.set)

  side = ttk.Frame(body, padding=(10, 0, 0, 0))
  side.grid(row=0, column=2, sticky="n")
  canvas = tk.Canvas(side, width=BIG * 2, height=BIG * 2, highlightthickness=0, background="#282830")
  canvas.pack()
  prevlabel = ttk.Label(side, text="Select a game to see\nits cover as the cart\nshows it.", justify="center")
  prevlabel.pack(pady=6)

  def show_preview(_event=None):
    sel = tree.selection()
    outfn = state["rows"].get(sel[0]) if sel else None
    if sel and sel[0] in state["why"]:
      status.configure(text="No cheats: " + state["why"][sel[0]])
    canvas.delete("all")
    state["photo"] = None
    if not outfn or not os.path.exists(outfn):
      prevlabel.configure(text="No cover file.")
      return
    try:
      with open(outfn, "rb") as fd:
        state["photo"] = ImageTk.PhotoImage(cover_preview(fd.read()))
      canvas.create_image(0, 0, anchor="nw", image=state["photo"])
      prevlabel.configure(text=os.path.basename(outfn))
    except Exception as e:
      prevlabel.configure(text="Cannot read the cover:\n%s" % e)
  tree.bind("<<TreeviewSelect>>", show_preview)

  def worker(wargs):
    def report(event, **kw):
      events.put((event, kw))
    try:
      make_covers(wargs, report, stop.is_set)
    except Exception as e:
      events.put(("error", {"text": str(e)}))
    events.put(("finished", {}))

  def start():
    if state["worker"]:
      stop.set()
      gobtn.configure(state="disabled", text="Stopping...")
      return
    sd = sdvar.get().strip()
    if not sd or not os.path.isdir(sd):
      messagebox.showerror("SuperFW cover maker", "Pick the SD card (or a folder with ROMs) first.")
      return
    images = imgvar.get().strip() or None
    if images and not os.path.isdir(images):
      messagebox.showerror("SuperFW cover maker", "The images folder does not exist.")
      return
    wargs = argparse.Namespace(**vars(args))
    cheats = chtvar.get().strip() or None
    if cheats and not os.path.isdir(cheats):
      messagebox.showerror("SuperFW cover maker", "The cheats folder does not exist.")
      return
    wargs.sdroot, wargs.images, wargs.cheats = sd, images, cheats
    wargs.no_download, wargs.no_checksum, wargs.force = not dlvar.get(), not ckvar.get(), forcevar.get()
    wargs.dry_run = False
    wargs.no_cheats = not cheatvar.get()
    wargs.firmware = BOARDS[fwnames.index(fwvar.get()) - 1] if fwvar.get() != fwnames[0] else None
    tree.delete(*tree.get_children())
    state["rows"].clear()
    state["names"].clear()
    state["why"].clear()
    prog.configure(value=0, maximum=1)
    stop.clear()
    gobtn.configure(text="Stop")
    status.configure(text="Looking for games...")
    state["worker"] = threading.Thread(target=worker, args=(wargs,), daemon=True)
    state["worker"].start()
  gobtn.configure(command=start)

  def poll():
    try:
      while True:
        event, kw = events.get_nowait()
        if event == "start":
          prog.configure(maximum=max(1, kw["total"]))
          status.configure(text="%d games found." % kw["total"])
        elif event == "log":
          status.configure(text=kw["text"].strip())
        elif event == "rom":
          prog.configure(value=kw["index"] + 1)
          status.configure(text="%d / %d  %s" % (kw["index"] + 1, kw["total"], kw["romname"]))
          if kw["status"] != "duplicate":
            iid = tree.insert("", "end", text=kw["romname"],
                              values=(STATUS_TEXT[kw["status"]], kw["source"] or "", ""))
            state["rows"][iid] = kw["outfn"]
            state["names"][kw["romname"]] = iid
            if kw["status"] == "made":
              tree.see(iid)
        elif event == "cheats":
          iid = state["names"].get(kw["romname"])
          if iid:
            tree.set(iid, "cheats", kw["source"] or "none: " + kw["short"])
            if kw["reason"]:
              state["why"][iid] = kw["reason"]
        elif event == "done":
          status.configure(text="Covers made: %d, already there: %d, not found: %d" %
                           (kw["made"], kw["skipped"], len(kw["missing"])) +
                           ("  (games without a cover can use your own images)" if kw["missing"] else ""))
        elif event == "error":
          messagebox.showerror("SuperFW cover maker", kw["text"])
        elif event == "finished":
          state["worker"] = None
          gobtn.configure(state="normal", text="Make covers")
    except queue.Empty:
      pass
    root.after(100, poll)
  poll()

  if os.environ.get("MAKE_COVERS_SELFTEST"):
    # Build check: the window opens, then closes by itself.
    def selftest_done():
      print("window ok")
      root.destroy()
    root.after(1500, selftest_done)
  else:
    hide_own_console()
  root.mainloop()

def want_gui(argv):
  # The window opens with --gui, or when started without arguments in a
  # desktop session (ie. the .exe double-clicked). Plain "python make-covers.py"
  # in a terminal without a display keeps the command line behaviour.
  if "--gui" in argv:
    return True
  if len(argv) > 1 or not getattr(sys, "frozen", False):
    return False
  if sys.platform.startswith("linux") and not os.environ.get("DISPLAY") and not os.environ.get("WAYLAND_DISPLAY"):
    return False
  try:
    import tkinter    # noqa: F401
  except ImportError:
    return False
  return True

if __name__ == "__main__":
  if want_gui(sys.argv):
    gui(make_args()[1])
    sys.exit(0)
  # When double-clicked (.exe without arguments) keep the window open at the end.
  pause = getattr(sys, "frozen", False) and len(sys.argv) == 1
  code = 0
  try:
    main()
  except SystemExit as e:
    if isinstance(e.code, str):
      print(e.code)
    code = 0 if e.code in (None, 0) else 1
  except Exception as e:
    print("Error: %s" % e)
    code = 1
  if pause:
    input("\nPress Enter to close...")
  sys.exit(code)
