/*
 * =============================================================
 *      INTENTIONALLY UNDEFINED-BEHAVIOR IOT FIRMWARE
 * =============================================================
 * Companion to Asan_demo. That one collects memory-safety bugs
 * (overflows, use-after-free); this one collects *undefined
 * behavior*: signed overflow, bad shifts, division by zero,
 * misaligned loads, out-of-range enum loads, bad VLA bounds,
 * float->int overflow.
 *
 * None of these corrupt memory, so ASan sees nothing. All of
 * them are cases where the C standard gives the compiler
 * permission to emit whatever it likes - which is exactly how
 * a "harmless" wraparound turns into a deleted bounds check.
 *
 * Which bug runs is chosen at build time: -DUB=<name>, default
 * "all". Not an env var, because this also runs on mps2/an385
 * under QEMU where there is no environment.
 *
 * =============================================================
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#if defined(UB_ALL)
#define UB_INTOVF
#define UB_SHIFT
#define UB_DIVZERO
#define UB_ALIGN
#define UB_BOUNDS
#define UB_BADLOAD
#define UB_VLA
#define UB_FCAST
#endif

/* ---------------------------------------------------------------
 * Peer/sensor-controlled inputs.
 *
 * Every one of these is volatile, given a plain constant, GCC folds
 * the expression at compile time, the undefined operation never
 * reaches the generated code, and the sanitizer has nothing left
 * to instrument. volatile forces the value to be loaded at run
 * time, which is also how it would really arrive from a UART,
 * a radio, or an ADC.
 * --------------------------------------------------------------- */

static volatile int32_t  g_uptime_seconds = 2200000; /* ~25 days */
static volatile uint8_t  g_reg_bit_index  = 35;      /* from a config frame */
static volatile uint8_t  g_sample_count;             /* frame said 0 samples */
static volatile uint8_t  g_header_offset  = 3;       /* odd offset in a packet */
static volatile uint8_t  g_sample_index   = 11;      /* array holds 8 */
static volatile uint8_t  g_wire_msg_type  = 7;       /* enum defines 0..3 */
static volatile int32_t  g_scratch_len;              /* peer said 0 bytes */
static volatile float    g_adc_volts      = 1.0e10f; /* miscalibrated ADC */

#define SENSOR_SAMPLE_MAX 8

static int16_t g_samples[SENSOR_SAMPLE_MAX] = { 10, 20, 30, 40, 50, 60, 70, 80 };

/* Message type as it appears in the protocol spec: four values, and the
 * compiler is entitled to assume a msg_type_t only ever holds one of them. */
typedef enum {
	MSG_PING    = 0,
	MSG_CONFIG  = 1,
	MSG_SENSOR  = 2,
	MSG_OTA     = 3,
} msg_type_t;

/* ---------------------------------------------------------------
 * 1. Uptime accounting
 * UB: signed integer overflow.
 *     seconds * 1000 exceeds INT32_MAX after ~24.8 days. The
 *     result is not "it wraps to negative" - it is undefined,
 *     and GCC is free to assume it never happens, which is how
 *     an "if (ms < 0)" guard elsewhere gets optimized away.
 * --------------------------------------------------------------- */

void report_uptime_ms(void)
{
	int32_t seconds = g_uptime_seconds;

	int32_t ms = seconds * 1000; /* UB: signed integer overflow */

	printf("[UPTIME] %d s -> %d ms\n", seconds, ms);
}

/* ---------------------------------------------------------------
 * 2. Peripheral register mask
 * UB: shift exponent out of range.
 *     The bit index arrives in a config frame and is used to
 *     build a mask without being checked against the width of
 *     the type. Shifting a 32-bit value by >= 32 is undefined;
 *     on Arm it typically yields 0, on x86 the count is taken
 *     mod 32, so the same firmware silently enables a different
 *     register bit depending on the target.
 * --------------------------------------------------------------- */

void build_register_mask(void)
{
	uint8_t bit = g_reg_bit_index;

	uint32_t mask = 1u << bit; /* UB: shift count >= 32 */

	printf("[REG]    bit=%u -> mask=0x%08x\n", bit, mask);
}

/* ---------------------------------------------------------------
 * 3. Sensor frame averaging
 * UB: integer division by zero.
 *     sample_count comes off the wire. A frame reporting zero
 *     samples is perfectly well-formed, and nothing here checks
 *     for it before dividing.
 * --------------------------------------------------------------- */

void average_samples(void)
{
	int32_t sum = 0;

	for (int i = 0; i < SENSOR_SAMPLE_MAX; i++) {
		sum += g_samples[i];
	}

	uint8_t count = g_sample_count;

	int32_t avg = sum / count; /* UB: division by zero */

	printf("[SENSOR] sum=%d count=%u avg=%d\n", sum, count, avg);
}

/* ---------------------------------------------------------------
 * 4. Zero-copy packet field access
 * UB: misaligned pointer dereference.
 *     The classic embedded shortcut: point a uint32_t * straight
 *     into a byte buffer to avoid a memcpy. It works on x86 and
 *     on Cortex-M3+ for ordinary loads, so it survives testing -
 *     right up until the compiler emits an LDM/LDRD, or the code
 *     is reused on a core that faults on unaligned access.
 * --------------------------------------------------------------- */

void read_packet_field(void)
{
	static uint8_t packet[16] = {
		0xCD, 0xAB, 0x01, 0x00,
		0xEF, 0xBE, 0xAD, 0xDE,
		0x11, 0x22, 0x33, 0x44,
		0x55, 0x66, 0x77, 0x88,
	};

	uint8_t off = g_header_offset; /* 3 - not a multiple of 4 */

	/* UB: the cast asserts 4-byte alignment that packet+3 does not have */
	const uint32_t *field = (const uint32_t *)(packet + off);
	uint32_t value = *field;

	printf("[NET]    field at offset %u = 0x%08x\n", off, value);
}

/* ---------------------------------------------------------------
 * 5. Sample lookup
 * UB: array index out of bounds.
 *     The one bug in this file that overlaps with the ASan demo.
 *     Worth keeping precisely for the comparison: ASan catches it
 *     with shadow memory at run time, UBSan catches it from the
 *     statically known array bound - and unlike ASan, UBSan can
 *     do so on the real target.
 * --------------------------------------------------------------- */

void read_sample(void)
{
	uint8_t idx = g_sample_index; /* 11, array holds 8 */

	int16_t sample = g_samples[idx]; /* UB: index out of bounds */

	printf("[SENSOR] sample[%u] = %d\n", idx, sample);
}

/* ---------------------------------------------------------------
 * 6. Protocol header parsing
 * UB: load of a value not valid for its type.
 *     A header is memcpy'd off the wire and its fields are then
 *     read back through their declared types. Neither field was
 *     validated, and neither type can represent what the peer
 *     actually sent.
 *
 *     Two fields, on purpose, because the sanitizer only catches
 *     one of them:
 *
 *       - 'retain' is a bool. A bool object may only hold 0 or 1;
 *         the wire byte is 7. UBSan reports this.
 *       - 'type' is an enum declared with values 0..3, and again
 *         the wire says 7. The C standard makes this just as
 *         undefined, and the compiler may assume the switch below
 *         is exhaustive - but neither GCC's nor Clang's
 *         -fsanitize=enum fires on it in C. See ubsan_doc.md.
 *
 *     A sanitizer that is silent is not the same as code that is
 *     correct, and this is the cheapest place in the demo to see
 *     that.
 * --------------------------------------------------------------- */

/* Layout as it arrives from the peer: both fields carry the byte 7. */
typedef struct {
	msg_type_t type;
	bool       retain;
} msg_header_t;

void parse_message_header(void)
{
	uint8_t wire = g_wire_msg_type; /* 7 */
	uint8_t raw[sizeof(msg_header_t)];
	msg_header_t hdr;

	memset(raw, 0, sizeof(raw));
	raw[0] = wire;                            /* type   */
	raw[offsetof(msg_header_t, retain)] = wire; /* retain */

	memcpy(&hdr, raw, sizeof(hdr));

	/* UB (reported): 'retain' holds 7, which is not 0 or 1 */
	printf("[NET]    retain flag: %s\n", hdr.retain ? "yes" : "no");

	switch (hdr.type) { /* UB (not reported): no msg_type_t can hold 7 */
	case MSG_PING:
		printf("[NET]    dispatch: PING\n");
		break;
	case MSG_CONFIG:
		printf("[NET]    dispatch: CONFIG\n");
		break;
	case MSG_SENSOR:
		printf("[NET]    dispatch: SENSOR\n");
		break;
	case MSG_OTA:
		printf("[NET]    dispatch: OTA\n");
		break;
	default:
		printf("[NET]    dispatch: unknown type %d\n", (int)hdr.type);
		break;
	}
}

/* ---------------------------------------------------------------
 * 7. Variable-length scratch buffer
 * UB: VLA bound not positive.
 *     Sizing a stack VLA from a peer-supplied length. Zero or
 *     negative is undefined, and on a real device a large value
 *     is a stack overflow on top of that - which is why VLAs are
 *     banned outright in most embedded coding standards.
 * --------------------------------------------------------------- */

void alloc_scratch(void)
{
	int32_t len = g_scratch_len; /* 0 */

	uint8_t scratch[len]; /* UB: VLA bound is not positive */

	/* Touch it so the array is not optimized out entirely. */
	memset(scratch, 0xAA, sizeof(scratch));

	printf("[OTA]    scratch buffer of %d bytes at %p\n", len, (void *)scratch);
}

/* ---------------------------------------------------------------
 * 8. ADC reading conversion
 * UB: floating-point cast overflow.
 *     A float that does not fit the destination integer type.
 *     Not "it saturates" and not "it wraps" - the result is
 *     undefined, so a miscalibrated or NaN-producing sensor
 *     yields a different garbage reading per architecture.
 * --------------------------------------------------------------- */

void convert_adc_reading(void)
{
	float volts = g_adc_volts; /* 1e10 */

	int16_t raw = (int16_t)volts; /* UB: value not representable in int16_t */

	printf("[ADC]    %.1f -> raw %d\n", (double)volts, raw);
}

/* ---------------------------------------------------------------
 * Demo harness
 * --------------------------------------------------------------- */

int main(void)
{
	printf("========= IoT Firmware Undefined-Behavior Demo =========\n");

#if defined(UB_INTOVF)
	report_uptime_ms();
#endif
#if defined(UB_SHIFT)
	build_register_mask();
#endif
#if defined(UB_ALIGN)
	read_packet_field();
#endif
#if defined(UB_BOUNDS)
	read_sample();
#endif
#if defined(UB_BADLOAD)
	parse_message_header();
#endif
#if defined(UB_VLA)
	alloc_scratch();
#endif
#if defined(UB_FCAST)
	convert_adc_reading();
#endif
	/* Deliberately last. Integer division by zero is the one bug here that
	 * the hardware itself traps on x86 (SIGFPE), so in the baseline build it
	 * ends the run - which would hide every bug placed after it. */
#if defined(UB_DIVZERO)
	average_samples();
#endif

	printf("\n=== Demo complete. Rebuild with -DUB=<name> to isolate one bug. ===\n");
	printf("=== Names: intovf, shift, divzero, align, bounds, badload, vla, fcast ===\n");
	return 0;
}
