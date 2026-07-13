CC = gcc
CFLAGS = -Wall -Wextra
ASAN_FLAGS = -fsanitize=address -g

MTE_CC = aarch64-linux-gnu-gcc
MTE_FLAGS = -march=armv8.5-a+memtag -g -static -fno-stack-protector

all: demo demo_asan demo_mte

demo: iot_vuln_demo.c
	$(CC) $(CFLAGS) -o $@ $<

demo_asan: iot_vuln_demo.c
	$(CC) $(ASAN_FLAGS) -o $@ $<

demo_mte: main_mte.c
	$(MTE_CC) $(MTE_FLAGS) -o $@ $<

run: demo_asan
	./demo_asan

run_mte: demo_mte
	GLIBC_TUNABLES=glibc.mem.tagging=3 qemu-aarch64 -cpu max ./demo_mte

clean:
	rm -f demo demo_asan demo_mte

.PHONY: all run run_mte clean