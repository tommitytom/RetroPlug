#include "box.h"

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/watchdog.h"
#include "edio.h"

// --- Status LED --------------------------------------------------------------------------------

#define LED_PIN PICO_DEFAULT_LED_PIN   // GP25 on a Pico 2

static led_mode          mode = LED_NO_N8;
static volatile uint32_t activity_us = 0;
static unsigned          count_n = 0;    // acknowledgement blinks still to show
static uint32_t          count_start_ms = 0;

void led_init(void) {
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
}

void led_set_mode(led_mode m) { mode = m; }
void led_activity(void) { activity_us = time_us_32(); }

void led_count(unsigned n) {
    count_n = n;
    count_start_ms = to_ms_since_boot(get_absolute_time());
}

static void led_task(void) {
    uint32_t now = to_ms_since_boot(get_absolute_time());
    bool on;
    if (count_n) {
        // n blinks at 150 ms on / 150 ms off, then 600 ms dark before the mode's pattern returns.
        uint32_t phase = (now - count_start_ms) / 150;
        if (phase < 2 * count_n) on = !(phase & 1);
        else if (phase < 2 * count_n + 4) on = false;
        else { count_n = 0; on = false; }
    } else {
        switch (mode) {
            case LED_NO_N8: on = now % 1000 < 80; break;
            case LED_BUSY:  on = now % 250 < 125; break;
            case LED_READY: on = time_us_32() - activity_us > 40000; break;
            default: {   // LED_ERROR
                uint32_t t = now % 1500;
                on = t < 100 || (t >= 250 && t < 350);
            }
        }
    }
    gpio_put(LED_PIN, on);
}

// --- Watchdog + recovery -------------------------------------------------------------------------

#define BOX_MAGIC        0x4e385042u   // "N8PB" in scratch[0]: the other scratch words are ours
#define SCR_RECOVERIES   1             // consecutive recoveries
#define SCR_REASON       2             // why the last one happened
#define SCR_PENDING_SEL  3             // 0x100 | sel while a MIDI pick waits for the menu
#define WATCHDOG_MS      3000

static uint32_t backoff_until_ms = 0;
static uint32_t healthy_since_ms = 0;

static const char *reason_name(uint32_t r) {
    switch (r) {
        case RECOVER_HANG:        return "the watchdog fired (nothing fed it for 3 s)";
        case RECOVER_NO_ENUM:     return "a device on the socket never enumerated";
        case RECOVER_LINK_DEAD:   return "the N8 stopped answering";
        case RECOVER_WRITES_FAIL: return "writes to the N8 kept failing";
        case RECOVER_TEST:        return "a test asked for it";
        default:                  return "unknown";
    }
}

void box_start(void) {
    // Only a watchdog reset continues the count - a timeout, or box_recover()'s own reboot. Scratch
    // also survives a debugger's reset (a reflash), which is a fresh start, not a recovery.
    if (watchdog_hw->scratch[0] != BOX_MAGIC || !watchdog_caused_reboot()) {
        watchdog_hw->scratch[0] = BOX_MAGIC;
        watchdog_hw->scratch[SCR_RECOVERIES] = 0;
        watchdog_hw->scratch[SCR_REASON] = 0;
        watchdog_hw->scratch[SCR_PENDING_SEL] = 0;
    }
    if (watchdog_enable_caused_reboot()) {         // a timeout, not one of our own reboots
        watchdog_hw->scratch[SCR_RECOVERIES]++;
        watchdog_hw->scratch[SCR_REASON] = RECOVER_HANG;
    }
    uint32_t n = watchdog_hw->scratch[SCR_RECOVERIES];
    if (n) {
        uint32_t backoff_s = n >= 5 ? 60 : 5u << (n - 1);
        backoff_until_ms = to_ms_since_boot(get_absolute_time()) + backoff_s * 1000;
        printf("[box] restarted to recover: %s (#%lu in a row; next recovery allowed in %lu s)\n",
               reason_name(watchdog_hw->scratch[SCR_REASON]), (unsigned long)n,
               (unsigned long)backoff_s);
    }
    watchdog_enable(WATCHDOG_MS, true);   // paused while a debugger halts the core
}

void box_feed(void) {
    watchdog_update();
    led_task();
}

bool box_may_recover(void) {
    return (int32_t)(to_ms_since_boot(get_absolute_time()) - backoff_until_ms) >= 0;
}

void box_recover(int reason) {
    watchdog_hw->scratch[SCR_RECOVERIES]++;
    watchdog_hw->scratch[SCR_REASON] = (uint32_t)reason;
    printf("[box] recovering: %s - rebooting\n", reason_name((uint32_t)reason));
    sleep_ms(20);   // let the line reach the console
    watchdog_reboot(0, 0, 0);
    for (;;) tight_loop_contents();
}

void box_note_healthy(void) {
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (!watchdog_hw->scratch[SCR_RECOVERIES]) return;
    if (!healthy_since_ms) healthy_since_ms = now;
    if (now - healthy_since_ms < 60000) return;
    watchdog_hw->scratch[SCR_RECOVERIES] = 0;
    printf("[box] healthy for 60 s - recovery backoff cleared\n");
}

// --- ROMs staged in the Pico's flash -------------------------------------------------------------

#define ROM_TABLE_ADDR (XIP_BASE + 0x200000)   // 2 MB in: far past the firmware, inside the 4 MB part
#define ROM_TABLE_END  (XIP_BASE + 0x400000)

static bool blob_ok(const rom_blob_hdr *h) {
    if ((uintptr_t)h + sizeof *h > ROM_TABLE_END) return false;
    if (memcmp(h->magic, "N8RM", 4) != 0) return false;           // erased flash reads 0xff
    if (h->size == 0 || h->size > 0x100000) return false;
    if ((uintptr_t)(h + 1) + h->size > ROM_TABLE_END) return false;
    return memchr(h->path, 0, sizeof h->path) != NULL;
}

static const rom_blob_hdr *blob_next(const rom_blob_hdr *h) {
    uintptr_t end = (uintptr_t)(h + 1) + h->size;
    return (const rom_blob_hdr *)((end + 0xfff) & ~(uintptr_t)0xfff);
}

int rom_count(void) {
    int n = 0;
    for (const rom_blob_hdr *h = (const rom_blob_hdr *)ROM_TABLE_ADDR; blob_ok(h); h = blob_next(h)) n++;
    return n;
}

const rom_blob_hdr *rom_get(int i) {
    const rom_blob_hdr *h = (const rom_blob_hdr *)ROM_TABLE_ADDR;
    for (; blob_ok(h); h = blob_next(h))
        if (i-- == 0) return h;
    return NULL;
}

uint32_t rom_crc32(const rom_blob_hdr *h) {
    // Nibble-table CRC-32 (IEEE): small, and ~100 ms for a 288 KB ROM read straight from flash.
    static const uint32_t t[16] = {
        0x00000000, 0x1db71064, 0x3b6e20c8, 0x26d930ac, 0x76dc4190, 0x6b6b51f4, 0x4db26158, 0x5005713c,
        0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c, 0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c,
    };
    const uint8_t *p = (const uint8_t *)(h + 1);
    uint32_t crc = 0xffffffff;
    for (uint32_t i = 0; i < h->size; i++) {
        crc ^= p[i];
        crc = (crc >> 4) ^ t[crc & 15];
        crc = (crc >> 4) ^ t[crc & 15];
        if ((i & 0xffff) == 0) watchdog_update();
    }
    return ~crc;
}

int box_pending_sel(void) {
    uint32_t v = watchdog_hw->scratch[SCR_PENDING_SEL];
    return (v & 0x100) ? (int)(v & 0xff) : -1;
}

void box_set_pending_sel(int sel) {
    watchdog_hw->scratch[SCR_PENDING_SEL] = sel < 0 ? 0 : (0x100u | (uint32_t)(sel & 0xff));
}

// --- The selection saved on the SD card -------------------------------------------------------------

bool box_state_load(box_state *s) {
    if (edio_file_get(BOX_STATE_PATH, (uint8_t *)s, sizeof *s) != 0) return false;   // none yet
    return memcmp(s->magic, "N8PB", 4) == 0;
}

bool box_state_save(const box_state *s) {
    return edio_file_put(BOX_STATE_PATH, (const uint8_t *)s, sizeof *s) == 0;
}
