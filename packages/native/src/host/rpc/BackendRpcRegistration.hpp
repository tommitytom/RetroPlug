#pragma once

// Per-facet registration of the Backend method surface. Each function mounts one capability facet's
// methods onto an rpcpp server via the cross-object addMethod<&Service::m>(instance) — the wire name is
// derived from the method identifier, so it is unchanged from when every method was a BackendFacade
// forwarder. A host calls only the facets it is allowed to expose (BackendFacade owns the services;
// registerAllBackendRpc mounts the full union). Adding a method = one line in the right facet.

#include "host/rpc/BackendFacade.hpp"

// --- host: filesystem / config / codec / sav (15) ---
template <class Server>
void registerHostRpc(Server& s, HostRpcService& h) {
    s.template addMethod<&HostRpcService::readFile>(h);
    s.template addMethod<&HostRpcService::writeFile>(h);
    s.template addMethod<&HostRpcService::writeFileAtomic>(h);
    s.template addMethod<&HostRpcService::fileExists>(h);
    s.template addMethod<&HostRpcService::rename>(h);
    s.template addMethod<&HostRpcService::listDir>(h);
    s.template addMethod<&HostRpcService::deleteFile>(h);
    s.template addMethod<&HostRpcService::drainChangedPaths>(h);
    s.template addMethod<&HostRpcService::canonicalize>(h);
    s.template addMethod<&HostRpcService::readFilePrefix>(h);
    s.template addMethod<&HostRpcService::configDir>(h);
    s.template addMethod<&HostRpcService::version>(h);
    s.template addMethod<&HostRpcService::zip>(h);
    s.template addMethod<&HostRpcService::unzip>(h);
    s.template addMethod<&HostRpcService::savFromJson>(h);
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

// --- harness: audio render / transport / MIDI + DSP profiling (12; CLI + tests only) ---
template <class Server>
void registerHarnessRpc(Server& s, EngineRpcService& e) {
    s.template addMethod<&EngineRpcService::renderAudio>(e);
    s.template addMethod<&EngineRpcService::renderAudioPerSystem>(e);
    s.template addMethod<&EngineRpcService::setTransport>(e);
    s.template addMethod<&EngineRpcService::setBpm>(e);
    s.template addMethod<&EngineRpcService::stageMidiIn>(e);
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
void registerDebugRpc(Server& s, EngineRpcService& e) {
    s.template addMethod<&EngineRpcService::getApuState>(e);
    s.template addMethod<&EngineRpcService::getPpuState>(e);
    s.template addMethod<&EngineRpcService::readCpu>(e);
    s.template addMethod<&EngineRpcService::writeCpu>(e);
    s.template addMethod<&EngineRpcService::readMemory>(e);
    s.template addMethod<&EngineRpcService::getCpuRegisters>(e);
    s.template addMethod<&EngineRpcService::stepInstruction>(e);
    s.template addMethod<&EngineRpcService::drainEvents>(e);
    s.template addMethod<&EngineRpcService::loadLabels>(e);
    s.template addMethod<&EngineRpcService::setCpuRegister>(e);
    s.template addMethod<&EngineRpcService::runUntilPc>(e);
    s.template addMethod<&EngineRpcService::setBreakpoints>(e);
    s.template addMethod<&EngineRpcService::runUntilBreak>(e);
    s.template addMethod<&EngineRpcService::setTrace>(e);
    s.template addMethod<&EngineRpcService::readTrace>(e);
    s.template addMethod<&EngineRpcService::stepInto>(e);
    s.template addMethod<&EngineRpcService::stepOver>(e);
    s.template addMethod<&EngineRpcService::stepOut>(e);
    s.template addMethod<&EngineRpcService::beginProfile>(e);
    s.template addMethod<&EngineRpcService::readProfile>(e);
    s.template addMethod<&EngineRpcService::disassemble>(e);
    s.template addMethod<&EngineRpcService::getCallStack>(e);
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

// The full union (69 methods) + discovery — every facet mounted. Hosts that expose the whole surface
// (CLI) use this; scoped hosts call the individual register functions they're allowed to.
template <class Server>
void registerAllBackendRpc(Server& s, BackendFacade& f) {
    registerHostRpc(s, f.host());
    registerEmulatorRpc(s, f.engine());
    registerDspKernelRpc(s, f.engine());
    registerHarnessRpc(s, f.engine());
    registerDebugRpc(s, f.engine());
    registerDriverRpc(s, f.driver());
    s.addDiscoveryMethod();
}
