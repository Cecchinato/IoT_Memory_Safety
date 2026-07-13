/*
 * MPU demo: stack overflow protection and userspace memory protection.
 *
 * Board: mps2/an385 (Cortex-M3, QEMU-emulated)
 *
 * main() runs both tests in sequence:
 *   1. test_userspace_thread() - an unprivileged thread tries to write
 *      to memory outside its assigned partition.
 *   2. test_stack_overflow()   - a recursive function exhausts its stack.
 *
 * The same source is built twice, once per protection profile
 * (see protected.conf / unprotected.conf), so the code never needs
 * to be edited to compare behavior:
 *
 *   PROTECTED   -> immediate, clean MPU fault reports.
 *   UNPROTECTED -> silent corruption / late or missing fault,
 *                  much harder to diagnose.
 *
 * Note: under the PROTECTED profile, if the thread test faults the
 * board halts right there, so the overflow test in that same boot
 * is never reached. That immediate stop is itself the point being
 * demonstrated.
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/app_memory/app_memdomain.h>
#include <string.h>
#include <zephyr/sys/libc-hooks.h>  /* for z_libc_partition */

static void print_config_status(void)
{
	printk("=== MPU demo: config status ===\n");
	printk("CONFIG_ARM_MPU              : %s\n",
	       IS_ENABLED(CONFIG_ARM_MPU) ? "enabled" : "disabled");
	printk("CONFIG_HW_STACK_PROTECTION:   %s\n",
	       IS_ENABLED(CONFIG_HW_STACK_PROTECTION) ? "enabled" : "disabled");
	printk("CONFIG_USERSPACE            : %s\n\n",
	       IS_ENABLED(CONFIG_USERSPACE) ? "enabled" : "disabled");
}

/* ---------- Test 1: userspace thread memory violation ----------
 *
 * struct k_mem_domain, K_APPMEM_PARTITION_DEFINE, K_APP_DMEM and the
 * K_USER thread option only exist when CONFIG_USERSPACE=y 
 * The type and macros are not even declared otherwise. 
 * The two profiles need two different (but equivalent) implementations.
 */

#ifdef CONFIG_USERSPACE

/* Memory partition that thread0 is explicitly allowed to access. */
K_APPMEM_PARTITION_DEFINE(part0);
K_APP_DMEM(part0) int guarded_var = 42;

static struct k_mem_domain dom0;
K_THREAD_STACK_DEFINE(thread0_stack, 2048);
static struct k_thread thread0_data;

static void thread0_entry(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	/* Legal access: this variable is inside our own partition. */
	printk("thread0: guarded_var = %d (legal access)\n", guarded_var);
	guarded_var++;

	/* Illegal access: an address never granted to this thread's
	 * memory domain. The MPU rejects this write immediately. */
	printk("thread0: attempting out-of-partition write...\n");
	volatile int *rogue = (int *)0x20001000;
	*rogue = 99;

	printk("thread0: write succeeded - no protection was in effect\n");
}

static void test_userspace_thread(void)
{
	printk("--- Test 1: userspace thread memory violation ---\n");

	struct k_mem_partition *parts[] = {
        &part0,
		/* thread0 is unprivileged: it needs read/write access to the
         * libc's internal globals (e.g. TLS pointer, errno, stack
         * canary) or any call that touches them (even printk) faults.
         * Not all libc configs define this partition, hence the #if. */
		#if Z_LIBC_PARTITION_EXISTS
			&z_libc_partition,
		#endif
		};

	k_mem_domain_init(&dom0, ARRAY_SIZE(parts), parts);

	k_thread_create(&thread0_data, thread0_stack,
        K_THREAD_STACK_SIZEOF(thread0_stack),
        thread0_entry, NULL, NULL, NULL,
        1, K_USER, K_FOREVER);

	k_mem_domain_add_thread(&dom0, &thread0_data);

	k_thread_start(&thread0_data);

	/* Give the thread time to run (and possibly fault) before
	 * moving on to the next test. */
	k_sleep(K_MSEC(200));
	printk("\n");
}

#else /* !CONFIG_USERSPACE */

static int guarded_var = 42;

static void test_userspace_thread(void)
{
	printk("--- Test 1: userspace thread memory violation ---\n\n");

	printk("guarded_var = %d (legal access)\n", guarded_var);
	guarded_var++;

	printk("attempting out-of-partition write...\n");
	volatile int *rogue = (int *)0x20001000;
	*rogue = 99;

	printk("write succeeded - no protection was in effect\n\n");
}

#endif /* CONFIG_USERSPACE */

/* ---------- Test 2: stack overflow ---------- */

/* Each call consumes this buffer plus normal call-frame overhead,
 * so the stack fills up after only a handful of recursive calls. */
#define CHUNK_SIZE 1024

static void recursive_overflow(int depth)
{
	volatile char buf[CHUNK_SIZE];

	memset((void *)buf, depth & 0xFF, CHUNK_SIZE);
	printk("recursion depth: %d (stack pointer moving down...)\n", depth);

	/* Keep recursing until the MPU guard region (or, if disabled,
	 * whatever memory happens to be adjacent) is reached. */
	recursive_overflow(depth + 1);
}

static void test_stack_overflow(void)
{
	printk("--- Test 2: stack overflow ---\n");

	recursive_overflow(0);

	/* Unreachable: recursion is expected to fault (protected case)
	 * or corrupt memory before ever returning here. */
	printk("recursion returned normally - this should not happen\n");
}



int main(void)
{
	print_config_status();

	test_userspace_thread();
	//test_stack_overflow();

	return 0;
}