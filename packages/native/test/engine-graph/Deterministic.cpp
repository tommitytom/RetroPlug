// Every test in this binary compares a core run through the graph with an identical core run directly, so
// both must power on in the same state: RETROPLUG_DETERMINISTIC (util/Determinism.hpp) is read once per
// process, at the first core built, which is after this runs.
#include <cstdlib>

namespace {

const bool kDeterministic = [] {
#if defined(_WIN32)
    return _putenv_s("RETROPLUG_DETERMINISTIC", "1") == 0;
#else
    return setenv("RETROPLUG_DETERMINISTIC", "1", 1) == 0;
#endif
}();

}  // namespace
