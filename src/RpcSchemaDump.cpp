// Tiny executable that dumps the PluginRpcService's OpenRPC schema to stdout.
//
// The schema generator inspects compile-time reflection on the method
// signatures — no service methods are actually called, so the all-null
// constructor is safe. Used by tools/gen-rpc-ts.js to generate the typed
// TypeScript client at build time.

#include <cstdio>
#include <iostream>

#include "PluginRpcRegistration.hpp"
#include "PluginRpcService.hpp"
#include "TypedRpcServer.h"
#include "codecs/MsgpackCodec.h"
#include "transports/QueueTransport.h"

int main() {
    PluginRpcService service(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
    rpcpp::QueueTransport<rpcpp::MsgpackCodec> transport;
    rpcpp::TypedRpcServer<PluginRpcService, rpcpp::MsgpackCodec> server(service, transport);

    registerPluginRpcMethods(server);

    std::cout << server.dumpSchema() << std::endl;
    return 0;
}
