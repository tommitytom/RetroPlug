#pragma once

#include <cstdint>

#include "system/mesen/NesEverdriveFifo.hpp"
#include "transport/MidiTypes.hpp"

class NesConsole;

// EverDrive N8 Pro FIFO emulator wrapper. Attaches to a NesConsole's memory
// manager so the ROM's reads/writes at $40F0/$40F1 reach the FIFO; pumps
// host MIDI bytes (delivered by Project::dispatchMidi → MesenNesSystem::onMidi)
// into the FIFO's RX queue so n8-midi.nes's `midiRead()` polling loop sees
// them.
//
// Currently the only Mesen-side "role". Always attached when MesenNesSystem
// activates with a NES ROM — the FIFO is benign if the ROM never touches
// $40F0/$40F1 (most NES homebrew). If non-N8 NES ROMs become a real concern,
// gate attachment on an iNES mapper-byte sniffer.
class NesN8MidiRole {
public:
    NesN8MidiRole();
    ~NesN8MidiRole();

    // Register the FIFO with `console`'s memory manager so memory accesses at
    // $40F0/$40F1 are routed through it. Called once, on the audio thread,
    // from MesenNesSystem::onActivate after the ROM has loaded.
    void onAttach(NesConsole& console);

    // Audio-thread: forward each event's bytes to the FIFO RX queue. The ROM
    // polls `$40F1` bit 7 (FIFO_MOS_RXF: set = no data); reading `$40F0`
    // pops the next byte. n8-midi.nes consumes only the data bytes after a
    // status byte; we forward verbatim and let the ROM parse it.
    void onMidi(const ::MidiEvent* events, std::uint32_t count);

private:
    rp::NesEverdriveFifo fifo_;
};
