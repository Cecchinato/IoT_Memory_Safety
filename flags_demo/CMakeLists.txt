cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(hardening_demo)

target_sources(app PRIVATE src/main.c)

# quale funzione vulnerabile eseguire: canary | fortify | cfi
set(DEMO "canary" CACHE STRING "canary | fortify | cfi")
string(TOUPPER ${DEMO} DEMO_UPPER)
target_compile_definitions(app PRIVATE DEMO_${DEMO_UPPER})

# interruttore hardening on/off
option(HARDENED "Abilita i flag di toolchain hardening" OFF)

# IMPORTANTE: i flag vanno solo sul target "app" (il nostro main.c),
# non su tutto Zephyr con zephyr_compile_options() - altrimenti si
# rompe la compilazione di kernel/lib interni (es. -Werror su codice
# non pensato per essere warning-clean, o CFI passato a gcc).

if(HARDENED)
  message(STATUS "*** BUILD HARDENED (DEMO=${DEMO}) ***")
  target_compile_options(app PRIVATE
    -fstack-protector-strong
    -D_FORTIFY_SOURCE=2
    -O2
    -Wall -Wextra -Werror -Wformat-security
  )

  # CFI: solo per la demo cfi, solo su native_sim, solo con Clang
  # (richiede -DZEPHYR_TOOLCHAIN_VARIANT=llvm in fase di build)
  if(DEMO STREQUAL "cfi")
    if(NOT CONFIG_ARCH_POSIX)
      message(FATAL_ERROR "DEMO=cfi richiede la board native_sim")
    endif()
    if(NOT CMAKE_C_COMPILER_ID STREQUAL "Clang")
      message(FATAL_ERROR
        "DEMO=cfi richiede il toolchain LLVM: aggiungi "
        "-DZEPHYR_TOOLCHAIN_VARIANT=llvm al comando west build")
    endif()
    target_compile_options(app PRIVATE -fsanitize=cfi -flto -fvisibility=hidden)
    target_link_options(app PRIVATE -fsanitize=cfi -flto)
  endif()
else()
  message(STATUS "*** BUILD VULNERABILE (DEMO=${DEMO}) - nessun hardening ***")
  target_compile_options(app PRIVATE -fno-stack-protector -U_FORTIFY_SOURCE -O0)
endif()
