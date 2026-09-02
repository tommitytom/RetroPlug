#pragma once

// Transport for the in-process Node codec - the N-API twin of rpcpp's QuickJSTransport. Async
// responses, notifications and errors are produced as deferred NodeOut thunks (see NodeCodec) and
// queued here from any thread; the JS thread materializes them into napi_values at drain() time and
// routes each to the host-supplied callback.
//
// Threading contract:
//   * send()  - safe to call from any thread (lock-free queue). Async handlers resolving on worker
//               threads land here.
//   * drain() - JS-THREAD ONLY, and only with a live handle scope.
//
// The synchronous processMessage() return does NOT pass through this queue: the caller is already on
// the JS thread and materializes that NodeOut directly. Nothing in the RetroPlug backend surface
// pushes async today (the CLI host's sink is a no-op too), so drain() is currently unwired - hooking
// it to the libuv loop via napi_threadsafe_function is the follow-up for server-push notifications.

#include <functional>
#include <utility>

#include <node_api.h>

#include <concurrentqueue.h>

#include "NodeCodec.hpp"

namespace rpcpp {

class NodeTransport {
 public:
    using output_t = NodeOut;

    explicit NodeTransport(std::function<void(napi_env, napi_value)> onResponse)
        : _onResponse(std::move(onResponse)) {}

    void send(output_t out) { _q.enqueue(std::move(out)); }

    // Materialize and deliver every queued response. `env` must be the JS thread's env, with a
    // handle scope open; the napi_values built here live in that scope.
    void drain(napi_env env) {
        output_t out;
        while (_q.try_dequeue(out)) {
            if (!out.materialize) continue;
            _onResponse(env, out.materialize(env));
        }
    }

 private:
    std::function<void(napi_env, napi_value)>  _onResponse;
    moodycamel::ConcurrentQueue<output_t>      _q;
};

}  // namespace rpcpp
