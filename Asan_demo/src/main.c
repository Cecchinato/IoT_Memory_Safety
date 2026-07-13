/*
 * =============================================================
 *        INTENTIONALLY VULNERABLE IOT FIRMWARE
 * =============================================================
 * This file simulates a small RTOS-style IoT device firmware.
 * It contains deliberate memory-safety bugs for 
 * mitigation demonstrations (ASan, ARM MTE, CHERI, etc.).
 *
 * =============================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* ---------------------------------------------------------------
 * Firmware-style constants and data structures
 * --------------------------------------------------------------- */


#define UART_LINE_MAX       18 //should be smthing like 64, for demostration lets say 18
#define CONFIG_KEY_MAX      16
#define CONFIG_VAL_MAX      32
#define CONFIG_MAX_ENTRIES  8
#define SENSOR_FRAME_MAX    32
#define OTA_CHUNK_MAX       128
#define PKT_PAYLOAD_MAX     256

/* Simple key/value config store, as found on many embedded devices */
typedef struct {
    char key[CONFIG_KEY_MAX];
    char value[CONFIG_VAL_MAX];
    uint8_t in_use;
} config_entry_t;

static config_entry_t g_config_store[CONFIG_MAX_ENTRIES];

/* Network packet header, as parsed from a raw byte stream */
typedef struct {
    uint16_t magic;
    uint16_t payload_len;   /* attacker/peer controlled */
    uint8_t  msg_type;
} pkt_header_t;

/* Sensor frame: fixed binary layout coming from a peripheral bus */
typedef struct {
    uint8_t  sensor_id;
    uint8_t  sample_count;
    int16_t  samples[8];    /* fixed capacity */
} sensor_frame_t;

/* Session object representing a connection lifecycle */
typedef struct {
    uint32_t session_id;
    uint8_t  active;
    char    *rx_buffer;      /* heap-allocated per session */
    size_t   rx_buffer_len;
} session_t;

/* OTA metadata describing an incoming firmware chunk */
typedef struct {
    uint32_t chunk_offset;
    uint16_t chunk_len;      /* peer-controlled */
    uint8_t  data[OTA_CHUNK_MAX];
} ota_chunk_t;

/* ---------------------------------------------------------------
 * 1. UART command parsing
 * BUG: unsafe copy of attacker/peer-controlled line into a fixed
 *      stack buffer without bounding by sizeof(dest). Classic
 *      stack buffer overflow via strcpy on unterminated input.
 * --------------------------------------------------------------- */

void uart_process_line(const char *raw_line)
{
    char cmd_buf[UART_LINE_MAX];

    /* Firmware devs often assume UART lines are "always short".
     * strcpy does not enforce that assumption. */
    strcpy(cmd_buf, raw_line);   /* VULN: buffer overflow */

    printf("[UART] processed command: %s\n", cmd_buf);
}

/* ---------------------------------------------------------------
 * 2. Network packet parsing
 * BUG: payload_len from the header is trusted directly when
 *      copying into a fixed-size local buffer -> classic
 *      out-of-bounds write if payload_len > PKT_PAYLOAD_MAX.
 * --------------------------------------------------------------- */

void parse_device_message(const uint8_t *packet, size_t packet_len)
{
    if (packet_len < sizeof(pkt_header_t)) {
        printf("[NET] packet too small for header\n");
        return;
    }

    pkt_header_t hdr;
    memcpy(&hdr, packet, sizeof(pkt_header_t));

    uint8_t payload[PKT_PAYLOAD_MAX];
    const uint8_t *payload_src = packet + sizeof(pkt_header_t);

    /* VULN: hdr.payload_len is peer-controlled and not validated
     * against PKT_PAYLOAD_MAX or against the actual packet_len. */
    memcpy(payload, payload_src, hdr.payload_len);

    printf("[NET] msg_type=%u payload_len=%u\n", hdr.msg_type, hdr.payload_len);
}

/* ---------------------------------------------------------------
 * 3. Configuration update handling
 * BUG: linear search finds a free slot but the incoming value
 *      string is copied with strcpy into a fixed-size field,
 *      and key comparison reads up to CONFIG_KEY_MAX without
 *      checking the source is NUL-terminated -> out-of-bounds
 *      read on key, and overflow on value if too long.
 * --------------------------------------------------------------- */

void apply_config_update(const char *key, const char *new_value)
{
    int free_slot = -1;

    for (int i = 0; i < CONFIG_MAX_ENTRIES; i++) {
        if (!g_config_store[i].in_use) {
            if (free_slot < 0) free_slot = i;
            continue;
        }
        /* VULN: strncmp bound uses struct field size, not the
         * actual length of 'key', so a short, non-terminated
         * 'key' buffer from a caller can read past its end. */
        if (strncmp(g_config_store[i].key, key, CONFIG_KEY_MAX) == 0) {
            strcpy(g_config_store[i].value, new_value); /* VULN: overflow */
            printf("[CFG] updated key=%s\n", key);
            return;
        }
    }

    if (free_slot >= 0) {
        strcpy(g_config_store[free_slot].key, key);       /* VULN: overflow */
        strcpy(g_config_store[free_slot].value, new_value); /* VULN: overflow */
        g_config_store[free_slot].in_use = 1;
        printf("[CFG] created key=%s\n", key);
    } else {
        printf("[CFG] store full, update dropped\n");
    }
}

/* ---------------------------------------------------------------
 * 4. Sensor frame processing
 * BUG: sample_count comes from the wire and is used as the loop
 *      bound into a fixed-capacity samples[] array without being
 *      clamped -> out-of-bounds write past sensor_frame_t.samples.
 * --------------------------------------------------------------- */

void handle_sensor_frame(const uint8_t *raw_frame, size_t raw_len)
{
    if (raw_len < 2) {
        printf("[SENSOR] frame too short\n");
        return;
    }

    sensor_frame_t frame;
    frame.sensor_id = raw_frame[0];
    frame.sample_count = raw_frame[1];

    const uint8_t *sample_src = raw_frame + 2;
    size_t available_bytes = raw_len - 2;

    /* VULN: frame.sample_count (0-255) is used directly as the
     * number of int16 samples to read, but frame.samples[] only
     * holds 8 entries. No clamping against capacity. */
    for (uint8_t i = 0; i < frame.sample_count; i++) {
        size_t off = (size_t)i * sizeof(int16_t);
        if (off + sizeof(int16_t) > available_bytes) {
            break; /* only guards the read side, not the write side */
        }
        int16_t sample;
        memcpy(&sample, sample_src + off, sizeof(int16_t));
        frame.samples[i] = sample; /* VULN: OOB write when i >= 8 */
    }

    printf("[SENSOR] id=%u samples=%u processed\n",
           frame.sensor_id, frame.sample_count);
}

/* ---------------------------------------------------------------
 * 5. Session lifecycle management
 * BUG A (use-after-free): session_close() frees rx_buffer but a
 *   later "async-style" callback (session_on_data_callback) still
 *   uses the stale pointer, mimicking a deferred/queued event
 *   firing after teardown.
 * BUG B (double free): session_close() can be invoked twice on
 *   the same session object (e.g. error path + normal teardown)
 *   without checking whether it was already freed.
 * --------------------------------------------------------------- */

session_t *session_open(uint32_t id, size_t buf_len)
{
    session_t *s = (session_t *)malloc(sizeof(session_t));
    if (!s) return NULL;

    s->session_id = id;
    s->active = 1;
    s->rx_buffer = (char *)malloc(buf_len);
    s->rx_buffer_len = buf_len;
    return s;
}

void session_close(session_t *s)
{
    if (!s) return;

    free(s->rx_buffer);
    /* VULN: pointer not set to NULL, session not marked inactive
     * before the free -> enables later use-after-free / double-free */
    free(s);
    /* VULN: caller-held handle is now dangling; nothing here
     * prevents a second session_close(s) call on the same handle */
}

/* Simulates a queued/async event firing after the session's
 * teardown request was issued but processed later (e.g. from a
 * retained callback context or pending task-queue entry). */
void session_on_data_callback(session_t *s, const char *incoming)
{
    /* VULN: no liveness check; if the session was already closed,
     * s and s->rx_buffer are dangling (use-after-free). */
    strncpy(s->rx_buffer, incoming, s->rx_buffer_len - 1);
    s->rx_buffer[s->rx_buffer_len - 1] = '\0';
    printf("[SESSION %u] rx: %s\n", s->session_id, s->rx_buffer);
}

/* ---------------------------------------------------------------
 * 6. OTA metadata / chunk processing
 * BUG: chunk_len is peer-controlled and used directly as the copy
 *      length into a fixed-size 'data' buffer -> out-of-bounds
 *      write. Also demonstrates missing length validation before
 *      trusting metadata for a firmware update path.
 * --------------------------------------------------------------- */

void process_ota_metadata(const uint8_t *meta, size_t meta_len,
                           const uint8_t *chunk_data, size_t chunk_data_len)
{
    if (meta_len < sizeof(uint32_t) + sizeof(uint16_t)) {
        printf("[OTA] metadata too short\n");
        return;
    }

    ota_chunk_t chunk;
    memcpy(&chunk.chunk_offset, meta, sizeof(uint32_t));
    memcpy(&chunk.chunk_len, meta + sizeof(uint32_t), sizeof(uint16_t));

    /* VULN: chunk.chunk_len (up to 65535) is trusted and copied
     * into chunk.data[OTA_CHUNK_MAX] without a bounds check. */
    memcpy(chunk.data, chunk_data, chunk.chunk_len);

    printf("[OTA] offset=%u len=%u applied\n",
           chunk.chunk_offset, chunk.chunk_len);
    (void)chunk_data_len; /* realistically should be validated too */
}

/* ---------------------------------------------------------------
 * Demo harness
 * --------------------------------------------------------------- */

int main(void)
{
    /* Runtime bug selection: set the BUG_TEST env var to pick a
     * single bug to trigger, without recompiling.
     * Values: uart, net, cfg_read, cfg_write, sensor, uaf, dfree, ota
     * If unset (or "none"), only the safe paths below are run. */
    const char *bug_test = getenv("BUG_TEST");
    if (!bug_test) bug_test = "none";

    printf("========= IoT Firmware Memory-Safety Demo =========\n");
    printf("BUG_TEST=%s\n\n", bug_test);

    /* 1. UART overflow demo (kept within safe bounds by default;
     *    grow the string to trigger the overflow under a sanitizer) */
    if (strcmp(bug_test, "uart") == 0) {
        uart_process_line("SET_MODE=ACTIVEeeeeeeeeeee");
    } else {
        uart_process_line("SET_MODE=ACTIVE");
    }

    // pkt_header_t hdr = { .magic = 0xABCD, .payload_len = 400, .msg_type = 1 };
    /* 2. Network packet parsing demo */
    {
        uint8_t pkt[16] = {0};
        /* set payload_len=400 to trigger the OOB write, otherwise use
         * the safe value of 4 */
        uint16_t payload_len = (strcmp(bug_test, "net") == 0) ? 400 : 4;
        pkt_header_t hdr = { .magic = 0xABCD, .payload_len = payload_len, .msg_type = 1 };
        memcpy(pkt, &hdr, sizeof(hdr));
        parse_device_message(pkt, sizeof(pkt));
    }

    /* 3. Config update (trigger OOB read + overflow value)
    uncomment the section for it to "not work"
    */
    if (strcmp(bug_test, "cfg_read") == 0) {
        /* key NOT terminated with '\0' (trigger OOB read via strncmp) */
        char bad_key[4] = { 'W','I','F','I' }; //less then 16 and without \0
        apply_config_update(bad_key, "MyHomeNetwork");
    } else if (strcmp(bug_test, "cfg_write") == 0) {
        /* new_value too long (trigger OOB write with strcpy) */
        char bad_value[CONFIG_VAL_MAX + 32]; // 32 byte over the limit
        memset(bad_value, 'B', sizeof(bad_value));
        bad_value[sizeof(bad_value) - 1] = '\0'; // \0 so that strcopy reat untill the end
        apply_config_update("wifi_ssid", bad_value);
    } else {
        apply_config_update("wifi_ssid", "MyHomeNetwork"); //ok values
    }

    /* 4. Sensor frame demo (sample_count kept small here) */
    if (strcmp(bug_test, "sensor") == 0) {
        uint8_t sensor_raw[2 + 16 * sizeof(int16_t)] = {0};
        sensor_raw[0] = 0x01; /* sensor_id */
        sensor_raw[1] = 12;   /* sample_count > 8 => OOB write */
        handle_sensor_frame(sensor_raw, sizeof(sensor_raw));
    } else {
        uint8_t sensor_raw[2 + 4 * sizeof(int16_t)] = {0};
        sensor_raw[0] = 0x01; /* sensor_id */
        sensor_raw[1] = 0x04; /* sample_count = 4, within capacity */
        handle_sensor_frame(sensor_raw, sizeof(sensor_raw));
    }

    /* 5. Session lifecycle demo */
    if (strcmp(bug_test, "uaf") == 0) {
        session_t *s = session_open(42, 64);
        session_on_data_callback(s, "hello device");
        session_close(s);
        /* Demonstrate use-after-free under ASan: */
        session_on_data_callback(s, "late callback");
    } else if (strcmp(bug_test, "dfree") == 0) {
        session_t *s = session_open(42, 64);
        session_on_data_callback(s, "hello device");
        session_close(s);
        /* Demonstrate double-free under ASan: */
        session_close(s);
    } else {
        session_t *s = session_open(42, 64);
        session_on_data_callback(s, "hello device");
        session_close(s);
    }

    /* 6. OTA metadata demo */
    {
        uint8_t ota_meta[6];
        uint32_t offset = 0;
        /* set len=200 to exceed OTA_CHUNK_MAX (128) and trigger the
         * OOB write, otherwise use the safe value of 8 */
        uint16_t len = (strcmp(bug_test, "ota") == 0) ? 200 : 8;
        memcpy(ota_meta, &offset, sizeof(offset));
        memcpy(ota_meta + sizeof(offset), &len, sizeof(len));

        uint8_t ota_data[200];
        if (strcmp(bug_test, "ota") == 0) {
            memset(ota_data, 0x41, sizeof(ota_data));
        } else {
            memset(ota_data, 0, sizeof(ota_data));
            uint8_t safe_data[8] = {1,2,3,4,5,6,7,8};
            memcpy(ota_data, safe_data, sizeof(safe_data));
        }
        process_ota_metadata(ota_meta, sizeof(ota_meta), ota_data, len);
    }

    printf("\n=== Demo complete. Set BUG_TEST env var to trigger each bug. ===\n");
    printf("=== Values: uart, net, cfg_read, cfg_write, sensor, uaf, dfree, ota ===\n");
    return 0;
}
