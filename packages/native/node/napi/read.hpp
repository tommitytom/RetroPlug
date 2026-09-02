#pragma once

#include <node_api.h>

#include <rfl/Processors.hpp>
#include <rfl/Result.hpp>
#include <rfl/internal/wrap_in_rfl_array_t.hpp>

#include "Parser.hpp"
#include "Reader.hpp"

namespace rpcpp::napi {

// Decodes a C++ value of type T directly from a live Node-API value. The input `_val` is borrowed:
// it belongs to the caller's handle scope and is only read from. Must be called on the JS thread
// with a handle scope open (the synchronous __rpcSend callback satisfies both).
template <class T, class... Ps>
rfl::Result<rfl::internal::wrap_in_rfl_array_t<T>> read(napi_env _env, napi_value _val) {
    Reader r(_env);
    return Parser<T, rfl::Processors<Ps...>>::read(r, Reader::InputVarType{_val});
}

}  // namespace rpcpp::napi
