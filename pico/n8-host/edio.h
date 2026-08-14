// A C port of the Everdrive N8 "Edio" USB protocol, over a TinyUSB cdc_host link.
// Mirrors packages/retroplug/src/n8/edio.ts (the SSOT). Every command is framed
// `2B D4 <cmd> <cmd^0xFF>` followed by little-endian args. The read/write
// primitives block, pumping tuh_task() while they wait (so call them from the
// main loop, NOT from a tuh_* callback).
#ifndef EDIO_H
#define EDIO_H

#include <stdint.h>
#include <stdbool.h>

// Command opcodes.
enum {
    EDIO_CMD_STATUS  = 0x10,
    EDIO_CMD_MEM_RD  = 0x19,
    EDIO_CMD_MEM_WR  = 0x1a,
    EDIO_CMD_SYS_INF = 0x26,
};

// N8 device addresses.
#define EDIO_ADDR_SRM   0x1000000u   // cart battery RAM (a game's .srm)
#define EDIO_ADDR_FIFO  0x1810000u   // cart FIFO -> the running ROM reads $40F0/$40F1

// Bind the CDC interface index the N8 is on (from tuh_cdc_mount_cb).
void edio_bind(uint8_t cdc_idx);

// Blocking primitives over the CDC link (pump tuh_task; false on timeout).
bool edio_write(const uint8_t *data, uint32_t len);
bool edio_read(uint8_t *buf, uint32_t len);

// Ops.
int  edio_get_status(void);                                    // status code (0=OK), or -1
bool edio_sys_info(uint8_t out[64]);
bool edio_mem_rd(uint32_t addr, uint8_t *buf, uint32_t len);
bool edio_mem_wr(uint32_t addr, const uint8_t *data, uint32_t len);
bool edio_fifo_wr(const uint8_t *data, uint32_t len);          // = mem_wr(ADDR_FIFO, ...)

#endif // EDIO_H
