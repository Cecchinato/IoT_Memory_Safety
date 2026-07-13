/*
 * build_b: TrustZone ENABLED, this is the non-secure image.
 *
 * TF-M boots first as the secure world, sets up the SAU/IDAU, then hands
 * control to this Zephyr image running non-secure. Any secret the secure
 * side owns is simply not addressable from here, the CPU enforces that
 * in hardware before the access ever reaches memory.
 *
 * On the MPS2+ AN521, the same physical flash and RAM are mapped twice:
 * once at the "normal" address for NS, and once again at
 * the same offset + 0x10000000 for the secure world. That second alias
 * is what a non-secure bus transaction is not allowed to touch.
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <stdint.h>
#include <string.h>
#include <psa/protected_storage.h>

#define SECURE_FLASH_ALIAS 0x10000000u
#define KEY_UID 1
const char key[] = "Secret123!";

static void legit_key_storage(void)
{
	char retrieved[32];
	size_t out_len;
	psa_status_t status;

	printk("[GOOD] saving key via PSA Protected Storage...\n");
	status = psa_ps_set(KEY_UID, strlen(key), key, PSA_STORAGE_FLAG_NONE);
	if (status != PSA_SUCCESS) {
		printk("[GOOD] psa_ps_set failed: %d\n", status);
		return;
	}

	printk("[GOOD] reading key via PSA Protected Storage...\n");
	status = psa_ps_get(KEY_UID, 0, sizeof(retrieved) - 1, retrieved, &out_len);
	if (status != PSA_SUCCESS) {
		printk("[GOOD] psa_ps_get failed: %d\n", status);
		return;
	}
	retrieved[out_len] = '\0';

	printk("[GOOD] retrieved key: %s\n", retrieved);
}

static void untrusted_read_attempt(void)
{
	volatile uint32_t *secure_ptr = (volatile uint32_t *)SECURE_FLASH_ALIAS;

	printk("[BAD] attempting to read secure flash at %p\n",
	       (void *)secure_ptr);

	/* This line does not return. The load itself trips the SAU check
	 * and the core takes a SecureFault before the value ever reaches
	 * a register.
	 */
	uint32_t value = *secure_ptr;

	printk("[BAD] got 0x%08x (you should never see this line)\n",
	       value);
}

int main(void)
{
	printk("\n\n=== build_b: Key saved in TrustZone ===\n");

	printk("[DBG] TrustZone is: ");
	#if defined(CONFIG_ARM_TRUSTZONE_M)
    	printk("enabled\n");
	#else
		printk("disabled\n");
	#endif

	legit_key_storage();

	printk("\n=== build_b: Key not saved in TrustZone ===\n");
	printk("[DBG] key lives at %p\n", (void *)key);
	printk("[untrusted code] key = ");

	for (size_t i = 0; i < sizeof(key); i++) {
		printk("%c", key[i]);
	}

	printk("\n[untrusted code] done.\n");
	
	printk("\n=== build_b: Accessing the key saved in the TrustZone ===\n");
	untrusted_read_attempt();
	
	return 0;
}