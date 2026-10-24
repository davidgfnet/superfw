# Fuzz tests for the parsers of files read from the SD card (libFuzzer).
#   make            build all the fuzzers
#   make run        run each one for FUZZTIME seconds (crashes are saved as
#                   crash-* files, the run fails)

CC = clang
FUZZTIME ?= 45
SRC = ../../src
CFLAGS = -g -O1 -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=undefined \
         -I$(SRC) -I../.. -ffunction-sections -fdata-sections -Wl,--gc-sections \
         -Wno-unused-function -Wno-unused-parameter \
         -Wno-gnu-variable-sized-type-not-at-end
COMMON = stubs.c $(SRC)/util.c $(SRC)/nanoprintf.c $(SRC)/fileutil.c

TARGETS = fuzz_cheats fuzz_recent fuzz_patch fuzz_flashmgr fuzz_settings fuzz_text

all: $(TARGETS)

fuzz_cheats: fuzz_cheats.c $(COMMON) $(SRC)/cheats.c
	$(CC) $(CFLAGS) -o $@ $^

fuzz_recent: fuzz_recent.c $(COMMON) $(SRC)/recent.c
	$(CC) $(CFLAGS) -o $@ $^

fuzz_patch: fuzz_patch.c patches_stubs.c $(COMMON) $(SRC)/patchengine.c $(SRC)/patcher.c
	$(CC) $(CFLAGS) -o $@ $^

fuzz_flashmgr: fuzz_flashmgr.c $(COMMON) $(SRC)/flash_mgr.c
	$(CC) $(CFLAGS) -o $@ $^

fuzz_settings: fuzz_settings.c $(COMMON) $(SRC)/settings.c
	$(CC) $(CFLAGS) -o $@ $^

fuzz_text: fuzz_text.c $(COMMON) $(SRC)/utf_util.c $(SRC)/carousel_util.c
	$(CC) $(CFLAGS) -o $@ $^

# Seed corpora: the test files we already have, plus whatever was found.
run: all
	set -e; for t in $(TARGETS); do \
	  mkdir -p corpus/$$t; \
	  if [ $$t = fuzz_cheats ]; then cp ../data/*.cht corpus/$$t/; fi; \
	  echo "== $$t"; \
	  ./$$t -max_total_time=$(FUZZTIME) -max_len=4096 -print_final_stats=1 corpus/$$t; \
	done

clean:
	rm -rf $(TARGETS) corpus crash-* leak-* timeout-* oom-*
