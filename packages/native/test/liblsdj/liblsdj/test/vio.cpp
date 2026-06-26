#include <lsdj/vio.h>

#include <array>
#include <catch2/catch.hpp>
#include <cstring>

using namespace Catch;

SCENARIO( "Virtual Memory I/O", "[vio]" )
{
	std::array<uint8_t, 5> memory = { 'H', 'e', 'l', 'l', 'o' };

	GIVEN( "A read state at the first byte of some memory" )
	{
		lsdj_memory_access_state_t state;
		state.begin = memory.data();
		state.cur = memory.data();
		state.size = memory.size();

		REQUIRE( lsdj_mtell(static_cast<void*>(&state)) == 0 );

		WHEN( "Reading a part of the buffer" )
		{
			std::array<uint8_t, 4> output = { '\0', '\0', '\0', '\0' };

			lsdj_mread(output.data(), output.size(), static_cast<void*>(&state));

			THEN( "The same data should come out" )
			{
				REQUIRE( std::memcmp(output.data(), "Hell", 4) == 0 );
			}
		}

		WHEN( "Writing to the buffer" )
		{
			std::array<uint8_t, 4> sample = { 'Y', 'o', 'y', 'o' };

			lsdj_mwrite(sample.data(), sample.size(), static_cast<void*>(&state));

			THEN( "Data is written at the correct position")
			{
				REQUIRE( std::memcmp(memory.data(), "Yoyoo", 5) == 0 );
			}
		}

		WHEN( "Seeking a new position from the beginning" )
		{
			lsdj_mseek(2, SEEK_SET, static_cast<void*>(&state));

			THEN( "We should end up at the correct offset ")
			{
				REQUIRE( state.cur - state.begin == 2 );
			}
		}

		WHEN( "Seeking a new position from the current position" )
		{
			lsdj_mseek(2, SEEK_CUR, static_cast<void*>(&state));

			THEN( "We should end up at the correct offset ")
			{
				REQUIRE( state.cur - state.begin == 2 );
			}
		}

		WHEN( "Seeking a new position from the end" )
		{
			lsdj_mseek(-2, SEEK_END, static_cast<void*>(&state));

			THEN( "We should end up at the correct offset ")
			{
				REQUIRE( state.cur - state.begin == 3 );
			}
		}
	}
}
