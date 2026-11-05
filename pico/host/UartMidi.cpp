#include "UartMidi.hpp"

#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "pico/time.h"

namespace rp::box {

namespace {

uart_inst_t*               gUart = nullptr;
std::uint8_t               gRing[kUartRing];
std::uint32_t              gStamp[kUartRing];  // time_us_32() each byte was taken off the UART
volatile std::uint32_t     gHead = 0, gTail = 0;
volatile std::uint32_t     gRx = 0, gOverrun = 0, gFraming = 0, gRingFull = 0;
volatile std::uint32_t     gLastRx = 0;

void onUartIrq() {
    const std::uint32_t now = time_us_32();
    while (uart_is_readable(gUart)) {
        // Read DR whole: bits 8-11 flag this byte's framing / parity / break / overrun.
        const std::uint32_t dr = uart_get_hw(gUart)->dr;
        if (dr & UART_UARTDR_OE_BITS) gOverrun = gOverrun + 1;
        if (dr & (UART_UARTDR_FE_BITS | UART_UARTDR_BE_BITS)) {
            gFraming = gFraming + 1;
            continue;
        }
        const std::uint32_t h = gHead, next = (h + 1) % kUartRing;
        if (next == gTail) {
            gRingFull = gRingFull + 1;
            continue;
        }
        gRing[h] = static_cast<std::uint8_t>(dr);
        gStamp[h] = now;
        __dmb();  // the byte is in the ring before the main loop can see the new head
        gHead = next;
        gRx = gRx + 1;
        gLastRx = now;
    }
}

}  // namespace

void uartMidiInit(uart_inst_t* uart, unsigned rxPin, unsigned baud) {
    gUart = uart;
    uart_init(uart, baud);
    gpio_set_function(rxPin, GPIO_FUNC_UART);
    uart_set_format(uart, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(uart, true);
    irq_set_exclusive_handler(UART_IRQ_NUM(uart), onUartIrq);
    irq_set_priority(UART_IRQ_NUM(uart), PICO_LOWEST_IRQ_PRIORITY);
    irq_set_enabled(UART_IRQ_NUM(uart), true);
    uart_set_irqs_enabled(uart, true, false);
}

void uartMidiPoll() {
    irq_set_enabled(UART_IRQ_NUM(gUart), false);
    onUartIrq();
    irq_set_enabled(UART_IRQ_NUM(gUart), true);
}

void uartMidiDrain(MidiStaging& staging) {
    while (gTail != gHead) {
        const std::uint32_t t = gTail;
        if (!staging.feed(gRing[t], gStamp[t])) return;
        __dmb();  // the byte is read before the interrupt may reuse its slot
        gTail = (t + 1) % kUartRing;
    }
}

void uartMidiDiscard() { gTail = gHead; }

std::uint32_t uartMidiLastRx() { return gLastRx; }

UartMidiCounts uartMidiCounts() { return UartMidiCounts{gRx, gOverrun, gFraming, gRingFull}; }

}  // namespace rp::box
