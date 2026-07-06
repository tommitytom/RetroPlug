#include "Engine.hpp"

#include <algorithm>
#include <cstddef>

#include "system/SystemBase.hpp"
#include "system/SystemTypes.hpp"   // AudioBlockInfo, SystemId
#include "transport/MidiTypes.hpp"  // ::MidiEvent

#include "transport/FrameBufferTriple.hpp"
#include "native/core/img/png/lodepng.h"

namespace {

// Read-only empties the kernel's per-block button/key inputs default to. The greenfield host
// doesn't yet feed the kernel per-block buttons/keys.
const std::vector<DspRuntime::ButtonIn> kNoButtons;
const std::vector<DspRuntime::KeyIn>    kNoKeys;

} // namespace

Engine::Engine(double sampleRate) : sampleRate_(sampleRate) {
    // Pre-reserve so adoptSystem/swapSystem never reallocate systems_ when the audio thread owns
    // the Project (production reserves the same way).
    project_.reserve(16);
}

SystemId Engine::nextSystemId() {
    return project_.nextSystemId();
}

void Engine::adoptSystem(std::unique_ptr<SystemBase> sys) {
    project_.adoptSystem(sys.release());  // Project takes ownership of the raw pointer
    project_.rebuildLinkGroups();
}

std::unique_ptr<SystemBase> Engine::removeSystem(SystemId id) {
    return std::unique_ptr<SystemBase>(project_.removeSystemAndRelease(id));  // does its own rebuild
}

std::unique_ptr<SystemBase> Engine::replaceSystem(SystemId id, std::unique_ptr<SystemBase> sys) {
    // swapSystem returns the displaced core, or the incoming one unchanged if id wasn't found
    // (then it was never adopted) — either way it's the leftover for the caller to dispose.
    SystemBase* old = project_.swapSystem(id, sys.release());
    project_.rebuildLinkGroups();
    return std::unique_ptr<SystemBase>(old);
}

std::size_t Engine::systemCount() const {
    return project_.systems().size();
}

SystemBase* Engine::findSystem(SystemId id) {
    return project_.findSystem(id);
}

bool Engine::loadKernel(const std::vector<std::uint8_t>& bytecode) {
    dspActive_ = dsp_.loadKernel(bytecode);
    return dspActive_;
}

bool Engine::setSystems(const std::vector<std::uint8_t>& json) {
    return dsp_.setSystems(json);
}

void Engine::stageMidi(std::vector<std::uint8_t> bytes) {
    pendingMidi_.push_back({ 0, std::move(bytes) });
}

void Engine::setBpm(double bpm) { bpm_ = bpm; }
void Engine::setTransport(bool playing) { transport_ = playing; }

// Run the kernel (if active) + fan its system-addressed sinks to the cores BEFORE onProcess
// (delivered this block); `dInfo`/`AudioBlockInfo` are both built at the block-start `ppq_`, and
// `ppq_` advances only after — so the kernel's walkTicks and the cores see the same block-start ppq.
void Engine::processBlock(std::uint32_t frames, float* outL, float* outR) {
    if (dspActive_) {
        const DspRuntime::BlockInfo dInfo{ frames, sampleRate_, bpm_, ppq_, transport_ };
        dsp_.processBlock(pendingMidi_, kNoButtons, kNoKeys, dInfo);
        pendingMidi_.clear();  // staged host MIDI consumed this block
        // serial-in sink → the addressed system's serial FIFO.
        for (const auto& sv : dsp_.serialIn_)
            if (SystemBase* t = project_.findSystem(sv.system)) t->pushSerialIn(sv.byte);
        // role-generated button presses → the addressed core.
        for (const auto& bo : dsp_.buttonOut_)
            if (SystemBase* t = project_.findSystem(bo.system))
                t->pressButton(static_cast<std::uint8_t>(bo.button), bo.down);
    }
    std::fill_n(outL, frames, 0.0f);  // cores mix additively → zero each block
    std::fill_n(outR, frames, 0.0f);
    float* outs[2] = { outL, outR };
    AudioBlockInfo info{ frames, sampleRate_, bpm_, ppq_, transport_ };
    project_.onProcess(info, outs);
    if (transport_)
        ppq_ += (bpm_ / 60.0) * (static_cast<double>(frames) / sampleRate_);
}

std::optional<std::vector<std::uint8_t>> Engine::readState(SystemId id) {
    SystemBase* s = project_.findSystem(id);
    if (!s) return std::nullopt;
    return s->saveStateBytes();
}

std::optional<std::vector<std::uint8_t>> Engine::readSram(SystemId id) {
    SystemBase* s = project_.findSystem(id);
    if (!s) return std::nullopt;
    return s->saveSramBytes();
}

bool Engine::screenshot(SystemId id, const std::string& path) {
    SystemBase* s = project_.findSystem(id);
    if (!s) return false;
    FrameBufferTriple* fb = s->framebuffer();
    if (!fb) return false;
    const std::uint32_t w = fb->width();
    const std::uint32_t h = fb->height();
    const std::size_t pixels = static_cast<std::size_t>(w) * h;
    // FrameBufferTriple stores XRGB8888 (little-endian B,G,R,X) → transcode to RGB24 for lodepng.
    std::vector<std::uint32_t> xrgb(pixels);
    if (!fb->readInto(xrgb.data(), static_cast<std::uint32_t>(pixels))) return false;
    std::vector<unsigned char> rgb(pixels * 3);
    const std::uint8_t* src = reinterpret_cast<const std::uint8_t*>(xrgb.data());
    for (std::size_t i = 0; i < pixels; ++i) {
        rgb[i * 3 + 0] = src[i * 4 + 2]; // R
        rgb[i * 3 + 1] = src[i * 4 + 1]; // G
        rgb[i * 3 + 2] = src[i * 4 + 0]; // B
    }
    return lodepng_encode24_file(path.c_str(), rgb.data(), w, h) == 0;
}

bool Engine::sendMidi(SystemId id, const std::vector<std::uint8_t>& bytes) {
    SystemBase* sys = project_.findSystem(id);
    if (!sys) return false;
    if (bytes.empty() || bytes.size() > ::MidiEvent::kDataSize) return false;
    ::MidiEvent ev{};
    ev.frame = 0;
    ev.size = static_cast<std::uint32_t>(bytes.size());
    for (std::size_t i = 0; i < bytes.size(); ++i) ev.data[i] = bytes[i];
    sys->onMidi(&ev, 1);  // mGB's role forwards the bytes to serialIn_, drained in onProcess
    return true;
}

bool Engine::pressButton(SystemId id, std::uint8_t button, bool down) {
    SystemBase* sys = project_.findSystem(id);
    if (!sys) return false;
    sys->pressButton(button, down);  // spread across the block in onProcess
    return true;
}
