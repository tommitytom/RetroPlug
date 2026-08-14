// N8 standalone bridge - stage 2, slice 2.2: Edio protocol port.
//
// The Pico hosts the N8 over PIO-USB (slice 2.1) and now speaks a real slice of
// the Edio protocol (edio.c): on mount it runs a probe - CMD_STATUS, SYS_INF
// (decoded device info), and a read-only memRD of the cart SRM. Proves the
// framed command + bulk-read paths end to end. Writes (memWR) are exercised
// save-safely via fifoWR in the next slice.
//
// Wiring + build: see README.md. USB-A host socket D+=GP2/D-=GP3, VBUS=pin40,
// GND=pin38; console on UART0/GP0-GP1 -> the debug probe -> /dev/ttyDbgProbe.

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "pio_usb.h"
#include "tusb.h"
#include "edio.h"

#define PIN_USB_DP 2          // GP2 = D+, GP3 = D-
#define N8_VID     0x38df
#define N8_PID     0x0017

static volatile bool n8_ready = false;   // set by the CDC mount callback
static bool          probed   = false;   // run the probe once per mount

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

    int st = edio_get_status();
    printf("[n8-host] CMD_STATUS -> %d %s\n", st, (st == 0) ? "(OK)" : "(error)");

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

    printf("[n8-host] --- probe done ---\n");
}

int main(void) {
    set_sys_clock_khz(120000, true);   // PIO-USB needs a 120 MHz-multiple clock
    stdio_init_all();
    sleep_ms(100);
    printf("\n[n8-host] USB host up (D+=GP%d, D-=GP%d). Waiting for the N8...\n",
           PIN_USB_DP, PIN_USB_DP + 1);

    pio_usb_configuration_t pio_cfg = PIO_USB_DEFAULT_CONFIG;
    pio_cfg.pin_dp = PIN_USB_DP;
    tuh_configure(1, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &pio_cfg);
    tuh_init(1);

    while (true) {
        tuh_task();
        if (n8_ready && !probed) {
            probed = true;
            n8_probe();
        }
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
