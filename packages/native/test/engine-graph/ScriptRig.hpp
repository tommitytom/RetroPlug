#pragma once

// A GraphHost carrying the core and host packages, both test packages (the graph tests' sources and sinks, and the
// engine tests' feed and probe) and a ScriptHost's script types. The script host is loaded with the test bundle
// (packages/retroplug/test/script/scriptTestBundle.ts: the authoring layer as a global `rp`, and a node of its own)
// and then with toy nodes the test writes as JS source, compiled by the control plane's compiler, so no toy ships.
//
// Two probes are bound into both of the script host's contexts, so a toy can say what ran where: `__probe(tag)`
// records the tag and the calling thread, and `__token(tag)` returns an object that records "released <tag>" and the
// thread that frees it, which is the thread that released the instance holding it.

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

extern "C" {
#include "quickjs.h"
}

#include "../graph/TestNodes.hpp"
#include "TestNodes.hpp"
#include "graph/GraphHost.hpp"
#include "graph/HostNodes.hpp"
#include "graph/nodes/CoreNodes.hpp"
#include "host/dsp/ScriptCompiler.hpp"
#include "host/script/ScriptHost.hpp"

extern "C" {
extern const std::uint8_t  rp_script_test_bundle[];
extern const std::uint32_t rp_script_test_bundle_size;
}

namespace rp::host::test {

inline ScriptHost::Chunk scriptTestBundle() { return {rp_script_test_bundle, rp_script_test_bundle_size}; }

// A toy bundle's source as bytecode, as the control plane compiles a script.
inline std::vector<std::uint8_t> compileToys(const std::string& source) {
    auto bytecode = dsp::compileToBytecode(source);
    if (!bytecode) throw std::runtime_error("the toy source does not compile");
    return *bytecode;
}

// What the probes recorded, in order.
struct ProbeLog {
    std::mutex                                           mutex;
    std::vector<std::pair<std::string, std::thread::id>> calls;
    JSClassID                                            tokenClass[2] = {0, 0};  // control side, audio side

    void add(std::string tag) {
        const std::lock_guard<std::mutex> lock(mutex);
        calls.emplace_back(std::move(tag), std::this_thread::get_id());
    }
    std::vector<std::thread::id> threadsOf(const std::string& tag) {
        const std::lock_guard<std::mutex> lock(mutex);
        std::vector<std::thread::id> out;
        for (const auto& [t, id] : calls)
            if (t == tag) out.push_back(id);
        return out;
    }
    int count(const std::string& tag) { return static_cast<int>(threadsOf(tag).size()); }
    // Until `tag` has been recorded `n` times, for at most a few seconds (the block thread is someone else).
    bool waitFor(const std::string& tag, int n = 1) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (count(tag) < n)
            if (std::chrono::steady_clock::now() > deadline) return false;
            else std::this_thread::sleep_for(std::chrono::microseconds(200));
        return true;
    }
};

struct Token {
    ProbeLog*   log;
    std::string tag;
};

inline ProbeLog* logOf(JSContext* ctx, JSValueConst data) {
    std::int64_t p = 0;
    JS_ToInt64(ctx, &p, data);
    return reinterpret_cast<ProbeLog*>(static_cast<std::intptr_t>(p));
}

inline std::string tagOf(JSContext* ctx, int argc, JSValueConst* argv) {
    const char* s = argc > 0 ? JS_ToCString(ctx, argv[0]) : nullptr;
    std::string tag = s ? s : "";
    if (s) JS_FreeCString(ctx, s);
    return tag;
}

inline JSValue probeFn(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv, int, JSValueConst* data) {
    logOf(ctx, data[0])->add(tagOf(ctx, argc, argv));
    return JS_UNDEFINED;
}

inline JSValue tokenFn(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv, int audioSide, JSValueConst* data) {
    ProbeLog* log = logOf(ctx, data[0]);
    JSValue token = JS_NewObjectClass(ctx, log->tokenClass[audioSide]);
    JS_SetOpaque(token, new Token{log, tagOf(ctx, argc, argv)});
    return token;
}

inline void tokenFinalizer(JSRuntime*, JSValueConst value) {
    JSClassID id = 0;
    auto* t = static_cast<Token*>(JS_GetAnyOpaque(value, &id));
    if (!t) return;
    t->log->add("released " + t->tag);
    delete t;
}

// ScriptHost::Binder.
inline void bindProbes(void* user, JSContext* ctx, bool audioSide) {
    auto* log = static_cast<ProbeLog*>(user);
    JSClassID& id = log->tokenClass[audioSide ? 1 : 0];
    JS_NewClassID(JS_GetRuntime(ctx), &id);
    JSClassDef def{};
    def.class_name = "Token";
    def.finalizer = &tokenFinalizer;
    JS_NewClass(JS_GetRuntime(ctx), id, &def);
    JSValue data = JS_NewInt64(ctx, static_cast<std::int64_t>(reinterpret_cast<std::intptr_t>(log)));
    JSValue global = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, global, "__probe", JS_NewCFunctionData(ctx, &probeFn, 1, 0, 1, &data));
    JS_SetPropertyStr(ctx, global, "__token", JS_NewCFunctionData(ctx, &tokenFn, 1, audioSide ? 1 : 0, 1, &data));
    JS_FreeValue(ctx, global);
    JS_FreeValue(ctx, data);
}

struct ScriptRig {
    ProbeLog                              log;
    // Before the graph, so it is destroyed after it: the graph's shims hand their instances back to it.
    ScriptHost                            scripts{&bindProbes, &log};
    graph::NodeRegistry                   registry;
    graph::ResourceTable                  resources;
    std::unique_ptr<graph::GraphHost>     host;
    std::vector<std::vector<float>>       outs;
    std::vector<float*>                   outPointers;
    rp_transport                          transport{48000.0, 120.0, 0.0, 0};

    // The test bundle, then `toys` (JS source; empty for none).
    explicit ScriptRig(const std::string& toys = "", std::uint32_t maxFrames = 256) {
        std::vector<std::uint8_t> toyBytecode;
        std::vector<ScriptHost::Chunk> chunks{scriptTestBundle()};
        if (!toys.empty()) {
            toyBytecode = compileToys(toys);
            chunks.emplace_back(toyBytecode);
        }
        std::string error;
        if (!scripts.load(chunks, &error)) throw std::runtime_error("the toy bundle does not load: " + error);
        for (const rp_package* p : {graph::corePackage(), graph::hostPackage(), graph::test::testPackage(),
                                    test::testPackage(), scripts.package()})
            if (!registry.addPackage(p, &error)) throw std::runtime_error("a package is refused: " + error);
        host = std::make_unique<graph::GraphHost>(registry, resources, graph::HostConfig{48000.0, maxFrames});
        outs.assign(8, std::vector<float>(maxFrames, 0.0f));
        for (auto& o : outs) outPointers.push_back(o.data());
    }

    ~ScriptRig() {
        setAudioThreadOwns(false);
        host.reset();
    }

    graph::SetGraphResult set(const std::string& json) { return host->setGraph(json); }

    // One block, as an Engine runs it: the script host's releases, the graph's drain and block, then the promise jobs
    // the block queued.
    void block(std::uint32_t frames = 64) {
        scripts.beginBlock();
        host->processBlock(frames, transport, outPointers.data(), static_cast<std::uint32_t>(outPointers.size()));
        scripts.endBlock();
    }
    void run(std::uint32_t frames = 64, int blocks = 1) {
        for (int i = 0; i < blocks; ++i) block(frames);
    }

    // True before an audio thread starts running blocks, false after it has stopped, as the Engine does.
    void setAudioThreadOwns(bool owns) {
        if (owns) scripts.setAudioThreadOwns(true);
        if (host) host->setAudioThreadOwns(owns);
        if (!owns) scripts.setAudioThreadOwns(false);
    }

    // The control thread's housekeeping: plans the block thread handed back, then the instance records.
    void reclaim() {
        host->reclaim();
        scripts.reclaim();
    }

    template <class T>
    T& node(const std::string& id) {
        T* t = rp::peer_as<T>(host->node(id));
        if (!t) throw std::runtime_error("no node " + id + " of that type");
        return *t;
    }

    static std::string codes(const graph::SetGraphResult& r) {
        std::string s;
        for (const auto& e : r.errors) s += (s.empty() ? "" : ",") + e.code;
        return s;
    }
};

}  // namespace rp::host::test
