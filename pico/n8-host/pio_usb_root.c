// The PIO-USB root port's attach state, for main.cpp: pio_usb_ll.h is C that does not compile as C++ (an inline
// function stores an int into an enum), so the box reads the one field it needs through this.
#include <stdbool.h>

#include "pio_usb_ll.h"

bool pio_usb_root_attached(void) { return PIO_USB_ROOT_PORT(0)->connected; }
