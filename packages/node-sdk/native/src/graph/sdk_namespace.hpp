#pragma once

// Where the node SDK's C++ names live in each build of it (docs/modular-graph-phase-e1-design.md section 6).
//
// A desktop build (no RP_DEVICE) keeps them where they always were: rp, rp::graph, rp::detail. A device-mode build
// (RP_DEVICE) puts them in the inline namespace rp::device, so a source still names rp::text and
// rp::graph::pushMessage, but a program holding both builds of one node source (retroplug-device-mode-test holds the
// desktop's and the device's rp.core and rp.dsp) has two sets of names rather than one name with two definitions:
// rp::text has no str() in the device build, the device's instance owns its settings in place, and the linker would
// otherwise keep whichever definition it saw first.
//
// rp::graph and rp::detail are declared inside rp::device here, before any header opens them, which is what carries a
// node source's own namespaces along with no change to them: `namespace rp::graph {` extends whichever rp::graph
// lookup finds first, and in a device-mode build that is rp::device::graph. So every C++ header of the SDK includes
// this one first, and so does rp_node.h (under __cplusplus), which every node source's own header includes before it
// opens rp::graph. The SDK's headers that put names straight into rp (the authoring layer, the settings types) open it
// with RP_NAMESPACE_BEGIN.
//
// The convention that follows, for a source built both ways: its names with linkage live under rp::graph or
// rp::detail (rp::graph::core, rp::graph::dsp, ...), or in an anonymous namespace, or are static. A name it puts
// anywhere else (straight into rp, or into rp::dsp or rp::lsdj) is the same name in both builds, and a program holding
// both would keep one definition of it, silently (code that includes no SDK header, the rp::dsp kernels, is the same
// in both builds, and such a program links one copy). A build of one mode alone, a device image, is unaffected.
// The C entry points a package exports have no namespace at all; retroplug-device-mode-test renames them for its
// device-mode objects (cmake/tests.cmake).

#ifdef RP_DEVICE
namespace rp { inline namespace device { namespace graph {} namespace detail {} } }
#define RP_NAMESPACE_BEGIN namespace rp { inline namespace device {
#define RP_NAMESPACE_END } }
#else
#define RP_NAMESPACE_BEGIN namespace rp {
#define RP_NAMESPACE_END }
#endif
