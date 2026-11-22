// The pair variant's second package (CMakeLists.txt's packfixture-pair): FixtureNodes.cpp compiled again in the same
// module under its own prefix, package id and entry, so the pack's library returns two packages. Its prefix is the
// good pack's, rp.packfixture, whose types this test binary also links statically: an Engine given that copy holds the
// second package's types and must keep the whole pair out, the first package with it.
#undef RP_PACKFIXTURE_PREFIX
#undef RP_PACKFIXTURE_PACKAGE
#undef RP_PACKFIXTURE_ENTRY
#define RP_PACKFIXTURE_PREFIX "rp.packfixture"
#define RP_PACKFIXTURE_PACKAGE "rp.packfixture-pair-second"
#define RP_PACKFIXTURE_ENTRY rp_packfixture_second_package
#include "FixtureNodes.cpp"
