#pragma once

// The box's expansion-volume write (BoxCartLink's Writer::volume): rp.n8.nes's value to the N8's FPGA master volume
// for the expansion audio (VRC6 / VRC7 / N163 / Sunsoft 5B / MMC5; at 0 every expansion voice is silent), written over
// the box's Edio and read back (a live register) for the console line. The rule: only a write the transport failed
// (memWR not ok) answers false, so the link tries it again a second later, and says so once (not on a retry,
// `again`); a write that went through is done whatever it reads back, as N8Host takes a write that went through, and
// says which of three it came to: `written (reads back N)`, `written, but reads back N`, `written (no read-back)`.
// The failure's line says `did not go through`, never `FAILED`: the hardware gate's boot legs stop on that word
// (pico/n8-host/loop.sh --fail "no 'k'|FAILED", tested before the boot's own line), and a transient failure here is
// retried, not a failed boot. Portable, so the desktop's test drives the outcomes over a fake transport.

#include "n8/Edio.hpp"

namespace rp::box {

// One console line, without its newline.
using VolumePrint = void (*)(const char* line);

bool writeExpansionVolume(rp::n8::Edio& edio, int value, bool again, VolumePrint print);

}  // namespace rp::box
