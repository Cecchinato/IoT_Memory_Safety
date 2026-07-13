# TrustZone demo details

## Why two different apps instead of one app with a config flag

TrustZone splits the system into two separate firmware images (secure and
non-secure), each with its own memory map, entry point, and in build_b's
case its own build system (TF-M for the secure side). That is not
something you toggle with a single Kconfig option, so build_a and build_b
are genuinely two different applications, not the same app rebuilt twice.

## build_a - no TrustZone

Board target: `mps2/an521/cpu0`

Single image, boots directly into `main()`, full access to the whole
address space. The "attack" is just reading a global array.

## build_b - TrustZone on

Board target: `mps2/an521/cpu0/ns`

Boot order: BL2 bootloader -> TF-M (secure world) -> this Zephyr image
(non-secure world). TF-M configures the SAU/IDAU before jumping to
non-secure code, marking its own flash and RAM ranges as secure-only.

The demo does not try to guess where TF-M keeps any particular secret.
It does not need to: on Armv8-M, the secure/non-secure split is enforced
per address range, not per variable. Any non-secure load or store into an
address flagged Secure faults immediately, regardless of what is stored
there. Address `0x10000000` is the secure alias of flash address `0x0`,
so it is guaranteed to be off-limits to this image.

## What "crash" actually looks like

Expect a fault report on the console instead of the "got 0x..." line,
then a halt or reboot depending on your fault handling config
(`CONFIG_RESET_ON_FATAL_ERROR`). The exact text differs across Zephyr
versions, what matters for the comparison is that build_a completes and
prints the key, build_b never reaches that point.

## Optional extension

If you want to also show the *correct* way to store a secret under
TrustZone, add a call to the PSA Protected Storage API
(`psa_ps_set()` / `psa_ps_get()`) from the non-secure app, which TF-M
exposes by default. That gives you a third comparison: direct memory
access (crashes) vs. going through the proper secure service (works).
Left out of the base demo to keep the build simple.
