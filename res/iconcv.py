
import sys, PIL.Image

def cc(r, g, b):
  return ( ((r >> 3) << 0) | ((g >> 3) << 5) | ((b >> 3) << 10) )

im = PIL.Image.open(sys.argv[1])

nicons = im.size[0] // 16

pal = [x[1] for x in im.getcolors()]

iconlst = [
  ("ICON_FOLDER", None),
  ("ICON_BINFILE", None),
  ("ICON_UPDFILE", None),
  ("ICON_GBCART", None),
  ("ICON_GBCCART", None),
  ("ICON_GBACART", None),
  ("ICON_SMSCART", None),
  ("ICON_NESCART", None),
  ("ICON_RECENT", None),
  ("ICON_DISK", None),
  ("ICON_FLASH", "SUPPORT_NORGAMES"),
  ("ICON_SETTINGS", None),
  ("ICON_UILANG_SETTINGS", None),
  ("ICON_TOOLS", None),
  ("ICON_INFO", None),
]

print("enum {")
for n, c in iconlst:
  if c:
    print("#ifdef %s" % c)
  print(" %s," % n)
  if c:
    print("#endif")
print("};")

print("const uint8_t icons_img[][4][8][8] = {")

for n in range(nicons):
  if iconlst[n][1]:
    print("#ifdef %s" % iconlst[n][1])
  print("  {")
  for subobj in range(4):
    sx, sy = subobj & 1, subobj >> 1
    icon = im.crop((n*16+sx*8, sy*8, n*16+(sx+1)*8, (sy+1)*8))

    print("    {")
    for r in range(8):
      line = []
      for c in range(8):
        col = icon.getpixel((c, r))
        if col[3] > 128:
          line.append(pal.index(col) + 1)
        else:
          line.append(0)
      print("     {" + ",".join("0x%02x" % x for x in line) + "},")
    print("    },")
  print("  },")
  if iconlst[n][1]:
    print("#endif")
print("};")

print("const uint16_t icons_pal[%d] = {" % (len(pal)+1))
print("  0x0000," + ",".join("0x%04x" % cc(x[0],x[1],x[2]) for x in pal))
print("};")

