// Pico-PIO-USB's 1 ms start-of-frame timer, made with nothing allocated (docs/modular-graph-phase-e2-design.md section
// 8). The host runs every transaction from pio_usb_host_frame(), which something must call once a millisecond: its own
// init makes an alarm pool for that, alarm_pool_create(2, 1), on the heap. The box's image has no heap (the link trap,
// CMakeLists.txt), so its configuration skips the pool (skip_alarm_pool) and the box drives the frame itself, from
// hardware alarm 2 on the default timer, with the IRQ, the priority and the absolute 1000 us cadence the pool's
// repeating timer had (SofTimer.cpp says how each is matched).
#pragma once

// Claims hardware alarm 2 and starts the frames, the first 1000 us from now. Once, after tuh_init() has initialised
// the PIO-USB host (pio_usb_host_frame() reads its state).
void sof_timer_start();
