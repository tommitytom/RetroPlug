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
    // TODO: This is duplicated, they should come from the same source
    PluginRpcService service(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
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
    server.addMethod<&PluginRpcService::duplicateSystem>();
    server.addMethod<&PluginRpcService::clearCurrentProjectPath>();
    server.addMethod<&PluginRpcService::listSystems>();
    server.addMethod<&PluginRpcService::setFocus>();
    server.addMethod<&PluginRpcService::getFocus>();
    server.addMethod<&PluginRpcService::pressButton>();
    server.addMethod<&PluginRpcService::setLinkGroupId>();
    server.addMethod<&PluginRpcService::getMidiRouting>();
    server.addMethod<&PluginRpcService::setMidiRouting>();
    server.addMethod<&PluginRpcService::getAudioRouting>();
    server.addMethod<&PluginRpcService::setAudioRouting>();
    server.addMethod<&PluginRpcService::getZoom>();
    server.addMethod<&PluginRpcService::setZoom>();
    server.addMethod<&PluginRpcService::getLayout>();
    server.addMethod<&PluginRpcService::setLayout>();
    server.addMethod<&PluginRpcService::resetSystem>();
    server.addMethod<&PluginRpcService::newSram>();
    server.addMethod<&PluginRpcService::setFastBoot>();
    server.addMethod<&PluginRpcService::setModel>();
    server.addMethod<&PluginRpcService::setHighpass>();
    server.addMethod<&PluginRpcService::setReloadOnRomChange>();
    server.addMethod<&PluginRpcService::setLsdjSyncConfig>();
    server.addMethod<&PluginRpcService::setWindowSize>();
    server.addMethod<&PluginRpcService::isWindowSizeControlled>();
    server.addMethod<&PluginRpcService::getKitsConfig>();
    server.addMethod<&PluginRpcService::compileAndPatchKit>();
    server.addMethod<&PluginRpcService::auditionSample>();
    server.addMethod<&PluginRpcService::eraseKit>();
    server.addMethod<&PluginRpcService::openSampleBrowser>();
    server.addMethod<&PluginRpcService::getUserConfig>();
    server.addMethod<&PluginRpcService::setActiveKeyboardBindings>();
    server.addMethod<&PluginRpcService::setActiveGamepadBindings>();
    server.addMethod<&PluginRpcService::openSettingsFolder>();
    server.addMethod<&PluginRpcService::saveSram>();
    server.addMethod<&PluginRpcService::openSaveSramBrowser>();
    server.addMethod<&PluginRpcService::saveState>();
    server.addMethod<&PluginRpcService::openSaveStateBrowser>();
    server.addMethod<&PluginRpcService::openLoadStateBrowser>();
    server.addMethod<&PluginRpcService::getRecentFiles>();
    server.addMethod<&PluginRpcService::getMemory>();
    server.addMethod<&PluginRpcService::subscribeMemory>();
    server.addMethod<&PluginRpcService::unsubscribeMemory>();
    server.addDiscoveryMethod();

    std::cout << server.dumpSchema() << std::endl;
    return 0;
}
