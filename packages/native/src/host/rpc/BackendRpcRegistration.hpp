#pragma once

// Single source of truth for the greenfield Backend method surface (mirrors
// cli/HarnessRpcRegistration.hpp). Registers each method on the rpcpp server. The bound type is the
// thin BackendFacade (one object per server); each method identifier IS the wire name.

#include "host/rpc/BackendFacade.hpp"

template <class Server>
void registerBackendRpcMethods(Server& server) {
    // filesystem
    server.template addMethod<&BackendFacade::readFile>();
    server.template addMethod<&BackendFacade::writeFile>();
    server.template addMethod<&BackendFacade::writeFileAtomic>();
    server.template addMethod<&BackendFacade::fileExists>();
    server.template addMethod<&BackendFacade::rename>();
    server.template addMethod<&BackendFacade::listDir>();
    server.template addMethod<&BackendFacade::deleteFile>();
    server.template addMethod<&BackendFacade::drainChangedPaths>();
    // paths / config
    server.template addMethod<&BackendFacade::canonicalize>();
    server.template addMethod<&BackendFacade::readFilePrefix>();
    server.template addMethod<&BackendFacade::configDir>();
    server.template addMethod<&BackendFacade::version>();
    // codec
    server.template addMethod<&BackendFacade::zip>();
    server.template addMethod<&BackendFacade::unzip>();
    // LSDJ sav authoring
    server.template addMethod<&BackendFacade::savFromJson>();
    // emulator lifecycle / reads
    server.template addMethod<&BackendFacade::constructSystem>();
    server.template addMethod<&BackendFacade::removeSystem>();
    server.template addMethod<&BackendFacade::applySystemSetting>();
    server.template addMethod<&BackendFacade::applyRoleConfig>();
    server.template addMethod<&BackendFacade::readState>();
    server.template addMethod<&BackendFacade::readSram>();
    server.template addMethod<&BackendFacade::screenshot>();
    server.template addMethod<&BackendFacade::getFrame>();
    // live-core debug reads (spec/09-cli-debugging.md)
    server.template addMethod<&BackendFacade::getApuState>();
    server.template addMethod<&BackendFacade::getPpuState>();
    server.template addMethod<&BackendFacade::readCpu>();
    server.template addMethod<&BackendFacade::writeCpu>();
    server.template addMethod<&BackendFacade::readMemory>();
    server.template addMethod<&BackendFacade::getCpuRegisters>();
    server.template addMethod<&BackendFacade::stepInstruction>();
    server.template addMethod<&BackendFacade::drainEvents>();
    server.template addMethod<&BackendFacade::loadLabels>();
    // live-core debug writes / control-flow (spec/09-cli-debugging.md)
    server.template addMethod<&BackendFacade::setCpuRegister>();
    server.template addMethod<&BackendFacade::runUntilPc>();
    // breakpoints + run-until-break (spec/09-cli-debugging.md)
    server.template addMethod<&BackendFacade::setBreakpoints>();
    server.template addMethod<&BackendFacade::runUntilBreak>();
    // execution trace + single-step (spec/09-cli-debugging.md)
    server.template addMethod<&BackendFacade::setTrace>();
    server.template addMethod<&BackendFacade::readTrace>();
    server.template addMethod<&BackendFacade::stepInto>();
    server.template addMethod<&BackendFacade::stepOver>();
    server.template addMethod<&BackendFacade::stepOut>();
    // profiler + disassembler + call stack (spec/09-cli-debugging.md)
    server.template addMethod<&BackendFacade::beginProfile>();
    server.template addMethod<&BackendFacade::readProfile>();
    server.template addMethod<&BackendFacade::disassemble>();
    server.template addMethod<&BackendFacade::getCallStack>();
    // DSP-side JS runtime (the role kernel)
    server.template addMethod<&BackendFacade::compileScript>();
    server.template addMethod<&BackendFacade::dspLoadKernel>();
    server.template addMethod<&BackendFacade::dspSetSystems>();
    // audio render / input drive
    server.template addMethod<&BackendFacade::pressButton>();
    server.template addMethod<&BackendFacade::renderAudio>();
    server.template addMethod<&BackendFacade::renderAudioPerSystem>();
    server.template addMethod<&BackendFacade::renderAudioPerChannel>();
    server.template addMethod<&BackendFacade::sampleRate>();
    server.template addMethod<&BackendFacade::setTransport>();
    server.template addMethod<&BackendFacade::setBpm>();
    server.template addMethod<&BackendFacade::setAudioRouting>();
    // DSP runtime in the render loop
    server.template addMethod<&BackendFacade::stageMidiIn>();
    server.template addMethod<&BackendFacade::setSerialOutCapture>();
    server.template addMethod<&BackendFacade::drainMidiOut>();
    // DSP-runtime allocation/GC profiling (spec/08-profiling.md)
    server.template addMethod<&BackendFacade::dspAllocStats>();
    server.template addMethod<&BackendFacade::dspResetAllocStats>();
    server.template addMethod<&BackendFacade::dspRunGc>();
    // per-role runtime tracing (spec/08-profiling.md Tier B)
    server.template addMethod<&BackendFacade::dspTraceReset>();
    server.template addMethod<&BackendFacade::dspTrace>();
    server.template addMethod<&BackendFacade::dspTraceNames>();
    // background audio thread (threaded mode)
    server.template addMethod<&BackendFacade::startAudio>();
    server.template addMethod<&BackendFacade::stopAudio>();
    server.template addMethod<&BackendFacade::audioCaptured>();
    server.template addMethod<&BackendFacade::sleepMs>();
    server.template addMethod<&BackendFacade::systemCount>();
    server.template addMethod<&BackendFacade::drainReleased>();
    server.addDiscoveryMethod();
}
