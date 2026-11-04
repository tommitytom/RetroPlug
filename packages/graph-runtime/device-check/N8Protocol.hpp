#pragma once

// The N8 pack's protocol in the device image (docs/modular-graph-phase-e2-design.md section 4): its Edio, Menu and Ops
// (packages/n8/native/src/n8), which the pack's node does not call, so an image that only registered the node would
// link none of them and the link trap would prove nothing about them. This check calls them, so the image links them
// under the trap, and when it runs (E3's rig) it drives them over a scripted transport standing in for the cart: a
// handshake, a ROM booted through the menu with its battery, a file read whole, every other primitive and menu command
// once (the image is linked with section GC, which drops a function nothing calls before the trap sees what it
// references, so each is called to be held), then a boot cancelled at its first stop point. retroplug-device-mode-test
// runs the same code on the desktop and holds it to the constants here.

#include <cstddef>
#include <cstdint>

namespace rp::device_check {

struct N8ProtocolResult {
    bool          ok     = false;
    const char*   failed = nullptr;  // the call that did not come to what the script says, or null
    std::size_t   written = 0;       // the bytes the calls wrote
    std::uint32_t hash    = 0;       // their FNV-1a, which the desktop's build of the same calls writes too
};

// The bytes the script's calls write, and their FNV-1a: what the desktop's Edio writes for them, the wire
// edio-golden.json holds the commands of (packages/n8/native/test), counted here by hand: the
// handshake 4; '*t' 15; the ROM 1074 (its open 26 and status 4, the write's 8, its 1024 bytes and status 4, the close
// and status 8); the battery 79 (open 39 and status 4, write 8, 16 bytes, status 4, close and status 8); the install 62
// ('*n' 15, then the path's length 15 and its 19 bytes in a 32-byte write); '*s' 15; the file read 56 (the listing's
// load 8, status 4, size 4 and range 10; open 10 and status 4; one block's command 8; close and status 8); then 85
// for the rest (sysInfo, vdc and freeSpace 4 each; dirMake and fileDelete of "chk" 9 and a status 4 each; memRD 13;
// '*v' and '*r' 15 each; a status 4): 1390.
inline constexpr std::size_t   kN8ProtocolBytes = 1390;
inline constexpr std::uint32_t kN8ProtocolHash  = 0x2FF68A57u;

N8ProtocolResult n8Protocol();

}  // namespace rp::device_check
