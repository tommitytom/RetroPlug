#pragma once

#include <memory>
#include <string>

namespace rp::launchpad {

// A claim on a control-surface port that OTHER PROCESSES can see. RtMidi lets two processes open the same port on ALSA
// and CoreMIDI, so two RetroPlug standalones that both restore the same config.json would both claim one Launchpad:
// every pad press launches in both, and whichever quits first sends the device back to Live mode under the other. (On
// Windows the second open simply fails, which is no better a message: the refusal cannot say why.)
//
// An OS file lock per port, in <dir>/locks/: flock() on POSIX, an unshared CreateFile on Windows. The OS drops it when
// the holder exits, crash included, so a stale lock cannot outlive its process. The returned token IS the claim; null
// means another process holds it.
using PortClaim = std::shared_ptr<void>;

PortClaim claimPort(const std::string& dir, const std::string& port);

}  // namespace rp::launchpad
