// Tiny executable that dumps the PluginRpcService's OpenRPC schema to stdout.
//
// The schema generator inspects compile-time reflection on the method
// signatures — no service methods are actually called, so the all-null
// constructor is safe. Used by tools/gen-rpc-ts.js to generate the typed
// TypeScript client at build time.

#include <cstdio>
#include <iostream>

#include "PluginRpcService.hpp"
#include "TypedRpcServer.h"
#include "codecs/MsgpackCodec.h"
#include "transports/QueueTransport.h"

int main() {
    PluginRpcService service(nullptr, nullptr, nullptr, nullptr, nullptr);
    rpcpp::QueueTransport<rpcpp::MsgpackCodec> transport;
    rpcpp::TypedRpcServer<PluginRpcService, rpcpp::MsgpackCodec> server(service, transport);

    server.addMethod<&PluginRpcService::getFrame>();
    server.addMethod<&PluginRpcService::openRomBrowser>();
    server.addMethod<&PluginRpcService::openSaveProjectBrowser>();
    server.addMethod<&PluginRpcService::openLoadProjectBrowser>();
    server.addMethod<&PluginRpcService::loadRomFromPath>();
    server.addMethod<&PluginRpcService::addRomFromPath>();
    server.addMethod<&PluginRpcService::replaceRomFromPath>();
    server.addMethod<&PluginRpcService::removeSystem>();
    server.addMethod<&PluginRpcService::listSystems>();
    server.addMethod<&PluginRpcService::setFocus>();
    server.addMethod<&PluginRpcService::getFocus>();
    server.addMethod<&PluginRpcService::pressButton>();
    server.addMethod<&PluginRpcService::setLinkGroupId>();
    server.addMethod<&PluginRpcService::getMidiRouting>();
    server.addMethod<&PluginRpcService::setMidiRouting>();
    server.addMethod<&PluginRpcService::setLsdjSyncConfig>();
    server.addMethod<&PluginRpcService::setWindowSize>();
    server.addMethod<&PluginRpcService::isWindowSizeControlled>();
    server.addDiscoveryMethod();

    std::cout << server.dumpSchema() << std::endl;
    return 0;
}
