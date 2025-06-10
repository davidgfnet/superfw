
VERSION_WORD := 0x00000011
VERSION_SLUG_WORD := $(shell git rev-parse --short=8 HEAD || echo FFFFFFFF)

PREFIX		:= arm-none-eabi-
CC		:= $(PREFIX)gcc
CXX		:= $(PREFIX)g++
OBJDUMP		:= $(PREFIX)objdump
OBJCOPY		:= $(PREFIX)objcopy

COMPRESSION_RATIO ?= 3

# BOARD can be "sd" or "lite"
BOARD ?= sd

ifeq ($(BOARD),lite)
  GLOBAL_DEFINES ?= -DSUPERCARD_LITE_IO
else ifeq ($(BOARD),sd)
  GLOBAL_DEFINES ?= -DBUNDLE_GBC_EMULATOR
else
  $(error No valid board specified in BOARD)
endif

CFLAGS=-O2 -ggdb \
       -D__GBA__ $(GLOBAL_DEFINES) \
       -DSC_FAST_ROM_MIRROR="use_fast_mirror()" \
       -DSD_PREERASE_BLOCKS_WRITE \
       -DVERSION_WORD="$(VERSION_WORD)" \
       -DVERSION_SLUG_WORD="0x$(VERSION_SLUG_WORD)" \
       -mcpu=arm7tdmi -mtune=arm7tdmi -Wall -Isrc -I. -mthumb -flto -flto-partition=none


INGAME_CFLAGS=-Os -ggdb \
              -D__GBA__ $(GLOBAL_DEFINES) \
              -DNO_SUPERCARD_INIT \
              -DSD_PREERASE_BLOCKS_WRITE \
              -mcpu=arm7tdmi -mtune=arm7tdmi -Wall -Isrc -I. \
              -mthumb -flto

DLDI_CFLAGS=-Os -ggdb \
            -D__GBA__ $(GLOBAL_DEFINES) \
            -DSD_PREERASE_BLOCKS_WRITE \
            -mcpu=arm7tdmi -mtune=arm7tdmi -Wall -Isrc -I. \
            -mthumb -flto -fPIC

DIRECTSAVE_CFLAGS=-Os -ggdb \
                 -D__GBA__ $(GLOBAL_DEFINES) \
                 -DNO_SUPERCARD_INIT \
                 -mcpu=arm7tdmi -mtune=arm7tdmi -Wall -Isrc -I. \
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
                src/crc.c \
                src/supercard_driver.c

MENUFILES=src/ingame.S \
          src/ingame_menu.c \
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

all:	firmware.ewram.gba.comp emu/jagoombacolor_v0.5.gba.comp res/patches.db.comp res/fonts.pack.comp
	# Wrap the firmware around a ROM->EWRAM loader
	$(CC) $(CFLAGS) -o firmware.elf rom_boot.S -T ldscripts/gba_romboot.ld -nostartfiles -nostdlib
	$(OBJCOPY) --output-target=binary firmware.elf superfw.gba
	# Fix the header/checksum.
	./tools/fw-fixer.py superfw.gba

firmware.ewram.gba: $(INFILES) ingamemenu.payload superfw.dldi.payload directsave.payload src/messages_data.h
	# Build the actual firmware image
	$(CC) $(CFLAGS) -o firmware.ewram.elf $(INFILES) -T ldscripts/gba_ewram.ld -nostartfiles -Wl,-Map=firmware.ewram.map -Wl,--print-memory-usage -fno-builtin
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

apultra/apultra:
	make -C apultra

upkr/target/release/upkr:
	cd upkr/ && cargo build --release

clean:
	rm -f *.gba *.elf *.payload *.map res/*.comp emu/*.comp *.comp src/menu_messages.h src/messages_data.h

