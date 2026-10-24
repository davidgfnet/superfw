#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import os, sys, json, struct, subprocess, tempfile, zlib

# Main menu/firmware string database.

OTHER_LANGS = [
  "es",
  "fr",
  "it",
  "de",
  "ca",
  "pt",
  "cs",
  "zh",
  "ko",
  "uk",
  "ru",
  "id",
  "ms",
]

en_strings = [
  (None, {
  "MSG_LANG_NAME": "English",

  "MSG_EMPTY": "",
  "MSG_KNOB_ENABLED": "< Enabled >",
  "MSG_KNOB_DISABLED": "< Disabled >",

  "MSG_Q0_DELFILE":  "Delete this file?",
  "MSG_Q1_NOPATCH":  "No patches were found for this ROM, do you want to generate them?",
  "MSG_Q1_PATCHENG": "PatchEngine has not processed this ROM, generate patches now?",
  "MSG_Q2_SRAMTST":  "After running this test, you must shutdown your GBA for aprox. 2 minutes. Continue?",
  "MSG_Q3_LOADPDB":  "Do you want to load and replace your patch database?",
  "MSG_Q4_DELREC":   "Delete recently played game? (Does not delete the ROM!)",

  "MSG_PATCHGEN_OK": "Patch generation completed!",        # alertmsg
  "MSG_SRAMTST_RDY": "You might now power off!",           # alertmsg

  "MSG_SRAMTST_OK":    "SRAM test passed!",                # alertmsg
  "MSG_SRAMTST_FAIL":  "SRAM test FAILED!",                # alertmsg

  "MSG_Q_YES": "Yes",
  "MSG_Q_NO":  "No",

  "MSG_CANCEL": "Cancel",

  "MSG_SAVETYPE_NONE": "None",
  "MSG_SAVETYPE_SRAM": "SRAM",
  "MSG_SAVETYPE_EEPROM": "EEPROM",
  "MSG_SAVETYPE_FLASH": "Flash",

  "MSG_SET_TITL1":   "Global settings",
  "MSG_SET_TITL2":   "Default GBA settings",

  "MSG_SETT_HOTK":   "Menu Hot-key",
  "MSG_SETT_BOOT":   "Game boot",
  "MSG_SETT_SAVET":  "Save path",
  "MSG_SETT_SAVEBK": "Save backup #",
  "MSG_SETT_STATET": "Savestate path",
  "MSG_SETT_CHTEN":  "Enable cheats",
  "MSG_SETT_FASTSD": "Fast ROM loading",
  "MSG_SETT_FASTEW": "EWRAM overclock",

  "MSG_TOOLS0_SDRAM": "SDRAM memory test",
  "MSG_TOOLS1_SRAM":  "SRAM memory test",
  "MSG_TOOLS2_BAT":   "SRAM battery test",
  "MSG_TOOLS3_BENCH": "SD (read) benchmark",
  "MSG_TOOLS4_FBAK":  "Flash backup",

  "MSG_TOOLS_RUN": "Run",

  "MSG_DEFS_PATCH":  "Patching",
  "MSG_BROW_EMPTY":  "Empty directory",

  "MSG_CAR_SCAN":    "Looking for games...",
  "MSG_CAR_EMPTY":   "No games found in this folder or its subfolders.",
  "MSG_CAR_UPDATE":  "Updating game list...",
  "MSG_CAR_SAVE":    "Save",
  "MSG_CAR_LAST":    "Last played",
  "MSG_CAR_FLASH":   "Flash",
  "MSG_CAR_LOADSD":  "Load from SD card",
  "MSG_NOR_REMOVE":  "Remove from flash",

  "MSG_UIS_THEME": "Theme color",
  "MSG_UIS_LANG":  "Language",
  "MSG_UIS_RECNT": "Recent ROMs",
  "MSG_UIS_ANSPD": "Text speed",
  "MSG_UIS_BHID":  "Show hidden files",
  "MSG_UIS_START": "Start tab",
  "MSG_UIS_EXT":   "Show extensions",
  "MSG_UIS_SAVE":  "Save to SD card",

  "MSG_UIS_STB0":  "< Browser >",
  "MSG_UIS_STB1":  "< Carousel >",

  "MSG_UIS_SPD0":  "Very slow",
  "MSG_UIS_SPD1":  "Slow",
  "MSG_UIS_SPD2":  "Regular",
  "MSG_UIS_SPD3":  "Fast",
  "MSG_UIS_SPD4":  "Very fast",

  "MSG_STILLRTC":    "Frozen",

  "MSG_GBALOAD_MINFO":  "ROM information",
  "MSG_GBALOAD_OPTS":   "Loading options",
  "MSG_GBALOAD_MPATCH": "Patching options",

  "MSG_PATCH_TYPE0": "< Built-in database >",
  "MSG_PATCH_TYPE1": "< Patch engine >",
  "MSG_PATCH_TYPE2": "< No patching >",
  "MSG_PATCH_TYPE3": "< Auto >",
  "MSG_BOOT_TYPE0":  "< Skip BIOS boot >",
  "MSG_BOOT_TYPE1":  "< BIOS boot >",
  "MSG_NEXTTO_ROM":  "< Next to ROM >",
  "MSG_SETT_REMEMB": "Remember config",
  "MSG_SETT_LDCHT":  "Load cheats",

  "MSG_PATCH_TYPE_I0": "The built-in patch database is used, covers most commercial games",
  "MSG_PATCH_TYPE_I1": "Dynamically patches any game, might prompt patch generation",
  "MSG_PATCH_TYPE_I2": "No patching whatsoever (works well with most homebrew)",
  "MSG_PATCH_TYPE_I3": "Tries to find existing patch files, will use the built-in database otherwise",
  "MSG_BOOT_TYPE_I0":  "Skips BIOS and its splash screen",
  "MSG_BOOT_TYPE_I1":  "Boots to BIOS (GBA reset)",
  "MSG_SAVE_TYPE_NR":  ".sav in the same dir as the ROM",
  "MSG_SAVE_TYPE_PT":  "Save file lives in %s dir",
  "MSG_STATE_TYPE_PT": "Savestate files live in %s dir",
  "MSG_BACKUP_I":      "Keep the last N save files",
  "MSG_FASTSD_I":      "Use a fast ROM loading mechanism. Can result in crashes or incorrect reads in some devices",
  "MSG_FASTEW_I":      "Overclock EWRAM for some extra performance. Not available on NDS or GBA Micro",
  "MSG_INGAME_I":      "Show menu on combo key press",
  "MSG_PATCHE_I":      "Run PatchEngine to generate patches for this ROM",
  "MSG_PATCHRTC_I":    "Emulate an RTC clock (only on supported games). Press A to change time & date.",
  "MSG_REMEMB_I":      "Remembers this settings in the future",
  "MSG_CHEATSDIS_I":   "Cheats are globally disabled in the settings menu",
  "MSG_CHEATSNOA_I":   "No cheats were found for this ROM",

  "MSG_LOADER_SAVET":  "Save mode",
  "MSG_LOADER_ST0":    "< DirectSave >",
  "MSG_LOADER_ST1":    "< SRAM >",
  "MSG_LOADER_LOADP":  "Savegame load",
  "MSG_LOADER_SAVEP":  "Savegame save",
  "MSG_LOADER_LOADP0": "< Load >",
  "MSG_LOADER_LOADP1": "< Clear >",
  "MSG_LOADER_LOADP2": "< Manual >",
  "MSG_LOADER_SAVEP0": "< On reboot >",
  "MSG_LOADER_SAVEP1": "< Manual >",
  "MSG_LOADER_SAVEP2": "Automatic",

  "MSG_LOADER_PREFDS":  "Direct-Save",
  "MSG_LOADER_PREFDSI": "Directly save to SD card (only available for some games)",

  "MSG_LOADER_LOADP_I0": "Automatically loads .sav file",
  "MSG_LOADER_LOADP_I1": "Clears memory (start anew)",
  "MSG_LOADER_LOADP_I2": "No automatic .sav is loaded",
  "MSG_LOADER_SAVEP_I0": "Saves game on reboot",
  "MSG_LOADER_SAVEP_I1": "No automatic saving to .sav file",
  "MSG_LOADER_SAVEP_I2": "Direct save will automatically update the .sav file",

  "MSG_LOADER_ST_I0":    "Games will directly save to SD card while running",
  "MSG_LOADER_ST_I1":    "Savefile is kept in the SRAM. Saving happens on reboot or manually",

  "MSG_DEF_LOADP0": "< Auto-load >",
  "MSG_DEF_LOADP1": "< Manual >",
  "MSG_DEF_SAVEP0": "< Auto-save >",
  "MSG_DEF_SAVEP1": "< Manual >",

  "MSG_DEF_LOADP_I0": "Try to load .sav file if found",
  "MSG_DEF_LOADP_I1": "Do not automatically load",
  "MSG_DEF_SAVEP_I0": "Save .sav file (on reboot or directly)",
  "MSG_DEF_SAVEP_I1": "Do not automatically save",

  "MSG_LOADER_MENU": "In-game menu",
  "MSG_LOADER_RTCE": "Emulated RTC",
  "MSG_LOADER_PTCH": "PatchEngine",
  "MSG_DEF_RTCVAL":  "RTC time",
  "MSG_DEF_SPEED":   "RTC speed",
  "MSG_LOAD_GBA":    "Load GBA game ROM",

  "MSG_LOADINFO_GAME": "GameID: %s | Version: %d",
  "MSG_LOADINFO_SAVE": "Save type: %s (%s)",
  "MSG_LOADINFO_UNKW": "Unknown save type!",

  "MSG_OK_SETSAVE": "Settings saved!",                   # alertmsg
  "MSG_OK_DELFILE": "File deleted!",                     # alertmsg
  "MSG_OK_GENERIC": "Completed successfully!",           # alertmsg

  "MSG_FMGR_DEL":    "Delete file/directory",
  "MSG_FMGR_HIDE":   "Hide file/directory",
  "MSG_FMGR_UNHIDE": "Un-Hide file/directory",

  "MSG_SAVOPT_OPT0": "Write SRAM to sav",
  "MSG_SAVOPT_OPT1": "Load sav to SRAM",
  "MSG_SAVOPT_OPT2": "Clear/Erase sav",
  "MSG_SAVOPT_MSG0": "SRAM was written to sav file!",    # alertmsg
  "MSG_SAVOPT_MSG1": ".sav was loaded into SRAM!",       # alertmsg
  "MSG_SAVOPT_MSG2": ".sav was cleared!",                # alertmsg
  "MSG_SAVOPT_MSG_RERR": "Could not read sav file!",     # alertmsg
  "MSG_SAVOPT_MSG_WERR": "Could not write sav file!",    # alertmsg
  "MSG_REMEMB_CFG_OK": "Config saved!",                  # alertmsg

  "MSG_FWUP_HOTKEY":   "Use Down+B+Start to unlock updates",
  "MSG_FWUP_ENABLED":  "Update flashing is enabled",
  "MSG_FWUPD_MENU": "Firmware update",
  "MSG_FWUPD_UNK":  "Unknown firmware type",
  "MSG_FWUPD_GO":   "Press L+R+Up to flash",
  "MSG_FWUPD_LOADING":  "Loading firmware image ...",
  "MSG_FWUPD_CHECKING": "Checking image ...",
  "MSG_FWUPD_ERASING":  "Erasing flash ...",
  "MSG_FWUPD_PROGRAM":  "Programming firmware ...",
  "MSG_FWUP_DISABLED": "Flashing is disabled!",            # alertmsg
  "MSG_FWUPD_DONE":    "Flash update complete!",           # alertmsg
  "MSG_FLASH_READOK": "Flash dump successful!",            # alertmsg

  "MSG_FWUPD_BADCHK": "Bad firmware checksum!",            # alertmsg
  "MSG_FWUP_BADHD":   "Bad FW header!",                    # alertmsg
  "MSG_FWUP_BADFL":   "Bad FW variant!",                   # alertmsg

  "MSG_FWUP_ERRSZ":   "Invalid update size!",              # alertmsg
  "MSG_FWUP_ERRRD":   "Can't read file!",                  # alertmsg
  "MSG_FWUP_ERRCL":   "Erase error!",                      # alertmsg
  "MSG_FWUP_ERRPG":   "Flashing error!",                   # alertmsg
  "MSG_FWUP_ERRVR":   "Verification failed!",              # alertmsg

  "MSG_ERR_GENERIC": "An error occured!",                  # alertmsg
  "MSG_ERR_SETSAVE": "Error saving settings!",             # alertmsg
  "MSG_ERR_DELFILE": "Error deleting file!",               # alertmsg
  "MSG_ERR_READ":    "Error: could not load ROM!",         # alertmsg
  "MSG_ERR_LOADRST": "Load failed. A: restart",            # alertmsg
  "MSG_ERR_PATHLEN": "Path too long!",                     # alertmsg
  "MSG_ERR_DIROPEN": "Cannot open this folder!",           # alertmsg
  "MSG_ERR_NOEMU":   "Can't find emulator!",               # alertmsg
  "MSG_ERR_TOOBIG":  "The GBA file is too big!",           # alertmsg
  "MSG_ERR_SAVERD":  "Error: can't read save file",        # alertmsg
  "MSG_ERR_SAVEWR":  "Error: can't write save file",       # alertmsg
  "MSG_ERR_SAVEPR":  "Error: can't prepare save file",     # alertmsg
  "MSG_ERR_SAVEIT":  "Error: unknown internal error",      # alertmsg
  "MSG_ERR_UNKTYP":  "Unknown file type!",                 # alertmsg

  "MSG_BAD_SDRAM": "SDRAM (ROM storage) error!",           # alertmsg
  "MSG_BAD_SRAM":  "SRAM test error!",                     # alertmsg
  "MSG_GOOD_RAM":  "All memory tests passed!",             # alertmsg

  "MSG_BENCHSPD":  "Speed: %u KiB/s",
  "MSG_DBPINFO":   "Patch database version info",
  }),
  ("SUPPORT_NORGAMES", {
  "MSG_NOR_EMPTY":    "Flash is empty",
  "MSG_Q5_DELNORG":   "Delete this game from flash memory?",
  "MSG_Q6_CLRNOR":    "Do you want to format the internal flash memory?",
  "MSG_ERR_NORUPD":   "Flash write failed!",                 # alertmsg
  "MSG_ERR_NORSPC":   "Insufficient disk space!",            # alertmsg
  "MSG_NOR_WRITE":    "Write game to flash",
  "MSG_NOR_LAUNCH":   "Launch flash game",
  "MSG_NOR_WROK":     "Game flashed successfully!",          # alertmsg
  "MSG_NOR_CLOK":     "Flash erased successfully!",          # alertmsg
  "MSG_TOOLS5_FCLR":  "Erase flash",
  "MSG_SETT_SAVETX":  "Flash saves",
  "MSG_SAVE_TYPE_PTX":"Save file lives (for in-flash games) in %s dir",
  "MSG_SETT_VERNOR":  "Verify flashing",
  "MSG_VERNOR_I":     "Checks ROMs written to flash",
  }),
]

en_menu_strings = [
  (None, {
  "IMENU_QC0_NO":    "No",
  "IMENU_QC1_YES":   "Yes",

  "IMENU_MAIN0_BACK_GAME":  "Resume game",
  "IMENU_MAIN1_RESET":      "Reset",
  "IMENU_MAIN2_FLUSH_SAVE": "Save to SD card",
  "IMENU_MAIN3_SSTATE":     "Savestates",
  "IMENU_MAIN4_RTC":        "RTC clock",
  "IMENU_MAIN5_CHEATS":     "Cheats",

  "IMENU_GOBACK":           "Go back",
  "IMENU_UPDAT_RTC":        "Update RTC clock",

  "IMENU_RST0_GAME":        "Reset game",
  "IMENU_RST1_DEVICE":      "Reset to menu",
  "IMENU_RST2_DEVSKIP":     "Back to menu (skip save)",

  "IMENU_SAVE0_OW":         "Save to SD (overwrite)",
  "IMENU_SAVE1_BKP":        "Save to SD (with backup)",
  "IMENU_SAVE2_RST":        "Save and quit to menu",

  "IMENU_SSTATEQ0_SAVE":    "Quick save",
  "IMENU_SSTATEQ1_LOAD":    "Quick load",
  "IMENU_SSTATEQ2_WRITE":   "Make persistent",

  "IMENU_SSTATEP0_SAVE":    "Save state",
  "IMENU_SSTATEP1_LOAD":    "Load state",
  "IMENU_SSTATEP2_DEL":     "Delete",

  "IMENU_MAKEPER":          "Make persistent",
  "IMENU_CANCEL":           "Cancel",

  "IMENU_SSTATE_PN":        "Persistent Slot %d",
  "IMENU_SSTATE_QN":        "Memory Slot %d",

  "IMENU_MSG_SAVEC":        "Save completed",            # igm-alertmsg
  "IMENU_MSG_SAVEERR":      "A save error occured!",     # igm-alertmsg
  "IMENU_MSG_RTCWR":        "RTC data was updated",      # igm-alertmsg

  "IMENU_ST_OVER":          "Overwrite savestate?",      # igm-alertmsg
  "IMENU_ST_DEL":           "Delete savestate?",         # igm-alertmsg
  "IMENU_QLD_OK":           "Savestate loaded!",         # igm-alertmsg
  "IMENU_QLD_ERR":          "Invalid savestate!",        # igm-alertmsg
  "IMENU_WSAV_OK":          "Savestate created!",        # igm-alertmsg
  "IMENU_WSTAF_OK":         "Savestate written!",        # igm-alertmsg
  "IMENU_WSTAF_ERR":        "Error writing file!",       # igm-alertmsg
  "IMENU_WSTAR_ERR":        "Error reading file!",       # igm-alertmsg
  "IMENU_PLD_ERR":          "Corrupted savestate!",      # igm-alertmsg

  "IMENU_SAVING_BLOCKED":   "Saving in progress!",

  "IMENU_RTCSPD": "~main.MSG_DEF_SPEED",
  "IMENU_SPD0": "~main.MSG_UIS_SPD0",
  "IMENU_SPD1": "~main.MSG_UIS_SPD1",
  "IMENU_SPD2": "~main.MSG_UIS_SPD2",
  "IMENU_SPD3": "~main.MSG_UIS_SPD3",
  "IMENU_SPD4": "~main.MSG_UIS_SPD4",
  "IMENU_FRZRTC": "~main.MSG_STILLRTC",

  }),
]

all_entries = {
  "main": en_strings,
  "menu": en_menu_strings,
}

def flatten():
  # Returns {menu name: {key: (english value, condition)}}
  strall = {}
  for mname, smenu in all_entries.items():
    tmp = {}
    for c, entries in smenu:
      for k, v in entries.items():
        tmp[k] = (v, c)
    strall[mname] = tmp
  return strall

def resolve(strall, en_v):
  # Resolves "~menu.KEY" references to another menu's english string
  if en_v.startswith("~"):
    mn, mk = en_v[1:].split(".")
    return mk, strall[mn][mk][0]
  return None, en_v

def load_lang(l):
  langdir = os.path.join(os.path.dirname(__file__), "lang")
  with open(os.path.join(langdir, "%s.json" % l), encoding="utf-8") as fd:
    return json.load(fd)

def lang_strings(strall, mname, l):
  # Resolved strings of a language for a menu, in enum (sorted key) order.
  d = {} if l == "en" else load_lang(l)
  out = []
  for k, (en_v, c) in sorted(strall[mname].items()):
    ref, en_text = resolve(strall, en_v)
    key = ref if ref else k
    v = d[key] if key in d and d[key] else en_text
    out.append((k, v, c))
  return out

# Language pack (main menu only), bundled as the "LANG" virtual file. All the
# language blobs are compressed together (upkr) since that is much smaller than
# compressing them one by one. The firmware unpacks the stream into the SDRAM
# scratch area and copies only the chosen language into EWRAM (see lang_load()
# in src/menu.c). Blob layout:
#   uint16 offset[MSG_COUNT]   (from the blob start)
#   NUL terminated utf-8 strings, padded to a multiple of 4 bytes
# Pack layout (little endian):
#   uint32 magic "LNGP", uint16 version (2), uint16 language count,
#   uint16 message count, uint16 reserved, uint32 unpacked size,
#   uint32 CRC32 of the upkr stream (identifies the pack, see lang.cache),
#   per language: uint32 blob offset (in the unpacked data), uint32 blob size
#   upkr stream of all the blobs, one after the other
PACK_MAGIC = 0x504E474C    # "LNGP"
PACK_VERSION = 2
PACK_LANGS = ["en"] + OTHER_LANGS

def lang_blob(strall, l, conds):
  entries = [e for e in lang_strings(strall, "main", l) if not e[2] or e[2] in conds]
  data = bytearray(2 * len(entries))
  seen = {}
  for i, (k, v, c) in enumerate(entries):
    if "\\" in v or '"' in v:
      sys.exit("%s/%s: backslashes and quotes are not supported in the language pack" % (l, k))
    raw = v.encode("utf-8") + b"\0"
    if raw not in seen:
      seen[raw] = len(data)
      data += raw
    if seen[raw] > 0xFFFF:
      sys.exit("language blob too big for 16 bit offsets")
    struct.pack_into("<H", data, 2 * i, seen[raw])
  while len(data) % 4:
    data.append(0)
  return bytes(data)

def build_pack(conds, upkr, level):
  strall = flatten()
  blobs = [lang_blob(strall, l, conds) for l in PACK_LANGS]
  raw = b"".join(blobs)
  with tempfile.TemporaryDirectory() as tmp:
    fin, fout = os.path.join(tmp, "lang.bin"), os.path.join(tmp, "lang.comp")
    with open(fin, "wb") as fd:
      fd.write(raw)
    subprocess.run([upkr, "-l", str(level), fin, fout], check=True, stdout=subprocess.DEVNULL)
    with open(fout, "rb") as fd:
      stream = fd.read()

  nmsgs = len([1 for k, (v, c) in strall["main"].items() if not c or c in conds])
  hdr = struct.pack("<IHHHHII", PACK_MAGIC, PACK_VERSION, len(blobs), nmsgs, 0, len(raw),
                    zlib.crc32(stream))
  index, off = b"", 0
  for blob in blobs:
    index += struct.pack("<II", off, len(blob))
    off += len(blob)
  return hdr + index + stream

def max_blob_size(conds):
  strall = flatten()
  return max(len(lang_blob(strall, l, conds)) for l in PACK_LANGS)

if len(sys.argv) > 1 and sys.argv[1] == "json":
  # Dump english strings for translation
  allentries = {}
  for e in [en_strings, en_menu_strings]:
    for _, d in e:
      for k, v in d.items():
        if not v.startswith("~"):
          allentries[k] = v
  print(json.dumps(allentries, indent=2))
elif len(sys.argv) > 2 and sys.argv[1] == "pack":
  # pack <output> <upkr binary> <level> [defines...]: language pack file
  out, upkr, level = sys.argv[2], sys.argv[3], int(sys.argv[4])
  with open(out, "wb") as fd:
    fd.write(build_pack(set(sys.argv[5:]), upkr, level))
elif len(sys.argv) > 2 and sys.argv[1] == "h":
  mname = sys.argv[2]
  strall = flatten()
  strlist = strall[mname]

  print("enum TranslationID {")
  for k, (_, c) in sorted(strlist.items()):
    if c:
      print("#ifdef %s" % c)
    print("  %s," % k)
    if c:
      print("#endif")
  print("  MSG_COUNT")
  print("};")

  if mname == "main":
    # Strings live in the language pack. Only the chosen language is unpacked
    # into msg_table (src/menu.c), msgs[any language] points to it.
    print("#define LANG_BLOB_MAX_PLAIN %d" % max_blob_size(set()))
    print("#define LANG_BLOB_MAX_NOR   %d" % max_blob_size({"SUPPORT_NORGAMES"}))
    print("#ifdef SUPPORT_NORGAMES")
    print("  #define LANG_BLOB_MAX LANG_BLOB_MAX_NOR")
    print("#else")
    print("  #define LANG_BLOB_MAX LANG_BLOB_MAX_PLAIN")
    print("#endif")
    print("const char *msg_table[MSG_COUNT] __attribute__((section(\".ewram\")));")
    print("const char * const *msgs[] = {")
    for l in PACK_LANGS:
      print("  msg_table,     // %s" % l)
    print("};")
    print("const uint16_t lang_codes[] = {")
    for l in PACK_LANGS:
      c = ord(l[0]) | (ord(l[1]) << 8)
      print("  0x%04x,     // %s" % (c, l))
    print("};")
  else:
    for l in PACK_LANGS:
      print("const char * const msg_%s[] = {" % l)
      for k, v, c in lang_strings(strall, mname, l):
        if c:
          print("#ifdef %s" % c)
        print('  /* %s */ "%s",' % (k.ljust(20), v))
        if c:
          print("#endif")
      print("};")

    print("const char * const * const msgs[] = {")
    for l in PACK_LANGS:
      print("  msg_%s," % l)
    print("};")
