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
#define EDIO_ADDR_SSR   0x1802000u   // sniffer: a running game's live APU/PPU/OAM write-mirror (0x200 B)
#define EDIO_SNIFFER_SIZE 0x200u
#define EDIO_ADDR_FIFO  0x1810000u   // cart FIFO -> the running ROM reads $40F0/$40F1
#define EDIO_ADDR_CFG   0x1800020u   // the live mapper config the N8 OS writes at hand-over (16 B; [3] = expansion master volume)

// Bind the CDC interface index the N8 is on (from tuh_cdc_mount_cb).
void edio_bind(uint8_t cdc_idx);
void edio_unbind(void);   // the N8 went away: every wait fails at once until the next bind

// Called on every pump of the blocking waits below (the watchdog + the status LED).
void edio_set_idle_hook(void (*fn)(void));

// Blocking primitives over the CDC link (pump tuh_task; false on timeout).
bool edio_write(const uint8_t *data, uint32_t len);
bool edio_read(uint8_t *buf, uint32_t len);

// Ops.
int  edio_get_status(void);                                    // status code (0=OK), or -1
bool edio_sys_info(uint8_t out[64]);
bool edio_mem_rd(uint32_t addr, uint8_t *buf, uint32_t len);
bool edio_mem_wr(uint32_t addr, const uint8_t *data, uint32_t len);
bool edio_fifo_wr(const uint8_t *data, uint32_t len);          // = mem_wr(ADDR_FIFO, ...)

// Write a whole file to the N8's SD card (created or truncated, parent dirs made): fileOpen +
// fileWrite + fileClose, as packages/native/src/host/n8/Edio.cpp does. 0 = ok, else the step's
// device status (or -1 for no reply). An MCU-level operation: run it at the menu, not in a game.
int  edio_file_put(const char *path, const uint8_t *data, uint32_t len);
// Read the first `len` (<= 512) bytes of a file on the SD card. 0 = ok, else a device status / -1.
int  edio_file_get(const char *path, uint8_t *data, uint32_t len);

// N8 file-browser menu command channel: the running MENU reads '*'-prefixed commands from
// the same cart FIFO the bridge writes to, and streams replies back over the CDC. Menu-only
// (a running/frozen game won't answer). Mirrors packages/retroplug/src/n8/n8Menu.ts. Lets
// the Pico boot a ROM with no PC: edio_menu_test() then edio_menu_install(path) then
// edio_menu_start().
bool edio_menu_test(void);                 // '*t' handshake: true iff the menu replied 'k'
int  edio_menu_install(const char *path);  // '*n' + path -> load ROM; status (0=ok), or -1 on read fail
void edio_menu_start(void);                // '*s' boot the installed ROM (the game then runs)

#endif // EDIO_H
