# Zephyr Compile-Time Hardening Demo (native_sim)

Demonstrates 4 compile-time hardening flags by comparing a vulnerable
build vs a hardened build on the same code.

Target: **native_sim** – runs as a native Linux binary, no hardware
or real QEMU needed, but it is still an official Zephyr board target
(ideal here because it is the only practical way to also run Clang's CFI:
on bare-metal Cortex-M a hand-written `__cfi_check` runtime would be required).


## 1. Canary (`-fstack-protector-strong`)

```bash
cd flags_demo

west build -b native_sim/native/64 -d build_vuln_canary . -- -DHARDENED=OFF -DDEMO=canary
west build -d build_vuln_canary -t run
# silent overflow, undefined behavior

west build -b native_sim/native/64 -d build_hard_canary . -- -DHARDENED=ON -DDEMO=canary
west build -d build_hard_canary -t run
# *** stack smashing detected ***: terminated
```

Verify the canary is actually present in the binary:

```bash
nm build_hard_canary/zephyr/zephyr.elf | grep stack_chk
# should show __stack_chk_fail / __stack_chk_guard
```

## 2. FORTIFY_SOURCE (`-D_FORTIFY_SOURCE=2`)

```bash
west build -b native_sim/native/64 -d build_vuln_fortify . -- -DHARDENED=OFF -DDEMO=fortify
west build -d build_vuln_fortify -t run
# silent overflow

west build -b native_sim/native/64 -d build_hard_fortify . -- -DHARDENED=ON -DDEMO=fortify
west build -d build_hard_fortify -t run
# *** buffer overflow detected *** / abort from __chk_fail
```

```bash
nm build_hard_fortify/zephyr/zephyr.elf | grep _chk
# should show __strcpy_chk (or similar)
```

Note: picolibc is required (already in `prj.conf`) because Zephyr's minimal
libc does not implement the fortified `*_chk` variants.

## 3. Control Flow Integrity

Requires the Zephyr LLVM toolchain:

```bash
west sdk install   # if not already done, make sure llvm is included

west build -b native_sim/native/64 -d build_vuln_cfi \
    -- -DHARDENED=OFF -DDEMO=cfi -DZEPHYR_TOOLCHAIN_VARIANT=llvm
west build -d build_vuln_cfi -t run
# the call with the wrong type succeeds (or crashes unpredictably) – no protection

west build -b native_sim/native/64 -d build_hard_cfi \
    -- -DHARDENED=ON -DDEMO=cfi -DZEPHYR_TOOLCHAIN_VARIANT=llvm
west build -d build_hard_cfi -t run
# illegal instruction / "CFI failure": the call is blocked before it is executed
```

```bash
nm build_hard_cfi/zephyr/zephyr.elf | grep cfi_check
```

## 4. Warning-as-error (`-Wall -Wextra -Werror -Wformat-security`)

This flag acts **at compile-time**, not at runtime: in `src/main.c`
there is a commented-out line (`printk(payload);` instead of
`printk("%s", payload);`). Uncomment it and try:

```bash
west build -b native_sim/native/64 -d build_werror . -- -DHARDENED=ON -DDEMO=canary
```

With `HARDENED=ON` the build fails immediately for a non-literal format string
(`-Wformat-security`), before even producing a binary.
With `HARDENED=OFF` it compiles without a word – the bug would reach production.

## Flag → Defense Summary

| Flag | Bug class blocked | When it acts |
|---|---|---|
| `-fstack-protector-strong` | stack buffer overflow → return address overwrite | runtime (on function exit) |
| `-D_FORTIFY_SOURCE=2` | overflow on string.h functions with size known to the compiler | runtime (inside the call) |
| `-Wformat-security` (+`-Werror`) | format string bug | compile-time |
| `-fsanitize=cfi` | function pointer / vtable hijacking | runtime (at indirect call) |
