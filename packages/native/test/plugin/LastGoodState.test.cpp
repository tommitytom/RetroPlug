// LastGoodState: the cache that stops a failed project serialization from reaching the host as an
// empty project. Pure value logic, so it is tested directly rather than by standing up a PluginDSP
// (which needs a DAW, a live QuickJS runtime and a failure that cannot be provoked on demand).
#include <catch2/catch_test_macros.hpp>

#include "LastGoodState.hpp"

using retroplug::LastGoodState;

TEST_CASE("a successful serialization is passed straight through", "[laststate]") {
    LastGoodState s;
    REQUIRE(s.empty());
    REQUIRE(s.offer("chunk-a") == "chunk-a");
    REQUIRE_FALSE(s.empty());
    REQUIRE(s.fallbacks() == 0);
}

TEST_CASE("an empty serialization reports the previous chunk instead", "[laststate]") {
    LastGoodState s;
    s.offer("chunk-a");
    REQUIRE(s.offer("") == "chunk-a"); // the host is told the last thing that worked, not ""
    REQUIRE(s.fallbacks() == 1);
}

TEST_CASE("a later success replaces the cache and clears the fallback run", "[laststate]") {
    LastGoodState s;
    s.offer("chunk-a");
    s.offer("");
    REQUIRE(s.offer("chunk-b") == "chunk-b");
    REQUIRE(s.fallbacks() == 0);
    REQUIRE(s.offer("") == "chunk-b"); // ...and it is chunk-b that is now held
}

TEST_CASE("failing before any success yields empty, not a fabricated chunk", "[laststate]") {
    LastGoodState s;
    REQUIRE(s.offer("").empty()); // nothing good has ever been seen: "" is the honest answer
    REQUIRE(s.empty());
    REQUIRE(s.fallbacks() == 0); // no substitution happened, so there is nothing to log
}

TEST_CASE("only the first of a run of failures is worth logging", "[laststate]") {
    LastGoodState s;
    s.offer("chunk-a");
    REQUIRE(s.fallbacks() == 0);
    s.offer("");
    REQUIRE(s.fallbacks() == 1); // the caller logs here
    s.offer("");
    s.offer("");
    REQUIRE(s.fallbacks() == 3); // ...and stays quiet through the rest of the failure loop
}
