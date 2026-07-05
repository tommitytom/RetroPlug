#pragma once

// Single source of truth for the BackendRpcService method surface (mirrors
// cli/HarnessRpcRegistration.hpp). Registers each method on the rpcpp server.

#include "BackendRpcService.hpp"

template <class Server>
void registerBackendRpcMethods(Server& server) {
    // filesystem
    server.template addMethod<&BackendRpcService::readFile>();
    server.template addMethod<&BackendRpcService::writeFile>();
    server.template addMethod<&BackendRpcService::writeFileAtomic>();
    server.template addMethod<&BackendRpcService::fileExists>();
    server.template addMethod<&BackendRpcService::rename>();
    server.template addMethod<&BackendRpcService::listDir>();
    server.template addMethod<&BackendRpcService::deleteFile>();
    server.template addMethod<&BackendRpcService::drainChangedPaths>();
    // paths / config
    server.template addMethod<&BackendRpcService::canonicalize>();
    server.template addMethod<&BackendRpcService::readFilePrefix>();
    server.template addMethod<&BackendRpcService::configDir>();
    // codec
    server.template addMethod<&BackendRpcService::zip>();
    server.template addMethod<&BackendRpcService::unzip>();
    server.addDiscoveryMethod();
}
