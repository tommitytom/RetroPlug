// N8 standalone bridge - stage 2, slice 2.4: the MIDI -> N8 bridge (no PC).
//
// One Pico does the whole thing. MIDI comes in on a hardware UART (UART1/GP5, the
// stage-1 6N138 opto circuit) and is decoded by the reusable parser (../midi-in/
// midi.c). Each complete channel-voice message is forwarded straight to the N8's
// cart FIFO via Edio fifoWR (= memWR to 0x1810000) - which, with the EverMIDI ROM
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
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/uart.h"
#ifndef USE_NATIVE_USB
#include "pio_usb.h"
#endif
#include "tusb.h"
#include "edio.h"
#include "midi.h"

#define PIN_USB_DP 2          // GP2 = D+, GP3 = D-
#define N8_VID     0x38df
#define N8_PID     0x0017

#define MIDI_UART   uart1     // stage-1 MIDI IN
#define MIDI_RX_PIN 5         // GP5 = UART1 RX = physical pin 7
#define MIDI_BAUD   31250

static volatile bool n8_ready    = false;   // set by the CDC mount callback
static bool          probed      = false;   // run the probe once per mount
static bool          forward_ok  = false;   // gate MIDI->FIFO on a ready+probed N8

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

    uint8_t srm[16];
    if (edio_mem_rd(EDIO_ADDR_SRM, srm, sizeof srm))
        hexdump("memRD SRM[0..15]", srm, 16);
    else
        printf("[n8-host] memRD failed\n");

    // memWR isn't checked destructively here (an SRM round-trip would clobber a
    // save-game's battery RAM): the bridge exercises it live via fifoWR, and
    // sniff_report() reads Pulse1 back to confirm the forwarded notes landed.
    printf("[n8-host] --- probe done ---\n");
}

// Autonomous boot: if the N8 file-browser MENU is running (it answers '*t' with 'k'),
// drive it over the cart FIFO (edio_menu_*) to install + boot EverMIDI, no PC in the loop.
// If a game is already running (or wedged) the menu won't answer, and we just forward MIDI.
// NOTE: this depends on the cart-FIFO WRITE path (memWR to 0x1810000), which does NOT work
// over Pico-PIO-USB - the N8 ACKs the write but never routes it to the FIFO, so '*t' gets no
// reply and this returns early. It works from a silicon USB host. See pico-n8-fifo-write-bug.md.
#define EVERMIDI_SD_PATH "usb-games/n8-midi.nes"
static void boot_evermidi(void) {
    // The N8's USB (MCU) enumerates before the menu core is ready, so retry the handshake
    // for a few seconds after a fresh power-up.
    printf("[n8-host] menu handshake (*t)...\n");
    bool menu = false;
    for (int i = 0; i < 20 && !menu; i++) {
        menu = edio_menu_test();
        if (!menu) for (int j = 0; j < 4; j++) { sleep_ms(100); tuh_task(); }
    }
    if (!menu) {
        printf("[n8-host] no 'k' - a game is already running (or the FIFO write is blocked); "
               "forwarding MIDI as-is\n");
        return;
    }
    printf("[n8-host] menu up -> install %s (*n)...\n", EVERMIDI_SD_PATH);
    int st = edio_menu_install(EVERMIDI_SD_PATH);
    if (st != 0) {
        printf("[n8-host] install FAILED (status %d)%s\n", st,
               st == 0x44 ? " - dirty menu heap, power-cycle to a fresh menu" : "");
        return;
    }
    printf("[n8-host] installed -> boot (*s); EverMIDI starting...\n");
    edio_menu_start();
    for (int i = 0; i < 40; i++) { sleep_ms(100); tuh_task(); }   // let the NES reboot into the game
    printf("[n8-host] EverMIDI booted by the Pico - no PC used.\n");
}

// The bridge sink: a complete MIDI message from the parser -> the N8 FIFO. Forward
// only channel-voice messages (0x80-0xEF); system + realtime bytes (clock/sensing/
// transport) are dropped. CRITICAL: aftertouch is ALSO dropped - poly-aftertouch
// (0xA0) and channel-pressure (0xD0) are high-rate continuous streams (a Launchpad
// floods channel-pressure while any pad is held). Forwarding that flood overruns the
// N8 cart FIFO faster than EverMIDI drains it, which desyncs its MIDI parser and
// HANGS the ROM on the last note (observed on hardware). EverMIDI only plays notes,
// so we forward note-on/off + program-change + pitch-bend + CC and drop the rest.
// Program-change carries one data byte, the rest two. One fifoWR per message.
static void midi_to_fifo(const midi_message *m, void *user) {
    (void)user;
    if (m->status >= 0xf0) return;                      // channel-voice only
    if (m->type == MIDI_POLY_AFTERTOUCH ||
        m->type == MIDI_CHANNEL_PRESSURE) return;        // drop the aftertouch flood
    if (!forward_ok) return;                             // N8 not ready yet: drop

    uint8_t b[3];
    b[0] = m->status;
    b[1] = m->data0;
    uint32_t n = (m->type == MIDI_PROGRAM_CHANGE) ? 2 : 3;   // 1-data-byte message
    if (n == 3) b[2] = m->data1;

    edio_fifo_wr(b, n);
    printf("[bridge] MIDI -> FIFO:");
    for (uint32_t i = 0; i < n; i++) printf(" %02x", b[i]);
    printf("\n");
}

// Read-only confirmation: poll the running ROM's APU write-mirror and report Pulse1's
// PITCH whenever it changes. Injects NOTHING (the forwarded MIDI is what drives it), so
// a moving pitch here = EverMIDI really is following the notes (not stuck). Prints only
// on change so held notes stay quiet. PAL 2A07: f = 1662607 / (16 * (timer + 1)).
static void sniff_report(void) {
    static uint32_t next_ms = 0;
    uint32_t t = to_ms_since_boot(get_absolute_time());
    if (t < next_ms) return;
    next_ms = t + 250;

    uint8_t s[EDIO_SNIFFER_SIZE];
    if (!edio_mem_rd(EDIO_ADDR_SSR, s, sizeof s)) { printf("[sniff] read FAIL\n"); return; }
    uint16_t p1 = ((s[0x95] & 0x01) ? (s[0x82] | ((s[0x83] & 0x07) << 8)) : 0);
    unsigned hz = p1 ? (1662607u / (16u * (p1 + 1u))) : 0;
    printf("[sniff] timer=%-4u %4u Hz\n", p1, hz);
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

    midi_parser parser;
    midi_parser_init(&parser, midi_to_fifo, NULL);

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

        // Drain any MIDI bytes -> parser (which forwards complete messages via the
        // sink). Always drain so the UART FIFO can't overrun; the sink itself gates
        // on forward_ok, so bytes before the N8 is ready are parsed and dropped.
        while (uart_is_readable(MIDI_UART))
            midi_parser_byte(&parser, uart_getc(MIDI_UART));

        if (n8_ready && !probed) {
            probed = true;
            n8_probe();
            boot_evermidi();
        }
        forward_ok = n8_ready && probed;
        if (forward_ok) sniff_report();
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
