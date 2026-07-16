#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "lsdj/Effects.hpp"
#include "lsdj/KitUtil.hpp"
#include "lsdj/SampleCache.hpp"

// Async LSDJ kit compiler. Fans per-sample resample + nibble-pack work
// across an enkiTS thread pool, then assembles the 16 KB kit bank. The
// owning instance keeps a `SampleCache` so re-compiling a kit that shares
// samples with a previous compile avoids re-reading + re-decoding the
// audio file.
//
// Threading model: `compileKit` is synchronous from the caller's
// perspective; internally it dispatches per-sample tasks to enkiTS and
// joins before returning. Callers are expected to be off the audio
// thread (typically: an rpcpp method handler).

namespace enki { class TaskScheduler; }

namespace rp::lsdj {

struct CompileSampleSpec {
    std::string             path;       // source audio file
    std::string             name;       // 3-char uppercase slot name
    std::size_t             offset = 0; // skip the first N frames of the source
    std::size_t             length = 0; // 0 = use everything from offset
    std::vector<LsdjEffect> effects;
};

struct CompiledKit {
    bool                       ok = false;
    std::string                error;
    std::vector<std::uint8_t>  bytes;        // exactly Kit::kSize on success
    std::uint64_t              hash = 0;     // FNV-64 of `bytes` (dirty tracking)
};

class KitCompiler {
public:
    KitCompiler();
    ~KitCompiler();

    KitCompiler(const KitCompiler&)            = delete;
    KitCompiler& operator=(const KitCompiler&) = delete;

    // Compile a kit synchronously. Returns a `CompiledKit` with `ok=true`
    // on success and the 16 KB bank in `bytes`. On any per-sample failure
    // (missing file, decode error) the kit slot is left empty rather than
    // failing the whole compile; if every slot ends up empty the result
    // is still `ok=true` but the bank's offset table marks every slot
    // unused.
    CompiledKit compileKit(std::string_view kitName,
                           const std::vector<CompileSampleSpec>& samples);

    // Expose the inner cache for direct erase-by-path (UI signalling "file
    // changed on disk" or similar).
    SampleCache& cache() { return cache_; }

private:
    std::unique_ptr<enki::TaskScheduler> scheduler_;
    SampleCache                          cache_;
};

} // namespace rp::lsdj
