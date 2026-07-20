# Zephyr Compile-Time Hardening Demo (native_sim)

Demonstrates 4 compile-time hardening flags by comparing a vulnerable
build vs a hardened build on the same code.

Target: **native_sim** – runs as a native Linux binary, no hardware
or real QEMU needed, but it is still an official Zephyr board target
(ideal here because it is the only practical way to also run Clang's CFI:
on bare-metal Cortex-M a hand-written `__cfi_check` runtime would be required).


## 1. Canary (`-fstack-protector-strong`)

Canaries or canary words or stack cookies are known values that are placed between a buffer and control data on the stack to monitor buffer overflows. When the buffer overflows, the first data to be corrupted will usually be the canary, and a failed verification of the canary data will therefore alert of an overflow, which can then be handled, for example, by invalidating the corrupted data.

```bash
cd flags_demo

west build -b native_sim/native/64 -d build_vuln_canary . -- -DHARDENED=OFF -DDEMO=canary
west build -d build_vuln_canary -t run
# silent overflow, undefined behavior

west build -b native_sim/native/64 -d build_hard_canary . -- -DHARDENED=ON -DDEMO=canary
west build -d build_hard_canary -t run
# *** stack smashing detected ***: terminated
```

Without stack protector we are not sure that the program stops with a segfault.
In fact, if the overflow does not overwrite the return address (e.g. another variable) or if it overwrites it with a valid address, the execution of the program continues normally.

Note: Canary are checked at the return of a function and NOT during the Overflow


## 2. FORTIFY_SOURCE (`-D_FORTIFY_SOURCE=2`)

FORTIFY_SOURCE is a feature available in the GNU C Library that provides runtime protection against certain types of security vulnerabilities. Specifically, FORTIFY_SOURCE detects and prevents buffer overflow and formats string vulnerabilities, which are two common types of vulnerabilities that attackers can exploit to take control of a system or steal sensitive data.

FORTIFY_SOURCE works by providing enhanced versions of certain C library functions that can detect when a buffer overflow or format string vulnerability is about to occur. When a vulnerable function is called, FORTIFY_SOURCE checks the size of the buffer being used and ensures that it is not being overrun. If an overflow or vulnerability is detected, FORTIFY_SOURCE immediately terminates the program to prevent further damage.

When using FORTIFY_SOURCE, you can specify a level of protection between 0 and 3. The higher the level, the more security features are enabled. The default level is 1.

See this [article](https://developers.redhat.com/articles/2023/07/04/developers-guide-secure-coding-fortifysource#how_to_use_fortify_source) for a more detailed explanetion

```bash
west build -b native_sim/native/64 -d build_vuln_fortify . -- -DHARDENED=OFF -DDEMO=fortify
west build -d build_vuln_fortify -t run
# silent overflow

west build -b native_sim/native/64 -d build_hard_fortify . -- -DHARDENED=ON -DDEMO=fortify
west build -d build_hard_fortify -t run
# *** buffer overflow detected *** / abort from __chk_fail
```



Note: picolibc is required (already in `prj.conf`) because Zephyr's minimal
libc does not implement the fortified `*_chk` variants.

## 3. Control Flow Integrity

Clang includes an implementation of a number of control flow integrity (CFI) schemes, which are designed to abort the program upon detecting certain forms of undefined behavior that can potentially allow attackers to subvert the program’s control flow. These schemes have been optimized for performance, allowing developers to enable them in release builds.

Requires the Zephyr LLVM toolchain:

```bash
west sdk install   # if not already done, make sure llvm is included
```
```bash
west build -b native_sim/native/64 -d build_vuln_cfi \
    -- -DHARDENED=OFF -DDEMO=cfi -DZEPHYR_TOOLCHAIN_VARIANT=host/llvm
west build -d build_vuln_cfi -t run
# the call with the wrong type succeeds (or crashes unpredictably) – no protection

west build -b native_sim/native/64 -d build_hard_cfi \
    -- -DHARDENED=ON -DDEMO=cfi -DZEPHYR_TOOLCHAIN_VARIANT=host/llvm -DCONFIG_LLVM_USE_LLD=y
west build -d build_hard_cfi -t run
# illegal instruction / "CFI failure": the call is blocked before it is executed
```

Note: for native_sim/POSIX boards, `-DZEPHYR_TOOLCHAIN_VARIANT=llvm` alone is silently
overridden back to the host GCC toolchain (see `cmake/modules/FindHostTools.cmake`) –
it must be `host/llvm`. `-DCONFIG_LLVM_USE_LLD=y` is also required: the LLVM toolchain
defaults to linking with `ld.bfd`, which cannot read the LLVM bitcode objects produced
by `-flto` (needed by CFI), and the link fails with
`member ... in archive is not an object`.



## 4. Warning-as-error (`-Wall -Wextra -Werror -Wformat-security`)

This flag acts **at compile-time**, not at runtime: in `src/main.c`
there is a commented-out line (`printf(payload);` instead of
`printf("%s", payload);`). Uncomment it and try:

```bash
west build -b native_sim/native/64 -d build_werror . -- -DHARDENED=ON -DDEMO=warnings

west build -b native_sim/native/64 -d build_werror . -- -DHARDENED=OFF -DDEMO=warnings
```

With `HARDENED=ON` the build fails immediately for a non-literal format string
(`-Wformat-security`), before even producing a binary.
With `HARDENED=OFF` it compiles without a word – the bug would reach production.

## Summary

| Flag | Bug class blocked | When it acts |
|---|---|---|
| `-fstack-protector-strong` | stack buffer overflow → return address overwrite | runtime (on function exit) |
| `-D_FORTIFY_SOURCE=2` | overflow on string.h functions with size known to the compiler | runtime (inside the call) |
| `-Wformat-security` (with `-Werror`) | format string bug | compile-time |
| `-fsanitize=cfi` | function pointer / vtable hijacking | runtime (at indirect call) |
