#include "SofTimer.hpp"

#include <cstdint>

#include "hardware/irq.h"
#include "hardware/sync.h"
#include "hardware/timer.h"
#include "pico/platform.h"
#include "pico/time.h"
#include "pio_usb.h"

// What alarm_pool_create(2, 1) and Pico-PIO-USB's alarm_pool_add_repeating_timer_us(pool, -1000, sof_timer, ...) gave
// the frame, read from Pico SDK 2.1.1 (pico_time/time.c and pico_time_adapter's time_adapter.h), and how each is kept:
//   - the alarm and its IRQ: hardware alarm 2 of the default timer (alarm_pool_get_default_timer()), TIMER0_IRQ_2 on
//     the RP2350, claimed, its IRQ an exclusive handler, its bit set in INTE (ta_enable_irq_handler). The same here.
//   - the priority: the pool sets none, so its IRQ keeps the PICO_DEFAULT_IRQ_PRIORITY (0x80) the runtime gives every
//     IRQ at boot. Set here explicitly to that value, above the MIDI UART's PICO_LOWEST_IRQ_PRIORITY, as before.
//   - the cadence: a negative delay repeats "from the last fire time", so each frame's target is the last target plus
//     1000 us, on an absolute schedule that never drifts by the handler's own time; a handler that finds the next
//     target already passed (it ran late) fires again at once, in the same IRQ, until it is ahead (the pool's do-while,
//     which also keeps a past target from being armed: the alarm matches on the timer's low 32 bits, so it would next
//     fire after a wrap). The first frame is 1000 us after the start (make_timeout_time_us(1000)), armed from the IRQ
//     itself, which the pool forces to insert a new alarm. The same here, the force bit cleared on entry as the pool's
//     handler does, and the SEV at its end kept (it wakes a WFE on the other core; the box has none, and it costs
//     nothing).
//   - the code path: the pool's handler ran from flash and called sof_timer and pio_usb_host_frame, both in RAM; this
//     handler is in RAM too, so a frame never waits on a flash fetch.
namespace {

constexpr unsigned     kAlarm   = 2;
constexpr std::int64_t kFrameUs = 1000;

// The next frame's time in microseconds since boot: the pool entry's `target`. Written only by the IRQ once started.
std::int64_t gTarget = 0;

std::int64_t now() { return static_cast<std::int64_t>(timer_time_us_64(timer_hw)); }

void __not_in_flash_func(sofIrq)() {
    hw_clear_bits(&timer_hw->intf, 1u << kAlarm);
    std::int64_t target = 0;
    do {
        timer_hw->intr = 1u << kAlarm;
        if (now() - gTarget >= 0) {
            pio_usb_host_frame();
            gTarget += kFrameUs;
        }
        target                  = gTarget;
        timer_hw->alarm[kAlarm] = static_cast<std::uint32_t>(target);
    } while (target - now() <= 0);
    __sev();
}

}  // namespace

void sof_timer_start() {
    hardware_alarm_claim(kAlarm);  // a second claim panics, as the pool's did
    const unsigned irq = hardware_alarm_get_irq_num(kAlarm);
    timer_hw->armed    = 1u << kAlarm;  // disarmed until the first target is set
    gTarget            = now() + kFrameUs;  // before the IRQ can run, so the force below is all that starts it
    irq_set_exclusive_handler(irq, sofIrq);
    irq_set_priority(irq, PICO_DEFAULT_IRQ_PRIORITY);
    irq_set_enabled(irq, true);
    hw_set_bits(&timer_hw->inte, 1u << kAlarm);
    hw_set_bits(&timer_hw->intf, 1u << kAlarm);  // the IRQ arms the first frame
}

// Pico-PIO-USB's init calls alarm_pool_create(2, 1) unless its config skips the pool, so the call is in the image even
// though the box's config skips it, and the SDK's pool is made with malloc and calloc, which the link trap refuses.
// CMakeLists.txt therefore wraps alarm_pool_create_on_timer (what the inline alarm_pool_create calls) to this, which
// never runs: reaching it means the configuration lost skip_alarm_pool, and the box would have no frames.
extern "C" alarm_pool_t* __wrap_alarm_pool_create_on_timer(alarm_pool_timer_t*, uint, uint) {
    panic("alarm_pool_create: the box's PIO-USB config skips the pool (SofTimer.cpp drives the frames)");
}
