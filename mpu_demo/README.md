# MPU demo details

Board: `qemu_cortex_m3`, on purpose different from the TrustZone board.
The MPU is a separate mechanism from TrustZone (plain Cortex-M3 has an
MPU but no TrustZone at all), keeping it on its own board avoids mixing
the two stories together.

## The bug

`recurse()` allocates a 256-byte local buffer and calls itself again.
With a 1 KB main stack, somewhere around the 4th-5th call the stack
pointer runs past the memory Zephyr reserved for it.

## build_c - MPU off

Nothing catches the overflow. What happens next is undefined by
definition: it might silently overwrite the next global variable or the
idle thread's stack, garble the console output, hang, or crash later with
a fault that has nothing obviously to do with recursion. That
unpredictability is the actual lesson here, a bug like this can go
unnoticed for a long time on unprotected hardware.

## build_d - MPU on

`CONFIG_HW_STACK_PROTECTION=y` makes Zephyr program an MPU region right
past the end of the stack with no access permissions. The first write
into `chunk[]` that lands in that region takes a MemManage fault before
any corruption happens. Zephyr's fault handler reports it (something
like `***** MPU FAULT *****` / stacking error, exact wording depends on
your Zephyr version) and halts or reboots per
`CONFIG_RESET_ON_FATAL_ERROR`.

## What to look at in the output

Compare how far `depth` gets and what the last printed line looks like:
build_c either keeps going in a way that stops making sense, or dies with
a confusing fault far from the real cause. build_d dies immediately, at
a predictable depth, with a message that correctly names the problem.
