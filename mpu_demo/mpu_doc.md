# MPU (Memory Protection Unit) Demo

Board: `mps2/an385` (Cortex-M3, QEMU-emulated).

Two ways a Cortex-M3 firmware bug gets caught: an unprivileged thread reaching outside the memory it was granted, and a recursive function eating through its own stack. Same source, two Kconfig profiles, no code changes between runs.

## The two tests

`main()` defines two tests, but only the first one actually runs by default. The call to `test_stack_overflow()` is present in `main()` but commented out - uncomment it to exercise the second test.

### Test 1: userspace thread memory violation

With `CONFIG_USERSPACE=y` (the protected profile - Zephyr's userspace support depends on having an MPU), `guarded_var` lives inside a dedicated memory partition (`part0`), and a new thread (`thread0`) starts as `K_USER` - unprivileged - and is added to a memory domain containing only that partition (plus the libc partition, if the C library needs one for things like `errno`).

`thread0_entry()` does a legal write to `guarded_var`, inside its own partition, then an illegal write to a hardcoded address (`0x20001000`) that was never granted to the thread. The MPU catches that second write immediately: the thread faults before it ever reaches its own "write succeeded" line.

Without `CONFIG_USERSPACE` (the unprotected profile), there's no domain to violate. `guarded_var` is an ordinary static global, and the same sequence runs directly in the main thread with no partitioning at all. The illegal write to `0x20001000` lands on valid RAM and just succeeds - the demo prints its own confirmation that nothing stopped it.

### Test 2: stack overflow (disabled by default)

`recursive_overflow()` allocates a 1024-byte buffer on the stack, fills it, prints the current depth, and calls itself again. Nothing bounds the recursion. It runs until it hits either the MPU guard region behind the stack, or whatever memory happens to sit past it if there's no guard at all.

## Build profiles

The two profiles are `protected.conf` and `unprotected.conf`, layered on the same `prj.conf`:

| Profile | `CONFIG_ARM_MPU` | `CONFIG_HW_STACK_PROTECTION` | `CONFIG_USERSPACE` | Result |
|---|---|---|---|---|
| `unprotected.conf` | off | off | off | Silent corruption, or a late, unrelated fault - much harder to diagnose |
| `protected.conf` | on | on | on | Immediate `MemManage` fault on the illegal write (and, if test 2 is enabled, on stack overflow too) |

Worth calling out: under the protected profile, since test 1 is designed to fault, the board halts right there. Test 2 in that same boot never runs. That immediate stop, before the bug can do any more damage, is the actual point of the demo.

## Build and run

```bash
cd mpu_demo

west build -p always -b mps2/an385 -d build_unprotected . -- -DEXTRA_CONF_FILE=unprotected.conf
west build -d build_unprotected -t run
# silent corruption, nothing obviously wrong at all

west build -p always -b mps2/an385 -d build_protected . -- -DEXTRA_CONF_FILE=protected.conf
west build -d build_protected -t run
```

Expect a fault dump along these lines from the protected build:

```
***** MPU FAULT *****
  Data Access Violation
  MMFAR Address: 0x20001000
***** Hardware exception *****
Fatal fault in thread 0x... Aborting thread.
```

To also see the stack-overflow test, uncomment `test_stack_overflow();` in `main()` before building the protected profile.
