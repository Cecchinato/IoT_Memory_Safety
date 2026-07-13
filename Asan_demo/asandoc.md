# Memory-safety testing with ASan on Zephyr

Tutorial for testing memory bugs (stack/heap overflow, use-after-free, double-free) in Zephyr firmware using AddressSanitizer, without real hardware.

## What and why

**AddressSanitizer (ASan)** is a GCC/Clang instrumentation tool that detects at runtime: buffer overflows (stack/heap), use-after-free, double-free, memory leaks. It adds redzones around allocations and intercepts memory operations, slowing down execution but producing precise stack traces at the moment the bug fires.

**Problem**: ASan requires the host compiler's runtime (glibc/libgcc), which a real embedded board (Cortex-M, RISC-V, etc.) doesn't have. You can't run ASan on firmware compiled for the final target.

**Solution**: `native_sim`, a special Zephyr board that compiles the application as a **native x86_64/x86 Linux executable process**, not as embedded firmware. The Zephyr kernel and your code run inside an ordinary ELF binary, so GCC/Clang can instrument it with ASan just like any Linux program.

The correct workflow is therefore:
1. Develop the application firmware (pure logic, not driver-specific) so it's portable.
2. Test it on `native_sim` with ASan to find memory bugs.
3. Build it for the real board (no ASan, production build).

This only applies to **application logic** (parsing, buffers, session handling, etc.). If the firmware depends on hardware-specific drivers (real I2C, real GPIO), it needs to be abstracted behind an interface or tested with Zephyr's emulated drivers (`CONFIG_I2C_EMUL`, `CONFIG_GPIO_EMUL`).



## Project structure

```
my_app/
├── CMakeLists.txt
├── prj.conf
└── src/
    └── main.c
```

**`main.c`** - the vulnerable demo firmware. Contains 6 bug categories (UART overflow, packet parsing overflow, config store overflow/OOB-read, sensor frame OOB-write, session use-after-free/double-free, OTA chunk overflow), selectable at runtime via the `BUG_TEST` environment variable (see dedicated section below), without needing to rebuild between tests.

**`CMakeLists.txt`**
```cmake
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(iot_asan_demo)
target_sources(app PRIVATE src/main.c)
# Disable glibc _FORTIFY_SOURCE so overflows abort via ASan, not __*_chk wrappers
target_compile_options(app PRIVATE -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0)
```

The `_FORTIFY_SOURCE` line is needed because Ubuntu/glibc enables this protection by default even at `-Os`. Without disabling it, `strcpy`/`memcpy` get replaced by glibc wrappers (`__strcpy_chk`, etc.) that abort the process with `*** buffer overflow detected ***: terminated` **before** ASan can intercept the error and produce a detailed stack trace. Disabling it lets the overflow actually reach ASan.

**`prj.conf`**
```
CONFIG_ASAN=y
CONFIG_HEAP_MEM_POOL_SIZE=16384
```

`CONFIG_ASAN=y` enables the instrumentation. `CONFIG_NATIVE_APPLICATION` **does not exist** in Zephyr 4.4+ (removed), remove it if present in older configs.


## Build

From inside your west workspace (`~/zephyrproject` or similar):

```bash
source zephyr/zephyr-env.sh   # if not already done in this shell
west build -p always -b native_sim my_app -d build_asan
```

- `-p always` forces a pristine build (useful after changes to `prj.conf`/`CMakeLists.txt`).
- `-d build_asan` puts the output in a dedicated directory, so you can keep an ASan build separate from a production build for the real board.

### Note: native_sim by defoult use 32-bit libraries

Therefore you can either:

Install the multilib libraries:
```bash
sudo apt update
sudo apt install gcc-multilib g++-multilib libc6-dev-i386
```

Or use the 64-bit variant of the board (recommended):
```bash
west build -p always -b native_sim/native/64 my_app -d build_asan
```


## Run

You can run the progect via west:
```bash
BUG_TEST=<TEST> west build -t run -d build_asan
```



## Selecting which bug to test - `BUG_TEST`

`main()` reads the `BUG_TEST` environment variable to decide which bug to trigger, so you can test them one at a time without touching the source:

```bash
BUG_TEST=uart      west build -t run -d build_asan   # stack buffer overflow (strcpy)
BUG_TEST=net       west build -t run -d build_asan   # OOB write, unvalidated payload_len
BUG_TEST=cfg_read  west build -t run -d build_asan   # OOB read, non-NUL-terminated key
BUG_TEST=cfg_write west build -t run -d build_asan   # write overflow, value too long
BUG_TEST=sensor    west build -t run -d build_asan   # OOB write, sample_count > capacity
BUG_TEST=uaf       west build -t run -d build_asan   # use-after-free
BUG_TEST=dfree     west build -t run -d build_asan   # double-free
BUG_TEST=ota       west build -t run -d build_asan   # OOB write, chunk_len > OTA_CHUNK_MAX
```

Without `BUG_TEST` (or with `BUG_TEST=none`) the program only runs the valid paths - useful as a baseline to verify ASan doesn't produce false positives on "healthy" code.



## Reading the output

A real bug caught by ASan produces a block like this:

```
==12345==ERROR: AddressSanitizer: stack-buffer-overflow on address 0x...
WRITE of size 1 at 0x... thread T0
    #0 0x... in strcpy
    #1 0x... in uart_process_line src/main.c:XX
    #2 0x... in main src/main.c:YY
```

Key information:
- **Error type** (`stack-buffer-overflow`, `heap-buffer-overflow`, `heap-use-after-free`, `attempting double-free`, etc.)
- **READ/WRITE** and size of the out-of-bounds access
- **Stack trace** with exact file and line - trace directly back to the vulnerable function/line
- For UAF/double-free, ASan also shows the stack trace of the original `alloc`/`free`, not just the second access

The process exits with a non-zero exit code whenever ASan detects an error.

---

