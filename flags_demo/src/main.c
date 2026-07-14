#include <zephyr/kernel.h>
#include <string.h>

/* 
 * copies the *input into a buffer[16] using strcopy function.
 * strcpy is not secure an does not check for input legnth -> OOB read
 * -> return address / canary gets corrupted 
 * 
 * noinline so that the comppiler uses 2 different return and the canary can work as intended
 */
void __attribute__((noinline)) vulnerable_copy(const char *input)
{
    char buf[16];
    strcpy(buf, input);
    printk("Copied: %s\n", buf);
}

static void demo_canary(void)
{
    printk("\n=== DEMO: -fstack-protector-strong ===\n\n");/*if it is not too long, return is not corrupted and without canaries it will not crash -> silent OOB read*/
    const char *payload = "AAAAAAAAAAA_OVERFLOW";
    vulnerable_copy(payload);
    printk("If you read this, canary definitely didn't work.\n");
}

/* With _FORTIFY_SOURCE=2 enabled the compiler knows how big is 'small' (8 byte),
 * strcpy becomes __strcpy_chk and stops the runtime
 * if it gets past the limit
 */
static void demo_fortify(void)
{
    printk("\n=== DEMO: -D_FORTIFY_SOURCE=2 ===\n\n");
    char small[8];
    const char *payload = "This string is definitely longer than 8 bytes";
    strcpy(small, payload);
    printk("If you see this line, FORTIFY did NOT block the overflow: %s\n", small);
}


typedef int  (*op_int_fn)(int, int);
typedef void (*op_str_fn)(const char *);

static int  add(int a, int b) { return a + b; }
static void log_str(const char *s) { printk("[LOG]: %s\n", s); }

/* 
 * Control Flow Integrity (Clang -fsanitize=cfi) 
 * A function pointer of type op_int_fn is called via an op_str_fn type: 
 * a typical "type confusion" attack to divert the flow to 
 * an unexpected gadget/function.
 */
static void demo_cfi(void)
{
    printk("\n=== DEMO: Control Flow Integrity ===\n\n");
    void *fnptr = (void *)add;                  /* op_int_fn */
    op_str_fn wrong_type = (op_str_fn)fnptr;    /* type confusion */

    printk("Calling add() through a function pointer of the wrong type...\n");
    wrong_type("This call should never happen under CFI");
    printk("If you see this line, CFI has NOT blocked type confusion.\n");

    (void)log_str; /* silence 'unused' when the above branch does not terminate */
}


int main(void)
{
#if defined(DEMO_FORTIFY)
    demo_fortify();
#elif defined(DEMO_CFI)
    demo_cfi(); 
#else
    demo_canary();
#endif

    /*uncomment the following 2 lines to see how 
    * -Wall -Wextra -Werror -Wformat-security works
    */

    //const char *payload = "This string is definitely longer than 8 bytes";
    //printf(payload);

    return 0;
}
