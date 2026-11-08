#pragma once

#include <cstdint>
#include <string>

namespace rp {

// One named CPU register and its current value. Cross-emulator: each backend
// reports its own register file (SM83 af/bc/de/hl/sp/pc; 6502 a/x/y/sp/pc/ps;
// ARM7 r0..r15/cpsr), so the set is name-keyed rather than a fixed struct.
//
// Contract: every backend that supports CPU state includes a register named
// "pc" (the architectural program counter — r15 on ARM also surfaces as "pc").
// `value` is zero-extended to 32 bits; `bits` is the real register width so a
// caller can format / mask it.
struct CpuRegister {
    std::string   name;
    std::uint32_t value = 0;
    std::uint8_t  bits  = 0;
};

} // namespace rp
