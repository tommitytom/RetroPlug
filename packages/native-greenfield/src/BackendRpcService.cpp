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

#include "ScriptCompiler.hpp"
#include "util/MinizZip.hpp"

#include "EmbeddedRoms.hpp"
#include "system/RomFormat.hpp"
#include "system/SystemTypes.hpp"
#include "system/sameboy/SameBoyConfig.hpp"
#include "system/sameboy/SameBoySystem.hpp"
#include "system/sameboy/roles/LsdjSyncRole.hpp"
#include "transport/MidiTypes.hpp"

#include "lsdj/SavSerialization.hpp"
#include "lsdj/codec/SavCodec.hpp"

#include "transport/FrameBufferTriple.hpp"
#include "native/core/img/png/lodepng.h"

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

// Read-only empties the kernel's per-block inputs default to (const, so safe to read from either
// render thread). The greenfield host doesn't yet feed the kernel per-block buttons/keys.
const std::vector<DspRuntime::MidiIn>   kEmptyMidi;
const std::vector<DspRuntime::ButtonIn> kNoButtons;
const std::vector<DspRuntime::KeyIn>    kNoKeys;

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
// A real SameBoySystem (Game Boy) is built for every construct — the fold-in of the actual
// core. SameBoy-only for now: a non-GB file-backed ROM is rejected. NES/GBA (Mesen) is a
// later increment. Seed order matters: a live core restores SRAM/savestate INSIDE onActivate
// from cfg.sram/cfg.savestate, so bytes go into the config BEFORE construct (loadSramBytes /
// loadStateBytes no-op before gb_ exists).

namespace {

// Slurp a whole file into a byte vector (empty if unreadable). Used for the ROM + a seed .sav.
std::vector<std::uint8_t> slurpAll(const std::string& path) {
    auto bytes = slurp(path, fileSizeOr(path, 0));
    return bytes ? std::move(*bytes) : std::vector<std::uint8_t>{};
}

// Parse an LSDJ sync-mode name into the role enum (mirrors cli parseLsdjSyncMode). Unknown
// names throw so a typo surfaces rather than silently defaulting.
LsdjSyncMode parseLsdjSyncMode(const std::string& s) {
    if (s == "Off")                return LsdjSyncMode::Off;
    if (s == "MidiSync")           return LsdjSyncMode::MidiSync;
    if (s == "MidiSyncArduinoboy") return LsdjSyncMode::MidiSyncArduinoboy;
    if (s == "MidiMap")            return LsdjSyncMode::MidiMap;
    if (s == "Keyboard")           return LsdjSyncMode::Keyboard;
    if (s == "KeyboardMidi")       return LsdjSyncMode::KeyboardMidi;
    if (s == "MidiPassthrough")    return LsdjSyncMode::MidiPassthrough;
    if (s == "ArduinoboyMaster")   return LsdjSyncMode::ArduinoboyMaster;
    throw std::runtime_error("constructSystem: unknown lsdjSyncMode: " + s);
}

} // namespace

std::optional<std::uint32_t> BackendRpcService::constructSystem(BackendConstructSpec spec) {
    std::vector<std::uint8_t> romBytes;
    if (!spec.embeddedRom.empty()) {
        // Embedded ROM: resolve the marker to baked bytes; the format is SameBoy by fiat.
        const auto rom = rp::embeddedRom(spec.embeddedRom);
        if (rom.empty()) return std::nullopt;  // unknown marker
        romBytes.assign(rom.begin(), rom.end());
    } else {
        // File-backed: slurp the full ROM and sniff. SameBoy-only gate here.
        romBytes = slurpAll(spec.romPath);
        if (romBytes.empty() || detectRomFormat(romBytes) != RomFormat::SameBoy) return std::nullopt;
    }

    SameBoyConfig cfg;
    cfg.romPath = spec.romPath;
    cfg.model = SameBoyModel::CgbC;
    cfg.fastBoot = true;
    if (!spec.embeddedRom.empty()) {
        cfg.embeddedRom = spec.embeddedRom;
        cfg.embedRom = false;  // re-supplied from the marker on load; keeps saves small
    }
    // Seed SRAM: zip-import bytes win; else the on-disk .sav if present; else empty (cold boot).
    if (spec.sramBytes) cfg.sram = *spec.sramBytes;
    else if (spec.savPath) cfg.sram = slurpAll(*spec.savPath);
    // Seed savestate: zip-import bytes, else the on-disk savestate if present.
    if (spec.stateBytes) cfg.savestate = *spec.stateBytes;
    else if (spec.statePath) cfg.savestate = slurpAll(*spec.statePath);
    // Optional LSDJ sync-role mode: pre-seed the role so onActivate skips the sniffer default.
    // "Off" makes the role passive — the C++ side emits no host clock (e.g. so a DSP script can
    // be the sole clock).
    if (spec.lsdjSyncMode) {
        LsdjSyncConfig lsdj;
        lsdj.mode = parseLsdjSyncMode(*spec.lsdjSyncMode);
        cfg.roles.emplace_back(lsdj);
    }

    // Build + activate off the audio thread (heavy, non-RT) — nextSystemId only bumps nextId_, which
    // the audio thread never touches, so id allocation is race-free even mid-run.
    const SystemId id = project_.nextSystemId();
    auto sys = std::make_unique<SameBoySystem>(id, cfg, std::move(romBytes));
    sys->onActivate(sampleRate_);  // boots gb_ + restores cfg.sram then cfg.savestate

    if (audioRunning_.load(std::memory_order_acquire)) {
        // The audio thread owns project_ — ship the raw pointer through the queue for an alloc-free
        // adopt/swap there (production's AddSystem/LoadRom pattern). Id is already allocated, so we
        // still return it synchronously.
        DspCommand c;
        if (spec.replaceId) {
            c.kind = DspCommand::Kind::ReplaceSystem;
            c.replaceSystem = { sys.get(), *spec.replaceId };
        } else {
            c.kind = DspCommand::Kind::AddSystem;
            c.addSystem = { sys.get() };
        }
        if (!dspCommands_.tryPush(c)) return std::nullopt;  // full — unique_ptr frees the build
        sys.release();  // ownership transferred to the command / audio thread
        return id;
    }

    // Quiescent: apply directly on this thread.
    if (spec.replaceId) project_.removeSystem(*spec.replaceId);  // swap in place
    project_.adoptSystem(sys.release());
    project_.rebuildLinkGroups();
    publishSystemCount();
    return id;
}

std::optional<std::uint32_t> BackendRpcService::duplicateSystem(std::uint32_t srcId,
                                                                std::optional<std::string> savPath) {
    // Reads the SOURCE core's live state (clone) — unsafe while the audio thread owns it. Needs the
    // deferred per-system snapshot triple-buffers; until then, quiescent only.
    if (audioRunning_.load(std::memory_order_acquire)) return std::nullopt;
    SystemBase* src = project_.findSystem(srcId);
    if (!src) return std::nullopt;

    // clone() boots an independent copy of the live state (SRAM + savestate).
    const SystemId id = project_.nextSystemId();
    auto sys = src->clone(id, sampleRate_);
    if (!sys) return std::nullopt;
    if (savPath) sys->setSavPath(*savPath);  // the duplicate auto-saves to its own file
    project_.adoptSystem(sys.release());
    project_.rebuildLinkGroups();
    return id;
}

std::optional<std::uint32_t> BackendRpcService::reloadSystem(std::uint32_t id) {
    // Reads the existing core's live SRAM/config — unsafe mid-run (see duplicateSystem).
    if (audioRunning_.load(std::memory_order_acquire)) return std::nullopt;
    SystemBase* old = project_.findSystem(id);
    if (!old) return std::nullopt;

    // Rebuild the ROM from disk (or the embedded marker), carrying the live SRAM forward and
    // dropping the savestate — a genuine reload, swapped in place with a fresh id.
    const std::string romPath = old->romPath();
    const auto sram = old->saveSramBytes();
    SameBoyConfig cfg = static_cast<const SameBoySystem*>(old)->config_;  // carry paths/roles/model
    cfg.sram = sram;
    cfg.savestate.clear();

    std::vector<std::uint8_t> romBytes;
    if (!cfg.embeddedRom.empty()) {
        const auto rom = rp::embeddedRom(cfg.embeddedRom);
        romBytes.assign(rom.begin(), rom.end());
    } else {
        romBytes = slurpAll(romPath);
    }
    if (romBytes.empty()) return std::nullopt;

    const SystemId newId = project_.nextSystemId();
    auto sys = std::make_unique<SameBoySystem>(newId, cfg, std::move(romBytes));
    sys->onActivate(sampleRate_);
    project_.removeSystem(id);
    project_.adoptSystem(sys.release());
    project_.rebuildLinkGroups();
    publishSystemCount();
    return newId;
}

bool BackendRpcService::removeSystem(std::uint32_t id) {
    if (audioRunning_.load(std::memory_order_acquire)) {
        // Ship the id to the audio thread; it erases + releases the core and hands the pointer back
        // through dspReleased_ for the control thread to delete (drainReleased).
        DspCommand c;
        c.kind = DspCommand::Kind::RemoveSystem;
        c.removeSystem = { id };
        return dspCommands_.tryPush(c);
    }
    if (!project_.findSystem(id)) return false;
    project_.removeSystem(id);
    project_.rebuildLinkGroups();
    publishSystemCount();
    return true;
}

bool BackendRpcService::applySystemSetting(std::uint32_t id, std::string /*key*/, double /*value*/) {
    return project_.findSystem(id) != nullptr;
}

bool BackendRpcService::applyRoleConfig(std::uint32_t id, std::string /*kind*/, std::string /*config*/) {
    return project_.findSystem(id) != nullptr;
}

// Core reads below touch live core state, unsafe while the audio thread owns project_. Until the
// per-system snapshot triple-buffers land (deferred), they fail safe during a run.
std::optional<rfl::Bytestring> BackendRpcService::readState(std::uint32_t id) {
    if (audioRunning_.load(std::memory_order_acquire)) return std::nullopt;
    SystemBase* s = project_.findSystem(id);
    if (!s) return std::nullopt;
    return toBytestring(s->saveStateBytes());
}

std::optional<rfl::Bytestring> BackendRpcService::readSram(std::uint32_t id) {
    if (audioRunning_.load(std::memory_order_acquire)) return std::nullopt;
    SystemBase* s = project_.findSystem(id);
    if (!s) return std::nullopt;
    return toBytestring(s->saveSramBytes());
}

bool BackendRpcService::screenshot(std::uint32_t id, std::string path) {
    if (audioRunning_.load(std::memory_order_acquire)) return false;
    SystemBase* s = project_.findSystem(id);
    if (!s) return false;
    FrameBufferTriple* fb = s->framebuffer();
    if (!fb) return false;
    const std::uint32_t w = fb->width();
    const std::uint32_t h = fb->height();
    const std::size_t pixels = static_cast<std::size_t>(w) * h;
    // FrameBufferTriple stores XRGB8888 (little-endian B,G,R,X) -> transcode to RGB24 for lodepng.
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

// --- audio render / MIDI drive ----------------------------------------------

bool BackendRpcService::sendMidi(std::uint32_t id, std::vector<std::uint8_t> bytes) {
    if (audioRunning_.load(std::memory_order_acquire)) return false;  // direct core mutation; quiescent only
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

bool BackendRpcService::pressButton(std::uint32_t id, std::uint32_t button, bool down) {
    if (audioRunning_.load(std::memory_order_acquire)) return false;  // direct core mutation; quiescent only
    SystemBase* sys = project_.findSystem(id);
    if (!sys) return false;
    sys->pressButton(static_cast<std::uint8_t>(button), down);  // spread across the block in onProcess
    return true;
}

// Run the kernel (if active) + fan its system-addressed sinks to the cores BEFORE onProcess
// (delivered this block); `dInfo`/`AudioBlockInfo` are both built at the block-start `ppq`, and
// `ppq` advances only after — so the kernel's walkTicks and the cores see the same block-start ppq.
void BackendRpcService::renderBlock(std::uint32_t frames, double bpm, bool transport, double& ppq,
                                    const std::vector<DspRuntime::MidiIn>& midi, float* outL, float* outR) {
    if (dspActive_) {
        const DspRuntime::BlockInfo dInfo{ frames, sampleRate_, bpm, ppq, transport };
        dsp_.processBlock(midi, kNoButtons, kNoKeys, dInfo);
        // serial-in sink → the addressed system's serial FIFO (intra-block frame not yet honoured —
        // a plain FIFO). Host MIDI-out (midiOut_) has no destination in this cut.
        for (const auto& sv : dsp_.serialIn_)
            if (SystemBase* t = project_.findSystem(sv.system)) t->pushSerialIn(sv.byte);
        // role-generated button presses → the addressed core (distinct from a host UI tap, which
        // arrives via the pressButton RPC and is not fed back through the kernel).
        for (const auto& bo : dsp_.buttonOut_)
            if (SystemBase* t = project_.findSystem(bo.system))
                t->pressButton(static_cast<std::uint8_t>(bo.button), bo.down);
    }
    std::fill_n(outL, frames, 0.0f);  // cores mix additively → zero each block
    std::fill_n(outR, frames, 0.0f);
    float* outs[2] = { outL, outR };
    AudioBlockInfo info{ frames, sampleRate_, bpm, ppq, transport };
    project_.onProcess(info, outs);
    if (transport)
        ppq += (bpm / 60.0) * (static_cast<double>(frames) / sampleRate_);
}

rfl::Bytestring BackendRpcService::renderAudio(double ms) {
    if (ms <= 0.0) return {};
    scratchL_.resize(kBlockSize);  // idempotent — no per-call realloc after the first
    scratchR_.resize(kBlockSize);

    // Host MIDI staged for the kernel, consumed on the first block (nothing when no kernel loaded).
    std::vector<DspRuntime::MidiIn> pending;
    if (dspActive_) { pending = std::move(pendingMidiIn_); pendingMidiIn_.clear(); }

    const double bpm = bpm_.load(std::memory_order_relaxed);
    const bool   transport = transportPlaying_.load(std::memory_order_relaxed);
    const std::uint64_t total = static_cast<std::uint64_t>(ms * sampleRate_ / 1000.0);
    std::vector<float> out;
    out.reserve(total * 2);
    for (std::uint64_t s = 0; s < total; s += kBlockSize) {
        const auto frames = static_cast<std::uint32_t>(std::min<std::uint64_t>(kBlockSize, total - s));
        renderBlock(frames, bpm, transport, ppq_, s == 0 ? pending : kEmptyMidi,
                    scratchL_.data(), scratchR_.data());
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
    if (audioRunning_.load(std::memory_order_acquire)) {
        DspCommand c;
        c.kind = DspCommand::Kind::StageMidi;
        c.stageMidi.len = static_cast<std::uint8_t>(bytes.size());
        for (std::size_t i = 0; i < bytes.size(); ++i) c.stageMidi.data[i] = bytes[i];
        return dspCommands_.tryPush(c);
    }
    pendingMidiIn_.push_back({ 0, std::move(bytes) });
    return true;
}

bool BackendRpcService::setTransport(bool running) {
    transportPlaying_.store(running, std::memory_order_relaxed);
    return true;
}

bool BackendRpcService::setBpm(double bpm) {
    if (bpm <= 0.0) return false;
    bpm_.store(bpm, std::memory_order_relaxed);
    return true;
}

// --- DSP-side JS runtime ----------------------------------------------------

std::optional<rfl::Bytestring> BackendRpcService::compileScript(std::string source) {
    auto bytecode = dsp::compileToBytecode(source);
    if (!bytecode) return std::nullopt;
    return toBytestring(*bytecode);
}

bool BackendRpcService::dspLoadKernel(std::vector<std::uint8_t> bytecode) {
    if (audioRunning_.load(std::memory_order_acquire)) {
        DspCommand c;
        c.kind = DspCommand::Kind::LoadKernel;
        c.loadKernel.bytecode = new std::vector<std::uint8_t>(std::move(bytecode));
        if (!dspCommands_.tryPush(c)) { delete c.loadKernel.bytecode; return false; }
        return true;  // applied on the audio thread; dspActive_ is set there
    }
    dspActive_ = dsp_.loadKernel(bytecode);
    return dspActive_;
}

bool BackendRpcService::dspSetSystems(std::string json) {
    if (audioRunning_.load(std::memory_order_acquire)) {
        DspCommand c;
        c.kind = DspCommand::Kind::SetSystems;
        c.setSystems.json = new std::string(std::move(json));
        if (!dspCommands_.tryPush(c)) { delete c.setSystems.json; return false; }
        return true;
    }
    return dsp_.setSystems(std::vector<std::uint8_t>(json.begin(), json.end()));
}

// --- background audio thread ------------------------------------------------

BackendRpcService::BackendRpcService() {
    // Pre-reserve so the audio thread's adoptSystem/swapSystem never reallocate systems_ (production
    // reserves the same way). 16 is plenty for the greenfield host's tests.
    project_.reserve(16);
}

BackendRpcService::~BackendRpcService() {
    if (audioRunning_.load(std::memory_order_acquire)) {
        audioRunning_.store(false, std::memory_order_release);
        if (audioThread_.joinable()) audioThread_.join();
    }
    freePendingCommands();  // un-applied command payloads (built-but-unadopted systems etc.)
    drainReleased();        // cores the audio thread released but nobody drained
}

void BackendRpcService::applyDspCommand(const DspCommand& cmd, std::vector<DspRuntime::MidiIn>& pending) {
    switch (cmd.kind) {
        case DspCommand::Kind::SetSystems:
            dsp_.setSystems(std::vector<std::uint8_t>(cmd.setSystems.json->begin(), cmd.setSystems.json->end()));
            delete cmd.setSystems.json;  // owning payload — free on the audio thread (rare structural op)
            break;
        case DspCommand::Kind::LoadKernel:
            dspActive_ = dsp_.loadKernel(*cmd.loadKernel.bytecode);
            delete cmd.loadKernel.bytecode;
            break;
        case DspCommand::Kind::StageMidi:
            pending.push_back({ 0, std::vector<std::uint8_t>(cmd.stageMidi.data, cmd.stageMidi.data + cmd.stageMidi.len) });
            break;
        // Lifecycle: alloc-free pointer swaps into the pre-reserved Project; displaced/removed cores
        // are handed back to the control thread for delete (never freed here).
        case DspCommand::Kind::AddSystem:
            project_.adoptSystem(cmd.addSystem.sys);
            project_.rebuildLinkGroups();
            publishSystemCount();
            break;
        case DspCommand::Kind::ReplaceSystem: {
            // swapSystem returns the displaced core, or the incoming one unchanged if id wasn't
            // found (then it was never adopted) — either way it's the leftover to free.
            SystemBase* old = project_.swapSystem(cmd.replaceSystem.id, cmd.replaceSystem.sys);
            handBackReleased(old);
            project_.rebuildLinkGroups();
            publishSystemCount();
            break;
        }
        case DspCommand::Kind::RemoveSystem:
            handBackReleased(project_.removeSystemAndRelease(cmd.removeSystem.id));  // does its own rebuild
            publishSystemCount();
            break;
        default:
            break;
    }
}

void BackendRpcService::audioLoop() {
    // All of these are audio-thread-local: the control thread reaches the loop only through the
    // command queue (dspCommands_), the transport atomics, and audioRunning_.
    std::vector<float> l(kBlockSize), r(kBlockSize);
    std::vector<DspRuntime::MidiIn> pending;
    double ppq = 0.0;
    double energy = 0.0;
    std::uint64_t frames = 0;

    while (audioRunning_.load(std::memory_order_acquire)) {
        DspCommand cmd;
        while (dspCommands_.tryPop(cmd)) applyDspCommand(cmd, pending);

        const double bpm = bpm_.load(std::memory_order_relaxed);
        const bool   transport = transportPlaying_.load(std::memory_order_relaxed);
        renderBlock(kBlockSize, bpm, transport, ppq, pending, l.data(), r.data());
        pending.clear();  // staged MIDI consumed this block

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
    audioRunning_.store(true, std::memory_order_release);
    audioThread_ = std::thread([this] { audioLoop(); });
    return true;
}

bool BackendRpcService::stopAudio() {
    if (!audioRunning_.load(std::memory_order_acquire)) return false;
    audioRunning_.store(false, std::memory_order_release);
    if (audioThread_.joinable()) audioThread_.join();
    // The audio thread is joined, so both rings are single-threaded again: free any un-applied
    // command payloads, and delete any cores the audio thread released just before stopping.
    freePendingCommands();
    drainReleased();
    return true;
}

// Free command payloads the control thread pushed after the loop's last drain. Only safe once the
// audio thread is joined (dtor / stopAudio) — the ring has a single accessor again.
void BackendRpcService::freePendingCommands() {
    DspCommand cmd;
    while (dspCommands_.tryPop(cmd)) {
        switch (cmd.kind) {
            case DspCommand::Kind::SetSystems:    delete cmd.setSystems.json; break;
            case DspCommand::Kind::LoadKernel:    delete cmd.loadKernel.bytecode; break;
            case DspCommand::Kind::AddSystem:     delete cmd.addSystem.sys; break;      // built but never adopted
            case DspCommand::Kind::ReplaceSystem: delete cmd.replaceSystem.sys; break;  // built but never swapped in
            default: break;  // StageMidi/RemoveSystem carry no heap payload
        }
    }
}

void BackendRpcService::handBackReleased(SystemBase* sys) {
    if (!sys) return;
    DspEvent e;
    e.kind = DspEvent::Kind::SystemReleased;
    e.released.sys = sys;
    if (!dspReleased_.tryPush(e)) {
        // Ring full (256 pending releases undrained) — can't free on the audio thread; leak rather
        // than block/free in the render loop. In practice a test/app drains far more often than this.
        std::fprintf(stderr, "[greenfield] released-system ring full; leaking a core\n");
    }
}

void BackendRpcService::publishSystemCount() {
    liveSystemCount_.store(static_cast<std::uint32_t>(project_.systems().size()), std::memory_order_release);
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
    // SPSC-safe against the audio-thread producer; deleting here is safe because the audio thread
    // already dropped the core from the Project (no longer rendered) before pushing it.
    std::uint32_t freed = 0;
    DspEvent e;
    while (dspReleased_.tryPop(e)) {
        if (e.kind == DspEvent::Kind::SystemReleased) {
            delete e.released.sys;
            ++freed;
        }
    }
    return freed;
}
