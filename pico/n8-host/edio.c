#include "edio.h"
#include "pico/stdlib.h"
#include "tusb.h"

#define EDIO_TIMEOUT_MS 2000

static uint8_t g_idx = 0;

void edio_bind(uint8_t cdc_idx) { g_idx = cdc_idx; }

static inline uint32_t now_ms(void) { return to_ms_since_boot(get_absolute_time()); }

bool edio_write(const uint8_t *data, uint32_t len) {
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
    tuh_cdc_write_flush(g_idx);
    return true;
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
// Faithful to the SSOT (edio.ts / Edio.cpp): cmd frame, then the 9-byte header, then
// the payload, as separate writes. Verified to drive EverMIDI's FIFO end to end.
bool edio_mem_wr(uint32_t addr, const uint8_t *data, uint32_t len) {
    if (len == 0) return true;
    if (!tx_cmd(EDIO_CMD_MEM_WR)) return false;
    uint8_t h[9];
    put32(h, addr);
    put32(h + 4, len);
    h[8] = 0;
    if (!edio_write(h, sizeof h)) return false;
    return edio_write(data, len);
}

bool edio_fifo_wr(const uint8_t *data, uint32_t len) {
    return edio_mem_wr(EDIO_ADDR_FIFO, data, len);
}
