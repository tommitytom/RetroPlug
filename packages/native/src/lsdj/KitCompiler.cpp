#include "lsdj/KitCompiler.hpp"

#include <algorithm>
#include <atomic>
#include <cstring>

#include <TaskScheduler.h>

namespace rp::lsdj {

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

// One worker job per sample. Reads from a `CompileSampleSpec`, hits the
// shared cache, runs effects + resample + nibble-pack, and writes its
// finished byte stream into `outBytes`. Independent across samples — no
// shared mutable state besides the SampleCache (which is internally
// thread-safe).
class SampleTask : public enki::ITaskSet {
public:
    SampleTask(const CompileSampleSpec& spec,
               SampleCache&             cache,
               std::vector<std::uint8_t>& outBytes,
               std::atomic<bool>&       outOk)
        : spec_(&spec), cache_(&cache), out_(&outBytes), ok_(&outOk) {
        // Each task is a single unit of work (no partition).
        m_SetSize = 1;
    }

    void ExecuteRange(enki::TaskSetPartition, std::uint32_t) override {
        const SampleData* data = cache_->getOrLoad(spec_->path);
        if (!data || data->buffer.empty()) {
            ok_->store(false, std::memory_order_relaxed);
            return;
        }
        KitUtil::SampleInput input;
        input.name       = spec_->name;
        input.data       = data->buffer;       // copy: tasks mutate locally
        input.sampleRate = data->sampleRate;
        input.offset     = spec_->offset;
        input.length     = spec_->length;
        input.effects    = spec_->effects;
        *out_ = KitUtil::compileSample(input);
    }

private:
    const CompileSampleSpec*   spec_;
    SampleCache*               cache_;
    std::vector<std::uint8_t>* out_;
    std::atomic<bool>*         ok_;
};

} // namespace

CompiledKit KitCompiler::compileKit(std::string_view kitName,
                                    const std::vector<CompileSampleSpec>& samples) {
    CompiledKit result;

    // Per-sample byte vectors; one slot per sample so tasks can write in
    // parallel without sharing state.
    std::vector<std::vector<std::uint8_t>> perSample(samples.size());
    std::atomic<bool> ok{true};

    std::vector<std::unique_ptr<SampleTask>> tasks;
    tasks.reserve(samples.size());

    for (std::size_t i = 0; i < samples.size(); ++i) {
        tasks.push_back(std::make_unique<SampleTask>(
            samples[i], cache_, perSample[i], ok));
        scheduler_->AddTaskSetToPipe(tasks.back().get());
    }
    for (auto& t : tasks) {
        scheduler_->WaitforTask(t.get());
    }
    // `ok` going false just means one or more samples failed to load /
    // decode — empty slots are tolerated by buildKit, so this isn't a
    // fatal error. We surface it in the result for visibility.
    if (!ok.load(std::memory_order_relaxed)) {
        result.error = "one or more samples failed to load";
    }

    // Pair names + bytes for the bank assembler. Empty bytes -> empty slot.
    std::vector<std::pair<std::string, std::vector<std::uint8_t>>> assembled;
    assembled.reserve(samples.size());
    for (std::size_t i = 0; i < samples.size(); ++i) {
        assembled.emplace_back(samples[i].name, std::move(perSample[i]));
    }

    result.bytes = KitUtil::buildKit(kitName, assembled);
    result.hash  = SampleCache::hashBytes(result.bytes.data(), result.bytes.size());
    result.ok    = true;
    return result;
}

} // namespace rp::lsdj
