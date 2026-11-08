#pragma once

#include <cstddef>
#include <sstream>

#include "Core/Shared/Emulator.h"

// The savestate-snapshot CEILING for a Mesen core: how big SystemBase should size the snapshot triple so
// that no later capture has to be skipped. Shared by all three Mesen backends, which previously each
// carried their own copy of a formula that was wrong in the same way.
//
// The trap: Mesen savestates are zlib-DEFLATED (Serializer::SaveTo, level 1), so their size tracks the
// ENTROPY of the machine rather than its shape. Sizing from a state measured at construct - which every
// backend used to do, as `measured + measured/2 + 8192` - measures a console that has booted but not yet
// run, i.e. one whose RAM is nearly all zeros and compresses to almost nothing. Measured on a GBA cart:
//
//     uncompressed   457369 at boot, 457369 warmed   (stable: it is the serialized SHAPE)
//     compressed      27485 at boot,  97920 warmed   (3.6x: it is the serialized CONTENT)
//
// So the old ceiling came out at ~19.5 KB against a state that reaches ~98 KB in normal play. Every
// publish past that point was skipped, freezing readState/readSram on the boot snapshot for the life of
// the system - the same freeze a SameBoy model switch used to cause, arrived at from the other end.
// (NES/SMS escaped it only because their states barely compress, ~0.8x, so the 1.5x factor covered them.)
//
// The fix is to measure the one quantity that does not depend on entropy: serialize UNCOMPRESSED and size
// from that. Deflate output is bounded by compressBound(n), which is n plus well under 1%, so an
// uncompressed measurement is an upper bound on any compressed capture of the same machine. The margin on
// top is for the uncompressed size itself drifting - it is not perfectly fixed (a NES state grew 0.9%
// between boot and warm, from variable-length members) - and is deliberately generous, because the cost of
// being wrong is a silently frozen savestate and the cost of being right is some resident memory.
//
// Cost, measured: the ceiling lands near the uncompressed size, so a GBA system goes from a 49 KB ceiling
// to 580 KB - about 3.3 MB resident once the core's triple and SnapshotRegistry's shadow are counted
// (3 buffers each). That is the price of the bug being fixed rather than latent. NES is barely affected
// (66 KB -> 69 KB, 1.04x) because its state hardly compresses, so the old formula was already measuring
// something close to the truth there; SMS is the same shape.
inline std::size_t mesenStateCeiling(Emulator& emu) {
    // compressionLevel 0 -> Serializer::SaveTo writes the raw buffer, so this measures the shape.
    std::stringstream ss(std::ios::out | std::ios::binary);
    emu.Serialize(ss, /*includeSettings*/ false, /*compressionLevel*/ 0);
    const std::size_t uncompressed = static_cast<std::size_t>(ss.tellp());
    if (uncompressed == 0) return 0;
    return uncompressed + uncompressed / 4 + 8192;
}
