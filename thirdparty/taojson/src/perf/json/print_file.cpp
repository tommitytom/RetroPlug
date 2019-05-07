// Copyright (c) 2018-2019 Dr. Colin Hirsch and Daniel Frey
// Please see LICENSE for license or visit https://github.com/taocpp/json/

#include <tao/json.hpp>

#include "bench_mark.hpp"

int main( int argc, char** argv )
{
   for( int i = 1; i < argc; ++i ) {
      tao::json::events::to_value consumer;
      tao::json::events::parse_file( consumer, argv[ i ] );

      tao::bench::mark( "json", argv[ i ], [&]() {
         tao::json::to_string( consumer.value );
      } );
   }
   return 0;
}