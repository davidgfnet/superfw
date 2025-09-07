
COV_FLAGS=-ftest-coverage -fprofile-arcs
MEMCHK_FLAGS=-fsanitize=address -lasan
CFLAGS = -O0 -ggdb -I../src/ -Wall $(COV_FLAGS)

all:
	$(CC) $(CFLAGS) $(MEMCHK_FLAGS) -o cimpl_test.bin cimpl_test.c ../src/cimpl.c -DBUILTIN_PREFIX=superfw_
	./cimpl_test.bin
	lcov -c -d . -o cimpl_test.info
	$(CC) $(CFLAGS) $(MEMCHK_FLAGS) -o util_test.bin util_test.c ../src/util.c ../src/fileutil.c -I../
	./util_test.bin
	lcov -c -d . -o util_test.info
	$(CC) $(CFLAGS) $(MEMCHK_FLAGS) -o utf_util_test.bin utf_util_test.c ../src/utf_util.c -I../
	./utf_util_test.bin
	lcov -c -d . -o utf_util_test.info
	$(CC) $(CFLAGS) $(MEMCHK_FLAGS) -o sha256_test.bin sha256_test.c ../src/sha256.c -lcrypto
	./sha256_test.bin
	lcov -c -d . -o sha256_test.info
	$(CC) $(CFLAGS) -o cheats_test.bin cheats_test.c ../src/cheats.c ../src/util.c -I../
	./cheats_test.bin
	lcov -c -d . -o cheats_test.info
	$(CC) $(CFLAGS) $(MEMCHK_FLAGS) -o crc_test.bin crc_test.c ../src/crc.c
	./crc_test.bin
	lcov -c -d . -o crc_test.info

	lcov -a cimpl_test.info -a util_test.info -a utf_util_test.info -a crc_test.info -a sha256_test.info -a cheats_test.info -o total.info
	rm -rf coverage/
	genhtml -o coverage/ total.info

clean:
	rm -f *.bin *.gcda *.gcno *.info
	rm -rf coverage/

cli_tests:
	$(CC) -flto -O0 -ggdb -I../src/ -Wall $(MEMCHK_FLAGS) -o cli_patchengine.bin cli_patchengine.c ../src/patchengine.c  ../src/util.c  -I../  -ffunction-sections -fdata-sections  -Wl,--gc-sections
