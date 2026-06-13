#pragma once

// Single source of truth for the HarnessRpcService method surface (mirrors
// src/PluginRpcRegistration.hpp). Used by the runtime dispatcher in
// TestHarness.cpp and by the `retroplug-cli --dump-harness-schema` schema dump.

#include "HarnessRpcService.hpp"

template <class Server>
void registerHarnessRpcMethods(Server& server) {
    server.template addMethod<&HarnessRpcService::loadRom>();
    server.template addMethod<&HarnessRpcService::runMs>();
    server.template addMethod<&HarnessRpcService::press>();
    server.template addMethod<&HarnessRpcService::drainMidi>();
    server.template addMethod<&HarnessRpcService::readMemory>();
    server.template addMethod<&HarnessRpcService::getRegisters>();
    server.template addMethod<&HarnessRpcService::getFrame>();
    server.template addMethod<&HarnessRpcService::getAudio>();
    server.template addMethod<&HarnessRpcService::runUntilBreak>();
    server.addDiscoveryMethod();
}
