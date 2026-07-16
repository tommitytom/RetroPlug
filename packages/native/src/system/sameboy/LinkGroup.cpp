// LinkGroup is a membership container; the per-block lockstep that used to live
// here (LinkGroup::onProcess) moved into runUnit() in system/BlockRunner.cpp so
// each linked member can be routed to its own output bus. The only out-of-line
// bit is addMember, which caches each member as an upcast SystemBase* — the
// SameBoySystem* -> SystemBase* conversion needs the complete type, which the
// header can't see.
#include "system/sameboy/LinkGroup.hpp"

#include "system/SystemBase.hpp"
#include "system/sameboy/SameBoySystem.hpp"

void LinkGroup::addMember(SameBoySystem* sys) {
    members_.push_back(sys);
    membersBase_.push_back(sys);   // implicit derived->base upcast (complete type here)
}
