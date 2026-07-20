#include "kit/KitCompiler.hpp"

#include <atomic>
#include <utility>

#include <TaskScheduler.h>

namespace rp::kit {

KitCompiler::KitCompiler()
    : scheduler_(std::make_unique<enki::TaskScheduler>()) {
    // Default config: one worker per logical CPU minus one (the calling
    // thread also runs work via WaitforTask). Suitable for our short
    // per-sample jobs.
    scheduler_->Initialize();
}

KitCompiler::~KitCompiler() {
    scheduler_->WaitforAllAndShutdown();
}

namespace {

// One worker job per slot. Hits the shared cache for the decoded source, then
// asks the codec to resample + effects + pack it. Independent across slots —
// no shared mutable state besides the SampleCache (internally thread-safe) and
// the const codec.
class SampleTask : public enki::ITaskSet {
public:
    SampleTask(const KitCodec&            codec,
               std::size_t                index,
               SampleCache&               cache,
               std::vector<std::uint8_t>& outBytes,
               std::atomic<bool>&         outOk)
        : codec_(&codec), index_(index), cache_(&cache), out_(&outBytes), ok_(&outOk) {
        // Each task is a single unit of work (no partition).
        m_SetSize = 1;
    }

    void ExecuteRange(enki::TaskSetPartition, std::uint32_t) override {
        const SampleData* data = cache_->getOrLoad(codec_->source(index_).path);
        if (!data || data->buffer.empty()) {
            ok_->store(false, std::memory_order_relaxed);
            return; // leaves *out_ empty -> unused slot
        }
        *out_ = codec_->encode(index_, *data);
    }

private:
    const KitCodec*            codec_;
    std::size_t                index_;
    SampleCache*               cache_;
    std::vector<std::uint8_t>* out_;
    std::atomic<bool>*         ok_;
};

} // namespace

CompiledKit KitCompiler::compile(const KitCodec& codec) {
    CompiledKit result;

    const std::size_t count = codec.sampleCount();

    // Per-slot byte vectors; one slot per sample so tasks write in parallel
    // without sharing state.
    std::vector<std::vector<std::uint8_t>> encoded(count);
    std::atomic<bool> ok{true};

    std::vector<std::unique_ptr<SampleTask>> tasks;
    tasks.reserve(count);

    for (std::size_t i = 0; i < count; ++i) {
        tasks.push_back(std::make_unique<SampleTask>(codec, i, cache_, encoded[i], ok));
        scheduler_->AddTaskSetToPipe(tasks.back().get());
    }
    for (auto& t : tasks) {
        scheduler_->WaitforTask(t.get());
    }
    // `ok` going false just means one or more samples failed to load / decode
    // — empty slots are tolerated by the codec's assemble, so this isn't fatal.
    // We surface it in the result for visibility.
    if (!ok.load(std::memory_order_relaxed)) {
        result.error = "one or more samples failed to load";
    }

    result.bytes = codec.assemble(encoded);
    result.hash  = SampleCache::hashBytes(result.bytes.data(), result.bytes.size());
    result.ok    = true;
    return result;
}

} // namespace rp::kit
