#pragma once

// The exported plan's format (docs/modular-graph-phase-e3-design.md section 4): what the desktop's exportPlan writes
// (PlanExport.hpp) and a device's loadPlan reads (PlanLoader.hpp), the one definition both sides share. Portable: no
// heap, no exceptions, no standard container, so a device build includes it as the desktop's does.
//
// A blob is little-endian and 4-byte aligned, and holds no pointers: every reference is an index into a pool or an
// offset into a section. It is a header, a section table, the sections in the table's order and a CRC-32 of every byte
// before it. A section is an array of fixed-size records (or of bytes); the table gives each one's offset from the
// blob's start, its count and its record size, which the reader holds to its own so a writer of another layout is
// refused rather than misread (the version says which layout a writer meant; the record size catches one that lies).
//
// What a plan holds after the desktop planned it (PlanStorage.hpp) maps onto it pool for pool:
//
// - the nodes, in the document's order: id, type id and settings (msgpack, written from the instance's parsed
//   settings), its coupled group (the group's position in the schedule) and its step;
// - the steps, in the schedule's order: the node, the coupled group's size on its first member (0 elsewhere, and for
//   a node that is not coupled), and the step's input, output and param views as ranges of the view tables (each
//   param view saying whether a wire drives it);
// - the pools: audio buffers (each `maxFrames` floats, zeroed at load: the shared silence, the scratch every
//   unconnected output writes, every wire's buffer and each feedback front's channels), the channel table (which
//   buffer each entry of an audio view's channel array is), message queues by capacity (the empty one every
//   unconnected input reads, the scratch one every unconnected output writes, every wired output's and each events or
//   bytes feedback front's), control streams by capacity and start value (the empty one, every param's and control
//   output's, each control feedback front's) and resource cells (each a null handle at load);
// - after each block: the queues to clear, each with the node its drops are charged to, the streams to roll, the
//   feedback copies;
// - at load, after the nodes are created and activated, in PlanStorage::adopt's order: each linked node's peers, each
//   node's initial connected-output mask, and each param's start value from the document.
//
// Not carried: instance sizes (a device asks its own build of each type), resources (a plan that names one is refused
// at export) and settings commits (every device instance is created from its settings and never changes them).

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "graph/PortTypes.hpp"

namespace rp::graph::blob {

static_assert(std::endian::native == std::endian::little,
              "a plan blob is little-endian and is written and read as the host's own words");

inline constexpr std::uint8_t  kMagic[4] = {'R', 'P', 'P', 'L'};
inline constexpr std::uint32_t kVersion = 1;
// The targets a blob is exported for. The RP2350 is the only one; the field is there so a later target's blob is
// refused by name rather than run.
inline constexpr std::uint32_t kTargetRp2350 = 1;
inline constexpr const char*   kTargetName = "rp2350";
// The most alignment a node instance may ask of an rp2350 arena: the device's max_align_t (graph::kDeviceAlign, which a
// device-mode build aligns its instances to). The loader refuses a type that asks more, on the device and in the
// exporter's sizing pass alike, so a type the device cannot place fails the firmware's build, not its boot.
inline constexpr std::uint32_t kTargetAlign = static_cast<std::uint32_t>(kDeviceAlign);
// "No node": a queue to clear whose drops are charged to nobody (the scratch queue unconnected outputs share).
inline constexpr std::uint32_t kNoNode = 0xFFFFFFFFu;

// The sections, in the order the table lists them and the blob holds them.
enum class Section : std::uint32_t {
    Strings,      // bytes: node ids and type ids
    Settings,     // bytes: each node's settings, msgpack
    Nodes,        // NodeRecord
    Steps,        // StepRecord
    Views,        // ViewRecord: every step's inputs then outputs
    ParamViews,   // ParamViewRecord
    Channels,     // u32: the buffer each channel-table entry is
    Queues,       // QueueRecord
    Streams,      // StreamRecord
    Clears,       // ClearRecord
    Rolls,        // u32: a stream rolled after each block
    Feedback,     // FeedbackRecord
    Masks,        // MaskRecord
    MaskBytes,    // bytes: the masks' connected flags, 0 or 1
    Peers,        // PeerRecord
    PeerNodes,    // u32: the node each peer entry is
    ParamStarts,  // ParamStartRecord
    Count
};
inline constexpr std::uint32_t kSectionCount = static_cast<std::uint32_t>(Section::Count);

struct SectionEntry {
    std::uint32_t offset;       // from the blob's first byte, a multiple of 4
    std::uint32_t count;        // records (bytes, for a byte section)
    std::uint32_t recordBytes;  // the size of one record: 1 for a byte section
};

struct Header {
    std::uint8_t  magic[4];
    std::uint32_t version;
    std::uint32_t target;        // kTarget*
    std::uint32_t size;          // the whole blob, its CRC included
    std::uint32_t sampleRate[2];  // the plan's sample rate, a double's bits, low word first
    std::uint32_t maxFrames;     // every audio buffer's length, and the longest block the plan runs
    std::uint32_t arenaBytes;    // the arena the exporter sized it against (0: none was given)
    std::uint32_t buffers;       // the audio buffer pool's size
    std::uint32_t cells;         // the resource cell pool's size
    std::uint32_t sectionCount;  // kSectionCount
    SectionEntry  sections[kSectionCount];
};

// A param as its node reads it: the stream, and whether a wire drives it (the stream is then the wire's source, a
// control output or a feedback front, which a host must not write: the desktop's setParam refuses a wired param).
struct ParamViewRecord {
    std::uint32_t stream;
    std::uint32_t wired;  // 0 or 1
};

// A run of a byte section: [offset, offset + size).
struct Bytes {
    std::uint32_t offset;
    std::uint32_t size;
};

struct NodeRecord {
    Bytes         id;        // Strings
    Bytes         type;      // Strings
    Bytes         settings;  // Settings
    std::uint32_t group;     // its coupled group's position in the schedule (a node not coupled is a group of one)
    std::uint32_t step;      // its step
};

struct StepRecord {
    std::uint32_t node;
    std::uint32_t groupSize;  // > 1 on the first member of a coupled group of that many consecutive steps, else 0
    std::uint32_t firstInput, inputCount;    // Views
    std::uint32_t firstOutput, outputCount;  // Views
    std::uint32_t firstParam, paramCount;    // ParamViews
};

// One port as its node sees it. `index` and `count` by kind (rp_port_kind): audio, a run of the channel table at
// `index`, `count` channels long; events and bytes, the queue `index`; control, the stream `index`; resource, the cell
// `index`; link, nothing.
struct ViewRecord {
    std::uint32_t kind;
    std::uint32_t index;
    std::uint32_t count;
};

struct QueueRecord {
    std::uint32_t messages;
    std::uint32_t bytes;
};

struct StreamRecord {
    std::uint32_t changes;   // its capacity
    std::uint32_t start[2];  // its value at load, a double's bits, low word first
};

struct ClearRecord {
    std::uint32_t queue;
    std::uint32_t node;  // whose drops what it drops are, or kNoNode
};

// A feedback wire's copy (Plan.hpp's FeedbackCopy). By kind: audio, `back` and `front` are runs of the channel table
// `channels` long, and `frames` is how many frames each front channel holds (at most maxFrames); events and bytes,
// queues; control, streams.
struct FeedbackRecord {
    std::uint32_t kind;
    std::uint32_t back;
    std::uint32_t front;
    std::uint32_t channels;
    std::uint32_t frames;
};

// A node's connected-output mask, `count` flags at `offset` in MaskBytes: count is the node's output count.
struct MaskRecord {
    std::uint32_t node;
    std::uint32_t offset;
    std::uint32_t count;
};

// A linked node's peers: `count` entries of PeerNodes from `first`.
struct PeerRecord {
    std::uint32_t node;
    std::uint32_t first;
    std::uint32_t count;
};

// A param's start value from the document: the stream starts there, its queued changes dropped (PlanStorage::adopt).
struct ParamStartRecord {
    std::uint32_t stream;
    std::uint32_t value[2];  // a double's bits, low word first
};

// The record size each section holds (1 for bytes).
constexpr std::uint32_t recordBytes(Section s) {
    switch (s) {
        case Section::Strings:
        case Section::Settings:
        case Section::MaskBytes: return 1;
        case Section::Nodes: return sizeof(NodeRecord);
        case Section::Steps: return sizeof(StepRecord);
        case Section::Views: return sizeof(ViewRecord);
        case Section::Queues: return sizeof(QueueRecord);
        case Section::Streams: return sizeof(StreamRecord);
        case Section::Clears: return sizeof(ClearRecord);
        case Section::Feedback: return sizeof(FeedbackRecord);
        case Section::Masks: return sizeof(MaskRecord);
        case Section::Peers: return sizeof(PeerRecord);
        case Section::ParamStarts: return sizeof(ParamStartRecord);
        case Section::ParamViews: return sizeof(ParamViewRecord);
        case Section::Channels:
        case Section::Rolls:
        case Section::PeerNodes:
        case Section::Count: break;
    }
    return sizeof(std::uint32_t);
}

// Every record is whole words, so a section of them keeps the blob 4-byte aligned; a byte section is padded to 4.
static_assert(sizeof(Header) % 4 == 0 && sizeof(NodeRecord) % 4 == 0 && sizeof(StepRecord) % 4 == 0 &&
              sizeof(ViewRecord) % 4 == 0 && sizeof(StreamRecord) % 4 == 0 && sizeof(FeedbackRecord) % 4 == 0 &&
              sizeof(ParamStartRecord) % 4 == 0);
static_assert(sizeof(Header) == 44 + 12 * kSectionCount, "the header is words, with no padding");

// A double as the two words a blob holds it in, and back.
inline void splitDouble(double v, std::uint32_t (&out)[2]) {
    const auto bits = std::bit_cast<std::uint64_t>(v);
    out[0] = static_cast<std::uint32_t>(bits);
    out[1] = static_cast<std::uint32_t>(bits >> 32);
}
inline double joinDouble(const std::uint32_t (&in)[2]) {
    return std::bit_cast<double>(std::uint64_t{in[0]} | (std::uint64_t{in[1]} << 32));
}

// CRC-32 (IEEE 802.3, the reflected 0xEDB88320 polynomial, as zlib's crc32), bit by bit: a blob is read once, at load,
// so a table's kilobyte of flash buys nothing.
inline std::uint32_t crc32(const std::uint8_t* data, std::size_t size) {
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int k = 0; k < 8; ++k) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

}  // namespace rp::graph::blob
