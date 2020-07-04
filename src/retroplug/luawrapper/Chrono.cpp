#include "Wrappers.h"

#include <sol/sol.hpp>
#include <chrono>

using namespace std::chrono;

void luawrappers::registerChrono(sol::state& s) {
	s.create_named_table("chrono",
		"now", []() { return high_resolution_clock::now().time_since_epoch().count(); }
	);
}
