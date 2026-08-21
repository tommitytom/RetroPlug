// TinyUSB config for the N8 USB-host stage: host-only, PIO-USB root port,
// one CDC-ACM interface (the Everdrive N8 is a class-compliant CDC-ACM device,
// so no FTDI/CP210x/CH34x vendor drivers are needed). See pico/n8-host/README.md.
#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#ifndef CFG_TUSB_MCU
#error CFG_TUSB_MCU must be defined
#endif

#define CFG_TUSB_OS            OPT_OS_NONE
#define CFG_TUSB_DEBUG         0     // bump to 2 to trace USB enumeration on the console

//------------- Host stack: native USB (rhport 0) or PIO-USB (rhport 1) -------------//
// -DRP_NATIVE_USB=ON routes the host controller to the RP2350's SILICON USB controller
// (rhport 0, the Pico's native USB pins) instead of the software PIO-USB (rhport 1). The
// native controller is a hardware host like a PC's, so its wire timing may drive the N8's
// cart FIFO where PIO-USB can't (pico-n8-fifo-write-bug.md).
#define CFG_TUH_ENABLED        1
#ifdef USE_NATIVE_USB
#define CFG_TUH_RPI_PIO_USB    0     // native USB host controller
#define BOARD_TUH_RHPORT       0
#else
#define CFG_TUH_RPI_PIO_USB    1     // route the host controller to PIO-USB
#define BOARD_TUH_RHPORT       1     // PIO-USB is roothub port 1 (native = 0)
#endif
#define CFG_TUH_MAX_SPEED      OPT_MODE_FULL_SPEED

#define CFG_TUH_ENUMERATION_BUFSIZE 256
#define CFG_TUH_HUB            0     // N8 plugs in direct, no hub
#define CFG_TUH_DEVICE_MAX     1

#define CFG_TUH_CDC            1     // the N8 (CDC-ACM)
#define CFG_TUH_CDC_FTDI       0
#define CFG_TUH_CDC_CP210X     0
#define CFG_TUH_CDC_CH34X      0
#define CFG_TUH_HID            0
#define CFG_TUH_MSC            0
#define CFG_TUH_VENDOR        0

#define CFG_TUH_CDC_RX_BUFSIZE 1024
#define CFG_TUH_CDC_TX_BUFSIZE 1024

// The N8 ignores baud on its USB CDC; set DTR/RTS + a line coding on enum anyway (harmless).
#define CFG_TUH_CDC_LINE_CONTROL_ON_ENUM  0x03
#define CFG_TUH_CDC_LINE_CODING_ON_ENUM   { 9600, CDC_LINE_CODING_STOP_BITS_1, CDC_LINE_CODING_PARITY_NONE, 8 }

#endif // _TUSB_CONFIG_H_
