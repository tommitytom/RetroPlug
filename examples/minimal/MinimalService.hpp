#pragma once

// A trivial rpcpp service standing in for a real consumer's RPC surface. Used
// only by the dpf.js seam probe (MinimalProbe.cpp) to instantiate the framework
// templates against an arbitrary Service with zero RetroPlug coupling.
struct MinimalService {
    int ping() { return 0; }
};

template <class Server>
void registerMinimalRpcMethods(Server& server) {
    server.template addMethod<&MinimalService::ping>();
    server.addDiscoveryMethod();
}
