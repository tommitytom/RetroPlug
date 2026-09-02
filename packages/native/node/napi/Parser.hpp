#pragma once

#include <rfl/parsing/Parser.hpp>

#include "Reader.hpp"
#include "Writer.hpp"

namespace rpcpp::napi {

template <class T, class ProcessorsType>
using Parser = rfl::parsing::Parser<Reader, Writer, T, ProcessorsType>;

}  // namespace rpcpp::napi
