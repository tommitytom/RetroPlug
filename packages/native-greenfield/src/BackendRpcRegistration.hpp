#pragma once

// Single source of truth for the greenfield Backend method surface (mirrors
// cli/HarnessRpcRegistration.hpp). Registers each method on the rpcpp server. The bound type is the
// thin BackendFacade (one object per server); each method identifier IS the wire name.

#include "BackendFacade.hpp"

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
    // DSP-side JS runtime (the role kernel)
    server.template addMethod<&BackendFacade::compileScript>();
    server.template addMethod<&BackendFacade::dspLoadKernel>();
    server.template addMethod<&BackendFacade::dspSetSystems>();
    // audio render / input drive
    server.template addMethod<&BackendFacade::pressButton>();
    server.template addMethod<&BackendFacade::renderAudio>();
    server.template addMethod<&BackendFacade::setTransport>();
    server.template addMethod<&BackendFacade::setBpm>();
    server.template addMethod<&BackendFacade::setAudioRouting>();
    // DSP runtime in the render loop
    server.template addMethod<&BackendFacade::stageMidiIn>();
    // background audio thread (threaded mode)
    server.template addMethod<&BackendFacade::startAudio>();
    server.template addMethod<&BackendFacade::stopAudio>();
    server.template addMethod<&BackendFacade::audioCaptured>();
    server.template addMethod<&BackendFacade::sleepMs>();
    server.template addMethod<&BackendFacade::systemCount>();
    server.template addMethod<&BackendFacade::drainReleased>();
    server.addDiscoveryMethod();
}
