#pragma once

// In-process rpcpp codec that marshals C++ values directly to/from Node-API values via reflect-cpp
// (see napi/*), with no byte serialization and no framing. The N-API twin of rpcpp's QuickJSCodec.
//
// Like the QuickJS codec it satisfies Codec but deliberately NOT WireCodec: there is no sensible way
// to put live napi_values on a stream transport, so StdioTransport rejects it. (If you want RetroPlug
// over a pipe, use rpcpp's existing JSON/Msgpack codecs and its TypeScript stdio client - but note
// that client is async, and the whole TS layer calls the backend synchronously.)
//
// It DOES opt into NativeAstCodec via ast_view_t, which is the load-bearing part: input_t is a live,
// re-readable AST node, so RpcServer decodes method params straight into their typed tuple instead of
// through the lossy rfl::Generic intermediate. That is what preserves an rfl::Bytestring param as a
// Uint8Array rather than degrading it to an array of numbers.

#include <functional>

#include <node_api.h>

#include "napi/read.hpp"
#include "napi/write.hpp"

namespace rpcpp {

// Output of the Node codec. Construction of the actual napi_value is DEFERRED into this thunk so
// that NodeCodec::write(...) is safe to call from any thread - async handlers resolve on worker
// threads, but a napi_env may only be touched on the JS thread. The thunk is invoked on the JS
// thread: by NodeTransport::drain for async responses, or directly by the caller for the
// synchronous processMessage return.
//
// The thunk captures only plain C++ values, never a napi_value, so moving a NodeOut across threads
// touches no JS state.
struct NodeOut {
    std::function<napi_value(napi_env)> materialize;
};

class NodeCodec {
 public:
    using input_t  = napi_value;
    using output_t = NodeOut;
    // Opt into NativeAstCodec - see the header comment.
    using ast_view_t = napi_value;

    explicit NodeCodec(napi_env _env) : _env(_env) {}

    template <class T>
    auto read(napi_value value) const {
        return napi::read<T>(_env, value);
    }

    template <class T>
    output_t write(const T& value) const {
        return NodeOut{
            [value](napi_env env) { return napi::write(env, value); },
        };
    }

 private:
    napi_env _env;
};

}  // namespace rpcpp
