
VERSION_WORD := 0x00000013
VERSION_SLUG_WORD := $(shell git rev-parse --short=8 HEAD || echo FFFFFFFF)

PREFIX		:= arm-none-eabi-
CC		:= $(PREFIX)gcc
CXX		:= $(PREFIX)g++
OBJDUMP		:= $(PREFIX)objdump
OBJCOPY		:= $(PREFIX)objcopy

COMPRESSION_RATIO ?= 3

GLOBAL_DEFINES = -D__GBA__

# BOARD can be "sd", "lite", "chis"
BOARD ?= sd

ifeq ($(BOARD),lite)
  GLOBAL_DEFINES += -DSUPERCARD_LITE_IO
  COMPRESS_FIRMWARE = 1
  MAXFSIZE = 496
  FWFLAVOUR = "Lite"
else ifeq ($(BOARD),sd)
  GLOBAL_DEFINES += -DSUPERCARD_FLASH_ADDRPERM
  BUNDLE_GBC_EMULATOR = 1
  COMPRESS_FIRMWARE = 1
  MAXFSIZE = 512
  FWFLAVOUR = "SD"
else ifeq ($(BOARD),chis)
  GLOBAL_DEFINES += -DSUPPORT_NORGAMES -DSUPERCHIS_IO -DFONTS_EXT
  BUNDLE_GBC_EMULATOR = 1
  BUNDLE_OTHER_EMULATORS = 1
  # Can't be over 2MiB (hardlimit)
  MAXFSIZE = 2048
  FWFLAVOUR = "Chis"
else
  $(error No valid board specified in BOARD)
endif

FWBINFILES=firmware.ewram.gba res/patches.db res/fonts.pack

ifeq ($(COMPRESS_FIRMWARE),1)
  GLOBAL_DEFINES += -DCOMPRESS_FONTS -DCOMPRESS_PATCHES -DCOMPRESS_FIRMWARE
  FWBINFILES := $(addsuffix .comp,$(FWBINFILES))
endif

ifeq ($(BUNDLE_GBC_EMULATOR),1)
  GLOBAL_DEFINES += -DBUNDLE_GBC_EMULATOR
  BIEMUFILES += emu/jagoombacolor_v0.5.gba.comp
endif

ifeq ($(BUNDLE_OTHER_EMULATORS),1)
  GLOBAL_DEFINES += -DBUNDLE_OTHER_EMULATOR
  BIEMUFILES += emu/pocketnes_20130701.gba.comp \
                emu/wasabigba_v0.2.4.gba.comp \
                emu/NGPGBA_v0.5.7.gba.comp \
                emu/pceadvance-v7.5-scptch.gba.comp
endif

BASEFLAGS=$(GLOBAL_DEFINES) -mcpu=arm7tdmi -mtune=arm7tdmi

CFLAGS=-O2 -ggdb \
       $(BASEFLAGS) \
       -DFW_MAX_SIZE_KB=$(MAXFSIZE) -DFW_FLAVOUR="\"$(FWFLAVOUR)\"" \
       -DSC_FAST_ROM_MIRROR="use_fast_mirror()" \
       -DSD_PREERASE_BLOCKS_WRITE \
       -DVERSION_WORD="$(VERSION_WORD)" \
       -DVERSION_SLUG_WORD="0x$(VERSION_SLUG_WORD)" \
       -Wall -Isrc -I. -mthumb -flto -flto-partition=none


INGAME_CFLAGS=-Os -ggdb \
              $(BASEFLAGS) \
              -DNO_SUPERCARD_INIT \
              -DSD_PREERASE_BLOCKS_WRITE \
              -Wall -Isrc -I. \
              -mthumb -flto

DLDI_CFLAGS=-Os -ggdb \
            $(BASEFLAGS) -DSDDRV_TIMEOUT_MULT=2 \
            -DSD_PREERASE_BLOCKS_WRITE \
            -Wall -Isrc -I. \
            -mthumb -flto -fPIC

DIRECTSAVE_CFLAGS=-Os -ggdb \
                 $(BASEFLAGS) -DSDDRV_TIMEOUT_MULT=16 \
                 -DNO_SUPERCARD_INIT \
                 -Wall -Isrc -I. \
                 -marm -flto -fPIC

FATFSFILES=fatfs/diskio.c \
           fatfs/ff.c \
           fatfs/ffsystem.c \
           fatfs/ffunicode.c

DLDIFILES=src/dldi.S \
          src/dldi_driver.c \
          src/crc.c \
          src/supercard_io.S \
          src/supercard_driver.c

DIRECTSAVEFILES=src/directsaver.S \
                src/directsave_emu.c \
                src/crc.c \
                src/supercard_driver.c

MENUFILES=src/ingame.S \
          src/ingame_menu.c \
          src/cimpl.c \
          src/fonts/font_render.c \
          src/save.c \
          src/util.c \
          src/utf_util.c \
          src/fileutil.c \
          src/crc.c \
          src/nanoprintf.c \
          src/supercard_io.S \
          src/supercard_driver.c \
          ${FATFSFILES}

INFILES=src/gba_ewram_crt0.S \
        src/main.c \
        src/cimpl.c \
        src/settings.c \
        src/loader.c \
        src/save.c \
        src/patchengine.c \
        src/patcher.c \
        src/patches.S \
        src/menu.c \
        src/cheats.c \
        src/flash.c \
        src/sha256.c \
        src/misc.c \
        src/util.c \
        src/utf_util.c \
        src/emu.c \
        src/fileutil.c \
        src/asmutil.S \
        src/gbahw.c \
        src/virtfs.c \
        src/flash_mgr.c \
        src/binassets.S \
        src/crc.c \
        src/nds_loader.c \
        src/dldi_patcher.c \
        src/supercard_driver.c \
        src/supercard_io.S \
        src/heapsort.c \
        src/nanoprintf.c \
        src/fonts/font_render.c \
        ${FATFSFILES}

all:	$(FWBINFILES) $(BIEMUFILES) directsave.payload ingame_trampoline.payload
	# Wrap the firmware around a ROM->EWRAM loader
	$(CC) $(CFLAGS) -o firmware.elf rom_boot.S -T ldscripts/gba_romboot.ld -nostartfiles -nostdlib -Wl,--defsym,MAX_FLASH_SIZE=$(MAXFSIZE)K
	$(OBJCOPY) --output-target=binary firmware.elf superfw.gba
	# Fix the header/checksum.
	./tools/fw-fixer.py superfw.gba

firmware.ewram.gba: $(INFILES) ingamemenu.payload superfw.dldi.payload directsave.payload ingame_trampoline.payload src/messages_data.h ldscripts/gba_ewram.ld.i
	# Build the actual firmware image
	$(CC) $(CFLAGS) -o firmware.ewram.elf $(INFILES) -T ldscripts/gba_ewram.ld.i -nostartfiles -Wl,-Map=firmware.ewram.map -Wl,--print-memory-usage -fno-builtin
	$(OBJCOPY) --output-target=binary firmware.ewram.elf firmware.ewram.gba

superfw.dldi.payload:	$(DLDIFILES)
	# Build in-game menu
	$(CC) $(DLDI_CFLAGS) -o superfw.dldi.elf $(DLDIFILES) -T ldscripts/gba_dldi.ld \
			-nostartfiles -fno-builtin -Wl,-Map=superfw.dldi.map -Wl,--print-memory-usage
	$(OBJCOPY) --output-target=binary superfw.dldi.elf superfw.dldi.payload

directsave.payload:	$(DIRECTSAVEFILES)
	$(CC) $(DIRECTSAVE_CFLAGS) -o directsave.elf $(DIRECTSAVEFILES) -T ldscripts/gba_directsave.ld \
			-nostartfiles -fno-builtin -Wl,-Map=directsave.map -Wl,--print-memory-usage
	$(OBJCOPY) --output-target=binary directsave.elf directsave.payload

ingamemenu.payload:	$(MENUFILES) src/menu_messages.h
	# Build in-game menu
	$(CC) $(INGAME_CFLAGS) -o ingamemenu.elf $(MENUFILES) -T ldscripts/gba_ingame.ld \
			-nostartfiles -fno-builtin -Wl,-Map=firmware.ingame.map -Wl,--print-memory-usage
	$(OBJCOPY) --output-target=binary ingamemenu.elf ingamemenu.payload

ingame_trampoline.payload:	src/ingame_trampoline.S
	$(CC) $(BASEFLAGS) -nostartfiles -T ldscripts/gba_ingametramp.ld -o ingame_trampoline.elf src/ingame_trampoline.S
	$(OBJCOPY) --output-target=binary ingame_trampoline.elf ingame_trampoline.payload

src/messages_data.h:	res/messages.py
	./res/messages.py h main > src/messages_data.h

src/menu_messages.h:	res/messages.py
	./res/messages.py h menu > src/menu_messages.h

%.gba.comp:	%.gba.bin apultra/apultra
	./apultra/apultra $< $@

firmware.ewram.gba.comp:	firmware.ewram.gba ./upkr/target/release/upkr
	./upkr/target/release/upkr -l $(COMPRESSION_RATIO) $< $@

%.db.comp:	%.db ./upkr/target/release/upkr
	./upkr/target/release/upkr -l $(COMPRESSION_RATIO) $< $@

%.pack.comp:	%.pack apultra/apultra
	./apultra/apultra $< $@

%.ld.i:	%.ld
	cpp $< -o $@

apultra/apultra:
	make -C apultra

upkr/target/release/upkr:
	cd upkr/ && cargo build --release

clean:
	rm -f ldscripts/*.i *.gba *.elf *.payload *.map res/*.comp emu/*.comp *.comp src/menu_messages.h src/messages_data.h

