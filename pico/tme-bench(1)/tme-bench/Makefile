CC = gcc
CFLAGS = -O3 -std=gnu99 -Wall -Wextra -Wpedantic -Werror -fno-lto -msse2 -mno-avx
LDLIBS = -lm

.PHONY: all clean disasm
all: tme_bench
tme_bench: tme_bench.c
	$(CC) $(CFLAGS) $< -o $@ $(LDLIBS)
disasm: tme_bench
	objdump -d -Mintel tme_bench > tme_bench.disasm
clean:
	rm -f tme_bench tme_bench.disasm

.PHONY: check
check: tme_bench
	$(CC) $(CFLAGS) tests/test_kernels.c -o /tmp/tme-test-kernels $(LDLIBS)
	/tmp/tme-test-kernels
	python3 -m unittest discover -s tests -p 'test_*.py'
