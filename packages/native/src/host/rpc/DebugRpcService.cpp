#include "host/rpc/DebugRpcService.hpp"

#include "host/engine/Engine.hpp"
#include "system/MemoryType.hpp"  // rp::MemoryType / rp::AccessType (readMemory)
#include "system/SystemBase.hpp"

// Live-core debug reads — resolve the real system via findSystem (NOT the snapshot registry), so they
// are only valid on the control thread while the audio thread is not started (see the header note).
rp::ApuState DebugRpcService::getApuState(std::uint32_t id) {
    SystemBase* sys = engine_.findSystem(id);
    if (!sys) return {};
    rp::IDebugTarget* dbg = sys->debugTarget();  // null on SameBoy/GBA
    if (!dbg) return {};
    return dbg->getApuState();
}

rp::ExpansionAudioState DebugRpcService::getExpansionAudioState(std::uint32_t id) {
    SystemBase* sys = engine_.findSystem(id);
    if (!sys) return {};
    rp::IDebugTarget* dbg = sys->debugTarget();  // null on SameBoy/GBA
    if (!dbg) return {};
    return dbg->getExpansionAudioState();
}

rp::PpuState DebugRpcService::getPpuState(std::uint32_t id) {
    SystemBase* sys = engine_.findSystem(id);
    if (!sys) return {};
    rp::IDebugTarget* dbg = sys->debugTarget();  // null on SameBoy/GBA
    if (!dbg) return {};
    return dbg->getPpuState();
}

std::optional<std::uint8_t> DebugRpcService::readCpu(std::uint32_t id, std::uint32_t addr) {
    SystemBase* sys = engine_.findSystem(id);
    if (!sys) return std::nullopt;
    return sys->readCpuByte(addr);  // nullopt (→ JS null) when the peek is unsupported
}

bool DebugRpcService::writeCpu(std::uint32_t id, std::uint32_t addr, std::uint32_t value) {
    SystemBase* sys = engine_.findSystem(id);
    if (!sys) return false;
    return sys->writeCpuByte(addr, static_cast<std::uint8_t>(value));  // false when the write is unsupported
}

rfl::Bytestring DebugRpcService::readMemory(std::uint32_t id, std::uint32_t memType) {
    rfl::Bytestring out;
    SystemBase* sys = engine_.findSystem(id);
    if (!sys) return out;
    rp::MemoryAccessor acc = sys->getMemory(static_cast<rp::MemoryType>(memType), rp::AccessType::Read);
    if (acc.valid()) {
        const auto* p = reinterpret_cast<const std::byte*>(acc.data());
        out.assign(p, p + acc.size());
    }
    return out;
}

std::vector<rp::CpuRegister> DebugRpcService::getCpuRegisters(std::uint32_t id) {
    SystemBase* sys = engine_.findSystem(id);
    if (!sys) return {};
    return sys->getCpuRegisters();
}

std::uint64_t DebugRpcService::stepInstruction(std::uint32_t id) {
    SystemBase* sys = engine_.findSystem(id);
    if (!sys) return 0;
    return sys->stepInstruction();  // 0 when the backend can't step
}

std::vector<rp::DebugEvent> DebugRpcService::drainEvents(std::uint32_t id) {
    SystemBase* sys = engine_.findSystem(id);
    if (!sys) return {};
    rp::IDebugTarget* dbg = sys->debugTarget();  // null on SameBoy/GBA
    if (!dbg) return {};
    return dbg->drainEvents();  // events Mesen logged for the frame just rendered
}

bool DebugRpcService::loadLabels(std::uint32_t id, std::string path) {
    SystemBase* sys = engine_.findSystem(id);
    if (!sys) return false;
    rp::IDebugTarget* dbg = sys->debugTarget();  // null on SameBoy/GBA
    if (!dbg) return false;
    return dbg->loadLabels(path);  // false on read/parse failure
}

bool DebugRpcService::setCpuRegister(std::uint32_t id, std::string name, std::uint32_t value) {
    SystemBase* sys = engine_.findSystem(id);
    if (!sys) return false;
    return sys->setCpuRegister(name, value);  // false on unknown / read-only register
}

bool DebugRpcService::runUntilPc(std::uint32_t id, std::uint32_t target, std::uint64_t maxCycles) {
    SystemBase* sys = engine_.findSystem(id);
    if (!sys) return false;
    return sys->runUntilPc(target, maxCycles);  // false when the target is never reached / can't step
}

// Breakpoints + run-until-break — route through the Mesen NES debug target (null on SameBoy/GBA →
// false/default). The vector<BreakpointSpec> input auto-decodes from the JS array (the zip/BackendZipInput
// pattern).
bool DebugRpcService::setBreakpoints(std::uint32_t id, std::vector<rp::BreakpointSpec> bps) {
    SystemBase* sys = engine_.findSystem(id);
    if (!sys) return false;
    rp::IDebugTarget* dbg = sys->debugTarget();  // null on SameBoy/GBA
    if (!dbg) return false;
    dbg->setBreakpoints(bps);
    return true;
}

rp::BreakInfo DebugRpcService::runUntilBreak(std::uint32_t id, std::uint64_t maxCycles) {
    SystemBase* sys = engine_.findSystem(id);
    if (!sys) return {};
    rp::IDebugTarget* dbg = sys->debugTarget();
    if (!dbg) return {};
    return dbg->runUntilBreak(maxCycles);  // broke=false on the cycle cap
}

// Execution trace + single-step — all route through the Mesen NES debug target (null on SameBoy/GBA →
// no-op/empty/default).
bool DebugRpcService::setTrace(std::uint32_t id, bool on) {
    SystemBase* sys = engine_.findSystem(id);
    if (!sys) return false;
    rp::IDebugTarget* dbg = sys->debugTarget();
    if (!dbg) return false;
    dbg->setTraceEnabled(on);
    return true;
}

std::vector<rp::TraceLine> DebugRpcService::readTrace(std::uint32_t id, std::uint32_t count) {
    SystemBase* sys = engine_.findSystem(id);
    if (!sys) return {};
    rp::IDebugTarget* dbg = sys->debugTarget();
    if (!dbg) return {};
    return dbg->readTrace(count);
}

rp::BreakInfo DebugRpcService::stepInto(std::uint32_t id) {
    SystemBase* sys = engine_.findSystem(id);
    if (!sys) return {};
    rp::IDebugTarget* dbg = sys->debugTarget();
    if (!dbg) return {};
    return dbg->step();
}

rp::BreakInfo DebugRpcService::stepOver(std::uint32_t id) {
    SystemBase* sys = engine_.findSystem(id);
    if (!sys) return {};
    rp::IDebugTarget* dbg = sys->debugTarget();
    if (!dbg) return {};
    return dbg->stepOver();
}

rp::BreakInfo DebugRpcService::stepOut(std::uint32_t id) {
    SystemBase* sys = engine_.findSystem(id);
    if (!sys) return {};
    rp::IDebugTarget* dbg = sys->debugTarget();
    if (!dbg) return {};
    return dbg->stepOut();
}

// Profiler + disassembler + call stack — all route through the Mesen NES debug target (null on
// SameBoy/GBA → no-op/empty).
bool DebugRpcService::beginProfile(std::uint32_t id) {
    SystemBase* sys = engine_.findSystem(id);
    if (!sys) return false;
    rp::IDebugTarget* dbg = sys->debugTarget();
    if (!dbg) return false;
    dbg->beginProfile();
    return true;
}

std::vector<rp::ProfiledFunction> DebugRpcService::readProfile(std::uint32_t id) {
    SystemBase* sys = engine_.findSystem(id);
    if (!sys) return {};
    rp::IDebugTarget* dbg = sys->debugTarget();
    if (!dbg) return {};
    return dbg->readProfile();
}

std::vector<rp::DisasmLine> DebugRpcService::disassemble(std::uint32_t id, std::uint32_t addr, std::uint32_t count) {
    SystemBase* sys = engine_.findSystem(id);
    if (!sys) return {};
    rp::IDebugTarget* dbg = sys->debugTarget();
    if (!dbg) return {};
    return dbg->disassemble(addr, count);
}

std::vector<rp::CallFrame> DebugRpcService::getCallStack(std::uint32_t id) {
    SystemBase* sys = engine_.findSystem(id);
    if (!sys) return {};
    rp::IDebugTarget* dbg = sys->debugTarget();
    if (!dbg) return {};
    return dbg->getCallStack();
}
