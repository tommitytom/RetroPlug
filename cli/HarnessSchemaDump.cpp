// Dumps the HarnessRpcService OpenRPC schema to stdout for the codegen
// (tools/gen-rpc-ts.js -> build/generated/HarnessService.ts).
//
// A standalone exe, deliberately free of the txiki/QuickJS runtime, so the
// codegen step doesn't depend on the binary that *embeds* the CLI bundle —
// breaking what would otherwise be a build cycle (retroplug-cli embeds a bundle
// built from this schema). Mirrors src/RpcSchemaDump.cpp for the plugin.
//
// The schema generator inspects compile-time reflection on the method
// signatures only — no method runs, so the null-Impl constructor is safe.

#include <iostream>

#include "HarnessRpcRegistration.hpp"
#include "HarnessRpcService.hpp"
#include "SchemaDumpCodec.hpp"
#include "TypedRpcServer.h"
#include "transports/QueueTransport.h"

int main() {
    // Schema-only stub codec (see src/SchemaDumpCodec.hpp): the schema is
    // codec-independent, and the runtime QuickJS codec would need a JSContext.
    HarnessRpcService service(nullptr);
    rpcpp::QueueTransport<SchemaDumpCodec> transport;
    rpcpp::TypedRpcServer<HarnessRpcService, SchemaDumpCodec> server(service, transport);

    registerHarnessRpcMethods(server);

    std::cout << server.dumpSchema() << std::endl;
    return 0;
}
