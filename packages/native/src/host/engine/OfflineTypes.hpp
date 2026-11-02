#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

// The value types of an offline job's seam (docs/modular-graph-phase-d2-design.md section 3): a submission, a window
// and a completion. They sit apart from the pool (OfflineJobs.hpp, which documents the jobs) so the engine's header
// names them without dragging in the pool's threads, queue and node registry.
namespace rp::host {

// How a file input's frame cap scales a frame count at a kit rate back to the decoded rate, reproducing the two kit
// windows exactly, both of which depend on the decoded rate a submission cannot know:
//   - FloatRatio, the LSDj window's: ceil(float(frames) * (float(decodedRate) / float(rate))), every step in float,
//     the ratio first;
//   - DoubleScale, risa's DMC window: ceil(double(frames) * double(decodedRate) / rate), in double, the product first.
enum class OfflineCapArithmetic : std::uint8_t { FloatRatio, DoubleScale };

struct OfflineCap {
    std::uint32_t        frames = 0;   // at `rate`
    double               rate = 0.0;
    OfflineCapArithmetic arithmetic = OfflineCapArithmetic::FloatRatio;
};

// Audio the submission carries: interleaved, `channels` wide, at `rate`.
struct OfflineSamples {
    std::vector<float> samples;
    std::uint32_t      channels = 0;
    double             rate = 0.0;
};

// A file decoded through SampleCache (miniaudio, mono float at the file's own rate, the decode a kit compile reads its
// sources through) and windowed: from `offset` (clamped to the end), `length` frames (0: to the end), at most the
// cap's frames.
struct OfflineFile {
    std::string               path;
    std::uint32_t             offset = 0;
    std::uint32_t             length = 0;
    std::optional<OfflineCap> cap;
};

using OfflineInput = std::variant<OfflineSamples, OfflineFile>;

// An empty buffer for a recorder to write.
struct OfflineOutput {
    std::uint32_t channels = 0;
    std::uint32_t frames = 0;
    double        rate = 0.0;
};

struct OfflineRequest {
    std::string                document;  // a graph document; empty for none (the job returns its inputs)
    std::vector<OfflineInput>  inputs;
    std::vector<OfflineOutput> outputs;
    std::vector<std::uint32_t> returns;   // the handles whose buffers the completion carries, in this order
    std::uint32_t              frames = 0;
    double                     rate = 48000.0;  // the plan's rate
};

// The frames a window keeps of a decoded buffer, as the kit compilers cut it: `start` is the offset clamped to the
// end, and `frames` the least of the length (all of what is left when 0), what is left, and the cap at
// `decodedRate`. An offset at or past the end, or a cap of 0 frames, keeps nothing; a zero length is no limit. A cap
// whose scaled frame count is not finite or is past size_t's range has no window: the job refuses it rather than
// casting.
struct OfflineWindow {
    std::size_t start = 0;
    std::size_t frames = 0;
};
std::optional<OfflineWindow> offlineWindow(std::size_t decodedFrames, std::uint32_t decodedRate,
                                           std::uint32_t offset, std::uint32_t length,
                                           const std::optional<OfflineCap>& cap);

// submit's answer: the job's id (counting up from 1, never reused), or 0 with the refusal in `error`.
struct OfflineSubmit {
    std::uint32_t id = 0;
    std::string   error;
};

// A buffer a completion returns: interleaved, `channels` x `frames`, and whether any sample is not finite (read as the
// samples are copied out, so a caller that refuses a non-finite recording, as a kit compile does, need not scan them
// itself, which in a script runtime costs more than the job).
struct OfflineBuffer {
    std::uint32_t      handle = 0;
    std::uint32_t      channels = 0;
    std::uint32_t      frames = 0;
    double             rate = 0.0;
    std::vector<float> samples;
    bool               nonFinite = false;
};

// What a file input decoded to and kept: the decoded rate and length, the window's length, and whether any sample the
// window kept is not finite (a kit compile refuses such a slot).
struct OfflineInputReport {
    std::uint32_t handle = 0;
    std::uint32_t decodedRate = 0;
    std::uint32_t decodedFrames = 0;
    std::uint32_t frames = 0;
    bool          nonFinite = false;
};

// A finished job: ok with the buffers `returns` named, or the reason it failed. `inputs` reports each file input
// decoded before the job ended, a failed job's included. `guardsFired` counts the times a node's guard replaced what
// its kernel would have computed during the run (rp.host.guards/1, which each job's host offers its nodes), and
// `guardReason` is the first one's reason, empty when none fired: a job a guard stepped into still completes, and what
// it means is the caller's to decide (a kit compile refuses the pass, since its bytes are the kernels'; a render can
// accept a recovering filter).
struct OfflineCompletion {
    std::uint32_t                   id = 0;
    bool                            ok = false;
    std::string                     error;
    std::vector<OfflineBuffer>      buffers;
    std::vector<OfflineInputReport> inputs;
    std::uint32_t                   guardsFired = 0;
    std::string                     guardReason;
};

}  // namespace rp::host
