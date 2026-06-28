// LinkGroup is now a header-only membership container; the per-block lockstep
// that used to live here (LinkGroup::onProcess) moved into runBlock() in
// system/BlockRunner.cpp so each linked member can be routed to its own output
// bus. This translation unit is intentionally empty (kept so the build's source
// lists don't need to change).
#include "system/sameboy/LinkGroup.hpp"
