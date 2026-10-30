// N8 standalone bridge - stage 2, slice 2.4: the MIDI -> N8 bridge (no PC).
//
// One Pico does the whole thing. MIDI comes in on a hardware UART (UART1/GP5, the
// stage-1 6N138 opto circuit) and is decoded by the reusable parser (../midi-in/
// midi.c). Each complete channel-voice message is forwarded straight to the N8's
// cart FIFO via Edio fifoWR (= memWR to 0x1810000) - which, with the BlipToaster ROM
// running on the NES, plays it on the 2A03. The N8 is hosted over PIO-USB
// (slice 2.1) and driven with the Edio port (slice 2.2/2.3). A read-only sniff of
// the running ROM's APU write-mirror reports Pulse1 on/off as live confirmation.
//
// So: play a MIDI keyboard -> the real NES plays it, with no computer in the loop.
//
// Wiring + build: see README.md. USB-A host socket D+=GP2/D-=GP3, VBUS=pin40,
// GND=pin38; MIDI opto out -> GP5 (pin 7); console on UART0/GP0-GP1 -> the debug
// probe -> /dev/ttyDbgProbe. UART1/GP5, PIO-USB and UART0 don't contend.

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/uart.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#ifndef USE_NATIVE_USB
#include "pio_usb.h"
#endif
#include "tusb.h"
#include "edio.h"
#include "midi.h"

#define PIN_USB_DP 2          // GP2 = D+, GP3 = D-
#define N8_VID     0x38df
#define N8_PID     0x0017

// '*t' attempts before giving up on the menu (~2.4 s each); lowered for quick experiments.
// 1 = print every forwarded message. Off by default: at 115200 baud the console line costs
// more than the forward itself, so it caps the bridge's throughput; [stats] summarises instead.
#ifndef N8_BRIDGE_VERBOSE
#define N8_BRIDGE_VERBOSE 0
#endif

#ifndef N8_MENU_TRIES
#define N8_MENU_TRIES 20
#endif

#define MIDI_UART   uart1     // stage-1 MIDI IN
#define MIDI_RX_PIN 5         // GP5 = UART1 RX = physical pin 7
#define MIDI_BAUD   31250

static volatile bool n8_ready    = false;   // set by the CDC mount callback
static bool          probed      = false;   // run the probe once per mount
static bool          forward_ok  = false;   // gate MIDI->FIFO on a ready+probed N8

typedef struct {
    uint32_t rx_bytes;     // bytes off the MIDI UART
    uint32_t msgs;         // complete channel-voice messages parsed
    uint32_t fwd;          // messages forwarded
    uint32_t fwd_fail;     // messages in a fifoWR that returned false
    uint32_t writes;       // fifoWRs issued (each carries a batch of messages)
    uint32_t max_batch;    // most messages carried by one fifoWR
    uint32_t filtered;     // aftertouch, dropped on purpose
    uint32_t not_ready;    // dropped because the N8 was not up
    uint32_t uart_oe;      // bytes lost to a full UART hardware FIFO
    uint32_t uart_fe;      // framing / break errors (wiring, or a Type A/B swap)
    uint32_t ring_full;    // bytes lost to a full receive ring
    uint32_t max_fwd_us;   // slowest edio_fifo_wr call (queueing it, not its delivery: see lat)
    uint32_t lat_avg_us;   // this report window: a message's last byte taken off the UART -> the
    uint32_t lat_max_us;   //   N8 acknowledging the fifoWR that carried it (avg / worst)
} bridge_stats;
static bridge_stats stats;

// MIDI receive is interrupt-driven into this ring, so bytes keep arriving while the main loop
// is blocked in a USB write (~5 ms each). Polled, a burst overran the UART's 32-byte hardware
// FIFO in ~10 ms and lost ~70% of its bytes. on_midi_uart_irq() is the only writer of rx_head,
// last_rx_us and the irq_* counters - run as the ISR, or by the idle main loop with the IRQ
// masked; the main loop is the only writer of rx_tail.
#define RX_RING 4096
static uint8_t           rx_ring[RX_RING];
static uint32_t          rx_ts[RX_RING];   // time_us_32() each byte was taken off the UART
static volatile uint32_t rx_head = 0, rx_tail = 0;
static volatile uint32_t irq_rx = 0, irq_oe = 0, irq_fe = 0, irq_ring_full = 0;
static volatile uint32_t last_rx_us = 0;   // when the last MIDI byte arrived

static void on_midi_uart_irq(void) {
    uint32_t now = time_us_32();
    while (uart_is_readable(MIDI_UART)) {
        // Read DR whole: bits 8-11 flag this byte's framing / parity / break / overrun.
        uint32_t dr = uart_get_hw(MIDI_UART)->dr;
        if (dr & UART_UARTDR_OE_BITS) irq_oe++;
        if (dr & (UART_UARTDR_FE_BITS | UART_UARTDR_BE_BITS)) { irq_fe++; continue; }
        uint32_t h = rx_head, next = (h + 1) % RX_RING;
        if (next == rx_tail) { irq_ring_full++; continue; }
        rx_ring[h] = (uint8_t)dr;
        rx_ts[h] = now;
        __dmb();   // the byte is in the ring before the main loop can see the new head
        rx_head = next;
        irq_rx++;
        last_rx_us = now;
    }
}

// Forwarded messages collect here and go out as ONE fifoWR per pass of the main loop: a write
// costs the same ~5 ms whatever it carries, so batching is what lets a burst through.
#define BATCH_MAX 192
static uint8_t  batch[BATCH_MAX];
static uint32_t batch_len = 0, batch_msgs = 0;
static uint32_t cur_byte_us = 0;      // rx_ts of the byte the parser is being fed
static uint32_t batch_first_us = 0;   // ... of the last byte of the batch's first message
static uint32_t lat_sum = 0, lat_n = 0, lat_win_max = 0;
// Latency is measured to the N8's ACK, not to edio_fifo_wr returning: the write returns once
// TinyUSB has queued it, and the packet then waits for the next 1 ms USB frame. The oldest
// unacknowledged batch is timed, so overlapping writes over- rather than under-report.
static volatile bool ack_pending = false;
static uint32_t      ack_from_us = 0;

void tuh_cdc_tx_complete_cb(uint8_t idx) {
    // Invoked per completed bulk-OUT transfer; the TX FIFO back at full size = this write is all out.
    if (!ack_pending || tuh_cdc_write_available(idx) != CFG_TUH_CDC_TX_BUFSIZE) return;
    uint32_t lat = time_us_32() - ack_from_us;
    lat_sum += lat; lat_n++;
    if (lat > lat_win_max) lat_win_max = lat;
    ack_pending = false;
}

static void batch_flush(void) {
    if (!batch_len) return;
    if (!ack_pending) { ack_from_us = batch_first_us; ack_pending = true; }   // armed first: the
    // write pumps tuh_task, so its completion can arrive before edio_fifo_wr even returns
    uint64_t t0 = time_us_64();
    bool ok = edio_fifo_wr(batch, batch_len);
    uint32_t us = (uint32_t)(time_us_64() - t0);
    stats.writes++;
    if (ok) stats.fwd += batch_msgs; else { stats.fwd_fail += batch_msgs; ack_pending = false; }
    if (us > stats.max_fwd_us) stats.max_fwd_us = us;
    if (batch_msgs > stats.max_batch) stats.max_batch = batch_msgs;
#if N8_BRIDGE_VERBOSE
    printf("[bridge] MIDI -> FIFO (%lu msgs):", (unsigned long)batch_msgs);
    for (uint32_t i = 0; i < batch_len; i++) printf(" %02x", batch[i]);
    printf("%s\n", ok ? "" : "  (write FAILED)");
#endif
    batch_len = batch_msgs = 0;
}

// TinyUSB's no-RTOS timing hook (no board layer), backed by the SDK clock.
uint32_t tusb_time_millis_api(void) { return to_ms_since_boot(get_absolute_time()); }

static void hexdump(const char *label, const uint8_t *d, int n) {
    printf("[n8-host] %s:", label);
    for (int i = 0; i < n; i++) printf(" %02x", d[i]);
    printf("\n");
}

// Run the Edio probe. Called from the main loop (not a callback) because the
// edio_* ops block + pump tuh_task() internally.
static void n8_probe(void) {
    printf("[n8-host] --- Edio probe ---\n");

    // Status is 0 (OK) at the N8 menu; a running ROM reports a non-zero busy state
    // (e.g. 5) while the link stays fully functional - not an error. -1 = no reply.
    int st = edio_get_status();
    printf("[n8-host] CMD_STATUS -> %d %s\n", st,
           st == 0 ? "(OK)" : st < 0 ? "(no reply)" : "(busy - ROM running)");

    uint8_t info[64];
    if (edio_sys_info(info)) {
        uint32_t ser0 = info[20] | info[21] << 8 | info[22] << 16 | (uint32_t)info[23] << 24;
        uint32_t ser1 = info[24] | info[25] << 8 | info[26] << 16 | (uint32_t)info[27] << 24;
        uint16_t sw = info[40] | info[41] << 8;
        uint16_t hw = info[42] | info[43] << 8;
        uint8_t  did = info[46];
        printf("[n8-host] SYS_INF: serial=%08lX.%08lX  device_id=0x%02x%s  sw=%04x hw=%04x\n",
               (unsigned long)ser0, (unsigned long)ser1, did,
               (did == 0x17) ? " (N8 PRO)" : "", sw, hw);
        hexdump("SYS_INF raw", info, 64);
    } else {
        printf("[n8-host] SYS_INF read failed\n");
    }

    // NEVER touch cart memory (PRG / CHR / SRM) here. It is a DMA that takes the cart bus off the
    // console, and this runs the moment the N8 enumerates - while its menu is still booting. A
    // 16-byte SRM read at this point left the menu's FIFO command channel dead until the next
    // power-cycle: '*t' never got its 'k' in 9 of 10 trials, against 8 of 8 without the read.
    // That was the whole "Pico can't write the cart FIFO" blocker (../pico-n8-fifo-write-bug.md).
    printf("[n8-host] --- probe done ---\n");
}

// Autonomous boot: if the N8 file-browser MENU is running (it answers '*t' with 'k'),
// drive it over the cart FIFO (edio_menu_*) to install + boot BlipToaster, no PC in the loop.
// If a game is already running (or wedged) the menu won't answer, and we just forward MIDI.
// The handshake rides the cart-FIFO write (memWR to 0x1810000); it only works if nothing
// touched cart memory while the menu was booting - see n8_probe().
// The filename on the N8's PHYSICAL SD card, which is not renamed by this repo - it still reads n8-midi.nes
// until the card is rewritten. Update both together.
#define BLIPTOASTER_SD_PATH "usb-games/n8-midi.nes"
static void boot_bliptoaster(void) {
    // The N8's USB (MCU) enumerates before the menu core is ready, so retry the handshake
    // for a few seconds after a fresh power-up.
    printf("[n8-host] menu handshake (*t)...\n");
    bool menu = false;
    for (int i = 0; i < N8_MENU_TRIES && !menu; i++) {
        menu = edio_menu_test();
        if (!menu) for (int j = 0; j < 4; j++) { sleep_ms(100); tuh_task(); }
    }
    if (!menu) {
        printf("[n8-host] no 'k' - a game is already running (or the FIFO write is blocked); "
               "forwarding MIDI as-is\n");
        return;
    }
    printf("[n8-host] menu up -> install %s (*n)...\n", BLIPTOASTER_SD_PATH);
    int st = edio_menu_install(BLIPTOASTER_SD_PATH);
    if (st != 0) {
        printf("[n8-host] install FAILED (status %d)%s\n", st,
               st == 0x44 ? " - dirty menu heap, power-cycle to a fresh menu" : "");
        return;
    }
    printf("[n8-host] installed -> boot (*s); BlipToaster starting...\n");
    edio_menu_start();
    for (int i = 0; i < 40; i++) { sleep_ms(100); tuh_task(); }   // let the NES reboot into the game
    printf("[n8-host] BlipToaster booted by the Pico - no PC used.\n");
}

// The bridge sink: a complete MIDI message from the parser -> the N8 FIFO. Forward
// only channel-voice messages (0x80-0xEF); system + realtime bytes (clock/sensing/
// transport) are dropped. CRITICAL: aftertouch is ALSO dropped - poly-aftertouch
// (0xA0) and channel-pressure (0xD0) are high-rate continuous streams (a Launchpad
// floods channel-pressure while any pad is held). Forwarding that flood overruns the
// N8 cart FIFO faster than BlipToaster drains it, which desyncs its MIDI parser and
// HANGS the ROM on the last note (observed on hardware). BlipToaster only plays notes,
// so we forward note-on/off + program-change + pitch-bend + CC and drop the rest.
// Program-change carries one data byte, the rest two. Every message is sent with its status
// byte (no running status), so a batch is just the messages back to back.
static void midi_to_fifo(const midi_message *m, void *user) {
    (void)user;
    if (m->status >= 0xf0) return;                      // channel-voice only
    stats.msgs++;
    if (m->type == MIDI_POLY_AFTERTOUCH ||
        m->type == MIDI_CHANNEL_PRESSURE) { stats.filtered++; return; }   // the aftertouch flood
    if (!forward_ok) { stats.not_ready++; return; }     // N8 not ready yet: drop

    uint32_t n = (m->type == MIDI_PROGRAM_CHANGE) ? 2 : 3;   // 1-data-byte message
    if (batch_len + n > BATCH_MAX) batch_flush();
    batch[batch_len++] = m->status;
    batch[batch_len++] = m->data0;
    if (n == 3) batch[batch_len++] = m->data1;
    if (batch_msgs == 0) batch_first_us = cur_byte_us;
    batch_msgs++;
}

// Once a second, if anything moved: what the bridge received, forwarded and lost. A message
// lost before the parser shows up as a UART overrun (oe) or framing error (fe), not as a drop.
// Console output and the sniff's USB read both hold the main loop (a 150-char line blocks ~11 ms
// on the 115200 console), and MIDI arriving meanwhile waits behind them. So the diagnostics run
// only once MIDI has been quiet for 100 ms: a player never pays for them, and the stress test
// reads its counters after a pause anyway.
static bool midi_quiet(void) { return time_us_32() - last_rx_us > 100000; }

static void stats_report(void) {
    if (!midi_quiet()) return;
    static uint32_t next_ms = 0;
    static bridge_stats last;
    uint32_t t = to_ms_since_boot(get_absolute_time());
    if (t < next_ms) return;
    next_ms = t + 1000;
    stats.rx_bytes = irq_rx;          // the ISR's counters, folded in for the report
    stats.uart_oe = irq_oe;
    stats.uart_fe = irq_fe;
    stats.ring_full = irq_ring_full;
    stats.lat_avg_us = lat_n ? lat_sum / lat_n : 0;
    stats.lat_max_us = lat_win_max;
    lat_sum = lat_n = lat_win_max = 0;
    if (memcmp(&last, &stats, sizeof stats) == 0) return;
    last = stats;
    printf("[stats] rx=%lu msgs=%lu fwd=%lu fail=%lu writes=%lu maxbatch=%lu filtered=%lu "
           "notready=%lu oe=%lu fe=%lu ringfull=%lu maxfwd=%luus lat=%lu/%luus\n",
           (unsigned long)stats.rx_bytes, (unsigned long)stats.msgs, (unsigned long)stats.fwd,
           (unsigned long)stats.fwd_fail, (unsigned long)stats.writes,
           (unsigned long)stats.max_batch, (unsigned long)stats.filtered,
           (unsigned long)stats.not_ready, (unsigned long)stats.uart_oe,
           (unsigned long)stats.uart_fe, (unsigned long)stats.ring_full,
           (unsigned long)stats.max_fwd_us, (unsigned long)stats.lat_avg_us,
           (unsigned long)stats.lat_max_us);
}

// Read-only confirmation: poll the running ROM's APU write-mirror ($4000-$401F at SSR+0x80)
// and report each voice as <timer>/<level> whenever anything changes. Injects NOTHING (the
// forwarded MIDI is what drives it), so moving values here = BlipToaster really is following
// the notes (not stuck). Prints only on change so held notes stay quiet.
// The ROM leaves a voice's timer alone at note-off and silences it through its level, so the
// level is what says "sounding": the pulses' and noise's volume nibble ($4000/$4004/$400C),
// the triangle's linear counter ($4008). Pulse1's Hz is for a PAL 2A07:
// f = 1662607 / (16 * (timer + 1)).
static void sniff_report(void) {
    if (!midi_quiet()) return;
    static uint32_t next_ms = 0;
    static char last[96];
    uint32_t t = to_ms_since_boot(get_absolute_time());
    if (t < next_ms) return;
    next_ms = t + 250;

    // Only $4000-$4015 (22 bytes, one USB packet): the whole 0x200-byte mirror is eight packets,
    // and at one per 1 ms frame that read held the main loop - and any MIDI behind it - ~10 ms.
    uint8_t r[0x16];
    if (!edio_mem_rd(EDIO_ADDR_SSR + 0x80, r, sizeof r)) { printf("[sniff] read FAIL\n"); return; }
    uint16_t p1  = r[0x2] | ((r[0x3] & 0x07) << 8);
    uint16_t p2  = r[0x6] | ((r[0x7] & 0x07) << 8);
    uint16_t tri = r[0xa] | ((r[0xb] & 0x07) << 8);
    uint16_t noi = r[0xe] & 0x0f;
    unsigned hz = 1662607u / (16u * (p1 + 1u));
    char line[96];
    snprintf(line, sizeof line, "p1=%u/%x (%u Hz)  p2=%u/%x  tri=%u/%x  noise=%u/%x",
             p1, r[0x0] & 0x0f, hz, p2, r[0x4] & 0x0f, tri, r[0x8] & 0x7f, noi, r[0xc] & 0x0f);
    if (strcmp(line, last) == 0) return;
    strcpy(last, line);
    printf("[sniff] %s\n", line);
}

int main(void) {
#ifndef USE_NATIVE_USB
    set_sys_clock_khz(120000, true);   // PIO-USB needs a 120 MHz-multiple clock
#endif
    stdio_init_all();
    sleep_ms(100);
#ifdef USE_NATIVE_USB
    printf("\n[n8-host] USB host up (NATIVE controller, rhport 0). MIDI IN on UART1/GP%d @ %d. "
           "Waiting for the N8...\n", MIDI_RX_PIN, MIDI_BAUD);
#else
    printf("\n[n8-host] USB host up (PIO-USB D+=GP%d, D-=GP%d). MIDI IN on UART1/GP%d @ %d. "
           "Waiting for the N8...\n", PIN_USB_DP, PIN_USB_DP + 1, MIDI_RX_PIN, MIDI_BAUD);
#endif

    // MIDI IN (stage-1 opto circuit) on UART1/GP5.
    uart_init(MIDI_UART, MIDI_BAUD);
    gpio_set_function(MIDI_RX_PIN, GPIO_FUNC_UART);
    uart_set_format(MIDI_UART, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(MIDI_UART, true);
    // Lowest priority: the ISR only copies bytes, and must never delay PIO-USB's own timer IRQ.
    irq_set_exclusive_handler(UART_IRQ_NUM(MIDI_UART), on_midi_uart_irq);
    irq_set_priority(UART_IRQ_NUM(MIDI_UART), PICO_LOWEST_IRQ_PRIORITY);
    irq_set_enabled(UART_IRQ_NUM(MIDI_UART), true);
    uart_set_irqs_enabled(MIDI_UART, true, false);

    midi_parser parser;
    midi_parser_init(&parser, midi_to_fifo, NULL);
    bool was_forwarding = false;

#ifdef USE_NATIVE_USB
    tuh_init(0);   // RP2350 native USB host controller (its own D+/D- pins)
#else
    // N8 USB host on PIO-USB (GP2/GP3).
    pio_usb_configuration_t pio_cfg = PIO_USB_DEFAULT_CONFIG;
    pio_cfg.pin_dp = PIN_USB_DP;
    tuh_configure(1, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &pio_cfg);
    tuh_init(1);
#endif

    while (true) {
        tuh_task();

        // Drain the receive ring -> parser, whose sink batches complete messages; then send
        // the batch as one fifoWR. The sink gates on forward_ok, so bytes that arrive before
        // the N8 is ready are parsed and dropped.
        // Idle, read the UART here as well: a 3-byte message is under the RX interrupt's 4-byte
        // threshold, so left to the IRQ it waits ~1 ms for the receive timeout to fire.
        irq_set_enabled(UART_IRQ_NUM(MIDI_UART), false);
        on_midi_uart_irq();
        irq_set_enabled(UART_IRQ_NUM(MIDI_UART), true);
        while (rx_tail != rx_head) {
            uint32_t t = rx_tail;
            uint8_t b = rx_ring[t];
            cur_byte_us = rx_ts[t];
            __dmb();   // the byte is read before the ISR may reuse its slot
            rx_tail = (t + 1) % RX_RING;
            midi_parser_byte(&parser, b);
        }
        batch_flush();

        if (n8_ready && !probed) {
            probed = true;
            n8_probe();
            boot_bliptoaster();
        }
        forward_ok = n8_ready && probed;
        // Forwarding just became possible (after a mount, and the up-to-48 s menu boot): the ring
        // may hold seconds of MIDI from before the N8 could take it, and playing that stale burst
        // into the ROM now would only produce surprise notes. Start from a clean stream.
        if (forward_ok && !was_forwarding) {
            rx_tail = rx_head;
            midi_parser_init(&parser, midi_to_fifo, NULL);
        }
        was_forwarding = forward_ok;
        if (forward_ok) sniff_report();
        stats_report();
    }
}

void tuh_mount_cb(uint8_t daddr) {
    uint16_t vid = 0, pid = 0;
    tuh_vid_pid_get(daddr, &vid, &pid);
    printf("[n8-host] device mounted: addr %u  VID:PID %04x:%04x%s\n",
           daddr, vid, pid, (vid == N8_VID && pid == N8_PID) ? "   <- N8!" : "");
}

void tuh_umount_cb(uint8_t daddr) {
    printf("[n8-host] device %u unmounted\n", daddr);
    n8_ready = false;
    probed = false;
}

// The N8's CDC-ACM interface mounted -> bind Edio to it and arm the probe.
void tuh_cdc_mount_cb(uint8_t idx) {
    tuh_itf_info_t info = { 0 };
    tuh_cdc_itf_get_info(idx, &info);
    uint16_t vid = 0, pid = 0;
    tuh_vid_pid_get(info.daddr, &vid, &pid);
    printf("[n8-host] CDC mounted (idx %u, VID:PID %04x:%04x)\n", idx, vid, pid);
    if (vid == N8_VID && pid == N8_PID) {
        edio_bind(idx);
        n8_ready = true;
        probed = false;
    }
}

void tuh_cdc_umount_cb(uint8_t idx) {
    (void)idx;
    n8_ready = false;
    probed = false;
}
