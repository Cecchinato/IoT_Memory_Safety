cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(flags_demo)

target_sources(app PRIVATE src/main.c)

# Which vulnerable function to execute: canary | fortify | cfi | warnings
set(DEMO "canary" CACHE STRING "canary | fortify | cfi | warnings")

if(DEMO STREQUAL "warnings")
  # The "warnings" demo reuses the canary demo code (same vulnerable
  # printk format-string bug) - see main.c
  target_compile_definitions(app PRIVATE DEMO_CANARY SHOW_FORMAT_BUG)
else()
  string(TOUPPER ${DEMO} DEMO_UPPER)
  target_compile_definitions(app PRIVATE DEMO_${DEMO_UPPER})
endif()

# switch to enable/disable hardening
option(HARDENED "Enable the hardening flag specific to the chosen demo" OFF)

# IMPORTANT: flags should be applied ONLY to the target "app" (our main.c),
# not to all of Zephyr via zephyr_compile_options().
#
# Each DEMO isolates ONLY ONE mechanism. It's not enough to "not add"
# the flags from the other three: many toolchains (e.g., Ubuntu's gcc) have
# -fstack-protector and/or -D_FORTIFY_SOURCE=2 enabled by default
# via the system spec files. So for each DEMO we explicitly disable the
# other mechanisms, so the only difference between the vuln build and the
# hard build is truly the flag under test.

# baseline: everything explicitly disabled
set(_disable_all
  -fno-stack-protector
  -U_FORTIFY_SOURCE
)
target_compile_options(app PRIVATE ${_disable_all})

if(DEMO STREQUAL "canary")

  if(HARDENED)
    message(STATUS "*** HARDENED: -fstack-protector-strong (DEMO=canary) ***")
    target_compile_options(app PRIVATE -fstack-protector-strong)
  else()
    message(STATUS "*** VULNERABLE: canary explicitly OFF (DEMO=canary) ***")
    # -fno-stack-protector is already applied by the baseline above
  endif()

elseif(DEMO STREQUAL "fortify")

  # FORTIFY_SOURCE requires at least -O1 to generate the _chk variants,
  # so -O2 is necessary in BOTH builds (hardened and vuln)
  # only to make the comparison valid: the difference remains the
  # _FORTIFY_SOURCE macro, not the optimization level.
  target_compile_options(app PRIVATE -O2)

  if(HARDENED)
    message(STATUS "*** HARDENED: -D_FORTIFY_SOURCE=2 (DEMO=fortify) ***")
    target_compile_options(app PRIVATE -D_FORTIFY_SOURCE=2)
  else()
    message(STATUS "*** VULNERABLE: FORTIFY explicitly OFF (DEMO=fortify) ***")
    # -U_FORTIFY_SOURCE is already applied by the baseline above
  endif()

elseif(DEMO STREQUAL "cfi")

  if(NOT CONFIG_ARCH_POSIX)
    message(FATAL_ERROR "DEMO=cfi requires the native_sim native_sim board")
  endif()

  if(HARDENED)
    if(NOT CMAKE_C_COMPILER_ID STREQUAL "Clang")
      message(FATAL_ERROR
        "DEMO=cfi with HARDENED=ON requires the LLVM toolchain: add "
        "-DZEPHYR_TOOLCHAIN_VARIANT=llvm to the west build command")
    endif()
    message(STATUS "*** HARDENED: -fsanitize=cfi (DEMO=cfi) ***")
    target_compile_options(app PRIVATE -fsanitize=cfi -flto -fvisibility=hidden)
    target_link_options(app PRIVATE -fsanitize=cfi -flto)
  else()
    message(STATUS "*** VULNERABLE: no CFI (DEMO=cfi) ***")
    # no -fsanitize flag to disable: without -fsanitize=cfi there is
    # nothing to explicitly turn off, and it is simply absent
  endif()

elseif(DEMO STREQUAL "warnings")

  # Compile-time demo, not runtime: with HARDENED=ON the build must
  # fail due to the non-literal printk in vulnerable_copy().
  if(HARDENED)
    message(STATUS "*** HARDENED: -Wall -Wextra -Werror -Wformat-security (DEMO=warnings) ***")
    target_compile_options(app PRIVATE -Wall -Wextra -Werror -Wformat-security -Wno-unused-function)
  else()
    message(STATUS "*** VULNERABLE: warning-as-error explicitly OFF (DEMO=warnings) ***")
    target_compile_options(app PRIVATE -Wno-error -Wno-format-security)
  endif()

else()
  message(FATAL_ERROR "Unknown DEMO: '${DEMO}' (valid values: canary, fortify, cfi, warnings)")
endif()
