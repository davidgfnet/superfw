#!/usr/bin/env python3
# -*- coding: utf-8 -*-

# Renders a PC preview of the SuperFW carousel tab (240x160, like the GBA).
#
# Mirrors the drawing code in src/menu.c (render_carousel and friends) and uses
# the real assets: the icon strip, the unscii font and the .cov cover files
# produced by make-covers.py. Useful to check covers before copying them.
#
# Usage:
#   python carousel-preview.py <sd root> <rom dir on the sd, ie. /GBA/> out.png [--gif out.gif]

import argparse, importlib.util, os, re, sys
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

spec = importlib.util.spec_from_file_location("make_covers", os.path.join(HERE, "make-covers.py"))
mc = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mc)

W, H = 240, 160
FG, BG, FT, HI = 16, 17, 18, 19
CAR_BG = mc.BG_INDEX                 # Carousel background palette entry (tinted)
COVER_SPACING, COVER_CENTER_Y = 76, 62

# themes[] in src/menu.c: fg, bg, ft, hi
THEMES = [
  (0xaaaaaa, 0xffffff, 0x000000, 0xcccccc),
  (0xeca551, 0xe7c092, 0x000000, 0xbda27b),
  (0x26879c, 0x8fb1b8, 0x000000, 0x5296a5),
  (0x308855, 0x88aa99, 0x000000, 0x778888),
  (0xad11c8, 0xe47af6, 0x000000, 0xad5dc6),
  (0x222222, 0x444444, 0xeeeeee, 0x737573),
]

# Tab icons for the "chis" build (src/res/icons.h order)
ICON = {"GBCART": 3, "GBCCART": 4, "GBACART": 5, "SMSCART": 6, "NESCART": 7, "BINFILE": 1}
TAB_ICONS = [8, 9, 10, 17, 11, 12, 13, 14]   # Recent, SD, NOR, Carousel, Settings, UI, Tools, Info
CAROUSEL_TAB = 3

def rgb2gba(c24):
  # RGB2GBA(): 24 bit RGB -> BGR555
  return ((c24 >> 19) & 31) | (((c24 >> 11) & 31) << 5) | (((c24 >> 3) & 31) << 10)

def gba2rgb(c):
  # What the screen shows for a BGR555 color
  return tuple(((c >> sh) & 31) << 3 | ((c >> sh) & 31) >> 2 for sh in (0, 5, 10))

def rgb555_mix(a, b, weight):
  # Same as rgb555_mix() in src/carousel_util.c
  ret = 0
  for sh in (0, 5, 10):
    ca, cb = (a >> sh) & 31, (b >> sh) & 31
    ret |= ((ca * (16 - weight) + cb * weight + 8) // 16) << sh
  return ret

def cover_size_at(dist, spacing=COVER_SPACING):
  # Same as cover_size_at() in src/carousel_util.c
  if dist >= spacing:
    return mc.SMALL
  return (mc.BIG - (mc.BIG - mc.SMALL) * dist // spacing) & ~3

def carousel_title(fn):
  # Same as carousel_title() in src/carousel_util.c
  t = os.path.splitext(fn)[0]
  for i in range(1, len(t)):
    if t[i] in "([" and t[i - 1] == " ":
      t = t[:i - 1]
      break
  m = re.match(r"^\d{4} - (.+)$", t)
  return m.group(1) if m else t

def load_font():
  glyphs = {}
  with open(os.path.join(ROOT, "res", "fonts", "unscii-16.hex")) as fd:
    for line in fd:
      code, bits = line.strip().split(":")
      code = int(code, 16)
      if code > 0x24FF:
        continue
      width = len(bits) * 4 // 16    # 8 or 16 pixel wide glyphs
      rows = [int(bits[r * width // 4:(r + 1) * width // 4], 16) for r in range(16)]
      cols = [sum(((rows[r] >> (width - 1 - c)) & 1) << r for r in range(16)) for c in range(width)]
      while cols and cols[0] == 0:
        cols = cols[1:]
      while cols and cols[-1] == 0:
        cols = cols[:-1]
      glyphs[code] = cols or [0] * 4
  return glyphs

FONT = load_font()

def text_width(s):
  return sum(len(FONT.get(ord(ch), FONT[ord("?")])) + 1 for ch in s)

class Screen:
  def __init__(self, theme):
    self.px = bytearray([BG]) * (W * H)
    self.objs = []
    self.bands = []       # Selection highlight rows (icon 63: the highlight color, 50% blended)
    self.theme = theme

  def fill(self, left, top, w, h, color):
    x0, x1 = max(left, 0), min(left + w, W)
    for y in range(max(top, 0), min(top + h, H)):
      if x1 > x0:
        self.px[y * W + x0:y * W + x1] = bytes([color]) * (x1 - x0)

  def blit(self, src, size, left, top):
    x0, x1 = max(0, -left), min(size, W - left)
    if x1 <= x0:
      return
    for y in range(size):
      self.px[(top + y) * W + left + x0:(top + y) * W + left + x1] = src[y * size + x0:y * size + x1]

  def blit_scaled(self, src, size, left, top):
    # Nearest neighbour from the big cover, like carousel_blit_scaled()
    x0, x1 = max(0, -left), min(size, W - left)
    xmap = [x * mc.BIG // size for x in range(size)]
    for y in range(size):
      row = src[(y * mc.BIG // size) * mc.BIG:]
      for x in range(x0, x1):
        self.px[(top + y) * W + left + x] = row[xmap[x]]

  def text(self, s, x, y, maxw=None):
    for ch in s:
      for col in FONT.get(ord(ch), FONT[ord("?")]):
        if maxw is not None and x >= maxw:
          return
        for r in range(16):
          if (col >> r) & 1 and 0 <= x < W:
            self.px[(y + r) * W + x] = FT
        x += 1
      x += 1

  def central(self, s, x, y):
    self.text(s, x - text_width(s) // 2, y)

  def rightj(self, s, x, y):
    self.text(s, x - text_width(s), y)

  def icon(self, x, y, n, trans=False):
    self.objs.append((x, y, n, trans))

  def image(self, icons, tint=None):
    fg, bg, ft, hi = (rgb2gba(c) for c in THEMES[self.theme])
    pal = {FG: gba2rgb(fg), BG: gba2rgb(bg), FT: gba2rgb(ft), HI: gba2rgb(hi)}
    pal[CAR_BG] = gba2rgb(bg if tint is None else tint)
    for i, c in enumerate(mc.PALETTE):
      pal[mc.PAL_BASE + i] = c
    img = Image.new("RGB", (W, H))
    img.putdata([pal.get(p, (255, 255, 255)) for p in self.px])
    for y in self.bands:
      box = (0, y, W, min(y + 16, H))
      img.paste(Image.blend(img.crop(box), Image.new("RGB", (W, box[3] - y), pal[HI]), 0.5), box)
    for x, y, n, trans in self.objs:
      ic = icons.crop((n * 16, 0, n * 16 + 16, 16))
      if trans:   # 50% alpha blending (REG_BLDALPHA = 0x0808)
        a = ic.getchannel("A").point(lambda v: 128 if v > 128 else 0)
        ic.putalpha(a)
      img.paste(ic, (x, y), ic)
    return img

# Carousel systems (car_view / "Sort by system"): GBA, then the emulator
# groups in src/emu.c order.
SYSTEMS = [("GBA", ("gba",)), ("GB/GBC", ("gb", "gbc")), ("NES", ("nes",)), ("SMS", ("sms",)),
           ("GG", ("gg",)), ("SG", ("sg",)), ("SV", ("sv",)), ("NGC", ("ngc",)), ("PCE", ("pce",))]
SORTS = ["Name", "System", "Recently played"]

def system_of(fn):
  ext = os.path.splitext(fn)[1][1:].lower()
  for i, (_, exts) in enumerate(SYSTEMS):
    if ext in exts:
      return i
  return 0

def guessicon(fn):
  ext = os.path.splitext(fn)[1][1:].lower()
  return {"gba": ICON["GBACART"], "gb": ICON["GBCART"], "gbc": ICON["GBCCART"],
          "nes": ICON["NESCART"], "sms": ICON["SMSCART"]}.get(ext, ICON["BINFILE"])

def human_size(sz):
  # Same as human_size() in src/util.c
  if sz < 1024:
    return "1K"
  if sz < 1024 * 1024:
    return "%uK" % (sz >> 10)
  return "%uM" % (sz >> 20)

def target_tint(theme, cov):
  base = rgb2gba(THEMES[theme][1])
  return rgb555_mix(base, cov[2], 6) if cov and cov[2] is not None else base

def fade(c, target):
  # One step per channel, like carousel_update_tint()
  out = 0
  for sh in (0, 5, 10):
    cc, tc = (c >> sh) & 31, (target >> sh) & 31
    cc += (tc > cc) - (tc < cc)
    out |= cc << sh
  return out

def render_view_popup(s, view, sort, row):
  # Like render_carousel_view(): the Start pop up over the plain background.
  s.fill(0, 16, W, H - 16, BG)
  for y in (18, 19, 156, 157):
    s.fill(2, y, W - 4, 1, FG)
  s.fill(2, 18, 2, 140, FG)
  s.fill(W - 4, 18, 2, 140, FG)
  for i, (label, value) in enumerate((("Show", view), ("Sort by", sort))):
    y = 34 + i * 54
    s.central(label, W // 2, y)
    s.central("< %s >" % value, W // 2, y + 20)
    if i == row:
      s.bands.append(y + 20)

def render(games, covers, sel, offset, cpath, theme, icons, tint=None, info=None, letter=False,
           view=None, popup=None):
  s = Screen(theme)
  # Tab bar
  s.fill(0, 0, W, 16, FG)
  for i, ic in enumerate(TAB_ICONS):
    s.icon(i * 16, 0, ic, i != CAROUSEL_TAB)
  if popup:
    render_view_popup(s, *popup)
    return s.image(icons, tint)
  s.fill(0, 16, W, 128, CAR_BG)
  # Bottom bar: folder of the selected game
  s.fill(0, 144, W, 16, FG)
  s.text(games[sel][2] if games else cpath, 8, 144)

  if not games:
    s.central("No games found in this folder", W // 2, 48)
    s.central("or its subfolders.", W // 2, 64)
    return s.image(icons, tint)

  for k in (-2, 2, -1, 1, 0):
    n = sel + k
    if not 0 <= n < len(games):
      continue
    cx = W // 2 + k * COVER_SPACING + offset
    dist = abs(cx - W // 2)
    size = cover_size_at(dist)
    top = COVER_CENTER_Y - size // 2
    left = cx - size // 2
    if dist < COVER_SPACING // 2:
      s.fill(left - 2, top - 2, size + 4, size + 4, FG)
    cov = covers.get(games[n][3])
    if cov:
      if size == mc.BIG:
        s.blit(cov[0], size, left, top)
      elif size == mc.SMALL:
        s.blit(cov[1], size, left, top)
      else:
        s.blit_scaled(cov[0], size, left, top)
    else:
      s.fill(left, top, size, size, FG)
      s.fill(left + 2, top + 2, size - 4, size - 4, HI)
      if cx >= 8 and cx + 8 <= W:
        s.icon(cx - 8, COVER_CENTER_Y - 8, guessicon(games[n][0]))

  fn, fs = games[sel][:2]
  title = carousel_title(fn)
  if text_width(title) <= W - 16:
    s.central(title, W // 2, 105)
  else:
    s.text(title, 8, 105, maxw=W - 8)
  parts = [os.path.splitext(fn)[1][1:].upper(), human_size(fs)]
  if info and offset == 0:
    if info.get("fav"):
      parts.append("Favourite")
    if info.get("save"):
      parts.append("Save")
    if info.get("last"):
      parts.append("Last played")
  s.central(" · ".join(parts), W // 2, 122)

  if len(games) > 1:
    track = W - 16
    w = max(8, (track // len(games)) & ~1)
    x = 8 + (((track - w) * sel // (len(games) - 1)) & ~1)
    s.fill(8, 140, track, 2, FG)
    s.fill(x, 139, w, 4, FT)
  pos = "%d/%d" % (sel + 1, len(games))
  if view and text_width("%s %s" % (view, pos)) <= 92:    # Next to the tab icons
    pos = "%s %s" % (view, pos)
  s.rightj(pos, W - 1, 1)

  if letter:
    ch = title[:1].upper()
    box, left, top = 56, W // 2 - 28, COVER_CENTER_Y - 28
    s.fill(left, top, box, box, FG)
    s.fill(left + 2, top + 2, box - 4, box - 4, HI)
    cols = FONT.get(ord(ch), FONT[ord("?")])
    gx = (16 - min(len(cols), 16)) // 2
    for y in range(48):
      for x in range(48):
        c = x // 3 - gx
        on = 0 <= c < len(cols) and (cols[c] >> (y // 3)) & 1
        s.px[(top + 4 + y) * W + left + 4 + x] = FT if on else HI
  return s.image(icons, tint)

def game_info(sdroot, games, n):
  # Has a save (next to the ROM or in /SAVEGAME/), is the last played game.
  fn, _, folder, full = games[n]
  base = os.path.splitext(fn)[0] + ".sav"
  save = os.path.exists(os.path.splitext(full)[0] + ".sav") or \
         os.path.exists(os.path.join(sdroot, "SAVEGAME", base))
  last = False
  try:
    with open(os.path.join(sdroot, ".superfw", "recent.txt"), encoding="utf-8") as fd:
      for line in fd:
        line = line.strip()
        if line and not line.startswith("nor:"):
          last = line == folder + fn
          break
  except OSError:
    pass
  return {"save": save, "last": last, "fav": folder + fn in favourites(sdroot)}

def favourites(sdroot):
  try:
    with open(os.path.join(sdroot, ".superfw", "favorites.txt"), encoding="utf-8") as fd:
      return {line.rstrip("\r\n") for line in fd}
  except OSError:
    return set()

def main():
  p = argparse.ArgumentParser()
  p.add_argument("sdroot")
  p.add_argument("romdir", help="Dir on the SD card, ie. /GBA/")
  p.add_argument("out")
  p.add_argument("--gif", default=None, help="Also write an animated GIF scrolling through the games")
  p.add_argument("--select", type=int, default=0, help="Selected game index")
  p.add_argument("--theme", type=int, default=0)
  p.add_argument("--scale", type=int, default=3)
  p.add_argument("--letter", action="store_true", help="Show the big letter (as after an Up/Down jump)")
  p.add_argument("--view", default=None, help="Show only: Favourites or a system (GBA, GB/GBC, NES...)")
  p.add_argument("--sort", default="Name", choices=SORTS, help="Order of the games")
  p.add_argument("--popup", type=int, default=None, metavar="ROW",
                 help="Show the Start pop up (view and sort) with this row selected (0 or 1)")
  a = p.parse_args()

  romdir = os.path.join(a.sdroot, a.romdir.strip("/\\"))
  cpath = ("/" + a.romdir.strip("/\\").replace("\\", "/") + "/").replace("//", "/")

  # Same as carousel_scan(): every game in romdir and its subfolders, by name.
  games = []
  for dirpath, dirnames, filenames in os.walk(romdir):
    dirnames[:] = [d for d in dirnames if not d.startswith(".") and d != "System Volume Information"]
    rel = os.path.relpath(dirpath, romdir).replace("\\", "/")
    folder = cpath + ("" if rel == "." else rel + "/")
    for fn in filenames:
      full = os.path.join(dirpath, fn)
      ext = os.path.splitext(fn)[1][1:].lower()
      if not fn.startswith(".") and ext in mc.SYSTEMS:
        games.append((fn, os.path.getsize(full), folder, full))
  games.sort(key=lambda g: g[0].lower())

  # View (Start): filter and order, like carousel_build_view().
  if a.view == "Favourites":
    favs = favourites(a.sdroot)
    games = [g for g in games if g[2] + g[0] in favs]
  elif a.view:
    games = [g for g in games if SYSTEMS[system_of(g[0])][0] == a.view]
  if a.sort == "System":
    games.sort(key=lambda g: system_of(g[0]))     # Stable: by name within a system

  # Same lookup as carousel_open_cover(): <file name>.cov, then <game code>.cov
  covers = {}
  for fn, _, _, full in games:
    names = [fn + ".cov"]
    code = mc.gba_gamecode(full) if fn.lower().endswith(".gba") else None
    if code:
      names.append(code + ".cov")
    for name in names:
      cfn = os.path.join(a.sdroot, mc.COVERS_DIR, name)
      if os.path.exists(cfn):
        with open(cfn, "rb") as fd:
          covers[full] = mc.read_cover(fd.read())    # (big, small, tint)
        break

  icons = Image.open(os.path.join(ROOT, "res", "icons.png")).convert("RGBA")
  up = lambda im: im.resize((W * a.scale, H * a.scale), Image.NEAREST)

  sel = min(a.select, max(0, len(games) - 1))
  tint = target_tint(a.theme, covers.get(games[sel][3])) if games else None
  info = game_info(a.sdroot, games, sel) if games else None
  popup = (a.view or "All games", a.sort, a.popup) if a.popup is not None else None
  up(render(games, covers, sel, 0, cpath, a.theme, icons, tint, info, a.letter,
            a.view, popup)).save(a.out)
  print("Wrote", a.out)

  if a.gif and games:
    # Press "right" through the list, same slide and fade animations as the firmware.
    frames = []
    for target in list(range(1, len(games))) + list(range(len(games) - 2, -1, -1)):
      offset = COVER_SPACING if target > sel else -COVER_SPACING
      sel = target
      goal = target_tint(a.theme, covers.get(games[sel][3]))
      info = game_info(a.sdroot, games, sel)
      for f in range(40):
        tint = fade(tint, goal)
        if f % 2 == 0:
          frames.append(up(render(games, covers, sel, offset, cpath, a.theme, icons, tint, info)))
        dist = abs(offset)
        step = max(2, (dist // 4) & ~1)
        offset = max(0, offset - step) if offset > 0 else min(0, offset + step)
    frames[0].save(a.gif, save_all=True, append_images=frames[1:], duration=33, loop=0)
    print("Wrote", a.gif)

if __name__ == "__main__":
  main()
