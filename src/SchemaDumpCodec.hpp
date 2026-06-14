#pragma once

#include <string>
#include <string_view>

#include <rfl/Result.hpp>

#include "Framer.h"  // rpcpp::LineFramer

// A no-op rpcpp codec used solely by the OpenRPC schema dumpers
// (RpcSchemaDump / HarnessSchemaDump). dumpSchema() needs only reflect-cpp's
// compile-time type introspection (rfl::json::to_schema), which is
// codec-independent — but TypedRpcServer<Service, Codec> still instantiates the
// codec's read/write for every method's params/return type. None of the real
// codecs fit here: QuickJSCodec (the runtime codec) needs a live JSContext,
// MsgpackCodec needs msgpack-c (dropped with the QuickJS migration), and
// JsonCodec can't encode rfl::Bytestring (JSON has no binary type). This stub
// satisfies the codec shape without touching any format writer, so it compiles
// for any service — including ones with binary fields — while producing the
// exact same schema. No method is ever dispatched, so read/write stay inert.
struct SchemaDumpCodec {
    using input_t  = std::string_view;
    using output_t = std::string;
    using buffer_t = std::string;
    using default_in_framer  = rpcpp::LineFramer;
    using default_out_framer = rpcpp::LineFramer;
    static constexpr bool is_binary = false;

    template <class T>
    static rfl::Result<T> read(input_t) {
        return rfl::error("SchemaDumpCodec is schema-only and does not decode.");
    }

    template <class T>
    static output_t write(const T&) {
        return {};
    }
};
