// HarnessRpcService — the rpcpp-exposed emulator/debug/fixture surface for the
// cli test harness. A thin wrapper over TestHarness::Impl, which drives
// Project/SystemBase synchronously (it controls time).
//
// These bodies live in their own translation unit, deliberately free of the
// txiki/QuickJS host (which stays in TestHarness.cpp), so the service can be
// linked on its own to dump its OpenRPC schema — schema generation only needs
// the method signatures (reflect-cpp), never the JS host.

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include "TestHarnessImpl.hpp"

#include "lsdj/SavSerialization.hpp"
#include "system/SramAutoSave.hpp"
#include "lsdj/codec/SavCodec.hpp"
#include "lsdj/codec/SongCodec.hpp"

std::uint32_t HarnessRpcService::loadRom(std::string path,
        std::vector<std::uint8_t> sram,
        std::string lsdjSyncMode,
        std::uint32_t linkGroup) {
    // Resolve so a ROM a test staged at /tmp (e.g. to exercise the sibling-.sav
    // auto-save) is found, and cfg.romPath (which the sibling .sav derives from)
    // stays consistent with where readFile/writeFile put things.
    path = rpcli::resolveHostPath(path);
    const std::vector<std::uint8_t>* sramPtr = sram.empty() ? nullptr : &sram;
    return h_->loadRom(path, sramPtr, lsdjSyncMode,
                       static_cast<std::uint8_t>(linkGroup));
}

void HarnessRpcService::runMs(double ms) { h_->runMs(ms); }

void HarnessRpcService::press(std::uint32_t systemId, std::int32_t button, bool down) {
    SystemBase* sys = h_->system(systemId);
    if (!sys) throw std::runtime_error("unknown system id");
    sys->pressButton(static_cast<std::uint8_t>(button), down);
}

std::vector<HarnessMidiEvent> HarnessRpcService::drainMidi(std::uint32_t systemId) {
    std::vector<HarnessMidiEvent> out;
    for (const auto& rec : h_->takeMidi(systemId))
        out.push_back(HarnessMidiEvent{ rec.sample, rec.bytes });
    return out;
}

rfl::Bytestring HarnessRpcService::readMemory(std::uint32_t systemId, std::uint32_t type) {
    SystemBase* sys = h_->system(systemId);
    if (!sys) throw std::runtime_error("unknown system id");
    rp::MemoryAccessor acc =
        sys->getMemory(static_cast<rp::MemoryType>(type), rp::AccessType::Read);
    rfl::Bytestring out;
    if (acc.valid()) {
        const auto* p = reinterpret_cast<const std::byte*>(acc.data());
        out.assign(p, p + acc.size());
    }
    return out;
}

std::vector<rp::CpuRegister> HarnessRpcService::getRegisters(std::uint32_t systemId) {
    return h_->cpuSystem(systemId)->getCpuRegisters();
}

HarnessFrame HarnessRpcService::getFrame(std::uint32_t systemId) {
    SystemBase* sys = h_->system(systemId);
    if (!sys) throw std::runtime_error("unknown system id");
    FrameBufferTriple* fb = sys->framebuffer();
    if (!fb) throw std::runtime_error("system has no framebuffer");
    const std::uint32_t fbW = fb->width();
    const std::uint32_t fbH = fb->height();
    const std::size_t pixels = static_cast<std::size_t>(fbW) * fbH;
    std::vector<std::uint32_t> xrgb(pixels);
    const bool published =
        fb->readInto(xrgb.data(), static_cast<std::uint32_t>(pixels));
    HarnessFrame out;
    out.width = fbW;
    out.height = fbH;
    out.published = published;
    if (published) {
        const auto* p = reinterpret_cast<const std::byte*>(xrgb.data());
        out.data.assign(p, p + pixels * 4);
    }
    return out;
}

rfl::Bytestring HarnessRpcService::getAudio(double ms) {
    const std::vector<float> samples = h_->runMsCapture(ms);
    const auto* p = reinterpret_cast<const std::byte*>(samples.data());
    return rfl::Bytestring(p, p + samples.size() * sizeof(float));
}

rp::BreakInfo HarnessRpcService::runUntilBreak(std::uint32_t systemId, std::uint64_t maxCycles) {
    return h_->debugTarget(systemId)->runUntilBreak(maxCycles);
}

rfl::Bytestring HarnessRpcService::savFromJson(std::string json) {
    auto sav = rp::lsdj::savFromJsonFixture(json);
    if (!sav) throw std::runtime_error("savFromJson: " + sav.error().what());
    const auto bytes = rp::lsdj::codec::encodeSav(sav.value());
    const auto* p = reinterpret_cast<const std::byte*>(bytes.data());
    return rfl::Bytestring(p, p + bytes.size());
}

bool HarnessRpcService::loadSram(std::uint32_t systemId, std::vector<std::uint8_t> sram) {
    SystemBase* sys = h_->system(systemId);
    if (!sys) throw std::runtime_error("loadSram: unknown system id");
    return sys->loadSramBytes(std::move(sram));
}

rfl::Bytestring HarnessRpcService::saveSram(std::uint32_t systemId) {
    SystemBase* sys = h_->system(systemId);
    if (!sys) throw std::runtime_error("saveSram: unknown system id");
    const auto bytes = sys->saveSramBytes();
    const auto* p = reinterpret_cast<const std::byte*>(bytes.data());
    return rfl::Bytestring(p, p + bytes.size());
}

bool HarnessRpcService::autoSaveSram(std::uint32_t systemId) {
    SystemBase* sys = h_->system(systemId);
    if (!sys) throw std::runtime_error("autoSaveSram: unknown system id");
    return rp::autoSaveSramToSibling(*sys, sramAutoSaveHashes_[systemId]);
}

void HarnessRpcService::reset(std::uint32_t systemId) {
    SystemBase* sys = h_->system(systemId);
    if (!sys) throw std::runtime_error("reset: unknown system id");
    sys->onReset();
}

rfl::Bytestring HarnessRpcService::readFile(std::string path) {
    path = rpcli::resolveHostPath(path);
    const auto bytes = rpcli::slurpBytes(path);
    const auto* p = reinterpret_cast<const std::byte*>(bytes.data());
    return rfl::Bytestring(p, p + bytes.size());
}

void HarnessRpcService::writeFile(std::string path, std::vector<std::uint8_t> bytes) {
    path = rpcli::resolveHostPath(path);
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size())).good())
        throw std::runtime_error("writeFile: write failed: " + path);
}

void HarnessRpcService::removeFile(std::string path) {
    // Best-effort delete (no error if absent) — lets tests simulate a moved /
    // missing ROM or kit sample WAV.
    path = rpcli::resolveHostPath(path);
    std::remove(path.c_str());
}

std::int32_t HarnessRpcService::savRoundtripDiff(std::vector<std::uint8_t> sav) {
    constexpr std::size_t kSong = 0x8000;
    if (sav.size() < kSong) throw std::runtime_error("savRoundtripDiff: need >= 0x8000 bytes");
    std::span<const std::uint8_t> orig(sav.data(), kSong);
    auto res = rp::lsdj::codec::decodeSong(orig);
    if (!res) throw std::runtime_error("savRoundtripDiff: decode: " + res.error().what());
    const auto out = rp::lsdj::codec::encodeSong(res.value(), orig);
    const auto isVolatile = [](std::size_t off) {
        return off == 0x3FB2 || off == 0x3FB3 || (off >= 0x3FB6 && off <= 0x3FB9) || off == 0x3FC1;
    };
    for (std::size_t i = 0; i < kSong; ++i)
        if (orig[i] != out[i] && !isVolatile(i)) return static_cast<std::int32_t>(i);
    return -1;
}

void HarnessRpcService::sendMidi(std::uint32_t systemId, std::vector<std::uint8_t> bytes) {
    SystemBase* sys = h_->system(systemId);
    if (!sys) throw std::runtime_error("sendMidi: unknown system id");
    if (bytes.empty() || bytes.size() > ::MidiEvent::kDataSize)
        throw std::runtime_error("sendMidi: expected 1.." +
            std::to_string(::MidiEvent::kDataSize) + " bytes");
    ::MidiEvent ev{};
    ev.frame = 0;
    ev.size  = static_cast<std::uint32_t>(bytes.size());
    for (std::size_t i = 0; i < bytes.size(); ++i) ev.data[i] = bytes[i];
    sys->onMidi(&ev, 1);
}

void HarnessRpcService::dispatchMidi(std::vector<std::uint8_t> bytes, std::uint32_t routing) {
    h_->dispatchMidi(bytes, routing);
}

void HarnessRpcService::setTransport(bool running) { h_->transportPlaying = running; }

void HarnessRpcService::setBpm(double bpm) {
    if (bpm <= 0.0) throw std::runtime_error("setBpm: bpm must be > 0");
    h_->bpm = bpm;
}

std::vector<HarnessSerialByte> HarnessRpcService::drainSerial(std::uint32_t systemId) {
    std::vector<HarnessSerialByte> out;
    for (const auto& rec : h_->takeSerial(systemId))
        out.push_back(HarnessSerialByte{ rec.sample, rec.byte });
    return out;
}

void HarnessRpcService::setRegister(std::uint32_t systemId, std::string name, std::int64_t value) {
    const bool ok = h_->cpuSystem(systemId)->setCpuRegister(name, static_cast<std::uint32_t>(value));
    if (!ok) throw std::runtime_error("setRegister: unknown or read-only register '" + name + "'");
}

std::int32_t HarnessRpcService::readCpu(std::uint32_t systemId, std::uint32_t addr) {
    const std::optional<std::uint8_t> b = h_->cpuSystem(systemId)->readCpuByte(addr);
    if (!b) throw std::runtime_error(
        "side-effect-free CPU peek is not supported for this system (use readMemory)");
    return *b;
}

std::uint64_t HarnessRpcService::step(std::uint32_t systemId) {
    return h_->cpuSystem(systemId)->stepInstruction();
}

bool HarnessRpcService::runUntilPc(std::uint32_t systemId, std::uint32_t pc, std::uint64_t maxCycles) {
    if (maxCycles == 0) throw std::runtime_error("runUntilPc: maxCycles must be > 0");
    return h_->cpuSystem(systemId)->runUntilPc(pc, maxCycles);
}

bool HarnessRpcService::screenshot(std::uint32_t systemId, std::string path) {
    SystemBase* sys = h_->system(systemId);
    if (!sys) throw std::runtime_error("screenshot: unknown system id");
    return rpcli::writeFramebufferPng(*sys, rpcli::resolveHostPath(path));
}

HarnessPerSystemAudio HarnessRpcService::runMsPerSystem(double ms) {
    HarnessPerSystemAudio out;
    for (const auto& buf : h_->runMsPerSystem(ms)) {
        const auto* p = reinterpret_cast<const std::byte*>(buf.data());
        out.systems.emplace_back(p, p + buf.size() * sizeof(float));
    }
    return out;
}

void HarnessRpcService::writeWav(std::string path, std::vector<std::uint8_t> samples,
                                 std::uint32_t sampleRate) {
    if (sampleRate == 0) sampleRate = 44100;
    const float* data = reinterpret_cast<const float*>(samples.data());
    const std::size_t frames = (samples.size() / sizeof(float)) / 2; // interleaved stereo
    std::vector<float> l(frames), r(frames);
    for (std::size_t i = 0; i < frames; ++i) { l[i] = data[2 * i]; r[i] = data[2 * i + 1]; }
    float* outs[2] = { l.data(), r.data() };
    WavWriter w(rpcli::resolveHostPath(path), sampleRate, 2);
    w.writeBlockFloatPlanar(outs, static_cast<std::uint32_t>(frames));
}

void HarnessRpcService::renderWav(std::string path, double ms, std::uint32_t sampleRate) {
    h_->renderWav(rpcli::resolveHostPath(path), ms, sampleRate);
}

void HarnessRpcService::renderWavPerSystem(std::string mixPath,
        std::vector<std::string> perSystemPaths, double ms, std::uint32_t sampleRate) {
    mixPath = rpcli::resolveHostPath(mixPath);
    for (auto& p : perSystemPaths) p = rpcli::resolveHostPath(p);
    h_->renderWavPerSystem(mixPath, perSystemPaths, ms, sampleRate);
}

void HarnessRpcService::renderWavPerSystemParallel(std::string mixPath,
        std::vector<std::string> perSystemPaths, double ms, std::uint32_t sampleRate) {
    mixPath = rpcli::resolveHostPath(mixPath);
    for (auto& p : perSystemPaths) p = rpcli::resolveHostPath(p);
    h_->renderWavPerSystemParallel(mixPath, perSystemPaths, ms, sampleRate);
}

void HarnessRpcService::renderBegin(std::string mixPath,
        std::vector<std::string> perSystemPaths, std::uint32_t sampleRate) {
    mixPath = rpcli::resolveHostPath(mixPath);
    for (auto& p : perSystemPaths) p = rpcli::resolveHostPath(p);
    h_->renderBegin(mixPath, perSystemPaths, sampleRate);
}
void HarnessRpcService::renderChunk(double ms) { h_->renderChunk(ms); }
void HarnessRpcService::renderEnd() { h_->renderEnd(); }

rfl::Bytestring HarnessRpcService::zipEntries(std::vector<HarnessZipInput> entries) {
    MinizWriter zip;
    for (const auto& e : entries)
        if (!zip.add(e.name, e.bytes))
            throw std::runtime_error("zipEntries: failed to add entry " + e.name);
    const auto bytes = zip.finish();
    const auto* p = reinterpret_cast<const std::byte*>(bytes.data());
    return rfl::Bytestring(p, p + bytes.size());
}

std::vector<HarnessZipEntry> HarnessRpcService::unzipEntries(std::vector<std::uint8_t> bytes) {
    std::vector<HarnessZipEntry> out;
    MinizReader zip(bytes);
    if (!zip.valid()) return out;
    for (const auto& name : zip.names()) {
        const auto data = zip.read(name);
        const auto* p = reinterpret_cast<const std::byte*>(data.data());
        out.push_back({ name, rfl::Bytestring(p, p + data.size()) });
    }
    return out;
}

HarnessProjectSnapshot HarnessRpcService::snapshotProjectConfig() {
    auto snap = h_->snapshotProjectConfig();
    HarnessProjectSnapshot out;
    out.config = std::move(snap.config);
    out.blobs.reserve(snap.blobs.size());
    for (const auto& b : snap.blobs) {
        const auto* p = reinterpret_cast<const std::byte*>(b.bytes.data());
        out.blobs.push_back({ b.name, rfl::Bytestring(p, p + b.bytes.size()) });
    }
    return out;
}

std::uint32_t HarnessRpcService::applyProjectConfig(std::string config, std::vector<HarnessZipInput> blobs) {
    std::vector<TestHarness::Impl::RplgBlob> native;
    native.reserve(blobs.size());
    for (auto& b : blobs) native.push_back({ std::move(b.name), std::move(b.bytes) });
    return h_->applyProjectConfig(config, native);
}

bool HarnessRpcService::fileExists(std::string path) {
    std::error_code ec;
    return std::filesystem::exists(rpcli::resolveHostPath(path), ec);
}

void HarnessRpcService::patchKit(std::uint32_t systemId, std::uint32_t slot, std::string name,
                                 std::vector<HarnessKitSample> samples) {
    std::vector<std::pair<std::string, std::string>> pairs;
    pairs.reserve(samples.size());
    for (auto& s : samples) pairs.emplace_back(rpcli::resolveHostPath(s.path), std::move(s.name));
    h_->patchKit(systemId, static_cast<std::uint8_t>(slot), name, pairs);
}

void HarnessRpcService::beginProfile(std::uint32_t systemId) {
    h_->debugTarget(systemId)->beginProfile();
}
std::vector<rp::ProfiledFunction> HarnessRpcService::readProfile(std::uint32_t systemId) {
    return h_->debugTarget(systemId)->readProfile();
}
bool HarnessRpcService::loadLabels(std::uint32_t systemId, std::string path) {
    return h_->debugTarget(systemId)->loadLabels(path);
}
std::vector<rp::DisasmLine> HarnessRpcService::disassemble(std::uint32_t systemId,
                                                          std::uint32_t addr, std::uint32_t count) {
    return h_->debugTarget(systemId)->disassemble(addr, count);
}
void HarnessRpcService::setTrace(std::uint32_t systemId, bool on) {
    h_->debugTarget(systemId)->setTraceEnabled(on);
}
std::vector<rp::TraceLine> HarnessRpcService::readTrace(std::uint32_t systemId, std::uint32_t count) {
    return h_->debugTarget(systemId)->readTrace(count);
}
std::vector<rp::CallFrame> HarnessRpcService::getCallStack(std::uint32_t systemId) {
    return h_->debugTarget(systemId)->getCallStack();
}
rp::ApuState HarnessRpcService::getApuState(std::uint32_t systemId) {
    return h_->debugTarget(systemId)->getApuState();
}
void HarnessRpcService::setBreakpoints(std::uint32_t systemId, std::vector<rp::BreakpointSpec> bps) {
    h_->debugTarget(systemId)->setBreakpoints(bps);
}
rp::BreakInfo HarnessRpcService::stepInto(std::uint32_t systemId) { return h_->debugTarget(systemId)->step(); }
rp::BreakInfo HarnessRpcService::stepOver(std::uint32_t systemId) { return h_->debugTarget(systemId)->stepOver(); }
rp::BreakInfo HarnessRpcService::stepOut(std::uint32_t systemId)  { return h_->debugTarget(systemId)->stepOut(); }
