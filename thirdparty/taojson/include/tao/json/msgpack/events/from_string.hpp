// Copyright (c) 2017-2019 Dr. Colin Hirsch and Daniel Frey
// Please see LICENSE for license or visit https://github.com/taocpp/json/

#ifndef TAO_JSON_MSGPACK_EVENTS_FROM_STRING_HPP
#define TAO_JSON_MSGPACK_EVENTS_FROM_STRING_HPP

#include <string>
#include <string_view>

#include "../../external/pegtl/memory_input.hpp"
#include "../../external/pegtl/parse.hpp"

#include "../internal/grammar.hpp"

namespace tao
{
   namespace json
   {
      namespace msgpack
      {
         namespace events
         {
            template< typename Consumer >
            void from_string( Consumer& consumer, const char* data, const std::size_t size, const char* source = nullptr, const std::size_t byte = 0, const std::size_t line = 1, const std::size_t byte_in_line = 0 )
            {
               pegtl::memory_input< pegtl::tracking_mode::lazy, pegtl::eol::lf_crlf, const char* > in( data, data + size, source ? source : "tao::json::msgpack::from_string", byte, line, byte_in_line );
               pegtl::parse< msgpack::internal::grammar >( in, consumer );
            }

            template< typename Consumer >
            void from_string( Consumer& consumer, const char* data, const std::size_t size, const std::string& source, const std::size_t byte = 0, const std::size_t line = 1, const std::size_t byte_in_line = 0 )
            {
               msgpack::events::from_string( consumer, data, size, source.c_str(), byte, line, byte_in_line );
            }

            template< typename Consumer, typename... Ts >
            void from_string( Consumer& consumer, const std::string_view data, Ts&&... ts )
            {
               msgpack::events::from_string( consumer, data.data(), data.size(), std::forward< Ts >( ts )... );
            }

         }  // namespace events

      }  // namespace msgpack

   }  // namespace json

}  // namespace tao

#endif
