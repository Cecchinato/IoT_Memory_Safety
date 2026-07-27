cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ubsan_demo)

target_sources(app PRIVATE src/main.c)

# Which undefined-behavior bug to execute. "all" runs them in sequence.
#
# Selection is compile-time, not an environment variable like the ASan demo's
# BUG_TEST: this app also runs on mps2/an385 under QEMU, where there is no
# environment to read.
set(UB "all" CACHE STRING
    "all | intovf | shift | divzero | align | bounds | badload | vla | fcast")

set(_ub_valid all intovf shift divzero align bounds badload vla fcast)
if(NOT UB IN_LIST _ub_valid)
  message(FATAL_ERROR "Unknown UB: '${UB}' (valid values: ${_ub_valid})")
endif()

string(TOUPPER ${UB} UB_UPPER)
target_compile_definitions(app PRIVATE UB_${UB_UPPER})
message(STATUS "*** UBSan demo: UB=${UB} ***")

# The sanitizer instrumentation itself comes from CONFIG_UBSAN (see
# library.conf / trap.conf), which zephyr/CMakeLists.txt applies globally.
# Nothing to add here except keeping the bugs reachable at runtime:
#
#  -U_FORTIFY_SOURCE : glibc's fortified wrappers would abort before the
#                      sanitizer gets a chance (same reason as Asan_demo).
#  -Wno-array-bounds : GCC spots the out-of-bounds index at compile time and
#                      warns; the point here is the *runtime* trap, so silence
#                      the duplicate compile-time diagnostic.
target_compile_options(app PRIVATE -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 -Wno-array-bounds)

# One exception to "the flags come from Kconfig": -fsanitize=undefined is a
# group, and float-cast-overflow is deliberately NOT a member of it in either
# GCC or Clang (it is too noisy on code that relies on saturating conversions).
# CONFIG_UBSAN therefore never turns it on, and the 'fcast' bug goes unreported.
# Add it on top - only for the app, and only when a sanitizer is actually in
# play, so the baseline profile stays a true no-sanitizer build.
if(CONFIG_UBSAN)
  target_compile_options(app PRIVATE -fsanitize=float-cast-overflow)
endif()
