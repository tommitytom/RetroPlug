// N8 standalone bridge: MIDI in -> the N8's cart FIFO -> the ROM running on a real NES.
//
// One Pico does the whole thing. MIDI comes in on a hardware UART (UART1/GP5, the stage-1
// 6N138 opto circuit) and is decoded by the reusable parser (../midi-in/midi.c); channel-voice
// messages and SysEx are forwarded to the N8's cart FIFO via Edio fifoWR (= memWR to
// 0x1810000). The N8 is hosted over PIO-USB and driven with the N8 pack's Edio (packages/n8: the
// protocol, its menu commands and operations, over UsbCdcTransport). When the N8
// shows its menu, the Pico boots a ROM through it - one staged in its own flash (uploaded to the
// SD first) or one already on the card. box.cpp makes it a standalone box: status LED, watchdog
// and recovery reboots, and which staged ROM to boot (picked over MIDI, saved on the SD).
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
#include "SofTimer.hpp"
#include "pio_usb.h"
extern "C" bool pio_usb_root_attached(void);   // pio_usb_root.c: pio_usb_ll.h is C that C++ cannot compile
#endif
// TinyUSB's osal_none.h applies ++ and -- to a volatile, which C++20 deprecates: its warning, not this file's.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wvolatile"
#include "tusb.h"
#pragma GCC diagnostic pop
#include "UsbCdcTransport.hpp"
#include "box.h"
#include "graph/Packages.hpp"
#include "midi.h"
#include "n8/Edio.hpp"
#include "n8/Menu.hpp"
#include "n8/Ops.hpp"

using rp::n8::Edio;

#define PIN_USB_DP 2          // GP2 = D+, GP3 = D-
#define N8_VID     0x38df
#define N8_PID     0x0017

// 1 = print every forwarded message. Off by default: at 115200 baud the console line costs
// more than the forward itself, so it caps the bridge's throughput; [stats] summarises instead.
#ifndef N8_BRIDGE_VERBOSE
#define N8_BRIDGE_VERBOSE 0
#endif

// 1 = measure how long the ROM takes to act on a note (apu_latency_probe). Measurement only.
#ifndef N8_APU_LAT
#define N8_APU_LAT 0
#endif

// '*t' attempts before giving up on the menu (~2.4 s each); lowered for quick experiments.
#ifndef N8_MENU_TRIES
#define N8_MENU_TRIES 20
#endif

// 1 = accept the box's test SysEx commands (hang, fake a dead link): tests only.
#ifndef N8_TEST_HOOKS
#define N8_TEST_HOOKS 0
#endif

// The N8's link and its Edio, in static storage: one of each, for whichever N8 is mounted (UsbCdcTransport.hpp says
// how a mount rebinds it). The transport's waits run the box's idle hook, so the long Edio calls keep it fed.
static UsbCdcTransport usb(box_feed);
static Edio            edio(usb);
static uint8_t         n8_cdc_idx = 0;   // the N8's CDC interface, from its mount; bound before its probe

#define MIDI_UART   uart1     // stage-1 MIDI IN
#define MIDI_RX_PIN 5         // GP5 = UART1 RX = physical pin 7
#define MIDI_BAUD   31250

static volatile bool n8_ready    = false;   // set by the CDC mount callback
static bool          probed      = false;   // run the probe once per mount
static bool          forward_ok  = false;   // gate MIDI->FIFO on a ready+probed N8
static volatile bool any_mounted = false;   // some USB device has enumerated (the N8, or its MCU alone)
static volatile uint32_t n8_gen  = 0;      // bumped on every N8 mount: a boot checks it is still its N8
static bool          boot_error  = false;   // the last menu boot failed: LED_ERROR until the next mount
static int           write_fail_streak = 0; // consecutive failed fifoWRs
static int           cur_sel     = -1;      // the staged ROM the box last booted (-1 = none / unknown)
static uint32_t      menu_stuck_until = 0; // !0: the menu stopped answering - no forwarding; retry the boot then

typedef struct {
    uint32_t rx_bytes;     // bytes off the MIDI UART
    uint32_t msgs;         // complete channel-voice messages parsed
    uint32_t fwd;          // messages forwarded
    uint32_t fwd_fail;     // messages in a fifoWR that returned false
    uint32_t writes;       // fifoWRs issued (each carries a batch of messages)
    uint32_t max_batch;    // most messages carried by one fifoWR
    uint32_t sysex;        // complete SysEx messages forwarded (counted in fwd too)
    uint32_t filtered;     // aftertouch, dropped on purpose
    uint32_t not_ready;    // dropped because the N8 was not up
    uint32_t uart_oe;      // bytes lost to a full UART hardware FIFO
    uint32_t uart_fe;      // framing / break errors (wiring, or a Type A/B swap)
    uint32_t ring_full;    // bytes lost to a full receive ring
    uint32_t max_fwd_us;   // slowest Edio::fifoWR call (queueing it, not its delivery: see lat)
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
        if (dr & UART_UARTDR_OE_BITS) irq_oe = irq_oe + 1;
        if (dr & (UART_UARTDR_FE_BITS | UART_UARTDR_BE_BITS)) { irq_fe = irq_fe + 1; continue; }
        uint32_t h = rx_head, next = (h + 1) % RX_RING;
        if (next == rx_tail) { irq_ring_full = irq_ring_full + 1; continue; }
        rx_ring[h] = (uint8_t)dr;
        rx_ts[h] = now;
        __dmb();   // the byte is in the ring before the main loop can see the new head
        rx_head = next;
        irq_rx = irq_rx + 1;
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
// Latency is measured to the N8's ACK, not to Edio::fifoWR returning: the write returns once
// TinyUSB has queued it, and the packet then waits for the next 1 ms USB frame. The oldest
// unacknowledged batch is timed, so overlapping writes over- rather than under-report.
static volatile bool ack_pending = false;
static uint32_t      ack_from_us = 0;

extern "C" void tuh_cdc_tx_complete_cb(uint8_t idx) {
    // Invoked per completed bulk-OUT transfer; the TX FIFO back at full size = this write is all out.
    if (!ack_pending || tuh_cdc_write_available(idx) != CFG_TUH_CDC_TX_BUFSIZE) return;
    uint32_t lat = time_us_32() - ack_from_us;
    lat_sum += lat; lat_n++;
    if (lat > lat_win_max) lat_win_max = lat;
    ack_pending = false;
}

#if N8_APU_LAT
// Measurement build: after a lone ch1 note-on, poll Pulse1's volume ($4000) through the N8's live
// APU mirror until the ROM has written it, and report when, from the note's last byte off the
// UART. Poll i samples the register somewhere between sending its request and getting the reply,
// so the write happened after the last miss's request (lo) and before the hit's reply (hi). Each
// poll is a 1-byte memRD, ~2 USB frames. Holds the main loop while it runs: measurement only.
static void apu_latency_probe(uint32_t from_us) {
    uint32_t lo = 0, polls = 0;
    while (time_us_32() - from_us < 100000) {
        uint32_t req = time_us_32();
        uint8_t v = 0;
        if (!edio.memRD(Edio::ADDR_SSR + 0x80, &v, 1).ok()) { printf("[apu] read FAIL\n"); return; }
        polls++;
        if (v & 0x0f) {
            printf("[apu] ROM wrote $4000 between %lu and %lu us after the note (%lu polls)\n",
                   (unsigned long)lo, (unsigned long)(time_us_32() - from_us), (unsigned long)polls);
            return;
        }
        lo = req - from_us;
    }
    printf("[apu] no $4000 write within 100 ms\n");
}
#endif

static void batch_flush(void) {
    if (!batch_len) return;
    if (!ack_pending) { ack_from_us = batch_first_us; ack_pending = true; }   // armed first: the
    // write pumps tuh_task, so its completion can arrive before Edio::fifoWR even returns
    uint64_t t0 = time_us_64();
    bool ok = edio.fifoWR(batch, batch_len).ok();
    uint32_t us = (uint32_t)(time_us_64() - t0);
    stats.writes++;
    if (ok) stats.fwd += batch_msgs; else { stats.fwd_fail += batch_msgs; ack_pending = false; }
    write_fail_streak = ok ? 0 : write_fail_streak + 1;
    if (ok) led_activity();
    if (us > stats.max_fwd_us) stats.max_fwd_us = us;
    if (batch_msgs > stats.max_batch) stats.max_batch = batch_msgs;
#if N8_BRIDGE_VERBOSE
    printf("[bridge] MIDI -> FIFO (%lu msgs):", (unsigned long)batch_msgs);
    for (uint32_t i = 0; i < batch_len; i++) printf(" %02x", batch[i]);
    printf("%s\n", ok ? "" : "  (write FAILED)");
#endif
#if N8_APU_LAT
    if (ok && batch_msgs == 1 && batch[0] == 0x90 && batch[2]) apu_latency_probe(batch_first_us);
#endif
    batch_len = batch_msgs = 0;
}

// TinyUSB's no-RTOS timing hook (no board layer), backed by the SDK clock.
extern "C" uint32_t tusb_time_millis_api(void) { return to_ms_since_boot(get_absolute_time()); }

static void hexdump(const char *label, const uint8_t *d, int n) {
    printf("[n8-host] %s:", label);
    for (int i = 0; i < n; i++) printf(" %02x", d[i]);
    printf("\n");
}

// CMD_STATUS: the N8's status byte, or -1 for no (or a malformed) reply.
static int n8_status(void) {
    uint8_t st = 0;
    return edio.status(st).ok() ? st : -1;
}

// What a failed Edio call or operation reports: the N8's own status byte when it refused, else -1 (no reply, or the
// link went down).
static int status_byte(const rp::n8::Status &s) { return s.code == rp::n8::Code::Device ? s.device : -1; }

// Run the Edio probe. Called from the main loop (not a callback) because the
// Edio calls block + pump tuh_task() internally.
static int n8_probe(void) {
    printf("[n8-host] --- Edio probe ---\n");

    // Status is 0 (OK) at the N8 menu; a running ROM reports a non-zero busy state
    // (e.g. 5) while the link stays fully functional - not an error. -1 = no reply.
    int st = n8_status();
    printf("[n8-host] CMD_STATUS -> %d %s\n", st,
           st == 0 ? "(OK)" : st < 0 ? "(no reply)" : "(busy - ROM running)");

    uint8_t info[Edio::SIZE_SYS_INFO];
    if (edio.sysInfo(info).ok()) {
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
    return st;
}

// Pump USB, feed the watchdog and run the LED for `ms` - the boot's waits, never a bare sleep.
static void wait_ms(uint32_t ms) {
    uint32_t until = to_ms_since_boot(get_absolute_time()) + ms;
    while ((int32_t)(to_ms_since_boot(get_absolute_time()) - until) < 0) {
        tuh_task();
        box_feed();
    }
}

// Autonomous boot: if the N8 file-browser MENU is running (it answers '*t' with 'k'), drive it
// over the cart FIFO (rp::n8::Menu, through bootRom) to install + boot a ROM, no PC in the loop. If a game is
// already running (or wedged) the menu won't answer, and we just forward MIDI to it.
// The handshake rides the cart-FIFO write (memWR to 0x1810000); it only works if nothing
// touched cart memory while the menu was booting - see n8_probe().
//
// Which ROM: with ROMs staged in the Pico's flash (`loop.sh --rom ...`, box.cpp), the one picked
// over MIDI if a pick is waiting, else the one saved on the SD card as last booted, else the
// first - uploaded to the SD unless the saved CRC says it is already there. With none staged,
// N8_ROM_PATH, a ROM already on the card (-DN8_ROM_PATH=usb-games/bliptoaster.nes for the
// current BlipToaster; the default is the older MIDI ROM, still named n8-midi.nes there).
#ifndef N8_ROM_PATH
#define N8_ROM_PATH "usb-games/n8-midi.nes"
#endif

// The menu's '*t' handshake: true iff it answered 'k'. Input is flushed first, so a stale byte is never taken for the
// answer.
static bool menu_test(void) {
    edio.flushInput();
    return rp::n8::Menu(edio).test().ok();
}

// Each step of a boot checks that the N8 it started on is still the one mounted: a power cut
// mid-boot unmounts it, and a new one can be up (at a fresh menu) before a slow step returns.
// The link is checked too: it is down from the unmount until the main loop binds the next N8,
// so a boot whose N8 went (even one remounted since, which bumps n8_gen only once the boot has
// read it, as inside the probe) stops at once rather than handshaking with a link that is gone.
#define BOOT_GONE() (n8_gen != gen || !n8_ready || !usb.linkUp())
#define ABANDON() do { printf("[n8-host] boot abandoned: the N8 went away\n"); return; } while (0)

// One boot through the pack's bootRom, and what the box does at its steps (boot_step): the lines it prints where it
// always printed them, its choice saved on the SD between the upload and the install, and the N8 checked before the
// install and before the start.
typedef struct {
    uint32_t            gen;         // the N8 mount the boot began on
    const rom_blob_hdr *rom;         // the staged ROM, or null for one already on the card
    const char         *path;        // what the menu installs
    int                 sel;         // the staged ROM's index
    const box_state    *save;        // the choice to write before the install (null: unchanged)
    bool                retry;       // the upload again after a refused install: nothing saved, no "menu up" line
    uint64_t            upload_us;   // when the upload began (0: no upload)
} boot_run;

static bool boot_step(void *ctx, rp::n8::Step step) {
    boot_run &run = *static_cast<boot_run *>(ctx);
    const uint32_t gen = run.gen;
    switch (step) {
        case rp::n8::Step::Upload:
            printf("[n8-host] upload %s (%lu bytes)...\n", run.rom->path, (unsigned long)run.rom->size);
            run.upload_us = time_us_64();
            return true;
        case rp::n8::Step::Install:
            if (run.upload_us)
                printf("[n8-host] uploaded in %lu ms\n", (unsigned long)((time_us_64() - run.upload_us) / 1000));
            if (run.retry) return true;
            // Saved only when it changed: an unchanged boot then writes nothing to the SD card, and a
            // card write is the one thing a power cut at the wrong moment can damage.
            if (run.save && !box_state_save(edio, run.save))
                printf("[box] could not save the choice to %s\n", BOX_STATE_PATH);
            if (run.rom) {
                box_set_pending_sel(-1);
                cur_sel = run.sel;
            }
            if (BOOT_GONE()) return false;
            printf("[n8-host] menu up -> install %s (*n)...\n", run.path);
            return true;
        case rp::n8::Step::Start:
            if (BOOT_GONE()) return false;
            printf("[n8-host] installed -> boot (*s); the ROM is starting...\n");
            return true;
        default:
            return true;
    }
}

// bootRom for the box: no handshake of its own (boot_rom has had the menu's 'k'), boot_step around its steps, the
// staged ROM uploaded when `upload`. What it came to, as boot_rom reads it: 0 once the ROM is installed and its '*s'
// sent, -2 when the upload failed (its line printed), the menu's status byte when it refused the install, -1 for
// anything else (no reply, the N8 gone). As the box always had it, a map index that never follows a 0 install status
// fails nothing (the '*s' goes out regardless), and nor does a '*s' that could not be written, which is
// fire-and-forget: the wait for the game and the N8 check after it judge the boot.
static int boot_through_menu(boot_run &run, bool upload) {
    const rp::n8::ByteView rom = upload ? rp::n8::ByteView{(const uint8_t *)(run.rom + 1), run.rom->size}
                                        : rp::n8::ByteView{};
    const rp::n8::Status s = rp::n8::bootRom(edio, run.path, rom, {}, upload ? rp::n8::Upload::Always
                                                                             : rp::n8::Upload::Never,
                                             nullptr, {}, {false, boot_step, &run});
    if (s.ok()) return 0;
    if (s.step == rp::n8::Step::Upload) {
        printf("[n8-host] upload FAILED (status %d)\n", status_byte(s));
        return -2;
    }
    if (s.code == rp::n8::Code::Cancelled) return -1;   // boot_step found the N8 gone
    if (s.step == rp::n8::Step::Start) return 0;
    if (s.step == rp::n8::Step::Install && s.where == rp::n8::Where::MapIndex) {
        if (!boot_step(&run, rp::n8::Step::Start)) return -1;
        (void)rp::n8::Menu(edio).start();
        return 0;
    }
    return status_byte(s);
}

static void boot_rom(int status) {
    const uint32_t gen = n8_gen;
    if (status > 0) {   // CMD_STATUS is 0 at the menu and non-zero while a game runs
        printf("[n8-host] a game is already running (status %d) - forwarding MIDI to it\n", status);
        return;
    }
    // The N8's USB (MCU) enumerates before the menu core is ready, so retry the handshake
    // for a few seconds after a fresh power-up.
    printf("[n8-host] menu handshake (*t)...\n");
    bool menu = false;
    for (int i = 0; i < N8_MENU_TRIES && !menu && !BOOT_GONE(); i++) {
        menu = menu_test();
        if (!menu) wait_ms(400);
    }
    if (BOOT_GONE()) ABANDON();
    if (!menu) {
        int now_st = n8_status();
        if (now_st == 0) {
            // CMD_STATUS says menu, but the menu does not answer: the console came back in a bad
            // state (seen after a power cut only ~2.7 s long). MIDI must not go into a menu's FIFO -
            // a 0x2A data byte is its '*' command prefix - so hold off, show the error, and try
            // the boot again in 20 s in case it comes alive. A power-cycle of the NES clears it.
            printf("[n8-host] the N8 menu is not answering *t - power-cycle the NES (retrying in 20 s)\n");
            boot_error = true;
            menu_stuck_until = to_ms_since_boot(get_absolute_time()) + 20000;
            return;
        }
        printf("[n8-host] no 'k', and CMD_STATUS %d - a game is running; forwarding MIDI to it\n", now_st);
        return;
    }

    boot_run run = {gen, NULL, N8_ROM_PATH, -1, NULL, false, 0};
    box_state st;
    bool upload = false;
    int n = rom_count();
    if (n > 0) {
        bool have = box_state_load(edio, &st);
        int sel = box_pending_sel();
        if (sel < 0) sel = have ? st.sel : 0;
        if (sel >= n) sel = 0;
        const rom_blob_hdr *rom = rom_get(sel);
        uint32_t crc = rom_crc32(rom);
        printf("[box] ROM %d of %d: %s (crc %08lx)%s\n", sel, n, rom->path, (unsigned long)crc,
               box_pending_sel() >= 0 ? " - picked over MIDI" : have ? " - saved choice" : "");
        if (have && st.crc == crc) {
            printf("[box] already on the SD (its CRC was saved at the last upload) - no upload\n");
        } else {
            upload = true;
        }
        if (!have || st.sel != sel || st.crc != crc) {
            memcpy(st.magic, "N8PB", 4);
            st.sel = (uint8_t)sel;
            memset(st.pad, 0, sizeof st.pad);
            st.crc = crc;
            st.reserved = 0;
            run.save = &st;
        }
        run.rom = rom;
        run.sel = sel;
        run.path = rom->path;
    }

    int result = boot_through_menu(run, upload);
    if (result == -2) { if (BOOT_GONE()) ABANDON(); boot_error = true; return; }
    if (result > 0 && result != 0x44 && run.rom && !upload) {   // the device's own status, not a lost link
        // The saved CRC said the file was on the card, but the menu could not load it - it was
        // deleted or replaced behind the box's back. Put it back and try once more.
        printf("[n8-host] install FAILED (status %d) - uploading it again\n", result);
        run.retry = true;
        result = boot_through_menu(run, true);
        if (result == -2) { if (BOOT_GONE()) ABANDON(); boot_error = true; return; }
    }
    if (BOOT_GONE()) ABANDON();
    if (result != 0) {
        printf("[n8-host] install FAILED (status %d)%s\n", result,
               result == 0x44 ? " - dirty menu heap, power-cycle to a fresh menu" : "");
        boot_error = true;
        return;
    }
    wait_ms(4000);   // let the NES reboot into the game
    if (BOOT_GONE()) ABANDON();
    printf("[n8-host] %s booted by the Pico - no PC used.\n", run.path);
}

// The bridge sink: a complete MIDI message from the parser -> the N8 FIFO. Channel-voice
// messages (0x80-0xEF) are forwarded, SysEx through midi_sysex_to_fifo(); system common and
// realtime bytes (clock / sensing / transport) are dropped. Aftertouch is dropped too:
// poly-aftertouch (0xA0) and channel-pressure (0xD0) are high-rate continuous streams (a
// Launchpad floods channel-pressure while any pad is held), and no ROM here uses them -
// BlipToaster's parser reads and ignores both, and the older n8-midi.nes HUNG on that flood.
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
    if (batch_len == 0) batch_first_us = cur_byte_us;
    batch[batch_len++] = m->status;
    batch[batch_len++] = m->data0;
    if (n == 3) batch[batch_len++] = m->data1;
    batch_msgs++;
}

// SysEx, byte by byte from the parser (F0, payload, F7 - a terminator is synthesised if another
// status cuts one short), into the same batch as the channel messages so order is kept. BlipToaster
// takes N163 waves, GM System On and its settings this way. A long SysEx simply spans batches: the
// ROM reads one byte stream. One whose start was dropped (the N8 not ready) is dropped whole.
static bool sysex_open = false;
static void sysex_emit(uint8_t b) {
    if (b == 0xF0) sysex_open = forward_ok;
    if (!sysex_open) { if (b == 0xF7) stats.not_ready++; return; }
    if (batch_len + 1 > BATCH_MAX) batch_flush();
    if (batch_len == 0) batch_first_us = cur_byte_us;
    batch[batch_len++] = b;
    if (b == 0xF7) { stats.sysex++; batch_msgs++; sysex_open = false; }
}

// The box's own commands: F0 7D 50 <cmd> ... F7 (7D = non-commercial, 50 = 'P' for Pico;
// BlipToaster's are 7D 42). They are acted on here and never reach the ROM.
//   F0 7D 50 01 <n> F7   boot staged ROM n at the next menu (power-cycle the NES, or RESET if the N8 returns to its menu on RESET);
//                        the LED blinks n+1 times to acknowledge
//   F0 7D 50 02 F7       report the staged ROMs and the choice on the console; blink current+1
// Test builds (-DN8_TEST_HOOKS=1) also take 7E (hang: the watchdog must reboot) and 7D (treat the
// link as dead: the health check must recover).
static bool fake_dead_link = false;
static void box_command(const uint8_t *c, int n) {
    if (n < 1) return;
    int count = rom_count();
    if (c[0] == 0x01 && n >= 2) {
        if (c[1] >= count) {
            printf("[box] select %u: only %d ROM(s) staged\n", c[1], count);
            led_count(10);
            return;
        }
        box_set_pending_sel(c[1]);
        printf("[box] ROM %u selected (%s) - boots at the next menu: power-cycle the NES\n",
               c[1], rom_get(c[1])->path);
        led_count(c[1] + 1u);
    } else if (c[0] == 0x02) {
        printf("[box] %d ROM(s) staged; booted: %d; picked for the next menu: %d\n",
               count, cur_sel, box_pending_sel());
        for (int i = 0; i < count; i++)
            printf("[box]   %d: %s (%lu bytes)\n", i, rom_get(i)->path, (unsigned long)rom_get(i)->size);
        if (cur_sel >= 0) led_count((unsigned)cur_sel + 1u);
#if N8_TEST_HOOKS
    } else if (c[0] == 0x7E) {
        printf("[box] test: hanging - the watchdog should reboot within 3 s\n");
        for (;;) tight_loop_contents();
    } else if (c[0] == 0x7D) {
        printf("[box] test: treating the link as dead\n");
        fake_dead_link = true;
#endif
    }
}

static uint8_t sx_head[3], sx_cmd[8];
static int     sx_head_n = 0, sx_cmd_n = 0;
static bool    sx_box = false;
static void midi_sysex_to_fifo(uint8_t b, void *user) {
    (void)user;
    if (b == 0xF0) { sx_head_n = 0; sx_box = false; }
    if (sx_head_n < 3) {
        // Hold the F0 and the two ID bytes until it is known whose SysEx this is.
        sx_head[sx_head_n++] = b;
        if (b == 0xF7 || sx_head_n == 3) {
            sx_box = sx_head_n == 3 && sx_head[1] == 0x7D && sx_head[2] == 0x50;
            sx_cmd_n = 0;
            if (!sx_box) for (int i = 0; i < sx_head_n; i++) sysex_emit(sx_head[i]);
            if (b == 0xF7) sx_head_n = 3;   // ended inside the header: nothing more to hold
        }
        return;
    }
    if (!sx_box) { sysex_emit(b); return; }
    if (b == 0xF7) box_command(sx_cmd, sx_cmd_n);
    else if (sx_cmd_n < (int)sizeof sx_cmd) sx_cmd[sx_cmd_n++] = b;
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
    printf("[stats] rx=%lu msgs=%lu sysex=%lu fwd=%lu fail=%lu writes=%lu maxbatch=%lu filtered=%lu "
           "notready=%lu oe=%lu fe=%lu ringfull=%lu maxfwd=%luus lat=%lu/%luus\n",
           (unsigned long)stats.rx_bytes, (unsigned long)stats.msgs, (unsigned long)stats.sysex,
           (unsigned long)stats.fwd,
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
    if (!edio.memRD(Edio::ADDR_SSR + 0x80, r, sizeof r).ok()) { printf("[sniff] read FAIL\n"); return; }
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

// The N8 mixes expansion audio (VRC6 / VRC7 / N163 / Sunsoft 5B / MMC5) through an FPGA master
// volume; at 0 every expansion voice is silent, which reads exactly like a dead chip. The PC tools
// warn it is 0 after a power-cycle and set it (`n8-play --exp-vol`, defaulting to unity for these
// mappers). On this rig the N8 OS set it to 129-130 itself at every hand-over, so this is a guard
// rather than the usual path: the same rule, run whenever forwarding starts - after the Pico booted
// a ROM, or on finding one already running. It reads the live config block the N8 OS writes when it
// hands a game over (system region, not a cart-bus DMA), so it covers a ROM booted from the SD path
// as well as a staged one, and it only raises a volume still at 0: a deliberate setting is kept.
#ifndef N8_EXP_VOL
#define N8_EXP_VOL 128   // unity (0 mute, 255 = 2x)
#endif
static const char *expansion_chip(uint16_t mapper) {
    switch (mapper) {
        case 5:  return "MMC5";
        case 19: return "N163";
        case 24: case 26: return "VRC6";
        case 69: return "Sunsoft 5B";
        case 85: return "VRC7";
        default: return NULL;
    }
}

static void exp_vol_check(void) {
    uint8_t cfg[Edio::SIZE_CFG];
    if (!edio.memRD(Edio::ADDR_CFG, cfg, sizeof cfg).ok()) { printf("[n8-host] config read FAILED\n"); return; }
    uint16_t mapper = (uint16_t)(((cfg[2] >> 4) << 8) | cfg[0]);
    bool game = (cfg[7] & 0x80) != 0;   // ctrl bit 7: a game core is configured (the menu is map 255)
    const char *chip = expansion_chip(mapper);
    if (!game || !chip) return;
    if (cfg[3] != 0) {
        printf("[n8-host] mapper %u (%s): expansion volume already %u\n", mapper, chip, cfg[3]);
        return;
    }
    uint8_t v = N8_EXP_VOL;
    bool ok = edio.memWR(Edio::ADDR_EXP_VOL, &v, 1).ok();
    uint8_t back = 0;   // a live register: read it back to confirm the write took
    if (ok) ok = edio.memRD(Edio::ADDR_EXP_VOL, &back, 1).ok() && back == v;
    printf("[n8-host] mapper %u (%s): expansion volume 0 -> %u%s\n", mapper, chip, back,
           ok ? "" : " (write FAILED)");
}

// --- Recovery triggers and the menu coming back ---------------------------------------------------

static void recover(int reason) {
    if (box_may_recover()) box_recover(reason);   // backing off: leave it, the LED shows the error
}

// Something is on the socket but never enumerated (a stuck enumeration leaves the host stack
// waiting for a disconnect that never comes), or the writes keep failing: a fresh USB host clears
// both, and a Pico-only reboot re-enumerates a powered N8 reliably.
// When the current "attached, nothing enumerated" stretch began (0 = not in one). Reset by the
// mount callbacks themselves: link_watch() can go unrun for seconds while a boot blocks, and a
// start time left over from before that boot read as 13 s without enumeration (a false reboot).
static volatile uint32_t attached_since_ms = 0;

static void link_watch(void) {
    uint32_t now = to_ms_since_boot(get_absolute_time());
#ifndef USE_NATIVE_USB
    bool attached = pio_usb_root_attached();
#else
    bool attached = false;
#endif
    if (attached && !any_mounted) {
        if (!attached_since_ms) attached_since_ms = now;
        else if (now - attached_since_ms > 8000) recover(RECOVER_NO_ENUM);
    } else {
        attached_since_ms = 0;
    }
    if (write_fail_streak >= 5) recover(RECOVER_WRITES_FAIL);
}

// Once a second, while forwarding with MIDI quiet: is the N8 still answering, and is a game still
// running? CMD_STATUS is 0 at the menu and non-zero while a game runs, so 0 after a game has been
// seen means the console went back to the menu (RESET) - boot the chosen ROM again.
static bool game_seen = false;
static void health_check(void) {
    static uint32_t next_ms = 0;
    static int fails = 0;
    if (!midi_quiet()) return;
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if ((int32_t)(now - next_ms) < 0) return;
    next_ms = now + 1000;
    int st = fake_dead_link ? -1 : n8_status();
    if (st < 0) {
        printf("[box] the N8 did not answer CMD_STATUS (%d in a row)\n", ++fails);
        if (fails >= 3) recover(RECOVER_LINK_DEAD);
        return;
    }
    fails = 0;
    box_note_healthy();
    if (st != 0) { game_seen = true; return; }
    if (game_seen) {
        printf("[box] the N8 is back at its menu - booting the chosen ROM\n");
        game_seen = false;
        probed = false;   // the main loop re-runs the probe and the menu boot
    }
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

    led_init();
    box_start();                        // says why we (re)booted, then arms the watchdog

    // The node types this image registers (retroplug_product in CMakeLists.txt): rp.n8.nes, which nothing plans here
    // yet, named so that the node is linked into the image the trap judges, as a device image's types must be.
    size_t package_count = 0;
    const rp_package *const *packages = rp_register_packages(&package_count);
    for (size_t i = 0; i < package_count; i++)
        for (uint32_t t = 0; t < packages[i]->node_type_count; t++)
            printf("[n8-host] node type %s (registered, not planned)\n", packages[i]->node_types[t]->id);

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
    midi_parser_set_sysex_sink(&parser, midi_sysex_to_fifo);
    bool was_forwarding = false;

#ifdef USE_NATIVE_USB
    tuh_init(0);   // RP2350 native USB host controller (its own D+/D- pins)
#else
    // N8 USB host on PIO-USB (GP2/GP3).
    pio_usb_configuration_t pio_cfg = PIO_USB_DEFAULT_CONFIG;
    pio_cfg.pin_dp = PIN_USB_DP;
    pio_cfg.skip_alarm_pool = true;   // its pool is on the heap: SofTimer.cpp drives the 1 ms frames instead
    tuh_configure(1, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &pio_cfg);
    tuh_init(1);
    sof_timer_start();
#endif

    while (true) {
        box_feed();
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
            led_set_mode(LED_BUSY);   // the boot blocks for seconds: show it now, not after
            probed = true;
            usb.bind(n8_cdc_idx);     // here, at the top of the loop: never inside an Edio call's wait
            int status = n8_probe();
            game_seen = false;
            boot_rom(status);
        }
        if (menu_stuck_until && (int32_t)(to_ms_since_boot(get_absolute_time()) - menu_stuck_until) >= 0) {
            menu_stuck_until = 0;
            boot_error = false;
            probed = false;   // try the menu boot again
        }
        forward_ok = n8_ready && probed && !menu_stuck_until;
        // Forwarding just became possible (after a mount, and the up-to-48 s menu boot): the ring
        // may hold seconds of MIDI from before the N8 could take it, and playing that stale burst
        // into the ROM now would only produce surprise notes. Start from a clean stream.
        if (forward_ok && !was_forwarding) {
            rx_tail = rx_head;
            midi_parser_init(&parser, midi_to_fifo, NULL);
            midi_parser_set_sysex_sink(&parser, midi_sysex_to_fifo);
            sysex_open = false;
            exp_vol_check();
        }
        was_forwarding = forward_ok;
        if (forward_ok) { sniff_report(); health_check(); }
        link_watch();
        led_set_mode(boot_error ? LED_ERROR : !n8_ready ? LED_NO_N8 : !forward_ok ? LED_BUSY : LED_READY);
        stats_report();
    }
}

extern "C" void tuh_mount_cb(uint8_t daddr) {
    uint16_t vid = 0, pid = 0;
    tuh_vid_pid_get(daddr, &vid, &pid);
    any_mounted = true;
    attached_since_ms = 0;
    printf("[n8-host] device mounted: addr %u  VID:PID %04x:%04x%s\n",
           daddr, vid, pid, (vid == N8_VID && pid == N8_PID) ? "   <- N8!" : "");
}

extern "C" void tuh_umount_cb(uint8_t daddr) {
    printf("[n8-host] device %u unmounted\n", daddr);
    any_mounted = false;
    attached_since_ms = 0;   // a new stretch starts with the next link_watch()
    n8_ready = false;
    probed = false;
}

// The N8's CDC-ACM interface mounted -> note its index and arm the probe, which binds the transport to it. Not here:
// this runs inside tuh_task, so inside an Edio call's wait when one is in progress, and binding the new N8 there would
// let that call carry on, mid-command, on it (UsbCdcTransport.hpp).
extern "C" void tuh_cdc_mount_cb(uint8_t idx) {
    tuh_itf_info_t info = {};
    tuh_cdc_itf_get_info(idx, &info);
    uint16_t vid = 0, pid = 0;
    tuh_vid_pid_get(info.daddr, &vid, &pid);
    printf("[n8-host] CDC mounted (idx %u, VID:PID %04x:%04x)\n", idx, vid, pid);
    if (vid == N8_VID && pid == N8_PID) {
        n8_cdc_idx = idx;
        menu_stuck_until = 0;
        n8_gen = n8_gen + 1;
        boot_error = false;   // a fresh mount gets a fresh boot
        n8_ready = true;
        probed = false;
    }
}

extern "C" void tuh_cdc_umount_cb(uint8_t idx) {
    (void)idx;
    usb.unbind();
    n8_ready = false;
    probed = false;
}
