#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

// The value types of an offline job's seam (docs/modular-graph-phase-d2-design.md section 3): a submission, a window
// and a completion, for a graph job and for an engine job (docs/modular-graph-phase-d3-design.md section 3). They sit
// apart from the pool (OfflineJobs.hpp, which documents the jobs) so the engine's header names them without dragging
// in the pool's threads, queue and node registry.
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

// An engine job (docs/modular-graph-phase-d3-design.md section 3): a script bundle of `kind` run on a pool worker with
// a QuickJS runtime and an Engine of its own, given `spec`. D3 has one kind, "render", the render-worker bundle
// (RenderHost), whose spec is the render job JSON it reads; the pool refuses any other kind at submission.
struct EngineJobRequest {
    std::string kind;
    std::string spec;
    // Files the submission owns, which the pool deletes however the job ends: on the job's worker once its body has
    // returned and before its completion is queued, and with the job itself on a road that drops it unrun (a refusal,
    // a cancel while queued, the pool's teardown). Empty for a caller with none, so a user's own `--sav` or `--state`
    // is never in it. A UI render lists the snapshots it wrote into the config directory, which every process and
    // instance shares.
    std::vector<std::string> discardFiles;
};

// One console write a job's script made (console.log, console.warn and the rest), without the newline the console
// ends each with, and which stream it went to. A write can hold newlines of its own: a multi-line string, or a script
// exception's report with its stack.
enum class EngineJobStream : std::uint8_t { Out, Err };
struct EngineJobLine {
    EngineJobStream stream = EngineJobStream::Out;
    std::string     text;
};

// A running engine job's state at a drain: how much audio it has rendered (its last report, in milliseconds), the
// console lines it wrote since the drain before, and how many lines it wrote past the record's bound in that time,
// which were dropped (EngineJobRecord, in OfflineJobs.hpp). A render raises its report chunk by chunk; one that
// detects a song's end lowers it once, at its end, to the length it wrote.
struct EngineJobProgress {
    std::uint32_t              id = 0;
    double                     renderedMs = 0.0;
    std::vector<EngineJobLine> lines;
    std::uint32_t              droppedLines = 0;
};

// A finished engine job: `status` "done", "cancelled" or "error", `message` the error's (empty otherwise), `outputs`
// the files it wrote, and the progress fields as a drain reports them, which here are its final ones: the rendered
// length it ended at (for a render that detected its song's end, the length it wrote, which can be below its earlier
// reports), and the lines it wrote since its last progress report.
struct EngineJobCompletion {
    std::uint32_t              id = 0;
    std::string                status;
    std::string                message;
    std::vector<std::string>   outputs;
    double                     renderedMs = 0.0;
    std::vector<EngineJobLine> lines;
    std::uint32_t              droppedLines = 0;
};

// The engine jobs' part of a drain: the completions since the last drain, oldest first, and every running engine
// job's progress.
struct EngineJobsDrain {
    std::vector<EngineJobCompletion> completions;
    std::vector<EngineJobProgress>   progress;
};

}  // namespace rp::host
