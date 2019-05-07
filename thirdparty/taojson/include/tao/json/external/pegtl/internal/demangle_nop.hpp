// Copyright (c) 2014-2019 Dr. Colin Hirsch and Daniel Frey
// Please see LICENSE for license or visit https://github.com/taocpp/PEGTL/

#ifndef TAO_JSON_PEGTL_INTERNAL_DEMANGLE_NOP_HPP
#define TAO_JSON_PEGTL_INTERNAL_DEMANGLE_NOP_HPP

#include <string>

#include "../config.hpp"

namespace TAO_JSON_PEGTL_NAMESPACE::internal
{
   [[nodiscard]] inline std::string demangle( const char* symbol )
   {
      return symbol;
   }

}  // namespace TAO_JSON_PEGTL_NAMESPACE::internal

#endif
