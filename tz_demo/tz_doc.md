# TrustZone demo details

A single non-secure Zephyr app (`tz_demo/src/main.c`) stores the same
secret two different ways and then tries a direct, unrelated access into
secure memory, to show all three outcomes in one run.

Board target: `mps2/an521/cpu0/ns`

Boot order: BL2 bootloader -> TF-M (secure world) -> this Zephyr image
(non-secure world). TF-M configures the SAU/IDAU before jumping to
non-secure code, marking its own flash and RAM ranges as secure-only.

## What `main()` does, in order

1. **`legit_key_storage()`** - saves the key through the PSA Protected
   Storage API (`psa_ps_set()` / `psa_ps_get()`), which TF-M exposes by
   default and backs with secure-side storage. This works from non-secure
   code because it's a secure service call, not a direct memory access -
   the correct way to keep a secret under TrustZone.
2. **Plain non-secure global** - the same key also exists as an ordinary
   `const char key[]` in this image's own non-secure memory. Since it
   never goes through TF-M, it's just as readable as any other variable
   to anything running in this image. The demo prints it directly to show
   that TrustZone buys nothing for data you didn't route through it.
3. **`untrusted_read_attempt()`** - a direct, non-secure load from a
   hardcoded address (`0x10000000`, the secure alias of flash address
   `0x0`). This isn't an attempt to locate the PSA-stored key itself: on
   Armv8-M the secure/non-secure split is enforced per address range, not
   per variable, so any non-secure access into a Secure-flagged range
   faults regardless of what's actually stored there. This step
   demonstrates that boundary directly.

## What "crash" actually looks like

Expect steps 1 and 2 to complete and print their output normally. Step 3
produces a fault report on the console instead of a returned value, then a
halt or reboot depending on your fault handling config
(`CONFIG_RESET_ON_FATAL_ERROR`). The exact text differs across Zephyr
versions; what matters for the comparison is that step 3 never reaches its
own "you should never see this line" print.

## Build and run

`CONFIG_BUILD_WITH_TFM=y` in `prj.conf` means building against the
`.../cpu0/ns` board target pulls in and builds TF-M automatically as part
of the same `west build` - no separate secure-image build step. That does
make the first build noticeably slower than the other demos, since TF-M
itself has to compile.

```bash
cd tz_demo

QEMU_EXTRA_FLAGS=-no-reboot west build -p always -b mps2/an521/cpu0/ns . -d build_tz
west build -d build_tz -t run
```

Expect, in order: the `[GOOD]` lines from `legit_key_storage()`, the
`[untrusted code]` line printing the plain non-secure key, then a
`[BAD] attempting to read secure flash at ...` line followed by a fault
report - a `SecureFault` (or equivalent for your Zephyr version) instead
of the `[BAD] got 0x...` line that would follow a successful read.

### Avoiding a QEMU reboot loop

`QEMU_EXTRA_FLAGS=-no-reboot` (set in the environment, as in the command
above) tells QEMU to stop the guest's reset request from actually
restarting the VM: it exits instead, so you see the fault exactly once.
This flag is read by Zephyr's CMake at *configure* time, not at run time,
so it has to be present on the initial `west build` (or any rebuild with
`-p always`) - setting it only before `west build -t run` on an
already-configured `build_tz` has no effect.
