#!/usr/bin/env python3
# -*- coding: utf-8 -*-

# Makes the carousel example images in this folder. The games are made up and
# their covers are drawn here (simple shapes), so no real box art ends up in
# the repository. Everything else is the real tab, rendered by
# tools/carousel-preview.py (same layout code and assets as the firmware).
#
# Usage: python docs/images/make-examples.py

import importlib.util, os, subprocess, sys, tempfile
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
TOOLS = os.path.join(ROOT, "tools")

spec = importlib.util.spec_from_file_location("mc", os.path.join(TOOLS, "make-covers.py"))
mc = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mc)

# (file name, background colors top/bottom, drawing)
GAMES = [
  ("Astro Drift.gba",      ((10, 14, 60), (60, 20, 110)),   "planet"),
  ("Bloom Quest.gba",      ((120, 200, 90), (30, 110, 60)), "flower"),
  ("Castle Run.gba",       ((250, 170, 80), (170, 60, 50)), "castle"),
  ("Deep Blue Dive.gba",   ((40, 160, 220), (5, 30, 90)),   "waves"),
  ("Ember Knight.gba",     ((60, 10, 10), (220, 90, 20)),   "sword"),
  ("Frost Peak.gbc",       ((200, 230, 255), (90, 140, 210)), "mountain"),
  ("Moon Garden.gbc",      ((20, 30, 70), (70, 40, 110)),   "moon"),
  ("Pixel Racer.nes",      ((250, 220, 60), (230, 120, 20)), "stripes"),
]

def gradient(size, top, bottom):
  img = Image.new("RGB", (size, size))
  d = ImageDraw.Draw(img)
  for y in range(size):
    t = y / (size - 1)
    d.line([(0, y), (size, y)], fill=tuple(int(a + (b - a) * t) for a, b in zip(top, bottom)))
  return img

def cover(colors, kind, w=240, h=320):
  img = gradient(max(w, h), *colors).crop((0, 0, w, h))
  d = ImageDraw.Draw(img)
  if kind == "planet":
    for x, y in ((30, 40), (200, 70), (60, 250), (190, 280), (120, 30), (220, 200)):
      d.ellipse((x, y, x + 5, y + 5), fill=(255, 255, 220))
    d.ellipse((50, 90, 190, 230), fill=(240, 140, 60))
    d.ellipse((20, 145, 220, 175), outline=(255, 230, 150), width=8)
  elif kind == "flower":
    for a in range(6):
      import math
      cx, cy = 120 + 55 * math.cos(a * math.pi / 3), 150 + 55 * math.sin(a * math.pi / 3)
      d.ellipse((cx - 38, cy - 38, cx + 38, cy + 38), fill=(250, 120, 170))
    d.ellipse((85, 115, 155, 185), fill=(255, 220, 60))
    d.rectangle((114, 205, 126, 320), fill=(40, 120, 40))
  elif kind == "castle":
    d.rectangle((50, 140, 190, 320), fill=(90, 70, 80))
    for x in range(50, 190, 28):
      d.rectangle((x, 120, x + 16, 140), fill=(90, 70, 80))
    d.rectangle((100, 220, 140, 320), fill=(40, 25, 30))
    d.ellipse((160, 30, 220, 90), fill=(255, 240, 200))
  elif kind == "waves":
    for i, y in enumerate(range(120, 330, 40)):
      c = (60 + i * 10, 190 - i * 20, 240 - i * 25)
      d.chord((-40, y - 30, 280, y + 50), 180, 360, fill=c)
    d.ellipse((150, 40, 200, 90), outline=(220, 250, 255), width=6)
  elif kind == "sword":
    d.polygon([(120, 30), (135, 60), (135, 220), (105, 220), (105, 60)], fill=(220, 230, 240))
    d.rectangle((70, 220, 170, 236), fill=(200, 160, 40))
    d.rectangle((112, 236, 128, 300), fill=(110, 60, 30))
  elif kind == "mountain":
    d.polygon([(0, 320), (120, 90), (240, 320)], fill=(70, 90, 140))
    d.polygon([(120, 90), (90, 150), (110, 140), (130, 155), (150, 145)], fill=(250, 250, 255))
    d.ellipse((170, 30, 215, 75), fill=(255, 250, 220))
  elif kind == "moon":
    d.ellipse((60, 50, 180, 170), fill=(250, 240, 190))
    d.ellipse((95, 40, 205, 150), fill=(20, 30, 70))
    for x in range(0, 240, 30):
      d.ellipse((x, 250, x + 40, 290), fill=(60, 140, 80))
      d.rectangle((x + 18, 270, x + 22, 320), fill=(40, 100, 50))
  elif kind == "stripes":
    for i in range(6):
      d.polygon([(i * 60 - 80, 320), (i * 60 - 20, 320), (i * 60 + 100, 0), (i * 60 + 40, 0)],
                fill=(30, 30, 40) if i % 2 else (250, 250, 250))
    d.rounded_rectangle((60, 170, 180, 230), 18, fill=(220, 30, 40))
    for x in (80, 160):
      d.ellipse((x - 16, 218, x + 16, 250), fill=(20, 20, 20))
  return img

def main():
  with tempfile.TemporaryDirectory() as sd:
    gba = os.path.join(sd, "GBA")
    covers = os.path.join(sd, mc.COVERS_DIR)
    os.makedirs(gba)
    os.makedirs(covers)
    for i, (fn, colors, kind) in enumerate(GAMES):
      with open(os.path.join(gba, fn), "wb") as fd:
        fd.truncate((4 << 20) * (1 + i % 4))        # Only the size is used
      with open(os.path.join(covers, fn + ".cov"), "wb") as fd:
        fd.write(mc.cover_bytes(cover(colors, kind)))
    # A save for one of them (shown in the info line), and two favourites.
    open(os.path.join(gba, "Castle Run.sav"), "wb").close()
    os.makedirs(os.path.join(sd, ".superfw"), exist_ok=True)
    with open(os.path.join(sd, ".superfw", "favorites.txt"), "w", encoding="utf-8") as fd:
      fd.write("/GBA/Castle Run.gba\n/GBA/Frost Peak.gbc\n")

    preview = [sys.executable, os.path.join(TOOLS, "carousel-preview.py"), sd, "/GBA/"]
    run = lambda *args: subprocess.run(preview + list(args), check=True)
    run(os.path.join(HERE, "carousel.png"), "--select", "2", "--scale", "3")
    run(os.path.join(HERE, "carousel-letter.png"), "--select", "4", "--scale", "3", "--letter")
    run(os.path.join(HERE, "carousel-theme.png"), "--select", "5", "--scale", "3", "--theme", "3")
    # Start: the view pop up, and the favourites view.
    run(os.path.join(HERE, "carousel-view.png"), "--scale", "3", "--view", "GB/GBC", "--popup", "0")
    run(os.path.join(HERE, "carousel-gbc.png"), "--scale", "3", "--view", "GB/GBC")
    run(os.path.join(HERE, "carousel-scroll.png"), "--scale", "2",
        "--gif", os.path.join(HERE, "carousel.gif"))
    os.remove(os.path.join(HERE, "carousel-scroll.png"))

if __name__ == "__main__":
  main()
