cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(iot_asan_demo)
target_sources(app PRIVATE src/main.c)

# Disable glibc _FORTIFY_SOURCE so overflows abort via ASan, not __strcpy_chk
target_compile_options(app PRIVATE -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0)

