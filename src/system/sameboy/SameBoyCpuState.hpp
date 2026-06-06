#pragma once

#include <cstdint>

namespace rp {

// Plain-data snapshot of the SM83 CPU's 16-bit register file. Deliberately
// free of <gb.h> so the CLI test harness (cli/TestHarness.cpp) can name the
// type without dragging the SameBoy core into its translation unit.
//
// SameBoy-only: NES (6502) and GBA (ARM7) have incompatible register files,
// so CPU-state access is exposed on SameBoySystem rather than as a shared
// SystemBase virtual.
struct CpuRegisters {
    std::uint16_t af = 0;
    std::uint16_t bc = 0;
    std::uint16_t de = 0;
    std::uint16_t hl = 0;
    std::uint16_t sp = 0;
    std::uint16_t pc = 0;
};

// Identifies one writable 16-bit register for setCpuRegister(). Values are an
// internal contract between SameBoySystem and the harness bridge.
enum class CpuReg : std::uint8_t {
    AF = 0, BC = 1, DE = 2, HL = 3, SP = 4, PC = 5,
};

} // namespace rp
