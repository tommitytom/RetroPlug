#pragma once

// The port kinds and directions, the param types, and the bounds a port's queue and a settings field are sized by:
// what the authoring layer's port wrappers and a node's settings need on both builds of graph/node.hpp, apart from the
// descriptor model (Descriptor.hpp), which is the desktop's and includes this.

#include <cstddef>
#include <cstdint>

namespace rp::graph {

enum class PortKind : std::uint8_t { Audio, Events, Bytes, Control, Link, Resource };
enum class PortDir : std::uint8_t { In, Out, Link };  // link ports are undirected

enum class ParamType : std::uint8_t { Float, Int, Enum, Bool };

// An events or bytes output's queue: the most messages, and the most payload bytes, one block carries. A push past
// either is dropped and counted (rp_queue::dropped). An output may declare either part or both, as
// "capacity": {"messages": n, "bytes": n}; a part it leaves undeclared takes the default. A wire carries its
// source's queue (an output queue, or a feedback wire's front, which is sized from the source too), so an input has
// no queue of its own to size, and one that declares a capacity is refused rather than silently ignored.
constexpr std::uint32_t kDefaultQueueMessages = 256;
constexpr std::uint32_t kDefaultQueueBytes = 4096;
// The most a port may declare, so a typo cannot allocate gigabytes.
constexpr std::uint32_t kMaxQueueMessages = 1u << 20;  // 1 Mi
constexpr std::uint32_t kMaxQueueBytes = 16u << 20;    // 16 MiB

// The most channels one audio port carries; the planner refuses a port that resolves to more.
constexpr std::uint32_t kMaxChannels = 64;

// The bytes a layout name is stored in by a node's settings: "mono", "stereo", "5.1", "7.1" and "discrete(N)" with N up
// to kMaxChannels, whose longest spelling "discrete(64)" is twelve bytes; sixteen holds every name layoutChannels
// accepts.
constexpr std::size_t kMaxLayoutName = 16;

// The most alignment a device gives a node instance, and the alignment of an instance's tail memory there: the
// RP2350's alignof(std::max_align_t). A device-mode build aligns its instances to this rather than to the compiler's
// max_align_t, so the desktop's device-mode build of a type asks what the device's asks (16 on x86-64 would hide a type
// that needs 16, which the device cannot give), and the plan exporter refuses a type that asks more at export, as the
// device's loader would at boot (graph/PlanBlob.hpp's kTargetAlign). What that refuses falsely at export is a type
// whose own alignment is more on the desktop than on the device: one holding a long double or an alignas(max_align_t)
// member (16 on x86-64 and aarch64, 8 on the M33). No portable type holds either, and refusing is the safe direction.
constexpr std::size_t kDeviceAlign = 8;

}  // namespace rp::graph
