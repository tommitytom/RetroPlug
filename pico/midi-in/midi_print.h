// A `midi_sink` that prints one human-readable line per MIDI message via printf.
// Pure C (printf/snprintf only) so it is shared by the firmware and the offline
// host test. Clock (0xF8) and active-sensing (0xFE) are filtered out - they are
// high-rate and would drown the log; transport (Start/Continue/Stop) is shown.

#ifndef MIDI_PRINT_H
#define MIDI_PRINT_H

#include "midi.h"

void midi_print(const midi_message *m, void *user);

#endif // MIDI_PRINT_H
