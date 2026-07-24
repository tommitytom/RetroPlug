#pragma once

#include <memory>

#include "kit/KitCodec.hpp"
#include "kit/SampleCache.hpp"

// Console-neutral async kit compiler. Fans per-sample decode + encode work
// across an enkiTS thread pool, then hands the encoded slots to the codec to
// assemble the bank. Console specifics live entirely behind the `KitCodec`
// (KitCodec.hpp) it's handed; this class knows nothing about LSDj, NES, or any
// particular bank layout.
//
// The owning instance keeps a `SampleCache`, so re-compiling a kit that shares
// samples with a previous compile — or a live batch re-patch that only nudged
// one slot — avoids re-reading + re-decoding the unchanged source files.
//
// Threading model: `compile` is synchronous from the caller's perspective;
// internally it dispatches per-sample tasks to enkiTS and joins before
// returning. Callers are expected to be off the audio thread (typically an
// rpcpp method handler).

namespace enki { class TaskScheduler; }

namespace rp::kit {

class KitCompiler {
public:
    KitCompiler();
    ~KitCompiler();

    KitCompiler(const KitCompiler&)            = delete;
    KitCompiler& operator=(const KitCompiler&) = delete;

    // Compile a kit synchronously through `codec`. Returns `ok=true` with the
    // assembled bank in `bytes`. A per-sample load/decode failure leaves that
    // slot empty (the codec decides how) rather than failing the whole
    // compile; `error` is set for visibility when any slot failed to load.
    CompiledKit compile(const KitCodec& codec);

    // Expose the inner cache for direct erase-by-path (UI signalling "file
    // changed on disk" or reclaiming memory after a recompile).
    SampleCache& cache() { return cache_; }

private:
    std::unique_ptr<enki::TaskScheduler> scheduler_;
    SampleCache                          cache_;
};

} // namespace rp::kit
