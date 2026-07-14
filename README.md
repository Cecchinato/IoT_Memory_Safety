# Embedded Security Demonstrations on Zephyr

Five self-contained demos, each pairing a classic embedded memory bug with a specific hardware or compiler-level protection: what the bug looks like unprotected, and what actually happens when the protection catches it.

## Demos

| Demo | Focus | Board / runtime | Docs |
|---|---|---|---|
| **MPU** | Memory Protection Unit - stops an unprivileged thread and a runaway stack from touching memory outside their bounds | `mps2/an385` (Cortex-M3, QEMU) | [mpu_doc.md](./mpu_demo/mpu_doc.md) |
| **TrustZone** | Arm TrustZone-M - hardware isolation between Secure and Non-secure worlds | `mps2/an521` + TF-M (QEMU) | [tz_doc.md](./tz_demo/tz_doc.md) |
| **MTE** | Arm Memory Tagging Extension - hardware tag checking on heap memory, catches a real use-after-free | AArch64 Linux binary under `qemu-aarch64` (no Zephyr) | [mte_doc.md](./mte_demo/mte_doc.md) |
| **ASan** | AddressSanitizer - compile-time instrumentation; defines the reference bug set the other demos reuse | `native_sim` (runs as a plain Linux process) | [asandoc.md](./asan_demo/asandoc.md) |
| **Hardening flags** | Stack canary, `_FORTIFY_SOURCE`, CFI, `-Wformat-security` - four compiler/linker flags, vulnerable vs hardened | `native_sim` | [flagsdoc.md](./flags_demo/flagsdoc.md) |

The ASan demo's `main.c` defines six bug categories: stack overflow, unvalidated packet length, config-store overflow, sensor out-of-bounds write, session use-after-free/double-free, and OTA chunk overflow. The MTE demo reuses that same code, unmodified, to run the identical bugs through a second, independent detection mechanism.

## Prerequisites

MPU, TrustZone, ASan, and the hardening-flags demo are Zephyr applications and need a working Zephyr workspace: SDK, toolchain, `west`, and QEMU, same as any other Zephyr project.

The MTE demo is not a Zephyr project. It's a plain AArch64 Linux binary, so it only needs an aarch64 cross-compiler (`gcc-aarch64-linux-gnu`) and `qemu-user` with Arm MTE emulation support. See `mte_doc.md` for the exact commands.

## Navigation

- [MPU Demo](./mpu_demo/) - `mpu_demo/mpu_doc.md`
- [TrustZone Demo](./tz_demo/) - `tz_demo/tz_doc.md`
- [MTE Demo](./mte_demo/) - `mte_demo/mte_doc.md`
- [ASan Demo](./asan_demo/) - `asan_demo/asandoc.md`
- [Hardening Flags Demo](./flags_demo/) - `flags_demo/flagsdoc.md`
