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

// Each Edio frame goes out as ONE edio_write. Pico-PIO-USB runs host transactions from its 1 ms
// start-of-frame interrupt, one packet per endpoint per frame, so every separate write costs a
// whole frame: memWR as five writes (cmd / addr / len / exec / data) took 5 ms, one write takes
// one frame. The five-way split was an attempt at the "Pico can't write the cart FIFO" bug, which
// turned out to be the probe reading cart SRAM during menu boot (../pico-n8-fifo-write-bug.md).
#define EDIO_FRAME_MAX 256

static uint32_t frame_hdr(uint8_t *f, uint8_t cmd, uint32_t addr, uint32_t len) {
    f[0] = 0x2b; f[1] = 0xd4; f[2] = cmd; f[3] = (uint8_t)(cmd ^ 0xff);
    put32(f + 4, addr);
    put32(f + 8, len);
    f[12] = 0;   // exec
    return 13;
}

// memRD: CMD + addr(tx32) + len(tx32) + exec(0), then read `len` bytes.
bool edio_mem_rd(uint32_t addr, uint8_t *buf, uint32_t len) {
    uint8_t f[13];
    if (!edio_write(f, frame_hdr(f, EDIO_CMD_MEM_RD, addr, len))) return false;
    return edio_read(buf, len);
}

// memWR: CMD + addr(tx32) + len(tx32) + exec(0) + data. Fire-and-forget (no status).
bool edio_mem_wr(uint32_t addr, const uint8_t *data, uint32_t len) {
    if (len == 0) return true;
    uint8_t f[EDIO_FRAME_MAX];
    uint32_t n = frame_hdr(f, EDIO_CMD_MEM_WR, addr, len);
    if (n + len > sizeof f) return edio_write(f, n) && edio_write(data, len);
    memcpy(f + n, data, len);
    return edio_write(f, n + len);
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

// --- SD-card file write (fileOpen + fileWrite + fileClose, as the PC host's Edio.cpp does) ---

#define EDIO_CMD_F_FOPN   0xC9
#define EDIO_CMD_F_FWR    0xCC
#define EDIO_CMD_F_FCLOSE 0xCE
#define FA_WRITE          0x02
#define FA_CREATE_ALWAYS  0x08
#define FS_MAKEPATH       0x80    // create parent dirs if missing
#define ACK_BLOCK         1024    // the device releases each fileWrite block with a 0 ack byte
#define SD_TIMEOUT_MS     5000    // an SD write or a FAT update can take a while

int edio_file_put(const char *path, const uint8_t *data, uint32_t len) {
    uint8_t f[4 + 1 + 2 + 128];
    uint32_t plen = (uint32_t)strlen(path);
    if (plen > 128) return -1;
    f[0] = 0x2b; f[1] = 0xd4; f[2] = EDIO_CMD_F_FOPN; f[3] = (uint8_t)(EDIO_CMD_F_FOPN ^ 0xff);
    f[4] = FA_WRITE | FA_CREATE_ALWAYS | FS_MAKEPATH;
    f[5] = (uint8_t)plen; f[6] = (uint8_t)(plen >> 8);
    memcpy(f + 7, path, plen);
    if (!edio_write(f, 7 + plen)) return -1;
    int st = edio_get_status();
    if (st != 0) return st;

    f[2] = EDIO_CMD_F_FWR; f[3] = (uint8_t)(EDIO_CMD_F_FWR ^ 0xff);
    put32(f + 4, len);
    if (!edio_write(f, 8)) return -1;
    for (uint32_t off = 0; off < len; off += ACK_BLOCK) {
        uint8_t ack = 0xff;
        if (!edio_read_to(&ack, 1, SD_TIMEOUT_MS)) return -1;
        if (ack != 0) return ack;
        uint32_t n = len - off < ACK_BLOCK ? len - off : ACK_BLOCK;
        if (!edio_write(data + off, n)) return -1;
    }
    st = edio_get_status();
    if (st != 0) return st;

    if (!tx_cmd(EDIO_CMD_F_FCLOSE)) return -1;
    return edio_get_status();
}
