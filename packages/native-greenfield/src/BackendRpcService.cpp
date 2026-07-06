#include "BackendRpcService.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <system_error>
#include <thread>

#include "SameBoyBackend.hpp"
#include "ScriptCompiler.hpp"
#include "util/MinizZip.hpp"

#include "EmbeddedRoms.hpp"
#include "system/SystemTypes.hpp"
#include "system/sameboy/SameBoyConfig.hpp"
#include "system/sameboy/SameBoySystem.hpp"

#include "lsdj/SavSerialization.hpp"
#include "lsdj/codec/SavCodec.hpp"

namespace fs = std::filesystem;

namespace {

// The per-OS config dir. Reimplemented here (rather than linking native's
// UserConfigPaths.cpp) so this host stays free of UserConfig.hpp / efsw; the logic
// mirrors packages/native/src/config/UserConfigPaths.cpp exactly.
fs::path resolveConfigDir() {
    if (const char* env = std::getenv("RETROPLUG_USER_CONFIG_DIR"); env && *env)
        return fs::path(env);
#if defined(_WIN32)
    if (const char* env = std::getenv("APPDATA"); env && *env) return fs::path(env) / "RetroPlug";
    return {};
#elif defined(__APPLE__)
    if (const char* home = std::getenv("HOME"); home && *home)
        return fs::path(home) / "Library" / "Application Support" / "RetroPlug";
    return {};
#else
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg) return fs::path(xdg) / "retroplug";
    if (const char* home = std::getenv("HOME"); home && *home) return fs::path(home) / ".config" / "retroplug";
    return {};
#endif
}

rfl::Bytestring toBytestring(const std::vector<std::uint8_t>& v) {
    const auto* p = reinterpret_cast<const std::byte*>(v.data());
    return rfl::Bytestring(p, p + v.size());
}

// Per-block sleep on the background audio thread: runs faster than real time (so a short sleepMs
// window yields plenty of audio) while yielding the core rather than busy-spinning.
constexpr auto kAudioBlockPace = std::chrono::microseconds(200);

// Read a file's bytes (or the first `maxBytes`), or nullopt when it can't be opened.
std::optional<std::vector<std::uint8_t>> slurp(const std::string& path, std::size_t maxBytes) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    std::vector<std::uint8_t> buf(maxBytes);
    in.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(maxBytes));
    buf.resize(static_cast<std::size_t>(in.gcount()));
    return buf;
}

std::size_t fileSizeOr(const std::string& path, std::size_t fallback) {
    std::error_code ec;
    const auto n = fs::file_size(path, ec);
    return ec ? fallback : static_cast<std::size_t>(n);
}

} // namespace

std::optional<rfl::Bytestring> BackendRpcService::readFile(std::string path) {
    auto bytes = slurp(path, fileSizeOr(path, 0));
    if (!bytes) return std::nullopt;
    return toBytestring(*bytes);
}

std::optional<rfl::Bytestring> BackendRpcService::readFilePrefix(std::string path, std::uint32_t length) {
    auto bytes = slurp(path, length);
    if (!bytes) return std::nullopt;
    return toBytestring(*bytes);
}

bool BackendRpcService::writeFile(std::string path, std::vector<std::uint8_t> bytes) {
    // Create parent dirs on demand — the mock backend is dir-free, and the stores (e.g.
    // BindingsStore writing bindings/<name>.json) rely on that forgiving behaviour.
    std::error_code ec;
    if (const fs::path parent = fs::path(path).parent_path(); !parent.empty())
        fs::create_directories(parent, ec);
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    if (!bytes.empty())
        f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return f.good();
}

bool BackendRpcService::writeFileAtomic(std::string path, std::vector<std::uint8_t> bytes) {
    const std::string tmp = path + ".tmp";
    if (!writeFile(tmp, std::move(bytes))) return false;
    std::error_code ec;
    fs::rename(tmp, path, ec);
    if (ec) {
        fs::remove(tmp, ec);
        return false;
    }
    return true;
}

bool BackendRpcService::fileExists(std::string path) {
    std::error_code ec;
    return fs::exists(path, ec);
}

bool BackendRpcService::rename(std::string from, std::string to) {
    std::error_code ec;
    fs::rename(from, to, ec);
    return !ec;
}

std::vector<std::string> BackendRpcService::listDir(std::string dir) {
    std::vector<std::string> out;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        out.push_back(entry.path().filename().string());
    }
    return out;
}

bool BackendRpcService::deleteFile(std::string path) {
    std::error_code ec;
    const bool removed = fs::remove(path, ec);
    return removed && !ec;
}

std::vector<std::string> BackendRpcService::drainChangedPaths() {
    return {};
}

std::string BackendRpcService::canonicalize(std::string path) {
    std::error_code ec;
    const fs::path c = fs::weakly_canonical(path, ec);
    return ec ? path : c.string();
}

std::string BackendRpcService::configDir() {
    return resolveConfigDir().string();
}

rfl::Bytestring BackendRpcService::zip(std::vector<BackendZipInput> entries) {
    MinizWriter w;
    for (const auto& e : entries)
        if (!w.add(e.name, e.bytes)) return {};
    return toBytestring(w.finish());
}

std::vector<BackendZipEntry> BackendRpcService::unzip(std::vector<std::uint8_t> bytes) {
    std::vector<BackendZipEntry> out;
    MinizReader r(bytes);
    if (!r.valid()) return out;
    for (const auto& name : r.names())
        out.push_back({ name, toBytestring(r.read(name)) });
    return out;
}

// --- LSDJ sav authoring ----------------------------------------------------

rfl::Bytestring BackendRpcService::savFromJson(std::string json) {
    auto sav = rp::lsdj::savFromJsonFixture(json);  // lenient (DefaultIfMissing): author only set cells
    if (!sav) throw std::runtime_error("savFromJson: " + sav.error().what());
    const auto bytes = rp::lsdj::codec::encodeSav(sav.value());
    const auto* p = reinterpret_cast<const std::byte*>(bytes.data());
    return rfl::Bytestring(p, p + bytes.size());
}

// --- emulator lifecycle / reads --------------------------------------------
// Systems are built off the audio thread through the one SystemFactory path (SameBoyBackend for
// now — a non-GB file-backed ROM is rejected; NES/GBA via Mesen is a later increment). The
// audio thread only ever adopts the finished pointer.

namespace {

// Slurp a whole file into a byte vector (empty if unreadable). Used for a seed .sav/state.
std::vector<std::uint8_t> slurpAll(const std::string& path) {
    auto bytes = slurp(path, fileSizeOr(path, 0));
    return bytes ? std::move(*bytes) : std::vector<std::uint8_t>{};
}

// Map the wire construct spec to the backend-agnostic build spec: resolve the SRAM/savestate
// seeds (zip-import bytes win, else the on-disk file, else empty) and carry the SameBoy-specific
// LSDJ sync-role mode as the opaque settings blob the SameBoy backend decodes.
SystemBuildSpec toBuildSpec(const BackendConstructSpec& spec) {
    SystemBuildSpec out;
    out.backendKind = "sameboy";  // greenfield host is SameBoy-only for now
    out.romPath = spec.romPath;
    out.embeddedRom = spec.embeddedRom;
    if (spec.sramBytes) out.sram = *spec.sramBytes;
    else if (spec.savPath) out.sram = slurpAll(*spec.savPath);
    if (spec.stateBytes) out.savestate = *spec.stateBytes;
    else if (spec.statePath) out.savestate = slurpAll(*spec.statePath);
    if (spec.lsdjSyncMode) out.settings.assign(spec.lsdjSyncMode->begin(), spec.lsdjSyncMode->end());
    return out;
}

} // namespace

std::optional<std::uint32_t> BackendRpcService::constructSystem(BackendConstructSpec spec) {
    // Build + activate off the audio thread (heavy, non-RT) via the one factory path. nextSystemId
    // only bumps nextId_, which the audio thread never touches, so id allocation is race-free even
    // mid-run.
    // Id is allocated up front (nextSystemId only bumps a control-thread counter) so we return it
    // synchronously whether the invoker applies now or enqueues for the audio thread.
    const SystemId id = engine_.nextSystemId();
    auto sys = factory_.build(id, toBuildSpec(spec), engine_.sampleRate());
    if (!sys) return std::nullopt;  // unknown backend / unreadable or non-SameBoy ROM

    if (spec.replaceId) active_->replaceSystem(*spec.replaceId, std::move(sys));
    else                active_->adoptSystem(std::move(sys));
    return id;
}

std::optional<std::uint32_t> BackendRpcService::duplicateSystem(std::uint32_t srcId,
                                                                std::optional<std::string> savPath) {
    // Reads the SOURCE core's live state (clone) — unsafe while the audio thread owns it. Needs the
    // deferred per-system snapshot triple-buffers; until then, quiescent only.
    if (audioRunning_.load(std::memory_order_acquire)) return std::nullopt;
    SystemBase* src = engine_.findSystem(srcId);
    if (!src) return std::nullopt;

    // clone() boots an independent copy of the live state (SRAM + savestate).
    const SystemId id = engine_.nextSystemId();
    auto sys = src->clone(id, engine_.sampleRate());
    if (!sys) return std::nullopt;
    if (savPath) sys->setSavPath(*savPath);  // the duplicate auto-saves to its own file
    engine_.adoptSystem(std::move(sys));
    return id;
}

std::optional<std::uint32_t> BackendRpcService::reloadSystem(std::uint32_t id) {
    // Reads the existing core's live SRAM/config — unsafe mid-run (see duplicateSystem).
    if (audioRunning_.load(std::memory_order_acquire)) return std::nullopt;
    SystemBase* old = engine_.findSystem(id);
    if (!old) return std::nullopt;

    // Rebuild the ROM from disk (or the embedded marker), carrying the live SRAM forward and
    // dropping the savestate — a genuine reload, swapped in place with a fresh id.
    const std::string romPath = old->romPath();
    SameBoyConfig cfg = static_cast<const SameBoySystem*>(old)->config_;  // carry paths/roles/model
    cfg.sram = old->saveSramBytes();
    cfg.savestate.clear();

    std::vector<std::uint8_t> romBytes;
    if (!cfg.embeddedRom.empty()) {
        const auto rom = rp::embeddedRom(cfg.embeddedRom);
        romBytes.assign(rom.begin(), rom.end());
    } else {
        romBytes = slurpAll(romPath);
    }
    if (romBytes.empty()) return std::nullopt;

    // Quiescent only (guarded above) — swap in place through the active (direct) invoker so the
    // reload shares the one Engine::replaceSystem path.
    const SystemId newId = engine_.nextSystemId();
    auto sys = SameBoyBackend::buildSameBoy(newId, std::move(cfg), std::move(romBytes), engine_.sampleRate());
    active_->replaceSystem(id, std::move(sys));
    return newId;
}

bool BackendRpcService::removeSystem(std::uint32_t id) {
    // No threading branch: quiescent → direct erase; running → enqueue for the audio thread, which
    // hands the core back through the release ring for the control thread to delete (drainReleased).
    active_->removeSystem(id);
    return true;
}

bool BackendRpcService::applySystemSetting(std::uint32_t id, std::string /*key*/, double /*value*/) {
    return engine_.findSystem(id) != nullptr;
}

bool BackendRpcService::applyRoleConfig(std::uint32_t id, std::string /*kind*/, std::string /*config*/) {
    return engine_.findSystem(id) != nullptr;
}

// Core reads below touch live core state, unsafe while the audio thread owns the Engine. Until the
// per-system snapshot triple-buffers land (deferred), they fail safe during a run.
std::optional<rfl::Bytestring> BackendRpcService::readState(std::uint32_t id) {
    if (audioRunning_.load(std::memory_order_acquire)) return std::nullopt;
    auto bytes = engine_.readState(id);
    if (!bytes) return std::nullopt;
    return toBytestring(*bytes);
}

std::optional<rfl::Bytestring> BackendRpcService::readSram(std::uint32_t id) {
    if (audioRunning_.load(std::memory_order_acquire)) return std::nullopt;
    auto bytes = engine_.readSram(id);
    if (!bytes) return std::nullopt;
    return toBytestring(*bytes);
}

bool BackendRpcService::screenshot(std::uint32_t id, std::string path) {
    if (audioRunning_.load(std::memory_order_acquire)) return false;
    return engine_.screenshot(id, path);
}

// --- audio render / MIDI drive ----------------------------------------------

bool BackendRpcService::sendMidi(std::uint32_t id, std::vector<std::uint8_t> bytes) {
    if (audioRunning_.load(std::memory_order_acquire)) return false;  // direct core mutation; quiescent only
    return engine_.sendMidi(id, bytes);
}

bool BackendRpcService::pressButton(std::uint32_t id, std::uint32_t button, bool down) {
    if (audioRunning_.load(std::memory_order_acquire)) return false;  // direct core mutation; quiescent only
    return engine_.pressButton(id, static_cast<std::uint8_t>(button), down);
}

rfl::Bytestring BackendRpcService::renderAudio(double ms) {
    if (ms <= 0.0) return {};
    scratchL_.resize(kBlockSize);  // idempotent — no per-call realloc after the first
    scratchR_.resize(kBlockSize);

    const std::uint64_t total = static_cast<std::uint64_t>(ms * engine_.sampleRate() / 1000.0);
    std::vector<float> out;
    out.reserve(total * 2);
    // The Engine owns transport + the ppq clock and consumes any host MIDI staged via stageMidiIn on
    // the first block it renders.
    for (std::uint64_t s = 0; s < total; s += kBlockSize) {
        const auto frames = static_cast<std::uint32_t>(std::min<std::uint64_t>(kBlockSize, total - s));
        engine_.processBlock(frames, scratchL_.data(), scratchR_.data());
        for (std::uint32_t f = 0; f < frames; ++f) {
            out.push_back(scratchL_[f]);  // interleave L,R,L,R…
            out.push_back(scratchR_[f]);
        }
    }
    const auto* p = reinterpret_cast<const std::byte*>(out.data());
    return rfl::Bytestring(p, p + out.size() * sizeof(float));
}

bool BackendRpcService::stageMidiIn(std::vector<std::uint8_t> bytes) {
    if (bytes.empty() || bytes.size() > 4) return false;  // a MIDI message fits in 4 bytes
    active_->stageMidi(std::move(bytes));
    return true;
}

bool BackendRpcService::setTransport(bool running) {
    active_->setTransport(running);  // transport is a queued op — applied now (direct) or on the audio thread
    return true;
}

bool BackendRpcService::setBpm(double bpm) {
    if (bpm <= 0.0) return false;
    active_->setBpm(bpm);
    return true;
}

// --- DSP-side JS runtime ----------------------------------------------------

std::optional<rfl::Bytestring> BackendRpcService::compileScript(std::string source) {
    auto bytecode = dsp::compileToBytecode(source);
    if (!bytecode) return std::nullopt;
    return toBytestring(*bytecode);
}

bool BackendRpcService::dspLoadKernel(std::vector<std::uint8_t> bytecode) {
    active_->loadKernel(std::move(bytecode));
    return true;  // applied now (direct) or on the audio thread; the DSP stage goes active there
}

bool BackendRpcService::dspSetSystems(std::string json) {
    active_->setSystems(std::move(json));
    return true;
}

// --- background audio thread ------------------------------------------------

BackendRpcService::BackendRpcService() {
    // The one build path. SameBoy-only for now; a Mesen backend registers here later. (The Engine
    // pre-reserves its Project so the audio thread's adopt/swap never reallocates.)
    factory_.registerBackend("sameboy", std::make_unique<SameBoyBackend>());
}

BackendRpcService::~BackendRpcService() {
    if (audioRunning_.load(std::memory_order_acquire)) {
        audioRunning_.store(false, std::memory_order_release);
        if (audioThread_.joinable()) audioThread_.join();
    }
    queued_.freePending();  // un-applied command payloads (built-but-unadopted systems etc.)
    drainReleased();        // cores the audio thread released but nobody drained
}

void BackendRpcService::audioLoop() {
    // Audio-thread-local: the control thread reaches the loop only through the QueuedInvoker's
    // command ring and audioRunning_. drainInto applies every queued edit (structure + transport)
    // INTO the Engine before the block; the live count is republished each iteration.
    std::vector<float> l(kBlockSize), r(kBlockSize);
    double energy = 0.0;
    std::uint64_t frames = 0;

    while (audioRunning_.load(std::memory_order_acquire)) {
        queued_.drainInto(engine_);
        liveSystemCount_.store(static_cast<std::uint32_t>(engine_.systemCount()), std::memory_order_release);
        engine_.processBlock(kBlockSize, l.data(), r.data());

        for (std::uint32_t f = 0; f < kBlockSize; ++f)
            energy += static_cast<double>(l[f]) * l[f] + static_cast<double>(r[f]) * r[f];
        frames += kBlockSize;
        capturedEnergy_.store(energy, std::memory_order_release);
        capturedFrames_.store(frames, std::memory_order_release);

        std::this_thread::sleep_for(kAudioBlockPace);
    }
}

bool BackendRpcService::startAudio() {
    if (audioRunning_.load(std::memory_order_acquire)) return false;  // already running
    capturedEnergy_.store(0.0, std::memory_order_relaxed);
    capturedFrames_.store(0, std::memory_order_relaxed);
    liveSystemCount_.store(static_cast<std::uint32_t>(engine_.systemCount()), std::memory_order_relaxed);
    active_ = &queued_;  // subsequent mutations enqueue for the audio thread (control-thread state)
    audioRunning_.store(true, std::memory_order_release);
    audioThread_ = std::thread([this] { audioLoop(); });
    return true;
}

bool BackendRpcService::stopAudio() {
    if (!audioRunning_.load(std::memory_order_acquire)) return false;
    audioRunning_.store(false, std::memory_order_release);
    if (audioThread_.joinable()) audioThread_.join();
    active_ = &direct_;  // back to synchronous application
    // The audio thread is joined, so both rings are single-threaded again: free any un-applied
    // command payloads, and delete any cores the audio thread released just before stopping.
    queued_.freePending();
    drainReleased();
    return true;
}

AudioCaptured BackendRpcService::audioCaptured() {
    const std::uint64_t f = capturedFrames_.load(std::memory_order_acquire);
    const double        e = capturedEnergy_.load(std::memory_order_acquire);
    return { e, f };
}

bool BackendRpcService::sleepMs(double ms) {
    if (ms > 0.0) std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(ms));
    return true;
}

std::uint32_t BackendRpcService::systemCount() {
    return liveSystemCount_.load(std::memory_order_acquire);
}

std::uint32_t BackendRpcService::drainReleased() {
    // Control-thread consumer of the release ring: delete each core the audio thread handed back.
    // popReleased is SPSC-safe against the audio-thread producer; deleting here is safe because the
    // audio thread already dropped the core from the Project (no longer rendered) before pushing it.
    std::uint32_t freed = 0;
    while (std::unique_ptr<SystemBase> sys = queued_.popReleased()) ++freed;  // deleted at scope end
    return freed;
}
