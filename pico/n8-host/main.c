// N8 standalone bridge - stage 2, slice 2.1: USB host + Edio status handshake.
//
// The Pico acts as a USB host (via PIO-USB) and talks the Everdrive N8 "Edio"
// protocol to it over USB CDC-ACM. This slice proves the hard part: that PIO-USB
// can enumerate the N8 and that a framed Edio command round-trips. On CDC mount
// it sends CMD_STATUS (2B D4 10 EF) and expects the 2-byte reply <code> A5.
//
// Wiring (USB-A host socket on this Pico):
//   D+   -> GP2        D-   -> GP3   (must be consecutive; PIO-USB drives them)
//   VBUS -> Pico VBUS (pin 40, 5V)  GND  -> Pico GND
//   Then plug the N8's USB cable into this socket.
//   (Console stays on UART0/GP0-GP1 -> the debug probe -> /dev/ttyDbgProbe.)
//
// PIO-USB needs a 120 MHz-multiple system clock (set_sys_clock_khz below).

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "pio_usb.h"
#include "tusb.h"

#define PIN_USB_DP 2          // GP2 = D+, GP3 = D-
#define N8_VID     0x38df
#define N8_PID     0x0017

// Edio framing: every command is `2B D4 <cmd> <cmd^FF>`. CMD_STATUS = 0x10.
static const uint8_t EDIO_STATUS[4] = { 0x2b, 0xd4, 0x10, 0xef };

static uint8_t reply[2];
static int     reply_len = 0;
static bool    awaiting  = false;

// TinyUSB's no-RTOS timing hook (normally supplied by its board layer, which we
// don't use). Back it with the SDK clock.
uint32_t tusb_time_millis_api(void) {
    return to_ms_since_boot(get_absolute_time());
}

int main(void) {
    set_sys_clock_khz(120000, true);   // PIO-USB timing: sysclk must be a 12/120 MHz multiple
    stdio_init_all();                  // console -> UART0/GP0-GP1 (debug probe)

#ifdef PICO_DEFAULT_LED_PIN
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
#endif

    sleep_ms(100);
    printf("\n[n8-host] USB host up (D+=GP%d, D-=GP%d). Waiting for the N8...\n",
           PIN_USB_DP, PIN_USB_DP + 1);

    pio_usb_configuration_t pio_cfg = PIO_USB_DEFAULT_CONFIG;
    pio_cfg.pin_dp = PIN_USB_DP;
    tuh_configure(1, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &pio_cfg);
    tuh_init(1);                       // roothub port 1 = PIO-USB

    while (true) {
        tuh_task();
    }
}

// Any USB device enumerated.
void tuh_mount_cb(uint8_t daddr) {
    uint16_t vid = 0, pid = 0;
    tuh_vid_pid_get(daddr, &vid, &pid);
    printf("[n8-host] device mounted: addr %u  VID:PID %04x:%04x%s\n",
           daddr, vid, pid, (vid == N8_VID && pid == N8_PID) ? "   <- N8!" : "");
}

void tuh_umount_cb(uint8_t daddr) {
    printf("[n8-host] device %u unmounted\n", daddr);
    awaiting = false;
    reply_len = 0;
#ifdef PICO_DEFAULT_LED_PIN
    gpio_put(PICO_DEFAULT_LED_PIN, 0);
#endif
}

// The N8's CDC-ACM interface mounted -> fire the Edio status handshake.
void tuh_cdc_mount_cb(uint8_t idx) {
    tuh_itf_info_t info = { 0 };
    tuh_cdc_itf_get_info(idx, &info);
    uint16_t vid = 0, pid = 0;
    tuh_vid_pid_get(info.daddr, &vid, &pid);
    printf("[n8-host] CDC mounted (idx %u, addr %u, VID:PID %04x:%04x)\n",
           idx, info.daddr, vid, pid);

    if (vid == N8_VID && pid == N8_PID) {
#ifdef PICO_DEFAULT_LED_PIN
        gpio_put(PICO_DEFAULT_LED_PIN, 1);
#endif
        printf("[n8-host] -> Edio CMD_STATUS (2b d4 10 ef)\n");
        reply_len = 0;
        awaiting  = true;
        tuh_cdc_write(idx, EDIO_STATUS, sizeof EDIO_STATUS);
        tuh_cdc_write_flush(idx);
    }
}

void tuh_cdc_umount_cb(uint8_t idx) {
    (void)idx;
    printf("[n8-host] CDC unmounted\n");
    awaiting = false;
    reply_len = 0;
}

// Bytes back from the N8.
void tuh_cdc_rx_cb(uint8_t idx) {
    uint8_t buf[64];
    uint32_t n = tuh_cdc_read(idx, buf, sizeof buf);
    for (uint32_t i = 0; i < n; i++) {
        printf("[n8-host] rx %02x\n", buf[i]);
        if (awaiting && reply_len < 2) {
            reply[reply_len++] = buf[i];
            if (reply_len == 2) {
                awaiting = false;
                if (reply[1] == 0xa5)
                    printf("[n8-host] *** N8 STATUS OK (code=%02x) - Edio handshake works! ***\n",
                           reply[0]);
                else
                    printf("[n8-host] unexpected status reply: %02x %02x\n", reply[0], reply[1]);
            }
        }
    }
}
