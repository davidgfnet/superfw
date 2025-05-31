#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import os, sys, json

# Main menu/firmware string database.

OTHER_LANGS = [
  "es",
  "fr",
  "it",
  "de",
  "ca",
  "cs",
  "zh",
  "uk",
  "ru",
]

en_strings = {
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

  "MSG_UIS_THEME":      "Theme color",
  "MSG_UIS_LANG":       "Language",
  "MSG_UIS_RECNT":      "Recent ROMs",
  "MSG_UIS_SHOWHIDDEN": "Show Hidden files",
  "MSG_UIS_ANSPD":      "Text speed",
  "MSG_UIS_SAVE":       "Save to SD card",

  "MSG_UIS_SPD0":  "< Very slow >",
  "MSG_UIS_SPD1":  "< Slow >",
  "MSG_UIS_SPD2":  "< Regular >",
  "MSG_UIS_SPD3":  "< Fast >",
  "MSG_UIS_SPD4":  "< Very fast >",

  "MSG_GBALOAD_MINFO":  "ROM information",
  "MSG_GBALOAD_MSAVE":  "Savegame options",
  "MSG_GBALOAD_MPATCH": "Patching options",
  "MSG_GBALOAD_MSETT":  "Game settings",

  "MSG_PATCH_TYPE0": "< Built-in database >",
  "MSG_PATCH_TYPE1": "< Patch engine >",
  "MSG_PATCH_TYPE2": "< No patching >",
  "MSG_PATCH_TYPE3": "< Auto >",
  "MSG_BOOT_TYPE0":  "< Skip BIOS boot >",
  "MSG_BOOT_TYPE1":  "< BIOS boot >",
  "MSG_SAVE_TYPE0":  "< /SAVEGAME/ >",
  "MSG_SAVE_TYPE1":  "< /SAVES/ >",
  "MSG_SAVE_TYPE2":  "< Next to ROM >",
  "MSG_STTE_TYPE0":  "< /SAVESTATE/ >",
  "MSG_STTE_TYPE1":  "< Next to ROM >",
  "MSG_SETT_REMEMB": "Remember config",
  "MSG_SETT_LDCHT":  "Load cheats",

  "MSG_PATCH_TYPE_I0": "The built-in patch database is used, covers most commercial games",
  "MSG_PATCH_TYPE_I1": "Dynamically patches any game, might prompt patch generation",
  "MSG_PATCH_TYPE_I2": "No patching whatsoever (works well with most homebrew)",
  "MSG_PATCH_TYPE_I3": "Tries to find existing patch files, will use the built-in database otherwise",
  "MSG_BOOT_TYPE_I0":  "Skips BIOS and its splash screen",
  "MSG_BOOT_TYPE_I1":  "Boots to BIOS (GBA reset)",
  "MSG_SAVE_TYPE_I0":  "Save file lives in /SAVEGAME/ dir",
  "MSG_SAVE_TYPE_I1":  "Save file lives in /SAVES/ dir",
  "MSG_SAVE_TYPE_I2":  ".sav in the same dir as the ROM",
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
  "MSG_LOADER_ST1":    "< SRAM> ",
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
  "MSG_LOAD_GBA":    "Load GBA game ROM",

  "MSG_LOADINFO_GAME": "GameID: %s | Version: %d",
  "MSG_LOADINFO_SAVE": "Save type: %s (%s)",
  "MSG_LOADINFO_UNKW": "Unknown game!",

  "MSG_OK_SETSAVE": "Settings saved!",                   # alertmsg
  "MSG_OK_DELFILE": "File deleted!",                     # alertmsg
  "MSG_OK_GENERIC": "Completed successfully!",           # alertmsg

  "MSG_SAVOPT_OPT0": "Write SRAM to sav",
  "MSG_SAVOPT_OPT1": "Load sav to SRAM",
  "MSG_SAVOPT_OPT2": "Clear/Erase sav",
  "MSG_SAVOPT_MSG0": "SRAM was written to sav file!",    # alertmsg
  "MSG_SAVOPT_MSG1": ".sav was loaded into SRAM!",       # alertmsg
  "MSG_SAVOPT_MSG2": ".sav was cleared!",                # alertmsg
  "MSG_SAVOPT_MSG_RERR": "Could not read sav file!",     # alertmsg
  "MSG_SAVOPT_MSG_WERR": "Could not write sav file!",    # alertmsg

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

  "MSG_FWUP_ERRSZ":   "Invalid update size!",              # alertmsg
  "MSG_FWUP_ERRRD":   "Can't read file!",                  # alertmsg
  "MSG_FWUP_ERRCL":   "Erase error!",                      # alertmsg
  "MSG_FWUP_ERRPG":   "Flashing error!",                   # alertmsg
  "MSG_FWUP_ERRVR":   "Verification failed!",              # alertmsg

  "MSG_ERR_GENERIC": "An error occured!",                  # alertmsg
  "MSG_ERR_SETSAVE": "Error saving settings!",             # alertmsg
  "MSG_ERR_DELFILE": "Error deleting file!",               # alertmsg
  "MSG_ERR_READ":    "Error: could not load ROM!",         # alertmsg
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
  "MSG_CAPACITY":  "Capacity: %s",
  "MSG_DBPINFO":   "Patch database version info",

}

en_menu_strings = {
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
}

if len(sys.argv) > 1 and sys.argv[1] == "json":
  # Dump english strings for translation
  print(json.dumps(en_strings | en_menu_strings, indent=2))
elif len(sys.argv) > 1 and sys.argv[1] == "h":
  todump = en_menu_strings if sys.argv[2] == "menu" else en_strings

  for i, k in enumerate(sorted(todump)):
    print("#define %s %d" % (k.ljust(20), i))

  print("const char * const msg_en[] = {")
  for k, v in sorted(todump.items()):
    print('  /* %s */ "%s",' % (k.ljust(20), v))
  print("};")

  # Attempt to load strings for all other languages
  langdir = os.path.join(os.path.dirname(__file__), "lang")
  for l in OTHER_LANGS:
    d = json.load(open(os.path.join(langdir, "%s.json" % l)))
    print("const char * const msg_%s[] = {" % l)
    for k, en_v in sorted(todump.items()):
      v = d[k] if k in d and d[k] else en_v
      print('  /* %s */ "%s",' % (k.ljust(20), v))
    print("};")

  print("const char * const * const msgs[] = {")
  for l in ["en"] + OTHER_LANGS:
    print("  msg_%s," % l)
  print("};")

  if sys.argv[2] == "main":
    print("const uint16_t lang_codes[] = {")
    for l in ["en"] + OTHER_LANGS:
      c = ord(l[0]) | (ord(l[1]) << 8)
      print("  0x%04x,     // %s" % (c, l))
    print("};")


