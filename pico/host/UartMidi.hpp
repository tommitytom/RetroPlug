#pragma once

// A box's MIDI UART (docs/modular-graph-phase-e3-design.md section 5): MIDI IN on a hardware UART, read by an interrupt
// into a ring of bytes, each with the microsecond it was taken off the UART, so bytes keep arriving while the main loop
// is blocked in a USB write (about 5 ms each). Polled, a burst overran the UART's 32-byte hardware FIFO in about 10 ms
// and lost some 70% of its bytes. The main loop drains the ring into the staging (MidiStaging), which parses it.
//
// The interrupt is the only writer of the ring's head, the last arrival and the counters: run as the ISR, or by poll()
// with the interrupt masked. The main loop is the only writer of the tail.

#include <cstdint>

#include "MidiStaging.hpp"
#include "hardware/uart.h"

namespace rp::box {

// The ring: one block's worth of bytes, rp.device.midi-in's (RP_DEVICE_MIDI_IN_BYTES), which at the 31250 baud wire
// is 1.3 s of MIDI held while the main loop is busy.
inline constexpr std::uint32_t kUartRing = 4096;
static_assert(kUartRing == RP_DEVICE_MIDI_IN_BYTES, "the UART ring holds what rp.device.midi-in takes in a block");

struct UartMidiCounts {
    std::uint32_t rx;        // bytes off the UART
    std::uint32_t overrun;   // bytes lost to a full UART hardware FIFO
    std::uint32_t framing;   // framing / break errors (wiring, or a Type A/B swap)
    std::uint32_t ringFull;  // bytes lost to a full ring
};

// MIDI IN on `uart`'s RX pin `rxPin` at `baud`, 8N1, its interrupt at the lowest priority (it only copies bytes, and
// must never delay PIO-USB's own timer interrupt).
void uartMidiInit(uart_inst_t* uart, unsigned rxPin, unsigned baud);
// Reads what the UART holds now, as the interrupt would: the idle main loop calls it each pass, since a 3-byte message
// is under the receive interrupt's 4-byte threshold and would otherwise wait about 1 ms for the receive timeout.
void uartMidiPoll();
// The ring into the staging, oldest first, until it is empty or the staging refuses a byte (which stays for the next
// pass).
void uartMidiDrain(MidiStaging& staging);
// Drops what the ring holds: stale MIDI, from before the box could forward it.
void uartMidiDiscard();
// When the last byte arrived (the low 32 bits of the microsecond clock).
std::uint32_t uartMidiLastRx();
UartMidiCounts uartMidiCounts();

}  // namespace rp::box
