# UndefinedBehaviorSanitizer on Zephyr

Boards: `native_sim` (host process) and `mps2/an385` (Cortex-M3, QEMU).

Eight undefined-behavior bugs in IoT-shaped firmware, run three ways: with no
sanitizer at all, with UBSan in library mode, and with UBSan in trap mode. Same
source, three Kconfig profiles, no code changes between runs.

## What UBSan is, and why it needs its own demo

**Undefined behavior** is not "the wrong answer". It is the C standard declining
to specify *any* answer, which leaves the compiler free to assume the situation
never arises. That assumption is the dangerous part: a signed overflow that
"just wraps" today is also a licence for the optimizer to delete the
`if (x < 0)` check you wrote to catch it.

**UBSan** instruments the operations where this can happen - arithmetic,
shifts, casts, pointer loads - and reports the moment one of them is actually
undefined at run time.

### It finds a different class of bug than ASan

[`Asan_demo`](../Asan_demo/asandoc.md) covers *memory* errors: overflows,
use-after-free, double-free. UBSan covers *undefined operations*, most of which
never touch memory out of bounds at all. Of this demo's eight bugs, exactly one
(`bounds`) is something ASan would also catch. Run the other seven under ASan
and it stays silent - not because they are safe, but because they are not its
job.

### Unlike ASan, it runs on the real target

This is the bigger difference. `asandoc.md` opens by explaining that ASan needs
the host's runtime, which is why the ASan demo has to run on `native_sim`, and
why the honest workflow is "test on native_sim, ship without it".

UBSan has no such restriction:

- `CONFIG_UBSAN_TRAP` emits `__builtin_trap()` and needs no runtime at all, so
  it works on any architecture.
- `CONFIG_UBSAN_LIBRARY` needs handler routines, and **picolibc ships them**
  (`modules/lib/picolibc/newlib/libc/ubsan/`, ~35 `__ubsan_handle_*` functions).
  `mps2/an385` selects picolibc by default, so full diagnostics work on the
  Cortex-M build - verified below.

So UBSan is the one sanitizer in this collection you can plausibly leave on in
a debug build of real firmware.

## The two modes

From `subsys/debug/Kconfig`, both under the same `choice`, default `LIBRARY`:

| | `CONFIG_UBSAN_LIBRARY` | `CONFIG_UBSAN_TRAP` |
|---|---|---|
| On detection | calls into a ubsan runtime | executes `__builtin_trap()` |
| Output | check name, file, line, and the actual values | a fault, and a PC |
| Requires | `ARCH_POSIX` **or** `PICOLIBC` | nothing |
| Continues after? | host libubsan: yes. picolibc: no, `__ubsan_error()` calls `abort()` | no |

Kconfig's own summary of trap mode: *"This can be used on any target, but the
lack of information makes figuring out the triggering code difficult."* Section
[Recovering a trap](#recovering-a-trap-back-to-a-source-line) is that difficulty,
measured.

## The eight bugs

All in [`src/main.c`](./src/main.c). Each is written as a plausible firmware
mistake rather than a synthetic one.

| `-DUB=` | Undefined behavior | Check | Scenario |
|---|---|---|---|
| `intovf` | signed integer overflow | `signed-integer-overflow` | uptime `seconds * 1000` past `INT32_MAX` after ~24.8 days |
| `shift` | shift exponent out of range | `shift` | `1u << bit` building a register mask from a peer-supplied bit index of 35 |
| `align` | misaligned pointer load | `alignment` | casting `packet + 3` to `uint32_t *` to avoid a memcpy |
| `bounds` | array index out of range | `bounds` | `samples[11]` on an 8-element array |
| `badload` | value invalid for its type | `bool` | a `bool` flag memcpy'd from a wire byte holding 7 |
| `vla` | non-positive VLA bound | `vla-bound` | stack scratch buffer sized from a peer-supplied length of 0 |
| `fcast` | float→int cast overflow | `float-cast-overflow` | miscalibrated ADC reading of `1e10` narrowed to `int16_t` |
| `divzero` | integer division by zero | `integer-divide-by-zero` | `sum / sample_count` on a frame reporting 0 samples |

`-DUB=all` (the default) runs them in that order.

### Two things the source does deliberately

**Every peer-controlled input is `volatile`.** This is load-bearing, not style.
Given a plain constant, GCC folds the expression at compile time, the undefined
operation never reaches the generated code, and there is nothing left to
instrument - the demo silently stops demonstrating anything. `volatile` forces a
run-time load, which is also how the value would really arrive. `prj.conf` adds
`CONFIG_DEBUG_OPTIMIZATIONS=y` (`-Og`) as the second half of the same defense.

**Bug selection is compile-time, not an environment variable.** `Asan_demo` uses
`BUG_TEST` and `getenv()`, which only works under `native_sim`. This app also
runs on `mps2/an385` under QEMU, where there is no environment, so the selector
is a CMake cache variable mapped to a `-DUB_<NAME>` define - the same approach
[`flags_demo`](../flags_demo/flags_doc.md) uses for `-DDEMO=`.

**`divzero` is last on purpose.** It is the one bug here that the hardware
itself traps on x86 (`SIGFPE`), so in the baseline build it ends the run. Placed
anywhere but last, it would hide every bug after it.

## Build and run

### 1. Baseline - no sanitizer

```bash
cd ubsan_demo
west build -p always -b native_sim/native/64 -d build_off .
west build -d build_off -t run
```

```
========= IoT Firmware Undefined-Behavior Demo =========
[UPTIME] 2200000 s -> -2094967296 ms
[REG]    bit=35 -> mask=0x00000008
[NET]    field at offset 3 = 0xadbeef00
[SENSOR] sample[11] = 30064
[NET]    retain flag: yes
[NET]    dispatch: unknown type 7
[OTA]    scratch buffer of 0 bytes at 0x7ea8515fddf0
[ADC]    10000000000.0 -> raw 0
Floating point exception
```

Seven bugs pass without a word, and the run ends on a `SIGFPE` that names
neither the file nor the operation. Two lines are worth staring at:

- `-2094967296 ms` - the uptime went negative. Any downstream `if (ms > 0)`
  check is now wrong, and the compiler was entitled to delete it.
- `mask=0x00000008` - the demo asked for bit 35 and got bit **3**, because x86
  takes shift counts mod 32. Arm typically yields `0` for the same expression.
  The same firmware, same source, different behavior per architecture. That is
  what "undefined" buys you.

### 2. Library mode on `native_sim`

```bash
west build -p always -b native_sim/native/64 -d build_lib . -- -DEXTRA_CONF_FILE=library.conf
west build -d build_lib -t run
```

```
src/main.c:87:10: runtime error: signed integer overflow: 2200000 * 1000 cannot be represented in type 'int'
[UPTIME] 2200000 s -> -2094967296 ms
src/main.c:107:21: runtime error: shift exponent 35 is too large for 32-bit type 'unsigned int'
[REG]    bit=35 -> mask=0x00000008
src/main.c:158:11: runtime error: load of misaligned address 0x00000041b313 for type 'const uint32_t', which requires 4 byte alignment
0x00000041b313: note: pointer points here
 00  cd ab 01 00 ef be ad de  11 22 33 44 55 66 77 88  a8 42 41 00 00 00 00 00  18 01 00 00 02 00 00
              ^
[NET]    field at offset 3 = 0xadbeef00
src/main.c:177:28: runtime error: index 11 out of bounds for type 'int16_t [8]'
src/main.c:177:10: runtime error: load of address 0x00000041b4f6 with insufficient space for an object of type 'int16_t'
[SENSOR] sample[11] = 8963
src/main.c:225:42: runtime error: load of value 7, which is not a valid value for type '_Bool'
[NET]    retain flag: yes
[NET]    dispatch: unknown type 7
src/main.c:259:10: runtime error: variable length array bound evaluates to non-positive value 0
[OTA]    scratch buffer of 0 bytes at 0x741c3d7fdda0
src/main.c:280:2: runtime error: 1e+10 is outside the range of representable values of type 'short int'
[ADC]    10000000000.0 -> raw 0
src/main.c:130:20: runtime error: division by zero
Floating point exception
```

(Paths shortened for width; the real output prints them in full.)

Every finding names the check, the source line, and the operands. Note that host
libubsan **prints and continues** all eight are reported in a single run.

To isolate one:

```bash
west build -p always -b native_sim/native/64 -d build_shift . -- -DEXTRA_CONF_FILE=library.conf -DUB=shift
west build -d build_shift -t run
```
```
========= IoT Firmware Undefined-Behavior Demo =========
src/main.c:107:21: runtime error: shift exponent 35 is too large for 32-bit type 'unsigned int'
[REG]    bit=35 -> mask=0x00000008
```

### 3. Library mode on the Cortex-M target

The claim this demo exists to make. Same profile, real embedded board:

```bash
west build -p always -b mps2/an385 -d build_lib_arm . -- -DEXTRA_CONF_FILE=library.conf
west build -d build_lib_arm -t run     # exit QEMU with CTRL+a, x
```

```
*** Booting Zephyr OS build v4.4.0-6397-g4b3d6fa04129 ***
========= IoT Firmware Undefined-Behavior Demo =========
UBSAN: ERROR mul_overflow src/main.c:87 2200000(s32) * 1000(s32)
abort()
r0/a1:  0x00000004  r1/a2:  0x0000000a  r2/a3:  0x40004004
r3/a4:  0x00000004 r12/ip:  0x20007427 r14/lr:  0x00002cd5
 xpsr:  0x21000000
Faulting instruction address (r15/pc): 0x00002ce4
```

Different formatting from libubsan - this is picolibc's `__ubsan_message()` -
but the same three pieces of information: the check (`mul_overflow`), the
location (`main.c:87`), and the operands with their types
(`2200000(s32) * 1000(s32)`).

Unlike the host build, it stops here: picolibc's `__ubsan_error()` ends with
`abort()`, so the first finding is the last. To see the others, rebuild with
`-DUB=<name>`.

Confirm the handlers really came from picolibc:

```bash
$ nm build_lib_arm/zephyr/zephyr.elf | grep __ubsan_handle | head -4
00009249 T __ubsan_handle_add_overflow
0000927d T __ubsan_handle_builtin_unreachable
00009291 T __ubsan_handle_divrem_overflow
000092c5 T __ubsan_handle_float_cast_overflow
```

### 4. Trap mode on the Cortex-M target

```bash
west build -p always -b mps2/an385 -d build_trap . -- -DEXTRA_CONF_FILE=trap.conf
west build -d build_trap -t run
```

```
*** Booting Zephyr OS build v4.4.0-6397-g4b3d6fa04129 ***
========= IoT Firmware Undefined-Behavior Demo =========
***** USAGE FAULT *****
  Attempt to execute undefined instruction
r0/a1:  0x00000000  r1/a2:  0x002191c0  r2/a3:  0x83215600
r3/a4:  0x00000000 r12/ip:  0x200024ac r14/lr:  0x00000939
 xpsr:  0x01000000
Faulting instruction address (r15/pc): 0x000006a4
```

`__builtin_trap()` compiles to `udf` on Cortex-M, which raises a usage fault.
The bug is caught just as reliably as in library mode - the firmware stops
before the bad value propagates - but everything about *what* went wrong is
gone. There is no "signed integer overflow", no `main.c:87`, no operands. Just
an address.

## Recovering a trap back to a source line

Take the PC from the fault dump and resolve it against the ELF:

```bash
$ ~/zephyr-sdk-*/gnu/arm-zephyr-eabi/bin/arm-zephyr-eabi-addr2line \
      -f -e build_trap/zephyr/zephyr.elf 0x000006a4
report_uptime_ms
/home/mirko/zephyrproject/zephyr/samples/IoT_Memory_Safety/ubsan_demo/src/main.c:87
```

Same line library mode printed by itself. So trap mode is not *unusable* - but
it costs a manual step, needs the exact ELF that produced the fault (a stripped
or rebuilt binary will not do), and it recovers only *where*, never *what* or
*which values*. On a device in the field, where all you get back is a crash
address, that difference is the whole ballgame.

### What that information costs

Same source, same board, three profiles:

| Profile | text | data | vs. baseline |
|---|---|---|---|
| baseline | 18500 B | 200 B | reference |
| `trap.conf` | 26648 B | 240 B | +44% text, +40 B data |
| `library.conf` | 49472 B | 20800 B | +167% text, +20.6 KB data |

(`arm-zephyr-eabi-size zephyr/zephyr.elf`, `mps2/an385`, `-DUB=all`.)

Trap mode is not free - `-fsanitize=undefined` still inserts a comparison and
branch before every instrumented operation; the `udf` is only what sits at the
end of the branch. What it avoids is the *data*: library mode's extra 20.6 KB
are the source-location strings and type descriptors that let the handler print
`main.c:87 2200000(s32) * 1000(s32)`. Every instrumented site carries its own
static record.

That is the whole trade in one table. Both modes catch the bug; library mode
pays ~20 KB of flash to also tell you what it was.

Practical reading: use library mode wherever it fits (native_sim, and any
picolibc target with flash to spare), and keep trap mode for targets where it
does not, accepting the addr2line step.

## Caveats worth knowing

**`-fsanitize=undefined` is a group, and two useful checks are not in it.**

- `float-cast-overflow` is excluded by both GCC and Clang, so `CONFIG_UBSAN`
  alone never reports the `fcast` bug. This demo's `CMakeLists.txt` adds
  `-fsanitize=float-cast-overflow` explicitly, on the `app` target only, and
  only when `CONFIG_UBSAN` is set - so the baseline profile stays a true
  no-sanitizer build.
- `enum` is nominally in the group but does not fire in C. The `badload` bug
  loads *two* invalid values from the same wire byte: a `bool` holding 7, and a
  `msg_type_t` enum (declared `0..3`) also holding 7. Both are equally
  undefined. Only the `bool` is reported - neither GCC 15 nor Clang flags the
  enum in C mode. The two are side by side in `parse_message_header()`
  deliberately: a silent sanitizer is not the same as correct code.

**`CONFIG_UBSAN` instruments all of Zephyr, not just your app.** It is applied
via `zephyr_compile_options()` in `zephyr/CMakeLists.txt`, so kernel and driver
code is instrumented too. That is usually what you want, and in this demo it
produced no false positives on either board. If some other application does hit
noise from kernel code, the escape hatch is to drop `CONFIG_UBSAN` and put
`-fsanitize=undefined` on the `app` target alone in `CMakeLists.txt`.

**Optimization level changes what you see.** At `-Os` (Zephyr's default) GCC
folds many of these expressions at compile time and reports them as
`-Warray-bounds` / `-Woverflow` warnings instead, with no run-time check left.
The `volatile` inputs plus `CONFIG_DEBUG_OPTIMIZATIONS=y` are what keep the
run-time behavior observable. This cuts both ways in real use: UBSan findings
can appear and disappear as you change `-O` level, so pin it when you rely on
them.

**Some UB traps by accident, and that is not a guarantee.** x86 raises `SIGFPE`
on integer division by zero with no sanitizer at all. Arm does not. Hardware
that happens to catch one specific undefined operation on one specific
architecture is not a substitute for checking.

---
