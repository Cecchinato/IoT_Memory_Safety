# TrustZone vs MPU demo on Zephyr (QEMU only)

Four builds, no physical hardware needed:

| Build | Feature | Board | What it shows |
|---|---|---|---|
| build_a | TrustZone OFF | `mps2/an521/cpu0` | secret key read in plain code, no crash |
| build_tz | TrustZone ON | `mps2/an521/cpu0/ns` | same read attempt, non-secure code faults instantly |
| build_c | MPU OFF | `qemu_cortex_m3` | stack overflow silently corrupts memory |
| build_d | MPU ON (stack guard) | `qemu_cortex_m3` | same overflow, immediate MPU fault |

Each folder is a self-contained Zephyr app. Comments in the code are in English as requested.

## Prerequisites

You need a working Zephyr workspace (SDK + toolchain + QEMU), same as for any
other Zephyr project:

```
pip install west
west init ~/zephyrproject
cd ~/zephyrproject
west update
west zephyr-export
pip install -r zephyr/scripts/requirements.txt
```

Full instructions: https://docs.zephyrproject.org/latest/develop/getting_started/index.html

The TrustZone build (build_tz) also needs the Trusted Firmware-M module. If
`west build` complains that `modules/tee/tf-m` is missing, fetch it once with:

```
west config manifest.group-filter -- +tfm
west update
```

Copy the four project folders below into your workspace (e.g. next to
`zephyr/samples/`) and build from there.

## TrustZone demo

```
cd tz_demo/build_a_no_tz
west build -b mps2/an521/cpu0 -d build_a .
west build -d build_a -t run

cd ../build_tz_tz/
west build -b mps2/an521/cpu0/ns -d build_tz .
west build -d build_tz -t run -- -DQEMU_EXTRA_FLAGS="-no-reboot"
```

Expected result:
- build_a prints the key in the console, nothing stops it.
- build_tz prints one line, then faults the moment it touches the secure
  flash alias. The exact fault message depends on your Zephyr version, but
  the program never reaches the "got it" line, unlike build_a.

See `tz_demo/README.md` for how the addressing trick works.

note: add --pristine at the end for re-building properly

## MPU demo

```
install gperf if not installed
#ubuntu
sudo apt install gperf
#fedora
sudo dnf install gperf
#Arch
bashsudo pacman -S gperf

cd mpu_demo
 west build -b mps2/an385 -d build_protected . -- -DEXTRA_CONF_FILE=protected.conf
 west build -d build_protected -t run 

west build -b mps2/an385 -d build_unprotected . -- -DEXTRA_CONF_FILE=unprotected.conf
west build -d build_unprotected -t run
```

Expected result:
- build_c: no MPU fault. The recursive overflow runs into whatever memory
  is next in RAM. Output gets garbled, it hangs, or it crashes somewhere
  unrelated later on. Behavior is undefined on purpose, that is the point.
- build_d: MPU stack guard fires on the very first write past the stack,
  Zephyr prints a fault report naming the stack overflow and halts.

See `mpu_demo/README.md` for config details.

## SBOM
```
west spdx --init -d build_tz
west build -b mps2/an521/cpu0/ns -d build_tz .
west spdx -d build_tz --analyze-includes --include-sdk
```