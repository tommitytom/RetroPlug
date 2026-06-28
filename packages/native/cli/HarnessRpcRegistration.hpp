#pragma once

// Single source of truth for the HarnessRpcService method surface (mirrors
// src/PluginRpcRegistration.hpp). Used by the runtime dispatcher in
// TestHarness.cpp and by the `retroplug-cli --dump-harness-schema` schema dump.

#include "HarnessRpcService.hpp"

template <class Server>
void registerHarnessRpcMethods(Server& server) {
    server.template addMethod<&HarnessRpcService::loadRom>();
    server.template addMethod<&HarnessRpcService::savFromJson>();
    server.template addMethod<&HarnessRpcService::loadSram>();
    server.template addMethod<&HarnessRpcService::saveSram>();
    server.template addMethod<&HarnessRpcService::autoSaveSram>();
    server.template addMethod<&HarnessRpcService::reset>();
    server.template addMethod<&HarnessRpcService::readFile>();
    server.template addMethod<&HarnessRpcService::writeFile>();
    server.template addMethod<&HarnessRpcService::removeFile>();
    server.template addMethod<&HarnessRpcService::savRoundtripDiff>();
    server.template addMethod<&HarnessRpcService::runMs>();
    server.template addMethod<&HarnessRpcService::press>();
    server.template addMethod<&HarnessRpcService::sendMidi>();
    server.template addMethod<&HarnessRpcService::dispatchMidi>();
    server.template addMethod<&HarnessRpcService::setTransport>();
    server.template addMethod<&HarnessRpcService::setBpm>();
    server.template addMethod<&HarnessRpcService::drainMidi>();
    server.template addMethod<&HarnessRpcService::drainSerial>();
    server.template addMethod<&HarnessRpcService::readMemory>();
    server.template addMethod<&HarnessRpcService::getRegisters>();
    server.template addMethod<&HarnessRpcService::setRegister>();
    server.template addMethod<&HarnessRpcService::readCpu>();
    server.template addMethod<&HarnessRpcService::step>();
    server.template addMethod<&HarnessRpcService::runUntilPc>();
    server.template addMethod<&HarnessRpcService::getFrame>();
    server.template addMethod<&HarnessRpcService::screenshot>();
    server.template addMethod<&HarnessRpcService::getAudio>();
    server.template addMethod<&HarnessRpcService::runMsPerSystem>();
    server.template addMethod<&HarnessRpcService::writeWav>();
    server.template addMethod<&HarnessRpcService::renderWav>();
    server.template addMethod<&HarnessRpcService::renderWavPerSystem>();
    server.template addMethod<&HarnessRpcService::renderWavPerSystemParallel>();
    server.template addMethod<&HarnessRpcService::renderBegin>();
    server.template addMethod<&HarnessRpcService::renderChunk>();
    server.template addMethod<&HarnessRpcService::renderEnd>();
    server.template addMethod<&HarnessRpcService::saveRplg>();
    server.template addMethod<&HarnessRpcService::saveProjectFile>();
    server.template addMethod<&HarnessRpcService::loadRplg>();
    server.template addMethod<&HarnessRpcService::patchKit>();
    server.template addMethod<&HarnessRpcService::beginProfile>();
    server.template addMethod<&HarnessRpcService::readProfile>();
    server.template addMethod<&HarnessRpcService::loadLabels>();
    server.template addMethod<&HarnessRpcService::disassemble>();
    server.template addMethod<&HarnessRpcService::setTrace>();
    server.template addMethod<&HarnessRpcService::readTrace>();
    server.template addMethod<&HarnessRpcService::getCallStack>();
    server.template addMethod<&HarnessRpcService::setBreakpoints>();
    server.template addMethod<&HarnessRpcService::runUntilBreak>();
    server.template addMethod<&HarnessRpcService::stepInto>();
    server.template addMethod<&HarnessRpcService::stepOver>();
    server.template addMethod<&HarnessRpcService::stepOut>();
    server.addDiscoveryMethod();
}
