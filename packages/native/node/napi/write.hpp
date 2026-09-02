#pragma once

#include <type_traits>

#include <node_api.h>

#include <rfl/Processors.hpp>
#include <rfl/parsing/Parent.hpp>

#include "Parser.hpp"
#include "Writer.hpp"

namespace rpcpp::napi {

// Encodes a C++ value into a freshly built Node-API value. The result belongs to the enclosing
// handle scope (there is nothing for the caller to free). Must be called on the JS thread.
template <class... Ps>
napi_value write(napi_env _env, const auto& _obj) {
    using T          = std::remove_cvref_t<decltype(_obj)>;
    using ParentType = rfl::parsing::Parent<Writer>;
    Writer w(_env);
    Parser<T, rfl::Processors<Ps...>>::write(w, _obj, typename ParentType::Root{});
    return w.root();
}

}  // namespace rpcpp::napi
