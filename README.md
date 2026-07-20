# Embedded Security Demonstrations on Zephyr

Five self-contained demos. Each one pairs a classic embedded memory bug
with a specific hardware- or compiler-level protection, so you can see
exactly what the bug looks like unprotected, and exactly what happens the
moment the protection catches it.


## Demos

| Demo | Focus | Board / runtime | Docs |
|---|---|---|---|
| **MPU** | Memory Protection Unit — stops an unprivileged thread and a runaway stack from touching memory outside their bounds | `mps2/an385` (Cortex-M3, QEMU) | [mpu_doc.md](./mpu_demo/mpu_doc.md) |
| **TrustZone** | Arm TrustZone-M — hardware isolation between Secure and Non-secure worlds | `mps2/an521` + TF-M (QEMU) | [tz_doc.md](./tz_demo/tz_doc.md) |
| **MTE** | Arm Memory Tagging Extension — hardware tag checking on heap memory, catches a real use-after-free | AArch64 Linux binary under `qemu-aarch64` (no Zephyr) | [mte_doc.md](./MTE_demo/mte_doc.md) |
| **ASan** | AddressSanitizer — compile-time instrumentation; defines the reference bug set the other demos reuse | `native_sim` (runs as a plain Linux process) | [asandoc.md](./Asan_demo/asandoc.md) |
| **Hardening flags** | Stack canary, `_FORTIFY_SOURCE`, CFI, `-Wformat-security` — four compiler/linker flags, vulnerable vs. hardened | `native_sim` | [flags_doc.md](./flags_demo/flags_doc.md) |

### Shared bug set

The ASan demo's `main.c` defines six bug categories: stack overflow,
unvalidated packet length, config-store overflow, sensor out-of-bounds
write, session use-after-free/double-free, and OTA chunk overflow. The MTE
demo reuses that same code, unmodified, to run the identical bugs through
a second, independent detection mechanism — so the two demos are directly
comparable, bug for bug.



## Repository layout

```
.
├── mpu_demo/    MPU: unprivileged thread + stack-overflow protection
│   ├── mpu_doc.md
│   └── src/main.c
├── tz_demo/     TrustZone-M: secure vs. non-secure worlds
│   ├── tz_doc.md
│   └── src/main.c
├── MTE_demo/    Memory Tagging Extension (plain AArch64 binary, no Zephyr)
│   ├── mte_doc.md
│   └── main_mte.c
├── Asan_demo/   AddressSanitizer, defines the shared six-bug set
│   ├── asandoc.md
│   └── src/main.c
└── flags_demo/  Compiler/linker hardening flags
    ├── flags_doc.md
    └── src/main.c
```

Each demo folder is self-contained: its own `prj.conf`, `CMakeLists.txt`,
and a doc file that walks through the exact build and run commands.



## Prerequisites

**MPU, TrustZone, ASan, and Hardening flags** are Zephyr applications and
need a working Zephyr workspace: SDK, toolchain, `west`, and QEMU, same as
any other Zephyr project.

**MTE** is not a Zephyr project. It's a plain AArch64 Linux binary, so it
only needs an aarch64 cross-compiler (`gcc-aarch64-linux-gnu`) and
`qemu-user` with Arm MTE emulation support. See
[mte_doc.md](./MTE_demo/mte_doc.md) for the exact commands.

---
