#pragma once

// Per-facet registration of the Backend method surface. Each function mounts one capability facet's
// methods onto an rpcpp server via the cross-object addMethod<&Service::m>(instance) — the wire name is
// derived from the method identifier. A host composes its own service instances and calls only the
// facets it is allowed to expose (registerAllBackendRpc mounts the full union). Adding a method = one
// line in the right facet.

#include "host/rpc/AudioDriverRpcService.hpp"
#include "host/rpc/DebugRpcService.hpp"
#include "host/rpc/EngineRpcService.hpp"
#include "host/rpc/HostRpcService.hpp"

// --- host: filesystem / config / codec (19) ---
template <class Server>
void registerHostRpc(Server& s, HostRpcService& h) {
    s.template addMethod<&HostRpcService::readFile>(h);
    s.template addMethod<&HostRpcService::writeFile>(h);
    s.template addMethod<&HostRpcService::writeFileAtomic>(h);
    s.template addMethod<&HostRpcService::appendFile>(h);
    s.template addMethod<&HostRpcService::writeFileAt>(h);
    s.template addMethod<&HostRpcService::fileExists>(h);
    s.template addMethod<&HostRpcService::rename>(h);
    s.template addMethod<&HostRpcService::listDir>(h);
    s.template addMethod<&HostRpcService::deleteFile>(h);
    s.template addMethod<&HostRpcService::drainChangedPaths>(h);
    s.template addMethod<&HostRpcService::setWatchedRoms>(h);
    s.template addMethod<&HostRpcService::canonicalize>(h);
    s.template addMethod<&HostRpcService::readFilePrefix>(h);
    s.template addMethod<&HostRpcService::configDir>(h);
    s.template addMethod<&HostRpcService::version>(h);
    s.template addMethod<&HostRpcService::zip>(h);
    s.template addMethod<&HostRpcService::unzip>(h);
    s.template addMethod<&HostRpcService::pngEncode>(h);
    s.template addMethod<&HostRpcService::pngDecode>(h);
}

// --- emulator: lifecycle / snapshot reads / live config / input (11) ---
template <class Server>
void registerEmulatorRpc(Server& s, EngineRpcService& e) {
    s.template addMethod<&EngineRpcService::constructSystem>(e);
    s.template addMethod<&EngineRpcService::removeSystem>(e);
    s.template addMethod<&EngineRpcService::applySystemSetting>(e);
    s.template addMethod<&EngineRpcService::applyRoleConfig>(e);
    s.template addMethod<&EngineRpcService::readState>(e);
    s.template addMethod<&EngineRpcService::readSram>(e);
    s.template addMethod<&EngineRpcService::readRam>(e);
    s.template addMethod<&EngineRpcService::writeRam>(e);
    s.template addMethod<&EngineRpcService::screenshot>(e);
    s.template addMethod<&EngineRpcService::getFrame>(e);
    s.template addMethod<&EngineRpcService::pressButton>(e);
    s.template addMethod<&EngineRpcService::setAudioRouting>(e);
    s.template addMethod<&EngineRpcService::setSerialOutCapture>(e);
}

// --- dsp-kernel: the role kernel (3) ---
template <class Server>
void registerDspKernelRpc(Server& s, EngineRpcService& e) {
    s.template addMethod<&EngineRpcService::compileScript>(e);
    s.template addMethod<&EngineRpcService::dspLoadKernel>(e);
    s.template addMethod<&EngineRpcService::dspSetSystems>(e);
}

// --- harness: audio render / transport / MIDI + DSP profiling + kit compile (16; CLI + tests only) ---
template <class Server>
void registerHarnessRpc(Server& s, EngineRpcService& e) {
    s.template addMethod<&EngineRpcService::renderAudio>(e);
    s.template addMethod<&EngineRpcService::renderAudioPerSystem>(e);
    s.template addMethod<&EngineRpcService::renderAudioPerChannel>(e);
    s.template addMethod<&EngineRpcService::compileKit>(e);
    s.template addMethod<&EngineRpcService::compileDmc>(e);
    s.template addMethod<&EngineRpcService::sampleRate>(e);
    s.template addMethod<&EngineRpcService::setSampleRate>(e);
    s.template addMethod<&EngineRpcService::setTransport>(e);
    s.template addMethod<&EngineRpcService::setPpq>(e);
    s.template addMethod<&EngineRpcService::setBpm>(e);
    s.template addMethod<&EngineRpcService::stageMidiIn>(e);
    s.template addMethod<&EngineRpcService::stageControllerIn>(e);
    s.template addMethod<&EngineRpcService::drainMidiOut>(e);
    s.template addMethod<&EngineRpcService::dspAllocStats>(e);
    s.template addMethod<&EngineRpcService::dspResetAllocStats>(e);
    s.template addMethod<&EngineRpcService::dspRunGc>(e);
    s.template addMethod<&EngineRpcService::dspTraceReset>(e);
    s.template addMethod<&EngineRpcService::dspTrace>(e);
    s.template addMethod<&EngineRpcService::dspTraceNames>(e);
}

// --- debug: live-core inspection / stepping / breakpoints / profiler (22; CLI only, spec/09) ---
template <class Server>
void registerDebugRpc(Server& s, DebugRpcService& d) {
    s.template addMethod<&DebugRpcService::getApuState>(d);
    s.template addMethod<&DebugRpcService::getExpansionAudioState>(d);
    s.template addMethod<&DebugRpcService::getPpuState>(d);
    s.template addMethod<&DebugRpcService::readCpu>(d);
    s.template addMethod<&DebugRpcService::writeCpu>(d);
    s.template addMethod<&DebugRpcService::readMemory>(d);
    s.template addMethod<&DebugRpcService::getCpuRegisters>(d);
    s.template addMethod<&DebugRpcService::stepInstruction>(d);
    s.template addMethod<&DebugRpcService::drainEvents>(d);
    s.template addMethod<&DebugRpcService::loadLabels>(d);
    s.template addMethod<&DebugRpcService::setCpuRegister>(d);
    s.template addMethod<&DebugRpcService::runUntilPc>(d);
    s.template addMethod<&DebugRpcService::setBreakpoints>(d);
    s.template addMethod<&DebugRpcService::runUntilBreak>(d);
    s.template addMethod<&DebugRpcService::setTrace>(d);
    s.template addMethod<&DebugRpcService::readTrace>(d);
    s.template addMethod<&DebugRpcService::stepInto>(d);
    s.template addMethod<&DebugRpcService::stepOver>(d);
    s.template addMethod<&DebugRpcService::stepOut>(d);
    s.template addMethod<&DebugRpcService::beginProfile>(d);
    s.template addMethod<&DebugRpcService::readProfile>(d);
    s.template addMethod<&DebugRpcService::disassemble>(d);
    s.template addMethod<&DebugRpcService::getCallStack>(d);
}

// --- driver: background audio thread (6; threaded test host only) ---
template <class Server>
void registerDriverRpc(Server& s, AudioDriverRpcService& d) {
    s.template addMethod<&AudioDriverRpcService::startAudio>(d);
    s.template addMethod<&AudioDriverRpcService::stopAudio>(d);
    s.template addMethod<&AudioDriverRpcService::audioCaptured>(d);
    s.template addMethod<&AudioDriverRpcService::sleepMs>(d);
    s.template addMethod<&AudioDriverRpcService::systemCount>(d);
    s.template addMethod<&AudioDriverRpcService::drainReleased>(d);
}

// The full union (72 methods) + discovery — every facet mounted. Hosts that expose the whole surface
// (the CLI + test host) use this; scoped hosts call the individual register functions they're allowed to.
template <class Server>
void registerAllBackendRpc(Server& s, HostRpcService& host, EngineRpcService& engine,
                           DebugRpcService& debug, AudioDriverRpcService& driver) {
    registerHostRpc(s, host);
    registerEmulatorRpc(s, engine);
    registerDspKernelRpc(s, engine);
    registerHarnessRpc(s, engine);
    registerDebugRpc(s, debug);
    registerDriverRpc(s, driver);
    s.addDiscoveryMethod();
}
