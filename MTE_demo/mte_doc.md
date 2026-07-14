# MTE (Memory Tagging Extension) Demo

Not a Zephyr project. This is a plain AArch64 Linux binary, built with a standard cross-compiler and run under `qemu-aarch64` user-mode emulation. No board, no `west`, no QEMU-as-a-machine - just a process.

## What and why

Arm's Memory Tagging Extension (Armv8.5-A and later) tags every 16-byte chunk of memory with a small value, and tags the pointer meant to access it the same way. On every load or store, the hardware compares the two tags; a mismatch faults. That catches the same class of bugs as AddressSanitizer - buffer overflows, use-after-free - but the check is enforced by the CPU (or, here, QEMU's emulation of it), not by code the compiler injected. It's meant to be cheap enough to leave on in production, not just in test builds.

`main_mte.c` reuses the exact vulnerable functions from the ASan demo's `main.c` (see `asandoc.md`), unmodified: the same six bug categories - UART line copy, packet payload length, config-store update, sensor frame sample count, session lifecycle, OTA chunk metadata. The point is to run the identical bugs through a second, independent detection mechanism and compare results.

## Build and run

```bash
aarch64-linux-gnu-gcc -march=armv8.5-a+memtag -g -static main_mte.c -o demo_mte
GLIBC_TUNABLES=glibc.mem.tagging=3 qemu-aarch64 ./demo_mte
```

You need the aarch64 cross-toolchain (`gcc-aarch64-linux-gnu` on Debian/Ubuntu) and a `qemu-aarch64` build with Arm MTE emulation support. Older QEMU builds accept the tunable and run fine but never actually flag anything, so if a known-bad run comes back clean, check `qemu-aarch64 --version` before assuming the code is fine. `-static` keeps the binary self-contained, so it doesn't need a matching aarch64 sysroot at run time.

`GLIBC_TUNABLES=glibc.mem.tagging=3` turns tagging on inside glibc's allocator, in synchronous mode: the process aborts at the exact instruction that touches a mismatched tag, instead of running on with corrupted memory and only reporting it later (asynchronous mode). The source's own `prctl(PR_SET_TAGGED_ADDR_CTRL, ...)` call, which would extend tagging to the rest of the address space, is left commented out on purpose - tagging here comes entirely from the environment variable, not from the code.

## Why only one of the six bugs actually gets caught here

That distinction matters, because `GLIBC_TUNABLES=glibc.mem.tagging` only tags memory that comes back from `malloc`. It doesn't tag the stack, and it doesn't tag static or global buffers.

Looking at where each bug's buffer actually lives:

| Bug | Buffer | Memory region | Caught by this run? |
|---|---|---|---|
| UART line copy | `cmd_buf` | stack | No |
| Packet payload | `payload` | stack | No |
| Config store | `g_config_store` | static/global | No |
| Sensor frame | `frame.samples` | stack | No |
| Session lifecycle | `rx_buffer` | heap (`malloc`) | Yes |
| OTA chunk | `chunk.data` | stack | No |

Only the session's `rx_buffer` is heap-allocated. Everything else lives on the stack or in `.bss`, so MTE via this tunable has nothing to check there - those bugs can still corrupt memory, just silently, the same as an unprotected build would.

That one heap bug is also the one wired into `main()` by default. It arms a callback while the session is still alive (`session_arm_rx_callback`), closes the session and frees its buffer (`session_close`), then fires the callback anyway (`rx_callback_fire`) - a genuine use-after-free on `malloc`'d memory. Under this exact run command, that's the fault you should actually see. The UART overflow also triggers by default (the input line is longer than `cmd_buf`), but since that buffer is on the stack, expect silent corruption or an unrelated crash, not an MTE report.

The other four bug functions run with safe, in-bounds input by default; their unsafe call sites exist in the source but are commented out. Unlike the ASan demo, there's no `BUG_TEST` environment variable here - triggering one of them means editing `main()` directly and rebuilding. Given the table above, though, only routing a bug through a heap allocation would let this particular MTE configuration actually catch it.
