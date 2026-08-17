#include "edio.h"
#include "pico/stdlib.h"
#include "tusb.h"
#include <string.h>

#define EDIO_TIMEOUT_MS 2000

static uint8_t g_idx = 0;

void edio_bind(uint8_t cdc_idx) { g_idx = cdc_idx; }

static inline uint32_t now_ms(void) { return to_ms_since_boot(get_absolute_time()); }

// Read exactly `len` bytes with a caller-chosen timeout (the menu's ROM-install reply can
// take many seconds while it loads the mapper core off SD). Pumps tuh_task while waiting.
static bool edio_read_to(uint8_t *buf, uint32_t len, uint32_t timeout_ms) {
    uint32_t got = 0, start = now_ms();
    while (got < len) {
        tuh_task();
        uint32_t n = tuh_cdc_read(g_idx, buf + got, len - got);
        if (n) { got += n; start = now_ms(); }
        else if (now_ms() - start > timeout_ms) return false;
    }
    return true;
}

bool edio_write(const uint8_t *data, uint32_t len) {
    uint32_t avail_before = tuh_cdc_write_available(g_idx);   // TX FIFO free space when idle
    uint32_t sent = 0, start = now_ms();
    while (sent < len) {
        tuh_task();
        uint32_t n = tuh_cdc_write(g_idx, data + sent, len - sent);
        if (n) {
            sent += n;
            tuh_cdc_write_flush(g_idx);
            start = now_ms();
        } else if (now_ms() - start > EDIO_TIMEOUT_MS) {
            return false;
        }
    }
    // Small writes (< max packet) don't auto-flush and the explicit flush is a no-op if
    // the endpoint is busy, so the bytes can linger in the TX FIFO. Pump + re-flush until
    // the TX FIFO fully drains (free space back to avail_before) = the transfer completed.
    start = now_ms();
    while (now_ms() - start < 500) {
        tuh_task();
        tuh_cdc_write_flush(g_idx);
        if (tuh_cdc_write_available(g_idx) >= avail_before) return true;   // TX drained = transfer done
    }
    return true;   // best-effort: drain timed out but data is queued
}

bool edio_read(uint8_t *buf, uint32_t len) {
    uint32_t got = 0, start = now_ms();
    while (got < len) {
        tuh_task();
        uint32_t n = tuh_cdc_read(g_idx, buf + got, len - got);
        if (n) {
            got += n;
            start = now_ms();
        } else if (now_ms() - start > EDIO_TIMEOUT_MS) {
            return false;
        }
    }
    return true;
}

static bool tx_cmd(uint8_t cmd) {
    const uint8_t f[4] = { 0x2b, 0xd4, cmd, (uint8_t)(cmd ^ 0xff) };
    return edio_write(f, sizeof f);
}

static void put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

int edio_get_status(void) {
    if (!tx_cmd(EDIO_CMD_STATUS)) return -1;
    uint8_t r[2];
    if (!edio_read(r, 2)) return -1;
    if (r[1] != 0xa5) return -1;   // reply is `<code> A5`
    return r[0];
}

bool edio_sys_info(uint8_t out[64]) {
    if (!tx_cmd(EDIO_CMD_SYS_INF)) return false;
    return edio_read(out, 64);
}

// memRD: CMD + addr(tx32) + len(tx32) + exec(0), then read `len` bytes.
bool edio_mem_rd(uint32_t addr, uint8_t *buf, uint32_t len) {
    if (!tx_cmd(EDIO_CMD_MEM_RD)) return false;
    uint8_t h[9];
    put32(h, addr);
    put32(h + 4, len);
    h[8] = 0;
    if (!edio_write(h, sizeof h)) return false;
    return edio_read(buf, len);
}

// memWR: CMD + addr(tx32) + len(tx32) + exec(0) + data. Fire-and-forget (no status).
// The frame (cmd + 9-byte header + payload) MUST go out as ONE contiguous CDC write.
// The host's kernel CDC driver coalesces its separate tx calls into one bulk transfer,
// but the Pico's edio_write flushes each part on its own - and the cart FIFO (a streaming
// register, unlike RAM) mis-handles a write split across USB transfers with gaps: memWR to
// RAM (SRM) still round-trips, but a split FIFO write is silently dropped by the running
// NES code (menu *t/*v got zero reply until this was coalesced). So coalesce here.
bool edio_mem_wr(uint32_t addr, const uint8_t *data, uint32_t len) {
    if (len == 0) return true;
    // Match the host's exact write chunking (observed via strace of a working FIFO memWR):
    // FIVE separate transfers - cmd, addr, len, exec, data - with exec in its own 1-byte
    // write. edio_write drains each to completion, so each phase is its own USB transfer.
    // The cart FIFO needs this staging (a coalesced or cmd+9-header write is silently
    // dropped by the FIFO, though it works for RAM). See pico-n8-fifo-write-bug.md.
    uint8_t a[4], l[4], e = 0;
    put32(a, addr);
    put32(l, len);
    if (!tx_cmd(EDIO_CMD_MEM_WR)) return false;   // 2b d4 1a e5
    if (!edio_write(a, 4)) return false;          // addr (LE)
    if (!edio_write(l, 4)) return false;          // len  (LE)
    if (!edio_write(&e, 1)) return false;         // exec
    return edio_write(data, len);                 // payload
}

bool edio_fifo_wr(const uint8_t *data, uint32_t len) {
    return edio_mem_wr(EDIO_ADDR_FIFO, data, len);
}

// --- N8 menu command channel (over the FIFO; replies come back over the CDC) ---

// A menu command is the two bytes '*' + <c> written to the FIFO.
static bool menu_cmd(char c) {
    const uint8_t m[2] = { 0x2a, (uint8_t)c };
    return edio_fifo_wr(m, sizeof m);
}

// A length-prefixed string to the FIFO: 2-byte LE length, then the bytes.
static bool fifo_tx_string(const char *s) {
    uint16_t n = (uint16_t)strlen(s);
    const uint8_t lp[2] = { (uint8_t)(n & 0xff), (uint8_t)(n >> 8) };
    if (!edio_fifo_wr(lp, 2)) return false;
    return edio_fifo_wr((const uint8_t *)s, n);
}

// Drain any pending CDC RX so a stale byte can't be mistaken for a command reply
// (the host driver flushInput()s before menu commands too).
static void edio_flush_input(void) {
    uint8_t tmp[64];
    uint32_t idle = now_ms();
    while (now_ms() - idle < 20) {          // 20ms with nothing = drained
        tuh_task();
        if (tuh_cdc_read(g_idx, tmp, sizeof tmp)) idle = now_ms();
    }
}

bool edio_menu_test(void) {
    edio_flush_input();
    if (!menu_cmd('t')) return false;
    uint8_t r = 0;
    if (!edio_read_to(&r, 1, EDIO_TIMEOUT_MS)) return false;
    return r == 0x6b;   // 'k'
}

int edio_menu_install(const char *path) {
    if (!menu_cmd('n')) return -1;
    if (!fifo_tx_string(path)) return -1;
    uint8_t status = 0xff;
    if (!edio_read_to(&status, 1, 12000)) return -1;   // menu loads ROM + mapper core off SD
    if (status != 0) return status;                    // e.g. 0x44 = out-of-memory (dirty menu heap)
    uint8_t idx[2];
    edio_read_to(idx, 2, EDIO_TIMEOUT_MS);             // 16-bit map index (unused)
    return 0;
}

void edio_menu_start(void) {
    menu_cmd('s');   // fire-and-forget: the menu core drops out and the game runs
}
