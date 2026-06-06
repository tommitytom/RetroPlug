#include "TestHarness.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

extern "C" {
    #include "tjs.h"       // TJS_Initialize / TJS_NewRuntime / TJS_GetJSContext
                           // / TJS_FreeRuntime (+ <quickjs.h>)
    #include "private.h"   // TJS_EvalModuleContent / TJS_GetLoop /
                           // tjs__execute_jobs (+ <uv.h>)
}

#include "Screenshot.hpp"
#include "project/Project.hpp"
#include "system/DebugTarget.hpp"
#include "system/InputTypes.hpp"
#include "system/RomFormat.hpp"
#include "system/SystemBase.hpp"
#include "system/SystemTypes.hpp"
#include "system/MemoryType.hpp"
#include "system/mesen/MesenGbaConfig.hpp"
#include "system/mesen/MesenGbaSystem.hpp"
#include "system/mesen/MesenNesConfig.hpp"
#include "system/mesen/MesenNesSystem.hpp"
#include "system/sameboy/SameBoyConfig.hpp"
#include "system/sameboy/SameBoySystem.hpp"

// Guard the hand-mirrored TypeScript enums in test/harness/index.ts. The wire
// values are load-bearing; if a C++ renumber drifts from the TS Button/Mem
// objects, fail the build here with a pointed message rather than silently
// passing the wrong byte across the bridge.
static_assert(static_cast<int>(GameboyButton::Right)  == 0, "harness Button.Right out of sync");
static_assert(static_cast<int>(GameboyButton::Left)   == 1, "harness Button.Left out of sync");
static_assert(static_cast<int>(GameboyButton::Up)     == 2, "harness Button.Up out of sync");
static_assert(static_cast<int>(GameboyButton::Down)   == 3, "harness Button.Down out of sync");
static_assert(static_cast<int>(GameboyButton::A)      == 4, "harness Button.A out of sync");
static_assert(static_cast<int>(GameboyButton::B)      == 5, "harness Button.B out of sync");
static_assert(static_cast<int>(GameboyButton::Select) == 6, "harness Button.Select out of sync");
static_assert(static_cast<int>(GameboyButton::Start)  == 7, "harness Button.Start out of sync");
static_assert(static_cast<int>(rp::MemoryType::Ram)          == 0, "harness Mem.Ram out of sync");
static_assert(static_cast<int>(rp::MemoryType::Rom)          == 1, "harness Mem.Rom out of sync");
static_assert(static_cast<int>(rp::MemoryType::Sram)         == 2, "harness Mem.Sram out of sync");
static_assert(static_cast<int>(rp::MemoryType::Vram)         == 3, "harness Mem.Vram out of sync");
static_assert(static_cast<int>(rp::MemoryType::IORegisters)  == 4, "harness Mem.IORegisters out of sync");
static_assert(static_cast<int>(rp::MemoryType::HRam)         == 5, "harness Mem.HRam out of sync");
static_assert(static_cast<int>(rp::MemoryType::OAM)          == 6, "harness Mem.OAM out of sync");
static_assert(static_cast<int>(rp::MemoryType::NametableRam) == 7, "harness Mem.NametableRam out of sync");
static_assert(static_cast<int>(rp::MemoryType::ExtWorkRam)   == 8, "harness Mem.ExtWorkRam out of sync");

namespace {

std::string slurpText(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot open " + path);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::vector<std::uint8_t> slurpBytes(const std::string& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) throw std::runtime_error("cannot open " + path);
    const std::streamsize size = in.tellg();
    if (size <= 0) throw std::runtime_error("empty file: " + path);
    in.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> buf(static_cast<std::size_t>(size));
    if (!in.read(reinterpret_cast<char*>(buf.data()), size))
        throw std::runtime_error("read failed: " + path);
    return buf;
}

// Flatten a TAP YAML diagnostic message onto one logical block, escaping
// newlines so a multi-line stack trace doesn't break the `1..N` plan.
std::string oneLine(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) out += (c == '\n') ? ' ' : c;
    return out;
}

} // namespace

// ---------------------------------------------------------------------------
// Impl: owns the runtime + the Project the `emu` shims drive.
// ---------------------------------------------------------------------------

struct TestHarness::Impl {
    TJSRuntime* qrt = nullptr;
    JSContext*  ctx = nullptr;

    std::unique_ptr<Project> project;
    double      sampleRate = 44100.0;
    std::uint32_t blockSize = 1024;
    std::vector<float> scratchL, scratchR;

    // TAP state.
    int  testIndex   = 0;
    int  failures    = 0;
    bool donePrinted = false;

    Impl() : project(std::make_unique<Project>()),
             scratchL(blockSize), scratchR(blockSize) {}

    // -- emu surface (called from the static JS trampolines) ----------------

    std::uint32_t loadRom(const std::string& path) {
        auto bytes = slurpBytes(path);
        const RomFormat fmt = detectRomFormat(bytes);

        std::unique_ptr<SystemBase> sys;
        switch (fmt) {
            case RomFormat::SameBoy: {
                SameBoyConfig cfg;
                cfg.romPath  = path;
                cfg.model    = SameBoyModel::CgbC;
                cfg.fastBoot = true;
                sys = std::make_unique<SameBoySystem>(
                    project->nextSystemId(), cfg, std::move(bytes));
                break;
            }
            case RomFormat::MesenNes: {
                MesenNesConfig cfg;
                cfg.romPath = path;
                sys = std::make_unique<MesenNesSystem>(
                    project->nextSystemId(), cfg, std::move(bytes));
                break;
            }
            case RomFormat::MesenGba: {
                MesenGbaConfig cfg;
                cfg.romPath = path; // no biosPath -> Mesen falls back to HLE BIOS
                sys = std::make_unique<MesenGbaSystem>(
                    project->nextSystemId(), cfg, std::move(bytes));
                break;
            }
            default:
                throw std::runtime_error("loadRom: '" + path +
                    "' is not a recognised Game Boy, NES, or GBA ROM");
        }

        sys->onActivate(sampleRate);
        const SystemId id = sys->id();
        project->adoptSystem(sys.release());
        project->rebuildLinkGroups();
        return static_cast<std::uint32_t>(id);
    }

    SystemBase* system(std::uint32_t id) {
        return project->findSystem(static_cast<SystemId>(id));
    }

    // Resolve a system + require it expose CPU state (non-empty register file).
    // No dynamic_cast: every backend answers the SystemBase CPU virtuals.
    SystemBase* cpuSystem(std::uint32_t id) {
        SystemBase* sys = system(id);
        if (!sys) throw std::runtime_error("unknown system id");
        if (sys->getCpuRegisters().empty())
            throw std::runtime_error("CPU state is not available for this system");
        return sys;
    }

    // Resolve a system's debugger/profiler (Mesen NES). Throws if unsupported.
    rp::IDebugTarget* debugTarget(std::uint32_t id) {
        SystemBase* sys = system(id);
        if (!sys) throw std::runtime_error("unknown system id");
        rp::IDebugTarget* d = sys->debugTarget();
        if (!d) throw std::runtime_error(
            "no debugger for this system (Mesen NES only)");
        return d;
    }

    void runMs(double ms) {
        if (ms <= 0.0) return;
        const std::uint64_t total =
            static_cast<std::uint64_t>(ms * sampleRate / 1000.0);
        float* outs[2] = { scratchL.data(), scratchR.data() };
        for (std::uint64_t s = 0; s < total; s += blockSize) {
            const std::uint32_t frames = static_cast<std::uint32_t>(
                std::min<std::uint64_t>(blockSize, total - s));
            std::fill_n(scratchL.data(), frames, 0.0f);
            std::fill_n(scratchR.data(), frames, 0.0f);
            AudioBlockInfo info{ frames, sampleRate, 120.0, 0.0, false };
            project->onProcess(info, outs);
        }
    }

    // Like runMs but retains the mixed stereo output interleaved (L,R,L,R…).
    std::vector<float> runMsCapture(double ms) {
        std::vector<float> out;
        if (ms <= 0.0) return out;
        const std::uint64_t total =
            static_cast<std::uint64_t>(ms * sampleRate / 1000.0);
        out.reserve(total * 2);
        float* outs[2] = { scratchL.data(), scratchR.data() };
        for (std::uint64_t s = 0; s < total; s += blockSize) {
            const std::uint32_t frames = static_cast<std::uint32_t>(
                std::min<std::uint64_t>(blockSize, total - s));
            std::fill_n(scratchL.data(), frames, 0.0f);
            std::fill_n(scratchR.data(), frames, 0.0f);
            AudioBlockInfo info{ frames, sampleRate, 120.0, 0.0, false };
            project->onProcess(info, outs);
            for (std::uint32_t f = 0; f < frames; ++f) {
                out.push_back(scratchL[f]);
                out.push_back(scratchR[f]);
            }
        }
        return out;
    }

    // Fresh Project per test() case so cases can't bleed emulator state.
    void beginCase() { project = std::make_unique<Project>(); }

    void report(const std::string& name, bool ok, const std::string& msg) {
        ++testIndex;
        if (ok) {
            std::printf("ok %d - %s\n", testIndex, name.c_str());
        } else {
            ++failures;
            std::printf("not ok %d - %s\n", testIndex, name.c_str());
            if (!msg.empty()) {
                std::printf("  ---\n  message: \"%s\"\n  ...\n",
                            oneLine(msg).c_str());
            }
        }
        std::fflush(stdout);
    }

    void done() {
        if (donePrinted) return;
        donePrinted = true;
        std::printf("1..%d\n", testIndex);
        std::fflush(stdout);
    }
};

// The active harness for the current process. txiki occupies BOTH the QuickJS
// context- and runtime-opaque slots (vm.c stores its TJSRuntime* in each), so
// we cannot stash our Impl there. One runtime per process + single-threaded
// means a translation-unit pointer is the correct recovery mechanism for the
// static JS trampolines.
namespace { TestHarness::Impl* g_activeImpl = nullptr; }

// ---------------------------------------------------------------------------
// JS trampolines. Every body is wrapped so a C++ throw never crosses into
// QuickJS (which does not catch C++ exceptions).
// ---------------------------------------------------------------------------

namespace {

JSValue jsLoadRom(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    auto* h = g_activeImpl;
    if (!h) return JS_ThrowInternalError(ctx, "harness unavailable");
    if (argc < 1) return JS_ThrowTypeError(ctx, "loadRom(path) requires a path");
    const char* path = JS_ToCString(ctx, argv[0]);
    if (!path) return JS_EXCEPTION;
    try {
        const std::uint32_t id = h->loadRom(path);
        JS_FreeCString(ctx, path);
        return JS_NewInt32(ctx, static_cast<int32_t>(id));
    } catch (const std::exception& e) {
        JSValue err = JS_ThrowTypeError(ctx, "%s", e.what());
        JS_FreeCString(ctx, path);
        return err;
    }
}

JSValue jsRunMs(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    auto* h = g_activeImpl;
    if (!h) return JS_ThrowInternalError(ctx, "harness unavailable");
    double ms = 0.0;
    if (argc >= 1 && JS_ToFloat64(ctx, &ms, argv[0]) < 0) return JS_EXCEPTION;
    try {
        h->runMs(ms);
        return JS_UNDEFINED;
    } catch (const std::exception& e) {
        return JS_ThrowTypeError(ctx, "runMs: %s", e.what());
    }
}

JSValue jsPress(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    auto* h = g_activeImpl;
    if (!h) return JS_ThrowInternalError(ctx, "harness unavailable");
    int32_t id = 0, button = 0;
    if (argc < 3) return JS_ThrowTypeError(ctx, "press(id, button, down)");
    if (JS_ToInt32(ctx, &id, argv[0]) < 0) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &button, argv[1]) < 0) return JS_EXCEPTION;
    const int down = JS_ToBool(ctx, argv[2]);
    if (down < 0) return JS_EXCEPTION;
    try {
        SystemBase* sys = h->system(static_cast<std::uint32_t>(id));
        if (!sys) throw std::runtime_error("unknown system id");
        sys->pressButton(static_cast<std::uint8_t>(button), down == 1);
        return JS_UNDEFINED;
    } catch (const std::exception& e) {
        return JS_ThrowTypeError(ctx, "press: %s", e.what());
    }
}

JSValue jsReadMemory(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    auto* h = g_activeImpl;
    if (!h) return JS_ThrowInternalError(ctx, "harness unavailable");
    int32_t id = 0, type = 0;
    if (argc < 2) return JS_ThrowTypeError(ctx, "readMemory(id, type)");
    if (JS_ToInt32(ctx, &id, argv[0]) < 0) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &type, argv[1]) < 0) return JS_EXCEPTION;
    try {
        SystemBase* sys = h->system(static_cast<std::uint32_t>(id));
        if (!sys) throw std::runtime_error("unknown system id");
        // Always hand JS a COPY — the accessor's pointer is live emulator
        // memory and relocates on reset/cart-swap.
        rp::MemoryAccessor acc = sys->getMemory(
            static_cast<rp::MemoryType>(type), rp::AccessType::Read);
        if (!acc.valid()) {
            const std::uint8_t empty = 0;
            return JS_NewArrayBufferCopy(ctx, &empty, 0);
        }
        return JS_NewArrayBufferCopy(ctx, acc.data(), acc.size());
    } catch (const std::exception& e) {
        return JS_ThrowTypeError(ctx, "readMemory: %s", e.what());
    }
}

JSValue jsGetRegisters(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    auto* h = g_activeImpl;
    if (!h) return JS_ThrowInternalError(ctx, "harness unavailable");
    int32_t id = 0;
    if (argc < 1) return JS_ThrowTypeError(ctx, "getRegisters(id)");
    if (JS_ToInt32(ctx, &id, argv[0]) < 0) return JS_EXCEPTION;
    try {
        // Name-keyed register file — each backend reports its own CPU's set.
        const std::vector<rp::CpuRegister> regs =
            h->cpuSystem(static_cast<std::uint32_t>(id))->getCpuRegisters();
        JSValue obj = JS_NewObject(ctx);
        for (const auto& r : regs) {
            JS_SetPropertyStr(ctx, obj, r.name.c_str(),
                              JS_NewInt64(ctx, static_cast<int64_t>(r.value)));
        }
        return obj;
    } catch (const std::exception& e) {
        return JS_ThrowTypeError(ctx, "getRegisters: %s", e.what());
    }
}

JSValue jsSetRegister(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    auto* h = g_activeImpl;
    if (!h) return JS_ThrowInternalError(ctx, "harness unavailable");
    int32_t id = 0;
    int64_t value = 0;
    if (argc < 3) return JS_ThrowTypeError(ctx, "setRegister(id, name, value)");
    if (JS_ToInt32(ctx, &id, argv[0]) < 0) return JS_EXCEPTION;
    const char* name = JS_ToCString(ctx, argv[1]);
    if (!name) return JS_EXCEPTION;
    if (JS_ToInt64(ctx, &value, argv[2]) < 0) { JS_FreeCString(ctx, name); return JS_EXCEPTION; }
    try {
        const bool ok = h->cpuSystem(static_cast<std::uint32_t>(id))
            ->setCpuRegister(name, static_cast<std::uint32_t>(value));
        if (!ok) {
            JSValue err = JS_ThrowTypeError(ctx,
                "setRegister: unknown or read-only register '%s'", name);
            JS_FreeCString(ctx, name);
            return err;
        }
        JS_FreeCString(ctx, name);
        return JS_UNDEFINED;
    } catch (const std::exception& e) {
        JSValue err = JS_ThrowTypeError(ctx, "setRegister: %s", e.what());
        JS_FreeCString(ctx, name);
        return err;
    }
}

JSValue jsReadCpu(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    auto* h = g_activeImpl;
    if (!h) return JS_ThrowInternalError(ctx, "harness unavailable");
    int32_t id = 0, addr = 0;
    if (argc < 2) return JS_ThrowTypeError(ctx, "readCpu(id, addr)");
    if (JS_ToInt32(ctx, &id, argv[0]) < 0) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &addr, argv[1]) < 0) return JS_EXCEPTION;
    try {
        const std::optional<std::uint8_t> b =
            h->cpuSystem(static_cast<std::uint32_t>(id))
                ->readCpuByte(static_cast<std::uint32_t>(addr));
        if (!b)
            throw std::runtime_error(
                "side-effect-free CPU peek is not supported for this system "
                "(use readMemory)");
        return JS_NewInt32(ctx, *b);
    } catch (const std::exception& e) {
        return JS_ThrowTypeError(ctx, "readCpu: %s", e.what());
    }
}

JSValue jsStep(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    auto* h = g_activeImpl;
    if (!h) return JS_ThrowInternalError(ctx, "harness unavailable");
    int32_t id = 0;
    if (argc < 1) return JS_ThrowTypeError(ctx, "step(id)");
    if (JS_ToInt32(ctx, &id, argv[0]) < 0) return JS_EXCEPTION;
    try {
        // 0 = backend can't instruction-step (e.g. GBA without the debugger).
        const std::uint64_t cycles =
            h->cpuSystem(static_cast<std::uint32_t>(id))->stepInstruction();
        return JS_NewInt64(ctx, static_cast<int64_t>(cycles));
    } catch (const std::exception& e) {
        return JS_ThrowTypeError(ctx, "step: %s", e.what());
    }
}

JSValue jsRunUntilPc(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    auto* h = g_activeImpl;
    if (!h) return JS_ThrowInternalError(ctx, "harness unavailable");
    int32_t id = 0, target = 0;
    int64_t maxCycles = 0;
    if (argc < 3) return JS_ThrowTypeError(ctx, "runUntilPc(id, pc, maxCycles)");
    if (JS_ToInt32(ctx, &id, argv[0]) < 0) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &target, argv[1]) < 0) return JS_EXCEPTION;
    if (JS_ToInt64(ctx, &maxCycles, argv[2]) < 0) return JS_EXCEPTION;
    if (maxCycles <= 0) return JS_ThrowRangeError(ctx, "runUntilPc: maxCycles must be > 0");
    try {
        const bool hit = h->cpuSystem(static_cast<std::uint32_t>(id))->runUntilPc(
            static_cast<std::uint32_t>(target),
            static_cast<std::uint64_t>(maxCycles));
        return JS_NewBool(ctx, hit);
    } catch (const std::exception& e) {
        return JS_ThrowTypeError(ctx, "runUntilPc: %s", e.what());
    }
}

JSValue jsBeginProfile(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    auto* h = g_activeImpl;
    if (!h) return JS_ThrowInternalError(ctx, "harness unavailable");
    int32_t id = 0;
    if (argc < 1) return JS_ThrowTypeError(ctx, "beginProfile(id)");
    if (JS_ToInt32(ctx, &id, argv[0]) < 0) return JS_EXCEPTION;
    try {
        h->debugTarget(static_cast<std::uint32_t>(id))->beginProfile();
        return JS_UNDEFINED;
    } catch (const std::exception& e) {
        return JS_ThrowTypeError(ctx, "beginProfile: %s", e.what());
    }
}

JSValue jsReadProfile(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    auto* h = g_activeImpl;
    if (!h) return JS_ThrowInternalError(ctx, "harness unavailable");
    int32_t id = 0;
    if (argc < 1) return JS_ThrowTypeError(ctx, "readProfile(id)");
    if (JS_ToInt32(ctx, &id, argv[0]) < 0) return JS_EXCEPTION;
    try {
        const std::vector<rp::ProfiledFunction> fns =
            h->debugTarget(static_cast<std::uint32_t>(id))->readProfile();
        JSValue arr = JS_NewArray(ctx);
        for (std::uint32_t i = 0; i < fns.size(); ++i) {
            const rp::ProfiledFunction& f = fns[i];
            JSValue o = JS_NewObject(ctx);
            JS_SetPropertyStr(ctx, o, "address", JS_NewInt32(ctx, f.address));
            JS_SetPropertyStr(ctx, o, "label",
                JS_NewStringLen(ctx, f.label.data(), f.label.size()));
            JS_SetPropertyStr(ctx, o, "exclusiveCycles", JS_NewInt64(ctx, (int64_t)f.exclusiveCycles));
            JS_SetPropertyStr(ctx, o, "inclusiveCycles", JS_NewInt64(ctx, (int64_t)f.inclusiveCycles));
            JS_SetPropertyStr(ctx, o, "callCount",       JS_NewInt64(ctx, (int64_t)f.callCount));
            JS_SetPropertyStr(ctx, o, "minCycles",       JS_NewInt64(ctx, (int64_t)f.minCycles));
            JS_SetPropertyStr(ctx, o, "maxCycles",       JS_NewInt64(ctx, (int64_t)f.maxCycles));
            JS_SetPropertyUint32(ctx, arr, i, o);
        }
        return arr;
    } catch (const std::exception& e) {
        return JS_ThrowTypeError(ctx, "readProfile: %s", e.what());
    }
}

JSValue jsLoadLabels(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    auto* h = g_activeImpl;
    if (!h) return JS_ThrowInternalError(ctx, "harness unavailable");
    int32_t id = 0;
    if (argc < 2) return JS_ThrowTypeError(ctx, "loadLabels(id, path)");
    if (JS_ToInt32(ctx, &id, argv[0]) < 0) return JS_EXCEPTION;
    const char* path = JS_ToCString(ctx, argv[1]);
    if (!path) return JS_EXCEPTION;
    try {
        const bool ok = h->debugTarget(static_cast<std::uint32_t>(id))->loadLabels(path);
        JS_FreeCString(ctx, path);
        return JS_NewBool(ctx, ok);
    } catch (const std::exception& e) {
        JSValue err = JS_ThrowTypeError(ctx, "loadLabels: %s", e.what());
        JS_FreeCString(ctx, path);
        return err;
    }
}

JSValue jsDisassemble(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    auto* h = g_activeImpl;
    if (!h) return JS_ThrowInternalError(ctx, "harness unavailable");
    int32_t id = 0, addr = 0, count = 1;
    if (argc < 3) return JS_ThrowTypeError(ctx, "disassemble(id, addr, count)");
    if (JS_ToInt32(ctx, &id, argv[0]) < 0) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &addr, argv[1]) < 0) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &count, argv[2]) < 0) return JS_EXCEPTION;
    try {
        const auto lines = h->debugTarget(static_cast<std::uint32_t>(id))
            ->disassemble(static_cast<std::uint32_t>(addr), static_cast<std::uint32_t>(count));
        JSValue arr = JS_NewArray(ctx);
        for (std::uint32_t i = 0; i < lines.size(); ++i) {
            JSValue o = JS_NewObject(ctx);
            JS_SetPropertyStr(ctx, o, "address", JS_NewInt32(ctx, lines[i].address));
            JS_SetPropertyStr(ctx, o, "text",  JS_NewStringLen(ctx, lines[i].text.data(), lines[i].text.size()));
            JS_SetPropertyStr(ctx, o, "bytes", JS_NewStringLen(ctx, lines[i].bytes.data(), lines[i].bytes.size()));
            JS_SetPropertyUint32(ctx, arr, i, o);
        }
        return arr;
    } catch (const std::exception& e) {
        return JS_ThrowTypeError(ctx, "disassemble: %s", e.what());
    }
}

JSValue jsSetTrace(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    auto* h = g_activeImpl;
    if (!h) return JS_ThrowInternalError(ctx, "harness unavailable");
    int32_t id = 0;
    if (argc < 2) return JS_ThrowTypeError(ctx, "setTrace(id, on)");
    if (JS_ToInt32(ctx, &id, argv[0]) < 0) return JS_EXCEPTION;
    const int on = JS_ToBool(ctx, argv[1]);
    if (on < 0) return JS_EXCEPTION;
    try {
        h->debugTarget(static_cast<std::uint32_t>(id))->setTraceEnabled(on == 1);
        return JS_UNDEFINED;
    } catch (const std::exception& e) {
        return JS_ThrowTypeError(ctx, "setTrace: %s", e.what());
    }
}

JSValue jsReadTrace(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    auto* h = g_activeImpl;
    if (!h) return JS_ThrowInternalError(ctx, "harness unavailable");
    int32_t id = 0, count = 1;
    if (argc < 2) return JS_ThrowTypeError(ctx, "readTrace(id, count)");
    if (JS_ToInt32(ctx, &id, argv[0]) < 0) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &count, argv[1]) < 0) return JS_EXCEPTION;
    try {
        const auto rows = h->debugTarget(static_cast<std::uint32_t>(id))
            ->readTrace(static_cast<std::uint32_t>(count));
        JSValue arr = JS_NewArray(ctx);
        for (std::uint32_t i = 0; i < rows.size(); ++i) {
            JSValue o = JS_NewObject(ctx);
            JS_SetPropertyStr(ctx, o, "pc",   JS_NewInt32(ctx, (int32_t)rows[i].pc));
            JS_SetPropertyStr(ctx, o, "text", JS_NewStringLen(ctx, rows[i].text.data(), rows[i].text.size()));
            JS_SetPropertyUint32(ctx, arr, i, o);
        }
        return arr;
    } catch (const std::exception& e) {
        return JS_ThrowTypeError(ctx, "readTrace: %s", e.what());
    }
}

JSValue jsGetCallStack(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    auto* h = g_activeImpl;
    if (!h) return JS_ThrowInternalError(ctx, "harness unavailable");
    int32_t id = 0;
    if (argc < 1) return JS_ThrowTypeError(ctx, "getCallStack(id)");
    if (JS_ToInt32(ctx, &id, argv[0]) < 0) return JS_EXCEPTION;
    try {
        const auto frames = h->debugTarget(static_cast<std::uint32_t>(id))->getCallStack();
        JSValue arr = JS_NewArray(ctx);
        for (std::uint32_t i = 0; i < frames.size(); ++i) {
            JSValue o = JS_NewObject(ctx);
            JS_SetPropertyStr(ctx, o, "address", JS_NewInt32(ctx, frames[i].address));
            JS_SetPropertyStr(ctx, o, "label", JS_NewStringLen(ctx, frames[i].label.data(), frames[i].label.size()));
            JS_SetPropertyUint32(ctx, arr, i, o);
        }
        return arr;
    } catch (const std::exception& e) {
        return JS_ThrowTypeError(ctx, "getCallStack: %s", e.what());
    }
}

JSValue breakInfoToJs(JSContext* ctx, const rp::BreakInfo& bi) {
    JSValue o = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, o, "broke",        JS_NewBool(ctx, bi.broke));
    JS_SetPropertyStr(ctx, o, "pc",           JS_NewInt32(ctx, (int32_t)bi.pc));
    JS_SetPropertyStr(ctx, o, "breakpointId", JS_NewInt32(ctx, bi.breakpointId));
    return o;
}

JSValue jsSetBreakpoints(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    auto* h = g_activeImpl;
    if (!h) return JS_ThrowInternalError(ctx, "harness unavailable");
    int32_t id = 0;
    if (argc < 2) return JS_ThrowTypeError(ctx, "setBreakpoints(id, bps[])");
    if (JS_ToInt32(ctx, &id, argv[0]) < 0) return JS_EXCEPTION;
    if (!JS_IsArray(argv[1])) return JS_ThrowTypeError(ctx, "setBreakpoints: bps must be an array");
    try {
        JSValue lenv = JS_GetPropertyStr(ctx, argv[1], "length");
        int32_t len = 0; JS_ToInt32(ctx, &len, lenv); JS_FreeValue(ctx, lenv);
        std::vector<rp::BreakpointSpec> specs;
        for (int32_t i = 0; i < len; ++i) {
            JSValue o = JS_GetPropertyUint32(ctx, argv[1], (uint32_t)i);
            rp::BreakpointSpec s;
            JSValue tv = JS_GetPropertyStr(ctx, o, "type");
            if (const char* ts = JS_ToCString(ctx, tv)) { s.type = ts; JS_FreeCString(ctx, ts); }
            JS_FreeValue(ctx, tv);
            JSValue sv = JS_GetPropertyStr(ctx, o, "start");
            int32_t sa = 0; JS_ToInt32(ctx, &sa, sv); s.start = (uint32_t)sa; JS_FreeValue(ctx, sv);
            JSValue ev = JS_GetPropertyStr(ctx, o, "end");
            int32_t ea = 0; if (!JS_IsUndefined(ev)) JS_ToInt32(ctx, &ea, ev); s.end = (uint32_t)ea; JS_FreeValue(ctx, ev);
            JSValue cv = JS_GetPropertyStr(ctx, o, "condition");
            if (JS_IsString(cv)) { if (const char* cs = JS_ToCString(ctx, cv)) { s.condition = cs; JS_FreeCString(ctx, cs); } }
            JS_FreeValue(ctx, cv);
            JS_FreeValue(ctx, o);
            specs.push_back(std::move(s));
        }
        h->debugTarget(static_cast<std::uint32_t>(id))->setBreakpoints(specs);
        return JS_UNDEFINED;
    } catch (const std::exception& e) {
        return JS_ThrowTypeError(ctx, "setBreakpoints: %s", e.what());
    }
}

JSValue jsRunUntilBreak(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    auto* h = g_activeImpl;
    if (!h) return JS_ThrowInternalError(ctx, "harness unavailable");
    int32_t id = 0; int64_t maxCycles = 0;
    if (argc < 2) return JS_ThrowTypeError(ctx, "runUntilBreak(id, maxCycles)");
    if (JS_ToInt32(ctx, &id, argv[0]) < 0) return JS_EXCEPTION;
    if (JS_ToInt64(ctx, &maxCycles, argv[1]) < 0) return JS_EXCEPTION;
    if (maxCycles <= 0) return JS_ThrowRangeError(ctx, "runUntilBreak: maxCycles must be > 0");
    try {
        return breakInfoToJs(ctx, h->debugTarget(static_cast<std::uint32_t>(id))
            ->runUntilBreak(static_cast<std::uint64_t>(maxCycles)));
    } catch (const std::exception& e) {
        return JS_ThrowTypeError(ctx, "runUntilBreak: %s", e.what());
    }
}

// magic: 0=step, 1=stepOver, 2=stepOut
JSValue jsStepKind(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv, int magic) {
    auto* h = g_activeImpl;
    if (!h) return JS_ThrowInternalError(ctx, "harness unavailable");
    int32_t id = 0;
    if (argc < 1) return JS_ThrowTypeError(ctx, "step(id)");
    if (JS_ToInt32(ctx, &id, argv[0]) < 0) return JS_EXCEPTION;
    try {
        rp::IDebugTarget* d = h->debugTarget(static_cast<std::uint32_t>(id));
        rp::BreakInfo bi = magic == 1 ? d->stepOver()
                         : magic == 2 ? d->stepOut()
                                      : d->step();
        return breakInfoToJs(ctx, bi);
    } catch (const std::exception& e) {
        return JS_ThrowTypeError(ctx, "step: %s", e.what());
    }
}

JSValue jsGetFrame(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    auto* h = g_activeImpl;
    if (!h) return JS_ThrowInternalError(ctx, "harness unavailable");
    int32_t id = 0;
    if (argc < 1) return JS_ThrowTypeError(ctx, "getFrame(id)");
    if (JS_ToInt32(ctx, &id, argv[0]) < 0) return JS_EXCEPTION;
    try {
        SystemBase* sys = h->system(static_cast<std::uint32_t>(id));
        if (!sys) throw std::runtime_error("unknown system id");
        FrameBufferTriple* fb = sys->framebuffer();
        if (!fb) throw std::runtime_error("system has no framebuffer");

        const std::uint32_t fbW = fb->width();
        const std::uint32_t fbH = fb->height();
        const std::size_t pixels = static_cast<std::size_t>(fbW) * fbH;
        std::vector<std::uint32_t> xrgb(pixels);
        const bool published = fb->readInto(xrgb.data(),
                                            static_cast<std::uint32_t>(pixels));

        JSValue obj = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, obj, "width",  JS_NewInt32(ctx, fbW));
        JS_SetPropertyStr(ctx, obj, "height", JS_NewInt32(ctx, fbH));
        JS_SetPropertyStr(ctx, obj, "published", JS_NewBool(ctx, published));
        // XRGB8888 bytes (empty when no frame has been published yet).
        const std::uint8_t* bytes =
            reinterpret_cast<const std::uint8_t*>(xrgb.data());
        JS_SetPropertyStr(ctx, obj, "data",
            JS_NewArrayBufferCopy(ctx, bytes, published ? pixels * 4 : 0));
        return obj;
    } catch (const std::exception& e) {
        return JS_ThrowTypeError(ctx, "getFrame: %s", e.what());
    }
}

JSValue jsScreenshot(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    auto* h = g_activeImpl;
    if (!h) return JS_ThrowInternalError(ctx, "harness unavailable");
    int32_t id = 0;
    if (argc < 2) return JS_ThrowTypeError(ctx, "screenshot(id, path)");
    if (JS_ToInt32(ctx, &id, argv[0]) < 0) return JS_EXCEPTION;
    const char* path = JS_ToCString(ctx, argv[1]);
    if (!path) return JS_EXCEPTION;
    try {
        SystemBase* sys = h->system(static_cast<std::uint32_t>(id));
        if (!sys) throw std::runtime_error("unknown system id");
        const bool ok = rpcli::writeFramebufferPng(*sys, path);
        JS_FreeCString(ctx, path);
        return JS_NewBool(ctx, ok);
    } catch (const std::exception& e) {
        JSValue err = JS_ThrowTypeError(ctx, "screenshot: %s", e.what());
        JS_FreeCString(ctx, path);
        return err;
    }
}

JSValue jsGetAudio(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    auto* h = g_activeImpl;
    if (!h) return JS_ThrowInternalError(ctx, "harness unavailable");
    double ms = 0.0;
    if (argc >= 1 && JS_ToFloat64(ctx, &ms, argv[0]) < 0) return JS_EXCEPTION;
    try {
        const std::vector<float> samples = h->runMsCapture(ms);
        return JS_NewArrayBufferCopy(ctx,
            reinterpret_cast<const std::uint8_t*>(samples.data()),
            samples.size() * sizeof(float));
    } catch (const std::exception& e) {
        return JS_ThrowTypeError(ctx, "getAudio: %s", e.what());
    }
}

JSValue jsBeginCase(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    auto* h = g_activeImpl;
    if (!h) return JS_ThrowInternalError(ctx, "harness unavailable");
    h->beginCase();
    return JS_UNDEFINED;
}

JSValue jsReport(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    auto* h = g_activeImpl;
    if (!h) return JS_ThrowInternalError(ctx, "harness unavailable");
    if (argc < 2) return JS_ThrowTypeError(ctx, "report(name, ok, message?)");
    const char* name = JS_ToCString(ctx, argv[0]);
    const int   ok   = JS_ToBool(ctx, argv[1]);
    const char* msg  = (argc >= 3 && !JS_IsUndefined(argv[2]))
                           ? JS_ToCString(ctx, argv[2]) : nullptr;
    h->report(name ? name : "", ok == 1, msg ? msg : "");
    if (name) JS_FreeCString(ctx, name);
    if (msg)  JS_FreeCString(ctx, msg);
    return JS_UNDEFINED;
}

JSValue jsDone(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    auto* h = g_activeImpl;
    if (!h) return JS_ThrowInternalError(ctx, "harness unavailable");
    h->done();
    return JS_UNDEFINED;
}

// Console shim. txiki's built-in console writes to stdout, which would corrupt
// the TAP stream; route everything to stderr with the project's [js:<level>]
// convention instead.
JSValue jsLog(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    int level = 0;
    if (argc >= 1) JS_ToInt32(ctx, &level, argv[0]);
    const char* msg = (argc >= 2) ? JS_ToCString(ctx, argv[1]) : nullptr;
    const char* tag = level >= 2 ? "error" : level == 1 ? "warn" : "log";
    std::fprintf(stderr, "[js:%s] %s\n", tag, msg ? msg : "");
    if (msg) JS_FreeCString(ctx, msg);
    return JS_UNDEFINED;
}

} // namespace

// ---------------------------------------------------------------------------
// TestHarness lifecycle.
// ---------------------------------------------------------------------------

TestHarness::TestHarness() : impl_(std::make_unique<Impl>()) {
    // Idempotent global init (mirrors src/LvglJsEngine.cpp:130-136).
    static bool tjsInitialized = false;
    if (!tjsInitialized) {
        static char arg0[] = "retroplug-cli";
        static char* argv[] = { arg0, nullptr };
        TJS_Initialize(1, argv);
        tjsInitialized = true;
    }

    impl_->qrt = TJS_NewRuntime();
    if (!impl_->qrt) throw std::runtime_error("TJS_NewRuntime() failed");
    impl_->ctx = TJS_GetJSContext(impl_->qrt);

    // Recover *this Impl inside the static C trampolines. NOT via the context/
    // runtime opaque slots — txiki owns both for its TJSRuntime*.
    g_activeImpl = impl_.get();

    JSContext* ctx = impl_->ctx;

    // Build the Symbol.for("retroplug") namespace and attach the native
    // functions before defining it (DefinePropertyValue consumes the ref).
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue sym    = JS_NewSymbol(ctx, "retroplug", /*is_global*/ true);
    JSAtom atom    = JS_ValueToAtom(ctx, sym);
    JSValue ns     = JS_NewObjectProto(ctx, JS_NULL);

    auto bind = [&](const char* name, JSCFunction* fn, int argc) {
        JS_SetPropertyStr(ctx, ns, name, JS_NewCFunction(ctx, fn, name, argc));
    };
    bind("loadRom",      jsLoadRom,      1);
    bind("runMs",        jsRunMs,        1);
    bind("press",        jsPress,        3);
    bind("readMemory",   jsReadMemory,   2);
    bind("getRegisters", jsGetRegisters, 1);
    bind("setRegister",  jsSetRegister,  3);
    bind("readCpu",      jsReadCpu,      2);
    bind("step",         jsStep,         1);
    bind("runUntilPc",   jsRunUntilPc,   3);
    bind("getFrame",      jsGetFrame,      1);
    bind("screenshot",    jsScreenshot,    2);
    bind("getAudio",      jsGetAudio,      1);
    bind("beginProfile",  jsBeginProfile,  1);
    bind("readProfile",   jsReadProfile,   1);
    bind("loadLabels",    jsLoadLabels,    2);
    bind("disassemble",    jsDisassemble,    3);
    bind("setTrace",       jsSetTrace,       2);
    bind("readTrace",      jsReadTrace,      2);
    bind("getCallStack",   jsGetCallStack,   1);
    bind("setBreakpoints", jsSetBreakpoints, 2);
    bind("runUntilBreak",  jsRunUntilBreak,  2);
    JS_SetPropertyStr(ctx, ns, "stepInto",
        JS_NewCFunctionMagic(ctx, jsStepKind, "stepInto", 1, JS_CFUNC_generic_magic, 0));
    JS_SetPropertyStr(ctx, ns, "stepOver",
        JS_NewCFunctionMagic(ctx, jsStepKind, "stepOver", 1, JS_CFUNC_generic_magic, 1));
    JS_SetPropertyStr(ctx, ns, "stepOut",
        JS_NewCFunctionMagic(ctx, jsStepKind, "stepOut", 1, JS_CFUNC_generic_magic, 2));
    bind("beginCase",    jsBeginCase,    0);
    bind("report",       jsReport,       3);
    bind("done",         jsDone,         0);
    bind("log",          jsLog,          2);

    JS_DefinePropertyValue(ctx, global, atom, ns, JS_PROP_C_W_E);
    JS_FreeAtom(ctx, atom);
    JS_FreeValue(ctx, sym);
    JS_FreeValue(ctx, global);

    // Redirect console.* to stderr (keep stdout TAP-clean).
    static const char kConsoleShim[] =
        "(() => {"
        "  const rp = globalThis[Symbol.for('retroplug')];"
        "  const mk = (lvl) => (...a) => rp.log(lvl, a.map("
        "    x => typeof x === 'string' ? x : "
        "         (() => { try { return JSON.stringify(x); }"
        "                  catch (e) { return String(x); } })()"
        "  ).join(' '));"
        "  globalThis.console = { log: mk(0), info: mk(0), debug: mk(0),"
        "                         warn: mk(1), error: mk(2) };"
        "})();";
    JSValue r = JS_Eval(ctx, kConsoleShim, std::strlen(kConsoleShim),
                        "<console-shim>", JS_EVAL_TYPE_GLOBAL);
    JS_FreeValue(ctx, r);
}

TestHarness::~TestHarness() {
    if (impl_ && impl_->qrt) {
        TJS_FreeRuntime(impl_->qrt);
        impl_->qrt = nullptr;
        impl_->ctx = nullptr;
    }
    g_activeImpl = nullptr;
}

int TestHarness::runFile(const std::string& jsPath) {
    std::printf("TAP version 13\n");
    std::fflush(stdout);

    std::string code;
    try {
        code = slurpText(jsPath);
    } catch (const std::exception& e) {
        std::printf("Bail out! %s\n", e.what());
        std::fflush(stdout);
        return 1;
    }

    JSContext* ctx = impl_->ctx;
    // is_main=true fires the synthetic window 'load' event the runner hooks.
    JSValue res = TJS_EvalModuleContent(ctx, jsPath.c_str(), /*is_main*/ true,
                                        /*use_realpath*/ false, code.data(),
                                        code.size());
    const bool threw = JS_IsException(res);
    if (threw) tjs_dump_error(ctx);
    JS_FreeValue(ctx, res);

    // Drain any async work the tests scheduled (timers / promises). v1 tests
    // are synchronous, so a bounded pump is sufficient.
    for (int i = 0; i < 64; ++i) {
        uv_run(TJS_GetLoop(impl_->qrt), UV_RUN_NOWAIT);
        tjs__execute_jobs(ctx);
    }

    if (threw) {
        std::printf("Bail out! test module evaluation failed\n");
        std::fflush(stdout);
        return 1;
    }

    impl_->done();  // ensure a plan line even if the test forgot
    return impl_->failures > 0 ? 1 : 0;
}
