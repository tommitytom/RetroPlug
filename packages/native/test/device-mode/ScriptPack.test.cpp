// Device script packs on the MicroQuickJS host (graph-runtime's script/MqjsHost.hpp,
// docs/modular-graph-phase-e4-design.md sections 4, 5 and 7), both its builds: the desktop's, which plans, and the
// device's (RP_DEVICE, renamed in the device-mode objects), which a device runs a plan's script nodes with. The packs
// are devtest's (packages/devtest/ts) and the test's own (script-packs/: scripttest and hog), each compiled by the
// device-pack toolchain to bytecode, and each node is made and run through the C ABI as a host makes it, under a host
// of the case's own (its package memory, a clock, a console sink).
//
// - A pack registers on both builds: one rp_package with memory of its own and a type per definition, described on
//   the desktop's build alone, where every C++ package has no package memory.
// - The block API (frames, transport, a control input and a param, events and bytes in and out, emit) and what a block
//   refuses, the same on both builds; settings reach create as the definition parsed them, from JSON or msgpack; the
//   hooks run when the host says.
// - Each fault: a throw, a deadline overrun, a promise, "fault", no process, a create that throws; and a heap that runs
//   out silencing every node of its pack and no other pack's.
// - A warmed block allocates nothing: not the C allocator, not the JS heap.
// - A plan with a script node, exported and loaded over the device-mode builds in an arena, renders what GraphHost
//   renders from the same document, and the loader sizes, places and sets up the pack's memory, refusing a plan that
//   cannot hold it, a pack that cannot be sized and one that will not set up, each in its words.
// - The least context the host makes is one MicroQuickJS sets itself up in without writing past it.
// A hidden case ([.script-bench]) measures a script node's cost a message, and a pack's heap use.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <ctime>
#endif

#include "DeviceModeEntries.hpp"
#include "graph/CountingAllocator.hpp"
#include "graph/Executor.hpp"
#include "graph/GraphHost.hpp"
#include "graph/JsonToMsgpack.hpp"
#include "graph/NodeRegistry.hpp"
#include "graph/PackageMemory.hpp"
#include "graph/PlanExport.hpp"
#include "graph/PlanLoader.hpp"
#include "graph/Queue.hpp"
#include "graph/ResourceTable.hpp"
#include "graph/node_desc.hpp"
#include "graph/rp_device.h"
#include "script/MqjsHost.hpp"
#include "script/rp_mqjs.h"

extern "C" {
// The packs' registrations: the desktop's build, and the device's (the device-mode objects, renamed).
const rp_package* rp_devtest_script_package(void);
const rp_package* rp_devtest_script_package_device_mode(void);
const rp_package* rp_scripttest_script_package(void);
const rp_package* rp_scripttest_script_package_device_mode(void);
const rp_package* rp_hog_script_package(void);
const rp_package* rp_hog_script_package_device_mode(void);
const rp_package* rp_devtest_plan_package(void);
const rp_package* rp_core_package(void);
const rp_package* rp_dsp_package(void);
const rp_package* rp_n8_package(void);
// The bytecode the toolchain compiled for devtest and hog (their pack64.c).
extern const rp_mqjs_pack_source rp_script_devtest;
extern const rp_mqjs_pack_source rp_script_hog;
extern const rp_mqjs_pack_source rp_script_bliptoaster;
extern const rp_mqjs_pack_source rp_script_scripttest;
}

// Every device entry with both its builds (the generated device-entries.cpp).
std::vector<DeviceEntry> deviceEntries();

namespace {

using namespace rp::graph;

// ---- a host and a node, by hand ---------------------------------------------------------------------------------

// The calling thread's CPU time in microseconds, as the desktop's own deadline clock reads it: what the cases' hosts
// offer as their clock, so a preempted thread on a loaded machine (a parallel or sanitizer run) costs a node nothing.
std::uint64_t cpuMicros(void*) {
#if defined(_WIN32)
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!::GetThreadTimes(::GetCurrentThread(), &created, &exited, &kernel, &user)) return 0;
    const auto us = [](const FILETIME& f) {
        return ((static_cast<std::uint64_t>(f.dwHighDateTime) << 32) | f.dwLowDateTime) / 10;
    };
    return us(kernel) + us(user);
#else
    timespec t{};
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t) != 0) return 0;
    return static_cast<std::uint64_t>(t.tv_sec) * 1000000 + static_cast<std::uint64_t>(t.tv_nsec) / 1000;
#endif
}
void ignoreLog(void*, int, const char*) {}

// A host for script nodes made by hand: package memory of its own (rp.host.packages/1, set up at a node's first ask
// and torn down with the host, after its nodes), a CPU-time clock whose deadline the case may set (rp.host.clock/1;
// the desktop's own figure unless it does), and console.log collected (rp.host.script-log/1). Declare it before the
// nodes made under it.
struct Host {
    PackageMemory      packages;
    std::string        log;
    rp_host_clock      clock{this, &cpuMicros, rp::mqjs::kDesktopDeadlineMicros};
    rp_host_script_log sink{this, &Host::write};
    rp_host            host{RP_ABI_VERSION, this, nullptr, nullptr, &ignoreLog, &Host::interfaceOf};

    explicit Host(std::uint32_t deadlineMicros = rp::mqjs::kDesktopDeadlineMicros) {
        clock.deadline_micros = deadlineMicros;
    }
    static void write(void* ctx, const char* text, std::size_t size) {
        static_cast<Host*>(ctx)->log.append(text, size);
    }
    static const void* interfaceOf(const rp_host* h, const char* id) {
        auto* self = static_cast<Host*>(h->ctx);
        if (std::strcmp(id, RP_HOST_PACKAGES_INTERFACE) == 0) return self->packages.service();
        if (std::strcmp(id, RP_HOST_CLOCK_INTERFACE) == 0) return &self->clock;
        if (std::strcmp(id, RP_HOST_SCRIPT_LOG_INTERFACE) == 0) return &self->sink;
        return nullptr;
    }
};

const rp_node_type* typeOf(const rp_package* p, std::string_view id) {
    for (std::uint32_t i = 0; i < p->node_type_count; ++i)
        if (id == p->node_types[i]->id) return p->node_types[i];
    return nullptr;
}

std::vector<std::uint8_t> msgpackOf(const char* json) {
    auto bytes = settingsToMsgpack(json);
    REQUIRE(bytes);
    return *bytes;
}

// A message as a queue held it.
struct Sent {
    std::uint32_t             frame = 0;
    std::uint32_t             flags = 0;
    std::vector<std::uint8_t> bytes;
    bool operator==(const Sent&) const = default;
};

// What a port of a node made by hand is: its kind and whether it is an output.
struct PortSpec {
    rp_port_kind kind;
    bool         out;
};

// One node made by hand, as a host makes it: its memory, its create from `settings` (msgpack), activate, then the
// connected outputs (all of them unless `connected` says otherwise), and a block at a time over queues and streams of
// its own, in the order `ports` lists them (the layout's: inputs, then outputs), with `params` streams at the defaults
// `starts` gives.
struct Made {
    struct Queue {
        std::vector<rp_message>   items;
        std::vector<std::uint8_t> bytes;
        rp_queue                  q;
        explicit Queue(std::uint32_t capacity)
            : items(capacity), bytes(4096), q{items.data(), 0, capacity, bytes.data(), 0, 4096, 0} {}
    };
    struct Stream {
        std::vector<rp_control_change> changes = std::vector<rp_control_change>(64);
        rp_control_queue               q{0.0, changes.data(), 0, 64, 0};
    };

    const rp_node_type*                  type = nullptr;
    std::vector<std::max_align_t>        memory;
    rp_node*                             node = nullptr;
    std::string                          error;
    std::vector<std::unique_ptr<Queue>>  queues;
    std::vector<std::unique_ptr<Stream>> streams;
    std::vector<rp_port_view>            inputs, outputs;
    std::vector<rp_control_queue*>       params;
    rp_transport                         transport{48000.0, 120.0, 0.0, 0};

    Made(const rp_node_type* t, Host& host, const std::vector<std::uint8_t>& settings, std::vector<PortSpec> ports,
         std::vector<double> starts = {}, std::vector<std::uint8_t> connected = {},
         std::uint32_t encoding = RP_SETTINGS_MSGPACK, std::uint32_t capacity = 256)
        : type(t) {
        REQUIRE(type);
        const rp_settings s{reinterpret_cast<const char*>(settings.data()), settings.size(), encoding};
        rp_memory_req req{};
        rp_error err{};
        REQUIRE(type->query_memory(type, &s, &req, &err));
        memory.resize(req.size / sizeof(std::max_align_t) + 1);
        node = type->create(type, &host.host, memory.data(), &s, &err);
        error = err.message;
        if (!node) return;
        REQUIRE(node->vt->activate(node, 48000.0, 256, &err));
        for (const PortSpec& p : ports) {
            rp_port_view v{};
            v.kind = static_cast<std::uint32_t>(p.kind);
            if (p.kind == RP_PORT_CONTROL) {
                streams.push_back(std::make_unique<Stream>());
                v.u.control = &streams.back()->q;
            } else {
                queues.push_back(std::make_unique<Queue>(capacity));
                v.u.queue = &queues.back()->q;
            }
            (p.out ? outputs : inputs).push_back(v);
        }
        for (double start : starts) {
            streams.push_back(std::make_unique<Stream>());
            streams.back()->q.start = start;
            params.push_back(&streams.back()->q);
        }
        if (connected.empty()) connected.assign(outputs.size(), 1);
        node->vt->ports_changed(node, connected.data(), static_cast<std::uint32_t>(connected.size()));
    }
    ~Made() {
        if (node) type->destroy(node);
    }
    Made(const Made&) = delete;
    Made& operator=(const Made&) = delete;

    rp_queue&         in(std::size_t i) { return *inputs[i].u.queue; }
    rp_control_queue& control(std::size_t i) { return *inputs[i].u.control; }
    rp_queue&         out(std::size_t i) { return *outputs[i].u.queue; }

    // One block: every output emptied (and a control output rolled) first, as an executor does. Its rp_process is the
    // node's plan step, the same object every block as an executor's is; `plan` picks one of two, as if a re-plan had
    // made the node's step anew.
    rp_process steps[2]{};
    rp_status run(std::uint32_t frames = 256, int plan = 0) {
        for (rp_port_view& v : outputs) {
            if (v.kind == RP_PORT_CONTROL) {
                rollControl(*v.u.control);
            } else {
                clearQueue(*v.u.queue);
            }
        }
        steps[plan] = rp_process{frames,          &transport,
                                 inputs.data(),   static_cast<std::uint32_t>(inputs.size()),
                                 outputs.data(),  static_cast<std::uint32_t>(outputs.size()),
                                 params.data(),   static_cast<std::uint32_t>(params.size())};
        return node->vt->process(node, &steps[plan]);
    }

    std::vector<Sent> sent(std::size_t output) {
        std::vector<Sent> out;
        const rp_queue& q = *outputs[output].u.queue;
        for (std::uint32_t i = 0; i < q.count; ++i) {
            const auto data = messageData(q, q.items[i]);
            out.push_back({q.items[i].frame, q.items[i].flags, {data.begin(), data.end()}});
        }
        return out;
    }

    std::string fault() const {
        const auto* f = static_cast<const rp_script_fault*>(node->vt->get_interface(node, RP_SCRIPT_FAULT_INTERFACE));
        const char* why = f ? f->reason(node) : nullptr;
        return why ? why : "";
    }
    std::uint32_t faultKind() const {
        const auto* f = static_cast<const rp_script_fault*>(node->vt->get_interface(node, RP_SCRIPT_FAULT_INTERFACE));
        return f ? f->kind(node) : 0;
    }
    rp_script_heap_use heap(bool collect) const {
        const auto* h = static_cast<const rp_script_heap*>(node->vt->get_interface(node, RP_SCRIPT_HEAP_INTERFACE));
        rp_script_heap_use use{};
        if (h) h->use(node, collect, &use);
        return use;
    }
};

// The two builds of each pack.
struct Build {
    const char*       name;
    const rp_package* devtest;
    const rp_package* scripttest;
    const rp_package* hog;
};
std::vector<Build> builds() {
    std::vector<Build> out = {
        {"desktop", rp_devtest_script_package(), rp_scripttest_script_package(), rp_hog_script_package()},
        {"device", rp_devtest_script_package_device_mode(), rp_scripttest_script_package_device_mode(),
         rp_hog_script_package_device_mode()}};
    for (const Build& b : out) {
        INFO(b.name);
        REQUIRE(b.devtest);
        REQUIRE(b.scripttest);
        REQUIRE(b.hog);
    }
    return out;
}

void push(rp_queue& q, std::uint32_t frame, std::vector<std::uint8_t> bytes, std::uint32_t flags = 0) {
    REQUIRE(pushMessage(q, frame, bytes, flags));
}

const std::vector<PortSpec> kTransposePorts = {{RP_PORT_EVENTS, false}, {RP_PORT_EVENTS, true}};

std::string describe(const std::vector<Sent>& sent) {
    std::string out;
    char        buf[16];
    for (const Sent& s : sent) {
        std::snprintf(buf, sizeof buf, "%u/%u:", s.frame, s.flags);
        out += buf;
        for (std::uint8_t b : s.bytes) {
            std::snprintf(buf, sizeof buf, " %u", b);
            out += buf;
        }
        out += "\n";
    }
    return out;
}

}  // namespace

// ---- registration ---------------------------------------------------------------------------------------------------

TEST_CASE("a script pack registers on both builds of the host, with memory of its own", "[device-mode][script-pack]") {
    const rp_package* desktop = rp_devtest_script_package();
    const rp_package* device = rp_devtest_script_package_device_mode();
    REQUIRE(desktop);
    REQUIRE(device);
    for (const rp_package* p : {desktop, device}) {
        CHECK(p->abi_version == RP_ABI_VERSION);
        CHECK(std::string(p->manifest_json) == R"({"id":"rp.devtest.script","version":"1.0.0"})");
        REQUIRE(p->node_type_count == 2);
        CHECK(std::string(p->node_types[0]->id) == "rp.devtest.script-transpose");
        CHECK(std::string(p->node_types[1]->id) == "rp.devtest.script-nest");
        REQUIRE(p->memory);
        CHECK(std::string(p->memory->name) == "devtest");
        rp_memory_req req{};
        rp_error err{};
        REQUIRE(p->memory->query(p, &req, &err));
        // The pack's heap (the box's figure, kDeviceHalves halves of it on a device build of this width), its
        // context's own copy of the runtime's bytecode, and the host's state; the desktop's context is
        // desktopContextBytes().
        const std::size_t heap = std::size_t{rp_script_devtest.heap_bytes} * rp::mqjs::kDeviceHalves / 2;
        const std::size_t extra = p == desktop ? rp::mqjs::desktopContextBytes(rp_script_devtest) - heap : 0;
        CHECK(req.size == rp::mqjs::packageBytes(rp_script_devtest) + extra);
        CHECK(req.size > rp_script_devtest.heap_bytes + rp_script_devtest.bytecode_size);
        CHECK(req.align <= 8);
    }
    // Asked twice, the same package.
    CHECK(rp_devtest_script_package() == desktop);
    // The desktop's build describes its types, and a registry takes it; the device's describes nothing.
    const rp_node_type* d = desktop->node_types[0];
    REQUIRE(d->descriptor);
    REQUIRE(d->layout_for);
    CHECK(std::string(d->descriptor(d)).find(R"("id":"rp.devtest.script-transpose")") != std::string::npos);
    NodeRegistry registry;
    std::string  why;
    CHECK(registry.addPackage(desktop, &why));
    INFO(why);
    REQUIRE(registry.find("rp.devtest.script-transpose"));
    CHECK(registry.find("rp.devtest.script-transpose")->descriptor.layout.ports.size() == 2);
    CHECK_FALSE(device->node_types[0]->descriptor);
    CHECK_FALSE(device->node_types[0]->layout_for);
}

TEST_CASE("every C++ package has no package memory, on either build", "[device-mode][script-pack]") {
    for (const DeviceEntry& e : deviceEntries()) {
        INFO(e.package << ": " << e.entry);
        CHECK_FALSE(e.desktop()->memory);
        CHECK_FALSE(e.desktop()->ctx);
        CHECK_FALSE(e.device()->memory);
        CHECK_FALSE(e.device()->ctx);
    }
    for (const rp_package* p : {rp_core_package(), rp_dsp_package(), rp_n8_package(), rp_devtest_plan_package()})
        CHECK_FALSE(p->memory);
}

// ---- the block API ------------------------------------------------------------------------------------------------

TEST_CASE("send and forward take data that is no Uint8Array: an array-like, objects with a valueOf, a Float64Array",
          "[device-mode][script-pack]") {
    for (const Build& b : builds()) {
        INFO(b.name);
        Host host;
        Made n(typeOf(b.scripttest, "rp.scripttest.sends"), host, {}, {{RP_PORT_EVENTS, true}});
        REQUIRE(n.node);
        for (int block = 0; block < 4; ++block) {
            REQUIRE(n.run() == RP_OK);
            CHECK(n.sent(0) == std::vector<Sent>{{0, 0, {0x90, 60, 100}},
                                                 {1, 0, {0x80, 61, 0}},
                                                 {2, 0, {144, 62, 100}},
                                                 {3, 1, {0xC0, 5}}});
        }
        CHECK(n.fault().empty());
    }
}

TEST_CASE("a node the host skips when idle runs only on a block that brings it something, and forward sends a message "
          "as send would",
          "[device-mode][script-pack]") {
    // in, level; out, made, calls; the param p.
    const std::vector<PortSpec> ports = {{RP_PORT_EVENTS, false}, {RP_PORT_CONTROL, false}, {RP_PORT_EVENTS, true},
                                         {RP_PORT_EVENTS, true},  {RP_PORT_EVENTS, true}};
    for (const Build& b : builds()) {
        INFO(b.name);
        Host host;
        Made n(typeOf(b.scripttest, "rp.scripttest.skipper"), host, {}, ports, {0.0});
        REQUIRE(n.node);
        // What a block's call said its count was, or 0 when it was not called.
        auto calls = [&]() -> int {
            const std::vector<Sent> sent = n.sent(2);
            return sent.empty() ? 0 : sent[0].bytes.at(0);
        };
        // Its first block runs, though it brings nothing; then such a block is not run at all.
        REQUIRE(n.run() == RP_OK);
        CHECK(calls() == 1);
        for (int i = 0; i < 3; ++i) {
            REQUIRE(n.run() == RP_OK);
            CHECK(calls() == 0);
        }
        // A message runs it. Each goes on as it came, but for the velocity the node wrote into a note-on's data before
        // forwarding it; and an object the node made goes on as send would send its fields.
        push(n.in(0), 5, {0x90, 60, 100}, 1);
        push(n.in(0), 9, {0xF8});
        push(n.in(0), 9, {});
        REQUIRE(n.run() == RP_OK);
        CHECK(calls() == 2);
        CHECK(n.sent(0) == std::vector<Sent>{{5, 1, {0x90, 60, 1}}, {9, 0, {0xF8}}, {9, 0, {}}});
        CHECK(n.sent(1) == std::vector<Sent>{{6, 1, {0x90}}, {10, 1, {0xF8}}, {10, 1, {0}}});
        clearQueue(n.in(0));
        REQUIRE(n.run() == RP_OK);
        CHECK(calls() == 0);
        // A change on a control input runs it, and one on a param.
        REQUIRE(pushControl(n.control(1), 10, 0.5));
        REQUIRE(n.run() == RP_OK);
        CHECK(calls() == 3);
        n.control(1).count = 0;
        REQUIRE(pushControl(*n.params[0], 0, 0.25));
        REQUIRE(n.run() == RP_OK);
        CHECK(calls() == 4);
        n.params[0]->count = 0;
        REQUIRE(n.run() == RP_OK);
        CHECK(calls() == 0);
        // A re-activation's hook waits for its next call, which an idle block then makes.
        rp_error err{};
        REQUIRE(n.node->vt->activate(n.node, 44100.0, 256, &err));
        REQUIRE(n.run() == RP_OK);
        CHECK(calls() == 5);
        REQUIRE(n.run() == RP_OK);
        CHECK(calls() == 0);
        // A new plan's step (another rp_process) whose level starts elsewhere with no change, as a rewired or
        // disconnected input does: its first block there runs, and reads the start; the next is skipped again.
        n.control(1).start = 0.4;
        REQUIRE(n.run(256, 1) == RP_OK);
        REQUIRE(n.sent(2).size() == 1);
        CHECK(n.sent(2)[0].bytes == std::vector<std::uint8_t>{6, 40});
        REQUIRE(n.run(256, 1) == RP_OK);
        CHECK(calls() == 0);
        // On the desktop, a settings swap (the same layout, so the instance is kept) waits for the next call, which an
        // idle block then makes.
        if (b.name == std::string("desktop")) {
            const rp_settings none{nullptr, 0, RP_SETTINGS_JSON};
            void*             prepared = nullptr;
            void*             old = nullptr;
            REQUIRE(n.node->vt->prepare_settings(n.node, &none, &prepared, &err) == RP_PREPARE_SWAP);
            n.node->vt->commit_settings(n.node, prepared, &old);
            n.node->vt->release_settings(n.node, old);
            REQUIRE(n.run(256, 1) == RP_OK);
            CHECK(calls() == 7);
            REQUIRE(n.run(256, 1) == RP_OK);
            CHECK(calls() == 0);
        }
        CHECK(n.fault().empty());
    }
}

TEST_CASE("a script node transposes through the block API, the same on both builds", "[device-mode][script-pack]") {
    std::vector<std::vector<Sent>> runs;
    for (const Build& b : builds()) {
        INFO(b.name);
        Host host;
        Made n(typeOf(b.devtest, "rp.devtest.script-transpose"), host, msgpackOf(R"({"semitones":5})"),
               kTransposePorts);
        REQUIRE(n.node);
        std::vector<Sent> all;
        for (int block = 0; block < 3; ++block) {
            clearQueue(n.in(0));
            push(n.in(0), 0, {0x90, 60, 100});
            push(n.in(0), 3, {0x80, static_cast<std::uint8_t>(124 + block), 0}, 1);  // past 127 by 2 in the last block
            push(n.in(0), 3, {0xB0, 7, 64});
            push(n.in(0), 9, {0xC0, 5});
            push(n.in(0), 40, {0xF0, 0x7D, 1, 2, 0xF7}, 1);
            REQUIRE(n.run() == RP_OK);
            const std::vector<Sent> sent = n.sent(0);
            INFO(describe(sent));
            const std::vector<Sent> expected = {
                {0, 0, {0x90, 65, 100}},
                {3, 1, {0x80, static_cast<std::uint8_t>(std::min(129 + block, 127)), 0}},
                {3, 0, {0xB0, 7, 64}},
                {9, 0, {0xC0, 5}},
                {40, 1, {0xF0, 0x7D, 1, 2, 0xF7}},
            };
            CHECK(sent == expected);
            all.insert(all.end(), sent.begin(), sent.end());
        }
        CHECK(n.fault().empty());
        runs.push_back(all);
    }
    CHECK(runs[0] == runs[1]);
}

TEST_CASE("the block API: frames, transport, control and params, events and bytes, and what a block refuses",
          "[device-mode][script-pack]") {
    // The probe's layout: ctl, ev and raw in; out, bout and level out; the gain param.
    const std::vector<PortSpec> ports = {{RP_PORT_CONTROL, false}, {RP_PORT_EVENTS, false}, {RP_PORT_BYTES, false},
                                         {RP_PORT_EVENTS, true},   {RP_PORT_BYTES, true},   {RP_PORT_CONTROL, true}};
    std::vector<std::string> transcripts;
    for (const Build& b : builds()) {
        INFO(b.name);
        Host host;
        Made n(typeOf(b.scripttest, "rp.scripttest.probe"), host, {}, ports, {0.5});
        REQUIRE(n.node);
        n.transport = rp_transport{48000.0, 120.0, 2.5, RP_TRANSPORT_PLAYING};
        n.control(0).start = 0.25;
        REQUIRE(pushControl(n.control(0), 10, 0.5));
        REQUIRE(pushControl(n.control(0), 20, 1.0));
        REQUIRE(pushControl(*n.params[0], 3, 0.75));
        push(n.in(1), 1, {0x90, 60, 100});
        push(n.in(1), 5, {0xB0, 7, 64}, 0);
        push(n.in(2), 2, {0xF0, 1, 2, 0xF7}, RP_MESSAGE_FLUSH);
        REQUIRE(n.run(300) == RP_OK);
        INFO(n.fault());
        const std::vector<Sent> out = n.sent(0);
        INFO(describe(out));
        const std::vector<Sent> expected = {
            // frames 300 (44, 1), playing, tempo 120, ppq 2.5 * 4, 48 kHz, gain 0.5 at its start, one gain change
            {0, 0, {44, 1, 1, 120, 10, 48, 50, 1}},
            {1, 0, {0x90, 60, 100}},
            {5, 0, {0xB0, 7, 64}},
            // A port it does not have, an input named as an output and an audio port: three TypeErrors.
            {299, 0, {0xe0, 1, 2, 3}},
        };
        CHECK(out == expected);
        const std::vector<Sent> bytesOut = n.sent(1);
        CHECK(bytesOut == std::vector<Sent>{{2, RP_MESSAGE_FLUSH, {0xF0, 1, 2, 0xF7}}});
        const rp_control_queue& level = *n.outputs[2].u.control;
        REQUIRE(level.count == 3);
        CHECK(level.changes[0].frame == 0);
        CHECK(level.changes[0].value == 0.5);
        CHECK(level.changes[1].frame == 10);
        CHECK(level.changes[1].value == 1.0);
        CHECK(level.changes[2].frame == 20);
        CHECK(level.changes[2].value == 2.0);
        // The same objects, refilled, the next block: fewer messages, no changes.
        clearQueue(n.in(1));
        clearQueue(n.in(2));
        n.control(0).count = 0;
        n.params[0]->count = 0;
        push(n.in(1), 7, {0x80, 60, 0});
        REQUIRE(n.run(64) == RP_OK);
        const std::vector<Sent> second = n.sent(0);
        INFO(describe(second));
        REQUIRE(second.size() == 3);
        CHECK(second[0].bytes == std::vector<std::uint8_t>{64, 0, 1, 120, 10, 48, 50, 0});
        CHECK(second[1] == Sent{7, 0, {0x80, 60, 0}});
        transcripts.push_back(describe(out) + describe(second));
    }
    CHECK(transcripts[0] == transcripts[1]);
}

TEST_CASE("settings reach create as the definition parsed them, from JSON or msgpack", "[device-mode][script-pack]") {
    const rp_package* desktop = rp_scripttest_script_package();
    const rp_package* device = rp_scripttest_script_package_device_mode();
    const std::vector<PortSpec> ports = {{RP_PORT_EVENTS, true}};
    auto textOf = [](Made& n) {
        REQUIRE(n.run() == RP_OK);
        const std::vector<Sent> sent = n.sent(0);
        REQUIRE(sent.size() == 1);
        return std::string(sent[0].bytes.begin(), sent[0].bytes.end());
    };
    for (const char* json : {R"({"count":42,"name":"zz","taps":[9],"inner":{"depth":2}})",
                             R"({"count":-5,"ratio":1.5,"on":true,"mode":"fast"})", "{}"}) {
        INFO(json);
        Host host;
        // The desktop's build from JSON, as a graph document carries them: what it writes back is what it parsed.
        const std::string text(json);
        Made fromJson(typeOf(desktop, "rp.scripttest.settings"), host,
                      std::vector<std::uint8_t>(text.begin(), text.end()), ports, {}, {}, RP_SETTINGS_JSON);
        REQUIRE(fromJson.node);
        const auto* writer = static_cast<const SettingsJsonInterface*>(
            fromJson.node->vt->get_interface(fromJson.node, kSettingsJsonInterface));
        REQUIRE(writer);
        std::string parsed(512, '\0');
        parsed.resize(writer->write(fromJson.node, parsed.data(), parsed.size()));
        CHECK(textOf(fromJson) == parsed);
        // The device's build from the msgpack the exporter writes from that.
        Made fromMsgpack(typeOf(device, "rp.scripttest.settings"), host, msgpackOf(parsed.c_str()), ports);
        REQUIRE(fromMsgpack.node);
        CHECK(textOf(fromMsgpack) == parsed);
    }
    // Every default filled, and the fields in the schema's order.
    Host host;
    Made n(typeOf(desktop, "rp.scripttest.settings"), host, {}, ports);
    CHECK(textOf(n) ==
          R"({"count":7,"ratio":0.25,"on":false,"name":"abc","mode":"slow","taps":[1,2,3],"inner":{"depth":4},)"
          R"("extra":{}})");
    // Refused settings: the definition's words.
    const std::string bad = R"({"count":1e9})";
    const rp_settings s{bad.data(), bad.size(), RP_SETTINGS_JSON};
    rp_memory_req req{};
    rp_error err{};
    const rp_node_type* t = typeOf(desktop, "rp.scripttest.settings");
    CHECK_FALSE(t->query_memory(t, &s, &req, &err));
    CHECK(std::string(err.message).find("settings: count:") != std::string::npos);
    // Settings the definition takes and a device's decode would not: refused on the desktop as well, in the decode's
    // words, so both builds take the same settings and a planned node never faults on the box for its settings.
    const std::string longName = R"({"extra":{")" + std::string(64, 'k') + R"(":1}})";
    // The decode reads 8 containers deep: `fits` below is the settings object, extra's object and six arrays; `deep`
    // one array more.
    std::string deep = R"({"extra":{"k":)";
    for (int i = 0; i < 7; ++i) deep += "[";
    deep += "1";
    for (int i = 0; i < 7; ++i) deep += "]";
    deep += "}}";
    for (const std::string& text : {longName, deep}) {
        INFO(text);
        const rp_settings refused{text.data(), text.size(), RP_SETTINGS_JSON};
        CHECK_FALSE(t->query_memory(t, &refused, &req, &err));
        CHECK(std::string(err.message).find("its settings are not ones a device reads: TypeError: the settings ") == 0);
    }
    const std::string fits = R"({"extra":{")" + std::string(63, 'k') + R"(":[[[[[[1]]]]]]}})";
    const rp_settings taken{fits.data(), fits.size(), RP_SETTINGS_JSON};
    CHECK(t->query_memory(t, &taken, &req, &err));
}

TEST_CASE("portsChanged and activate run when the host says, create seeing the rate and block size",
          "[device-mode][script-pack]") {
    for (const Build& b : builds()) {
        INFO(b.name);
        Host host;
        Made n(typeOf(b.scripttest, "rp.scripttest.hooks"), host, {}, {{RP_PORT_EVENTS, true}, {RP_PORT_EVENTS, true}},
               {}, {1, 0});
        REQUIRE(n.node);
        REQUIRE(n.run() == RP_OK);
        // One portsChanged (two outputs, the first connected), no activate since create, 48 kHz and 256 frames.
        CHECK(n.sent(0) == std::vector<Sent>{{0, 0, {1, 2, 1, 0, 48, 16}}});
        rp_error err{};
        REQUIRE(n.node->vt->activate(n.node, 44100.0, 512, &err));
        const std::uint8_t both[2] = {1, 1};
        n.node->vt->ports_changed(n.node, both, 2);
        REQUIRE(n.run() == RP_OK);
        CHECK(n.sent(0) == std::vector<Sent>{{0, 0, {2, 2, 3, 1, 44, 32}}});
    }
}

TEST_CASE("the desktop's build keeps its instance through a settings change that keeps its layout",
          "[device-mode][script-pack]") {
    Host host;
    Made n(typeOf(rp_devtest_script_package(), "rp.devtest.script-transpose"), host, msgpackOf(R"({"semitones":2})"),
           kTransposePorts);
    REQUIRE(n.node);
    push(n.in(0), 0, {0x90, 60, 100});
    REQUIRE(n.run() == RP_OK);
    CHECK(n.sent(0) == std::vector<Sent>{{0, 0, {0x90, 62, 100}}});
    const std::string next = R"({"semitones":-12})";
    const rp_settings s{next.data(), next.size(), RP_SETTINGS_JSON};
    void*    prepared = nullptr;
    rp_error err{};
    REQUIRE(n.node->vt->prepare_settings(n.node, &s, &prepared, &err) == RP_PREPARE_SWAP);
    void* old = nullptr;
    n.node->vt->commit_settings(n.node, prepared, &old);
    n.node->vt->release_settings(n.node, old);
    REQUIRE(n.run() == RP_OK);
    CHECK(n.sent(0) == std::vector<Sent>{{0, 0, {0x90, 48, 100}}});
    const std::string refused = R"({"semitones":500})";
    const rp_settings r{refused.data(), refused.size(), RP_SETTINGS_JSON};
    CHECK(n.node->vt->prepare_settings(n.node, &r, &prepared, &err) == RP_PREPARE_INVALID);
    CHECK(std::string(err.message).find("semitones") != std::string::npos);
}

// ---- faults ---------------------------------------------------------------------------------------------------------

TEST_CASE("a script node faults on a throw (one the engine would drop among them), a deadline, a promise, \"fault\" "
          "and no process, and stays faulted",
          "[device-mode][script-pack]") {
    struct Case {
        const char*   id;
        std::uint32_t kind;
        const char*   words;
    };
    const std::vector<Case> cases = {
        {"rp.scripttest.throws", RP_SCRIPT_FAULT_THREW, "process threw: RangeError: the throws node threw"},
        {"rp.scripttest.spins", RP_SCRIPT_FAULT_DEADLINE, "process ran past its deadline of 50000 us"},
        {"rp.scripttest.thenable", RP_SCRIPT_FAULT_REFUSED,
         "process returned a promise or a thenable: a device pack's node runs synchronously"},
        {"rp.scripttest.fault", RP_SCRIPT_FAULT_REFUSED, "process returned \"fault\""},
        {"rp.scripttest.no-process", RP_SCRIPT_FAULT_REFUSED, "create returned no object with a process function"},
        {"rp.scripttest.create-throws", RP_SCRIPT_FAULT_THREW, "create threw: TypeError: create would not"},
        // Throws the engine itself would drop (rp_mqjs_engine.c's wrappers): a String.replace function's, and a
        // hasOwnProperty key's toString's.
        {"rp.scripttest.replace-throws", RP_SCRIPT_FAULT_THREW,
         "process threw: RangeError: the replace function threw"},
        {"rp.scripttest.has-own-throws", RP_SCRIPT_FAULT_THREW, "process threw: RangeError: the key's toString threw"},
    };
    for (const Build& b : builds()) {
        Host host(50000);  // 50 ms of the thread's CPU time: what a preempted thread does not use up
        Made probe(typeOf(b.scripttest, "rp.scripttest.probe"), host, {},
                   {{RP_PORT_CONTROL, false}, {RP_PORT_EVENTS, false}, {RP_PORT_BYTES, false},
                    {RP_PORT_EVENTS, true}, {RP_PORT_BYTES, true}, {RP_PORT_CONTROL, true}},
                   {0.5});
        REQUIRE(probe.node);
        for (const Case& c : cases) {
            INFO(b.name << ": " << c.id);
            Made n(typeOf(b.scripttest, c.id), host, {}, {{RP_PORT_EVENTS, true}});
            REQUIRE(n.node);
            const auto start = std::chrono::steady_clock::now();
            CHECK(n.run() == RP_FAULT);
            CHECK(std::chrono::steady_clock::now() - start < std::chrono::seconds(2));
            CHECK(n.faultKind() == c.kind);
            CHECK(n.fault().find(c.words) == 0);
            // Faulted, it runs nothing more: the next block answers the same at once.
            CHECK(n.run() == RP_FAULT);
            CHECK(n.fault().find(c.words) == 0);
            // A fault is the node's own: another node of the pack runs on.
            CHECK(probe.run() == RP_OK);
        }
        CHECK(probe.fault().empty());
    }
}

TEST_CASE("by a host's clock, a block's deadline grows by kHostClockMessageMicros for each message it hands the node",
          "[device-mode][script-pack]") {
    // The host's clock, once the node is made, a millisecond on at each reading: the entry's start and its restart once
    // the ports are ready are its first two, so a node held to L microseconds faults at the reading past L + 2 ms, the
    // (L / 1000 + 3)th. Before, the thread's CPU time, as the cases' hosts give it.
    struct Ticks {
        bool          live = false;
        std::uint32_t calls = 0;
        static std::uint64_t read(void* c) {
            auto& t = *static_cast<Ticks*>(c);
            if (!t.live) return cpuMicros(nullptr);
            return std::uint64_t{++t.calls} * 1000;
        }
    };
    for (const Build& b : builds()) {
        // None, then 40 on the events input and one on the bytes input: each counts.
        for (const std::uint32_t events : {0u, 40u}) {
            const std::uint32_t messages = events ? events + 1 : 0;
            INFO(b.name << ", " << messages << " messages");
            Ticks clock;
            Host  host(20000);
            host.clock.ctx = &clock;
            host.clock.micros = &Ticks::read;
            Made n(typeOf(b.scripttest, "rp.scripttest.spins-in"), host, {},
                   {{RP_PORT_EVENTS, false}, {RP_PORT_BYTES, false}, {RP_PORT_EVENTS, true}});
            REQUIRE(n.node);
            for (std::uint32_t k = 0; k < events; ++k) {
                const std::uint8_t note[3] = {0x90, static_cast<std::uint8_t>(k), 100};
                REQUIRE(pushMessage(n.in(0), k, note));
            }
            if (events) {
                const std::uint8_t raw[2] = {0xF8, 0xFA};
                REQUIRE(pushMessage(n.in(1), 0, raw));
            }
            const std::uint32_t limit = 20000 + messages * rp::mqjs::kHostClockMessageMicros;
            clock.live = true;
            CHECK(n.run() == RP_FAULT);
            CHECK(n.faultKind() == RP_SCRIPT_FAULT_DEADLINE);
            CHECK(n.fault() == "process ran past its deadline of " + std::to_string(limit) + " us");
            // The interrupt held the node to the lengthened deadline, not the fixed one: it read the clock until then.
            CHECK(clock.calls == limit / 1000 + 3);
        }
    }
}

TEST_CASE("a node reads and forwards its messages, SysEx among them, from the deepest call into JavaScript a context "
          "allows on the C stack",
          "[device-mode][script-pack]") {
    for (const Build& b : builds()) {
        INFO(b.name);
        Host host;
        Made n(typeOf(b.scripttest, "rp.scripttest.deep"), host, {},
               {{RP_PORT_EVENTS, false}, {RP_PORT_EVENTS, true}, {RP_PORT_EVENTS, true}});
        REQUIRE(n.node);
        std::vector<Sent> sent;
        for (std::uint32_t k = 0; k < 6; ++k) {
            std::vector<std::uint8_t> bytes = {0x90, static_cast<std::uint8_t>(60 + k), 100};
            if (k % 2) {  // a SysEx past the three bytes a place keeps a view of: its array is made in the refill
                bytes.assign(64, static_cast<std::uint8_t>(k));
                bytes.front() = 0xF0;
                bytes.back() = 0xF7;
            }
            REQUIRE(pushMessage(n.in(0), k * 10, bytes, k % 3 == 0 ? 1u : 0u));
            sent.push_back({k * 10, k % 3 == 0 ? 1u : 0u, bytes});
        }
        for (int block = 0; block < 2; ++block) {
            REQUIRE(n.run() == RP_OK);
            INFO(n.fault());
            CHECK(n.sent(0) == sent);
            REQUIRE(n.sent(1).size() == 1);
            // The host's own call into process is the first of the calls on the C stack, each forEach one more.
            CHECK(n.sent(1)[0].bytes == std::vector<std::uint8_t>{static_cast<std::uint8_t>(rp::mqjs::kMaxReentries)});
        }
        CHECK(n.fault().empty());
    }
}

TEST_CASE("a clock reading of 0 is no reading: the call runs on, and its deadline holds once the clock reads again",
          "[device-mode][script-pack]") {
    // The host's clock, scripted once the nodes are made, by the reading's number: `start` readings (the entry's start,
    // and its restart once the ports are ready), then `zeros` of 0 (a clock that answers nothing for a while), then
    // each `step` on from `from`.
    struct Scripted {
        bool          live = false;
        std::uint32_t calls = 0;
        std::uint64_t start = 1000, from = 1000, step = 10000;
        std::uint32_t zeros = 20;
        static std::uint64_t read(void* c) {
            auto& s = *static_cast<Scripted*>(c);
            if (!s.live) return cpuMicros(nullptr);
            const std::uint32_t k = s.calls++;
            if (k < 2) return s.start;
            if (k < 2 + s.zeros) return 0;
            return s.from + std::uint64_t{k - 1 - s.zeros} * s.step;
        }
    };
    for (const Build& b : builds()) {
        INFO(b.name);
        {
            Scripted clock;
            Host host(50000);
            host.clock.ctx = &clock;
            host.clock.micros = &Scripted::read;
            Made spins(typeOf(b.scripttest, "rp.scripttest.spins"), host, {}, {{RP_PORT_EVENTS, true}});
            REQUIRE(spins.node);
            clock.live = true;
            CHECK(spins.run() == RP_FAULT);
            CHECK(spins.faultKind() == RP_SCRIPT_FAULT_DEADLINE);
            CHECK(spins.fault() == "process ran past its deadline of 50000 us");
            // The twenty readings of 0 timed nothing; the sixth poll after them, 60 ms from the start, faulted it.
            CHECK(clock.calls == 2 + 20 + 6);
        }
        {
            // An entry whose start reads 0 runs untimed: polls that read a time far past any deadline after it fault
            // nothing, where taking the 0 as a reading would fault the node at its first poll.
            Scripted clock;
            clock.start = 0;
            clock.zeros = 0;
            clock.from = std::uint64_t{1} << 40;
            Host host(1);
            host.clock.ctx = &clock;
            host.clock.micros = &Scripted::read;
            Made busy(typeOf(b.scripttest, "rp.scripttest.busy"), host, {}, {{RP_PORT_EVENTS, true}});
            REQUIRE(busy.node);
            clock.live = true;
            REQUIRE(busy.run() == RP_OK);
            CHECK(clock.calls == 2);  // the start and the restart: an untimed entry's polls read no clock
            CHECK(busy.fault().empty());
        }
    }
}

TEST_CASE("a pack whose heap runs out silences every node of the pack, and no other pack's",
          "[device-mode][script-pack]") {
    for (const Build& b : builds()) {
        INFO(b.name);
        Host host;
        Made hog(typeOf(b.hog, "rp.hog.hog"), host, msgpackOf(R"({"grow":true})"), {{RP_PORT_EVENTS, true}});
        Made quiet(typeOf(b.hog, "rp.hog.quiet"), host, {}, {{RP_PORT_EVENTS, true}});
        Made other(typeOf(b.devtest, "rp.devtest.script-transpose"), host, msgpackOf(R"({"semitones":0})"),
                   kTransposePorts);
        REQUIRE(hog.node);
        REQUIRE(quiet.node);
        REQUIRE(other.node);
        // The quiet node's first block makes its pools, before the hog grows into the rest.
        REQUIRE(quiet.run() == RP_OK);
        // The heap runs out in whichever node's process allocates when it is full: the quiet node makes an array a
        // block too.
        int blocks = 0;
        while (blocks < 10000 && hog.run() == RP_OK && quiet.run() == RP_OK) ++blocks;
        CHECK(blocks > 0);
        CHECK(blocks < 10000);
        // Both nodes of the pack, at their next call, with the same words; the other pack's node runs on.
        CHECK(hog.run() == RP_FAULT);
        CHECK(quiet.run() == RP_FAULT);
        CHECK(hog.faultKind() == RP_SCRIPT_FAULT_HEAP);
        // The desktop's context is desktopContextBytes() (1 MiB here), a device build's kDeviceHalves halves of the
        // manifest's heapBytes.
        const std::size_t  deviceHeap = std::size_t{rp_script_hog.heap_bytes} * rp::mqjs::kDeviceHalves / 2;
        const std::string heap = std::to_string(b.name == std::string("desktop")
                                                    ? rp::mqjs::desktopContextBytes(rp_script_hog)
                                                    : deviceHeap);
        CHECK(hog.fault() == "the pack's heap of " + heap +
                                 " bytes ran out (in a node's process), so every node of the pack is silenced");
        CHECK(quiet.faultKind() == RP_SCRIPT_FAULT_HEAP);
        CHECK(quiet.fault() == hog.fault());
        push(other.in(0), 4, {0x90, 64, 1});
        CHECK(other.run() == RP_OK);
        INFO(describe(other.sent(0)) << other.fault());
        CHECK(other.sent(0) == std::vector<Sent>{{4, 0, {0x90, 64, 1}}});
        CHECK(other.fault().empty());
    }
}

// ---- a warmed block -------------------------------------------------------------------------------------------------

TEST_CASE("a script node's first block sizes its heap: no later block, a full queue's included, makes anything",
          "[device-mode][script-pack]") {
    for (const Build& b : builds()) {
        INFO(b.name);
        Host host;
        Made n(typeOf(b.devtest, "rp.devtest.script-transpose"), host, msgpackOf(R"({"semitones":3})"),
               kTransposePorts);
        REQUIRE(n.node);
        // Messages of every length up to three bytes, `count` of them, up to the queue's whole capacity.
        auto fill = [&](std::uint32_t block, std::uint32_t count) {
            clearQueue(n.in(0));
            for (std::uint32_t k = 0; k < count; ++k) {
                const std::uint32_t length = 1 + (block + k) % 3;
                const std::uint8_t status = length == 3 ? 0x90 : length == 2 ? 0xC0 : 0xF8;
                const std::uint8_t bytes[3] = {status, static_cast<std::uint8_t>((block + k) % 100), 90};
                pushMessage(n.in(0), k, std::span<const std::uint8_t>(bytes, length));
            }
        };
        fill(0, 1);
        REQUIRE(n.run() == RP_OK);
        const rp_script_heap_use first = n.heap(false);
        bool ran = true;
        std::uint32_t sentFull = 0;
        rp::test::beginCountingAllocations(rp::test::countsCAllocations());
        for (std::uint32_t block = 1; block < 320; ++block) {
            const std::uint32_t count = block == 100 || block == 300 ? 256 : block % 17;
            fill(block, count);
            ran = ran && n.run() == RP_OK;
            if (count == 256) sentFull = n.out(0).count;
        }
        const auto counted = rp::test::endCountingAllocations();
        const rp_script_heap_use after = n.heap(false);
        CHECK(ran);
        CHECK(counted.allocations == 0);
        CHECK(counted.frees == 0);
        // The heap's top where the first block left it: nothing made, so no collection either, though two blocks
        // filled the queue whole.
        CHECK(first.heap > 0);
        CHECK(after.heap == first.heap);
        CHECK(sentFull == 256);
    }
}

TEST_CASE("a warmed block of the whole API allocates nothing, natively or in the JS heap",
          "[device-mode][script-pack]") {
    // The steady node: frames, transport, a control input and its changes, a param, messages, send and emit. Whole
    // numbers throughout, so it holds at 32 bits too, where a non-integer number is boxed (script/MqjsHost.hpp).
    const std::vector<PortSpec> ports = {{RP_PORT_CONTROL, false}, {RP_PORT_EVENTS, false}, {RP_PORT_EVENTS, true},
                                         {RP_PORT_CONTROL, true}};
    for (const Build& b : builds()) {
        INFO(b.name);
        Host host;
        Made n(typeOf(b.scripttest, "rp.scripttest.steady"), host, {}, ports, {50.0});
        REQUIRE(n.node);
        n.transport = rp_transport{48000.0, 120.0, 0.0, RP_TRANSPORT_PLAYING};
        auto fill = [&](std::uint32_t block) {
            clearQueue(n.in(1));
            n.control(0).count = 0;
            n.control(0).start = static_cast<double>(block % 8);
            for (std::uint32_t k = 0; k < block % 5; ++k) {
                const std::uint8_t note[3] = {0x90, static_cast<std::uint8_t>(k), 1};
                pushMessage(n.in(1), k, note);
                pushControl(n.control(0), k * 10, static_cast<double>(k));
            }
        };
        fill(0);
        REQUIRE(n.run() == RP_OK);
        const rp_script_heap_use first = n.heap(false);
        bool ran = true;
        rp::test::beginCountingAllocations(rp::test::countsCAllocations());
        for (std::uint32_t block = 1; block < 256; ++block) {
            fill(block);
            ran = ran && n.run() == RP_OK;
        }
        const auto counted = rp::test::endCountingAllocations();
        const rp_script_heap_use after = n.heap(false);
        INFO(n.fault());
        CHECK(ran);
        CHECK(counted.allocations == 0);
        CHECK(counted.frees == 0);
        CHECK(after.heap == first.heap);
        // The last block (255: 0 messages, start 7): the summary, and the level as the stream summed.
        CHECK(n.sent(0) == std::vector<Sent>{{0, 0, {0, 1, 120, 50}}});
        REQUIRE(n.outputs[1].u.control->count == 1);
        CHECK(n.outputs[1].u.control->changes[0].value == 7.0);
    }
}

TEST_CASE("an array messages() handed out and JS emptied is grown again on a rooted slot, through collections",
          "[device-mode][script-pack]") {
    // The emptier sets its array's length to 0 every block, freeing its storage, so the host grows it again the next.
    // In the least heap it runs its blocks in, nearly every allocation collects, and the counts move from one of the
    // port's arrays to another (and every other block's input is SysEx, whose arrays are made for the block), so what
    // a collection frees lies below an array as well as above it, and the collection inside a grow moves the array.
    // Each block's output must be its input, message for message: with the grow handed a copy of the array rather than
    // its rooted slot, the engine writes the grown storage where the array was. The desktop's build runs it, in a pack
    // of the hog's bytecode whose heap the case sets, as the toolchain's runner does.
    static rp_mqjs_pack_source source = rp_script_hog;
    source.name = "hog-tight";
    const rp_package* package = rp::mqjs::exactPackage(source);
    INFO(rp::mqjs::packageRefusal());
    REQUIRE(package);
    const rp_node_type* type = typeOf(package, "rp.hog.emptier");
    REQUIRE(type);
    std::string wrong;  // a block whose output was not its input: the case fails, whatever the heap
    auto runs = [&](std::uint32_t heap, std::uint32_t blocks) {
        source.heap_bytes = heap;
        Host host;
        Made n(type, host, {}, kTransposePorts);
        REQUIRE(n.node);
        for (std::uint32_t block = 0; block < blocks; ++block) {
            clearQueue(n.in(0));
            std::vector<Sent> expected;
            const std::uint32_t count = block % 2 ? 1 + block * 7 % 32 : 1 + block * 37 % 256;
            for (std::uint32_t k = 0; k < count; ++k) {
                std::vector<std::uint8_t> bytes = {0x90, static_cast<std::uint8_t>((block + k) % 128),
                                                   static_cast<std::uint8_t>(k)};
                if (block % 2) {
                    bytes.assign(64, static_cast<std::uint8_t>((block + k) % 128));
                    bytes.front() = 0xf0;
                    bytes.back() = 0xf7;
                }
                push(n.in(0), k, bytes, k % 2);
                expected.push_back({k, k % 2, bytes});
            }
            if (n.run() != RP_OK) return false;  // the heap ran out: too small a heap for the search
            if (n.sent(0) != expected && wrong.empty())
                wrong = "heap " + std::to_string(heap) + ", block " + std::to_string(block) + ": " +
                        describe(n.sent(0));
        }
        return true;
    };
    // The least heap that runs 512 blocks (every count, both kinds), by halving, as the runner finds a pack's; then
    // long runs at it and above.
    std::uint32_t lo = rp::mqjs::kLeastContextBytes, hi = 81920;
    while (hi - lo > 8) {
        const std::uint32_t mid = (lo + (hi - lo) / 2) & ~7u;
        (runs(mid, 512) ? hi : lo) = mid;
    }
    for (const std::uint32_t margin : {0u, 64u, 512u, 4096u}) {
        INFO("margin " << margin);
        CHECK(runs(hi + margin, 4000));
    }
    INFO(wrong);
    CHECK(wrong.empty());
}

TEST_CASE("an object a pack kept past its block, read by the node that took its slot, throws and reads nothing",
          "[device-mode][script-pack]") {
    for (const Build& b : builds()) {
        INFO(b.name);
        Host host;
        {
            Made keeper(typeOf(b.scripttest, "rp.scripttest.keeper"), host, {},
                        {{RP_PORT_CONTROL, false}, {RP_PORT_CONTROL, false}, {RP_PORT_EVENTS, false},
                         {RP_PORT_EVENTS, true}});
            REQUIRE(keeper.node);
            push(keeper.in(2), 3, {0x90, 60, 1});
            REQUIRE(pushControl(keeper.control(1), 5, 1.0));
            REQUIRE(keeper.run() == RP_OK);
        }
        // The keeper destroyed, its slot released; the reader, of one output, takes it, so the kept objects name its
        // slot, and ordinals past its one port: every read but data's throws the kept-object TypeError (a read in
        // any other slot would throw the not-running one, 4). data is the message's own value, read as it was.
        Made reader(typeOf(b.scripttest, "rp.scripttest.reader"), host, {}, {{RP_PORT_EVENTS, true}});
        REQUIRE(reader.node);
        REQUIRE(reader.run() == RP_OK);
        INFO(reader.fault());
        CHECK(reader.sent(0) == std::vector<Sent>{{0, 0, {2, 1, 2, 2, 2}}});
    }
}

// ---- plans ----------------------------------------------------------------------------------------------------------

namespace {

constexpr std::uint32_t kMaxFrames = 256;
constexpr std::size_t   kFrames = 9000;
constexpr std::size_t   kArenaBytes = 256 * 1024;
const std::vector<std::uint32_t> kSplit = {1, 31, 256, 3, 200, 77, 128};
const rp_transport               kTransport{48000.0, 120.0, 0.0, 0};

// A source's messages transposed by a script node into a digest, beside its audio straight in.
const char* kDocument = R"({"nodes":[
    {"id":"src","type":"rp.devtest.source","settings":{"seed":3}},
    {"id":"tp","type":"rp.devtest.script-transpose","settings":{"semitones":7}},
    {"id":"dg","type":"rp.devtest.digest"}],
  "wires":[
    {"from":"src.out","to":"dg.in"},
    {"from":"src.events","to":"tp.in"},
    {"from":"tp.out","to":"dg.events"}]})";

std::unique_ptr<NodeRegistry> desktopRegistry() {
    auto registry = std::make_unique<NodeRegistry>();
    for (const rp_package* p : {rp_devtest_plan_package(), rp_devtest_script_package()}) {
        std::string why;
        const bool added = registry->addPackage(p, &why);
        INFO(why);
        REQUIRE(added);
    }
    return registry;
}
const std::vector<const rp_package*>& deviceTypes() {
    static const std::vector<const rp_package*> types{rp_devtest_plan_package_device_mode(),
                                                      rp_devtest_script_package_device_mode()};
    return types;
}

void* acquireNoDevice(void*, const char*, const char*) { return nullptr; }
void  releaseNoDevice(void*, void*) {}
const rp_host_devices kDevices{nullptr, &acquireNoDevice, &releaseNoDevice};
const void*           deviceInterface(const rp_host*, const char* id) {
    return std::strcmp(id, RP_HOST_DEVICES_INTERFACE) == 0 ? &kDevices : nullptr;
}
const rp_host kDeviceHost{RP_ABI_VERSION, nullptr, nullptr, nullptr, &ignoreLog, &deviceInterface};

ExportResult exported(std::size_t arena = kArenaBytes, const char* document = kDocument) {
    auto          registry = desktopRegistry();
    ExportOptions o;
    o.maxFrames = kMaxFrames;
    o.arenaBytes = arena;
    return exportPlan(document, *registry, deviceTypes(), o);
}

std::string described(const ExportResult& r) {
    std::string out;
    for (const auto& e : r.errors) out += e.code + " " + e.nodeId + ": " + e.message + "\n";
    return out;
}

struct Arena {
    std::vector<std::max_align_t> storage;
    explicit Arena(std::size_t bytes) : storage(bytes / sizeof(std::max_align_t) + 1) {}
    std::span<std::byte> span(std::size_t bytes) { return {reinterpret_cast<std::byte*>(storage.data()), bytes}; }
};

std::string refusal(const std::vector<std::uint8_t>& blob, std::size_t bytes,
                    const std::vector<const rp_package*>& types) {
    Arena         arena(bytes);
    PlanLoadError err{};
    LoadedPlan*   plan = loadPlan(blob, arena.span(bytes), types, &kDeviceHost, &err);
    if (plan) {
        unloadPlan(plan);
        return "";
    }
    return std::string(err.code) + " " + err.node + ": " + err.message;
}

}  // namespace

TEST_CASE("a plan with a script node, exported and loaded over the device-mode builds, renders what GraphHost renders",
          "[device-mode][script-pack][plan]") {
    // The desktop's render: GraphHost with the desktop's builds, its own package memory.
    rp_devtest_digest desktop{};
    std::uint32_t     desktopFaults = 0;
    {
        auto          registry = desktopRegistry();
        ResourceTable resources;
        GraphHost     host(*registry, resources, HostConfig{48000.0, kMaxFrames, Target::Desktop});
        const auto    result = host.setGraph(kDocument);
        std::string   errors;
        for (const auto& e : result.errors) errors += e.code + ": " + e.message + "\n";
        INFO(errors);
        REQUIRE(result.ok);
        std::size_t at = 0, block = 0;
        while (at < kFrames) {
            const auto n = static_cast<std::uint32_t>(std::min<std::size_t>(kSplit[block++ % kSplit.size()],
                                                                            kFrames - at));
            host.processBlock(n, kTransport);
            at += n;
        }
        rp_node* dg = host.node("dg");
        REQUIRE(dg);
        desktop = *static_cast<const rp_devtest_digest*>(dg->vt->get_interface(dg, kDigestInterface));
        desktopFaults = host.faultCount();
    }
    CHECK(desktopFaults == 0);
    CHECK(desktop.heard);

    const ExportResult r = exported();
    INFO(described(r));
    REQUIRE(r.ok());
    CHECK(r.size.packages == 1);
    // The export sizes a pack's memory as the box's 32-bit build takes it; the trial load and this binary's loader
    // place this width's.
    CHECK(r.size.packageBytes == rp::mqjs::boxPackageBytes(rp_script_devtest));

    // The loaded render, counting what the load, the blocks and the unload allocate (no Catch2 macro in the window).
    Arena                 arena(kArenaBytes);
    PlanLoadError         err{};
    rp_devtest_digest     loaded{};
    std::uint32_t         loadedFaults = ~0u;
    bool                  ok = false;
    std::size_t           packageCount = 0;
    rp::test::beginCountingAllocations(rp::test::countsCAllocations());
    LoadedPlan* plan = loadPlan(r.blob, arena.span(kArenaBytes), deviceTypes(), &kDeviceHost, &err);
    if (plan) {
        ok = true;
        packageCount = plan->packages.size();
        std::size_t at = 0, block = 0;
        while (at < kFrames) {
            const auto n = static_cast<std::uint32_t>(std::min<std::size_t>(kSplit[block++ % kSplit.size()],
                                                                            kFrames - at));
            runBlock(plan->view, n, kTransport);
            at += n;
        }
        if (LoadedNode* d = plan->find("dg"))
            loaded = *static_cast<const rp_devtest_digest*>(d->node->vt->get_interface(d->node, kDigestInterface));
        loadedFaults = plan->faults.load();
        unloadPlan(plan);
    }
    const auto counted = rp::test::endCountingAllocations();
    INFO(err.code << " " << err.node << ": " << err.message);
    REQUIRE(ok);
    CHECK(packageCount == 1);
    CHECK(loadedFaults == 0);
    CHECK(loaded.blocks == desktop.blocks);
    CHECK(loaded.hash == desktop.hash);
    CHECK(counted.allocations == 0);
    CHECK(counted.frees == 0);
}

TEST_CASE("the loader sizes a pack's memory with the plan, places it before the nodes, and refuses what cannot fit",
          "[device-mode][script-pack][plan]") {
    const ExportResult r = exported();
    INFO(described(r));
    REQUIRE(r.ok());
    PlanSize      size;
    PlanLoadError err{};
    REQUIRE(sizePlan(r.blob, deviceTypes(), 0, &size, &err));
    CHECK(size.packages == 1);
    CHECK(size.packageBytes == rp::mqjs::packageBytes(rp_script_devtest));
    CHECK(size.bytes >= size.structureBytes + size.packageBytes + size.instanceBytes);

    // Loaded, the pack's memory lies after the plan's structures and before the first node's instance.
    {
        Arena       arena(size.bytes);
        LoadedPlan* plan = loadPlan(r.blob, arena.span(size.bytes), deviceTypes(), &kDeviceHost, &err);
        INFO(err.message);
        REQUIRE(plan);
        REQUIRE(plan->packages.size() == 1);
        const LoadedPackage& pk = plan->packages[0];
        CHECK(pk.setUp);
        CHECK(pk.name() == "devtest");
        CHECK(pk.bytes == size.packageBytes);
        const auto* base = reinterpret_cast<const std::byte*>(arena.storage.data());
        CHECK(static_cast<const std::byte*>(pk.memory) >= base + size.structureBytes);
        for (const LoadedNode& n : plan->nodes)
            CHECK(static_cast<const std::byte*>(n.memory) >= static_cast<const std::byte*>(pk.memory) + pk.bytes);
        unloadPlan(plan);
        CHECK_FALSE(plan->packages[0].setUp);
    }

    // An arena that ends inside the pack's memory: refused, naming the pack and the overrun, before anything is made.
    const std::size_t short_ = size.structureBytes + size.packageBytes - 1;
    const std::string words = refusal(r.blob, short_, deviceTypes());
    INFO(words);
    CHECK(words.rfind("arena : the \"devtest\" pack's memory (" + std::to_string(size.packageBytes) +
                          " bytes) does not fit: the plan needs " + std::to_string(size.bytes) + " bytes, ",
                      0) == 0);
    CHECK(words.find(" past the " + std::to_string(short_) + "-byte arena") != std::string::npos);
    CHECK(rp_devtest_plan_live_device_mode() == 0);

    // The exporter refuses the plan for an arena that ends inside the pack's memory as the box sizes it (its 32-bit
    // build's, the target's), in the loader's words, and passes it for the arena the target's sizing gives.
    PlanSize target;
    REQUIRE(sizePlan(r.blob, deviceTypes(), 0, &target, &err, blob::kTargetPointerBytes));
    CHECK(target.packageBytes == rp::mqjs::boxPackageBytes(rp_script_devtest));
    CHECK(target.packageBytes < size.packageBytes);
    CHECK(target.bytes == size.bytes - size.packageBytes + target.packageBytes);
    const ExportResult tooSmall = exported(target.structureBytes + target.packageBytes - 1);
    REQUIRE_FALSE(tooSmall.ok());
    CHECK(tooSmall.errors[0].code == "arena");
    CHECK(tooSmall.errors[0].message.find("the \"devtest\" pack's memory (" + std::to_string(target.packageBytes) +
                                          " bytes) does not fit") == 0);
    CHECK(exported(target.bytes).ok());

    // A pack whose heap is under the least context the engine sets itself up in cannot be sized.
    static rp_mqjs_pack_source small = rp_script_devtest;
    // A context of 768 bytes by the toolchain check's 3/2 (the desktop's own registration has a floor of 1 MiB).
    small.heap_bytes = 512;
    const rp_package* tiny = rp::mqjs::exactPackage(small);
    REQUIRE(tiny);
    const std::string under = refusal(r.blob, kArenaBytes, {rp_devtest_plan_package_device_mode(), tiny});
    CHECK(under == "package-memory : the \"devtest\" pack's memory cannot be sized: its heap is under the least "
                   "context the engine sets itself up in at this width, " +
                       std::to_string(rp::mqjs::kLeastContextBytes) + " bytes");

    // A pack that will not set up: refused at its setup, before any node is made, and nothing left alive.
    struct Refusing {
        static bool setup(const rp_package*, void*, rp_error* e) {
            std::snprintf(e->message, sizeof e->message, "%s", "not today");
            return false;
        }
    };
    const rp_package*       real = rp_devtest_script_package_device_mode();
    static rp_package_memory hooks;
    hooks = *real->memory;
    hooks.setup = &Refusing::setup;
    static rp_package refusing;
    refusing = *real;
    refusing.memory = &hooks;
    const std::string refused = refusal(r.blob, kArenaBytes, {rp_devtest_plan_package_device_mode(), &refusing});
    CHECK(refused == "package-setup : the \"devtest\" pack would not set up in its memory: not today");
    CHECK(rp_devtest_plan_live_device_mode() == 0);

    // A plan of C++ nodes alone has no package memory.
    const char* plain = R"({"nodes":[{"id":"src","type":"rp.devtest.source"},{"id":"dg","type":"rp.devtest.digest"}],
                           "wires":[{"from":"src.out","to":"dg.in"}]})";
    const ExportResult cpp = exported(kArenaBytes, plain);
    INFO(described(cpp));
    REQUIRE(cpp.ok());
    CHECK(cpp.size.packages == 0);
    CHECK(cpp.size.packageBytes == 0);
}

// ---- the engine's floor ---------------------------------------------------------------------------------------------

TEST_CASE("the exporter refuses a script node whose first block faults on the device's build, and settings it would",
          "[device-mode][script-pack][plan]") {
    auto registry = std::make_unique<NodeRegistry>();
    for (const rp_package* p : {rp_devtest_plan_package(), rp_scripttest_script_package()}) {
        std::string why;
        REQUIRE(registry->addPackage(p, &why));
    }
    const std::vector<const rp_package*> types{rp_devtest_plan_package_device_mode(),
                                               rp_scripttest_script_package_device_mode()};
    ExportOptions o;
    o.maxFrames = kMaxFrames;
    o.arenaBytes = 1024 * 1024;
    // A create that throws plans (the desktop makes no instance) and loads; its first block, which the trial load runs,
    // faults it, and the export is refused naming it.
    const ExportResult throws = exportPlan(R"({"nodes":[{"id":"ct","type":"rp.scripttest.create-throws"}],"wires":[]})",
                                           *registry, types, o);
    INFO(described(throws));
    REQUIRE_FALSE(throws.ok());
    CHECK(throws.errors[0].code == "create-failed");
    CHECK(throws.errors[0].nodeId == "ct");
    CHECK(throws.errors[0].message.find(
              "its first block faulted on the device's build: create threw: TypeError: create would not") == 0);
    // A first block that runs on is held to the exporter's clock (the thread's CPU time at the desktop's deadline),
    // so it faults the node rather than hanging the export.
    const ExportResult spins =
        exportPlan(R"({"nodes":[{"id":"sp","type":"rp.scripttest.spins"}],"wires":[]})", *registry, types, o);
    INFO(described(spins));
    REQUIRE_FALSE(spins.ok());
    CHECK(spins.errors[0].code == "create-failed");
    CHECK(spins.errors[0].message ==
          "its first block faulted on the device's build: process ran past its deadline of 500000 us");
    // Settings the device's decode would not read are refused where they are planned.
    const std::string longName = R"({"nodes":[{"id":"st","type":"rp.scripttest.settings","settings":{"extra":{")" +
                                 std::string(70, 'k') + R"(":1}}}],"wires":[]})";
    const ExportResult named = exportPlan(longName, *registry, types, o);
    INFO(described(named));
    REQUIRE_FALSE(named.ok());
    CHECK(named.errors[0].code == "settings-invalid");
    CHECK(named.errors[0].message.find("not ones a device reads") != std::string::npos);
    // And a node that runs is no fault of the export's.
    const ExportResult fine = exportPlan(R"({"nodes":[{"id":"st","type":"rp.scripttest.settings"}],"wires":[]})",
                                         *registry, types, o);
    INFO(described(fine));
    CHECK(fine.ok());
}

TEST_CASE("a script node across threads: made on the audio thread, swapped, rebuilt and released while blocks run",
          "[device-mode][script-pack][script-threads]") {
    // GraphHost with an audio thread running blocks while the control thread plans. The first document has devtest's
    // pack alone; the second adds scripttest's steady node, so its pack is set up (control thread) while blocks run.
    // Each document moves the devtest transpose's semitones (a settings swap, onSettings on the audio thread) and every
    // eighth renames it (a new instance made on the audio thread at its first block, the old one's slot released
    // through the ring, both pools in the pack's heap until the old plan is reclaimed: what the desktop's larger
    // context holds). The ThreadSanitizer build runs this case for the host's cross-thread protocol.
    NodeRegistry registry;
    for (const rp_package* p :
         {rp_devtest_plan_package(), rp_devtest_script_package(), rp_scripttest_script_package()}) {
        std::string why;
        INFO(why);
        REQUIRE(registry.addPackage(p, &why));
    }
    ResourceTable resources;
    GraphHost     host(registry, resources, HostConfig{48000.0, kMaxFrames, Target::Desktop});
    auto document = [](int k) {
        const std::string tp = "tp" + std::to_string(k / 8);
        const std::string semitones = std::to_string(k % 12);
        if (k == 0)
            return R"({"nodes":[
                {"id":"src","type":"rp.devtest.source","settings":{"seed":3}},
                {"id":")" + tp + R"(","type":"rp.devtest.script-transpose",
                 "settings":{"semitones":)" + semitones + R"(}},
                {"id":"dg","type":"rp.devtest.digest"}],
              "wires":[{"from":"src.events","to":")" + tp + R"(.in"},{"from":")" + tp + R"(.out","to":"dg.events"}]})";
        return R"({"nodes":[
            {"id":"src","type":"rp.devtest.source","settings":{"seed":3}},
            {"id":"st","type":"rp.scripttest.steady"},
            {"id":")" + tp + R"(","type":"rp.devtest.script-transpose","settings":{"semitones":)" + semitones + R"(}},
            {"id":"dg","type":"rp.devtest.digest"}],
          "wires":[{"from":"src.events","to":"st.ev"},{"from":"st.out","to":")" + tp + R"(.in"},
                   {"from":")" + tp + R"(.out","to":"dg.events"}]})";
    };
    REQUIRE(host.setGraph(document(0)).ok);
    std::atomic<bool> stop{false};
    std::atomic<std::uint32_t> blocks{0};
    host.setAudioThreadOwns(true);
    std::thread audio([&] {
        while (!stop.load(std::memory_order_acquire)) {
            host.processBlock(64, kTransport);
            blocks.fetch_add(1, std::memory_order_relaxed);
        }
    });
    auto settle = [&] {
        // Let the audio thread take each plan (and run the node) before the next.
        const std::uint32_t seen = blocks.load(std::memory_order_relaxed);
        while (blocks.load(std::memory_order_relaxed) < seen + 4) std::this_thread::yield();
    };
    settle();
    bool planned = true;
    for (int k = 1; k < 200 && planned; ++k) {
        planned = host.setGraph(document(k)).ok;
        settle();
    }
    stop.store(true, std::memory_order_release);
    audio.join();
    host.setAudioThreadOwns(false);
    CHECK(planned);
    CHECK(host.faultCount() == 0);
    rp_node* dg = host.node("dg");
    REQUIRE(dg);
    CHECK(static_cast<const rp_devtest_digest*>(dg->vt->get_interface(dg, kDigestInterface))->heard);
}

namespace {

// A GraphHost node's script fault, or null.
const char* faultOf(GraphHost& host, std::string_view id) {
    rp_node* n = host.node(id);
    if (!n) return nullptr;
    const auto* f = static_cast<const rp_script_fault*>(n->vt->get_interface(n, RP_SCRIPT_FAULT_INTERFACE));
    return f ? f->reason(n) : nullptr;
}

}  // namespace

TEST_CASE("on the desktop, a pack whose heap ran out is set up again once its last node is gone",
          "[device-mode][script-pack]") {
    NodeRegistry registry;
    for (const rp_package* p : {rp_devtest_plan_package(), rp_hog_script_package()}) {
        std::string why;
        INFO(why);
        REQUIRE(registry.addPackage(p, &why));
    }
    ResourceTable resources;
    GraphHost     host(registry, resources, HostConfig{48000.0, kMaxFrames, Target::Desktop});
    REQUIRE(host.setGraph(R"({"nodes":[{"id":"hog","type":"rp.hog.hog","settings":{"grow":true}},
                                       {"id":"q","type":"rp.hog.quiet"}],"wires":[]})")
                .ok);
    int blocks = 0;
    while (blocks < 100000 && host.faultCount() < 2) {
        host.processBlock(64, kTransport);
        ++blocks;
    }
    REQUIRE(host.faultCount() == 2);
    const std::string dead = faultOf(host, "q") ? faultOf(host, "q") : "";
    CHECK(dead.find("the pack's heap of " + std::to_string(rp::mqjs::desktopContextBytes(rp_script_hog)) +
                    " bytes ran out") == 0);
    // While a node of the dead pack is planned, a new one is as dead.
    REQUIRE(host.setGraph(R"({"nodes":[{"id":"hog","type":"rp.hog.hog","settings":{"grow":true}},
                                       {"id":"q","type":"rp.hog.quiet"},{"id":"q2","type":"rp.hog.quiet"}],
                             "wires":[]})")
                .ok);
    host.processBlock(64, kTransport);
    CHECK(faultOf(host, "q2") != nullptr);
    // A plan without them: once the old plans are reclaimed and the last node is destroyed, the pack is set up afresh,
    // and the next plan's node of it runs.
    REQUIRE(host.setGraph(R"({"nodes":[],"wires":[]})").ok);
    host.processBlock(64, kTransport);
    host.reclaim();
    REQUIRE(host.setGraph(R"({"nodes":[{"id":"q3","type":"rp.hog.quiet"}],"wires":[]})").ok);
    for (int k = 0; k < 16; ++k) host.processBlock(64, kTransport);
    CHECK(host.node("q3"));
    CHECK(faultOf(host, "q3") == nullptr);
    CHECK_FALSE(host.isFaulted("q3"));
}

TEST_CASE("on the desktop, with no clock of its host's, a script node's deadline is its thread's CPU time at 500 ms",
          "[device-mode][script-pack]") {
    NodeRegistry registry;
    for (const rp_package* p : {rp_devtest_plan_package(), rp_scripttest_script_package()}) {
        std::string why;
        INFO(why);
        REQUIRE(registry.addPackage(p, &why));
    }
    ResourceTable resources;
    GraphHost     host(registry, resources, HostConfig{48000.0, kMaxFrames, Target::Desktop});
    REQUIRE(host.setGraph(R"({"nodes":[{"id":"sp","type":"rp.scripttest.spins"}],"wires":[]})").ok);
    const std::uint64_t start = cpuMicros(nullptr);
    host.processBlock(64, kTransport);
    const std::uint64_t spent = cpuMicros(nullptr) - start;
    CHECK(host.faultCount() == 1);
    const char* why = faultOf(host, "sp");
    REQUIRE(why);
    CHECK(std::string(why) == "process ran past its deadline of 500000 us");
    // The thread's CPU time, polled every 10000 instructions: at the deadline, and not much past it.
    CHECK(spent >= rp::mqjs::kDesktopDeadlineMicros);
    CHECK(spent < 4 * rp::mqjs::kDesktopDeadlineMicros);
}

TEST_CASE("the least context the host makes is one the engine sets itself up in without writing past it",
          "[device-mode][script-pack]") {
    // A buffer of the least size, a guard after it: MicroQuickJS's setup of RetroPlug's standard library stays inside.
    constexpr std::size_t kGuard = 512;
    std::vector<std::uint64_t> memory((rp::mqjs::kLeastContextBytes + kGuard) / 8, 0xa5a5a5a5a5a5a5a5ull);
    JSContext* ctx = JS_NewContext(memory.data(), rp::mqjs::kLeastContextBytes, &rp_mqjs_stdlib);
    REQUIRE(ctx);
    JS_FreeContext(ctx);
    bool guarded = true;
    for (std::size_t i = rp::mqjs::kLeastContextBytes / 8; i < memory.size(); ++i)
        guarded = guarded && memory[i] == 0xa5a5a5a5a5a5a5a5ull;
    CHECK(guarded);
}

// ---- measured -------------------------------------------------------------------------------------------------------

TEST_CASE("a script node's cost a message, and its pack's heap use", "[.][script-bench]") {
    // The devtest transpose, each build, and rp.core.midi-filter (a C++ node of the same shape, passing everything), at
    // 16 and 256 messages a block (a queue's whole capacity). The heap is the same at any count: the port's pool is
    // made to its queue's 256 places at the first block, whose time (the instance made, the pools, the block) is given
    // too.
    constexpr std::uint32_t kBlocks = 16000;
    struct Subject {
        const char*         name;
        const rp_node_type* type;
        bool                script;
    };
    const std::vector<Subject> subjects = {
        {"desktop script", typeOf(rp_devtest_script_package(), "rp.devtest.script-transpose"), true},
        {"device script", typeOf(rp_devtest_script_package_device_mode(), "rp.devtest.script-transpose"), true},
        {"device C++ midi-filter", typeOf(rp_core_package_device_mode(), "rp.core.midi-filter"), false},
    };
    for (const std::uint32_t messages : {16u, 256u}) {
        for (const Subject& subject : subjects) {
            Host host;
            Made n(subject.type, host, subject.script ? msgpackOf(R"({"semitones":3})") : msgpackOf("{}"),
                   kTransposePorts);
            REQUIRE(n.node);
            auto fill = [&](std::uint32_t block) {
                clearQueue(n.in(0));
                for (std::uint32_t k = 0; k < messages; ++k) {
                    const std::uint8_t note[3] = {static_cast<std::uint8_t>(k % 2 ? 0x80 : 0x90),
                                                  static_cast<std::uint8_t>(40 + (block + k) % 40), 90};
                    pushMessage(n.in(0), k * 256 / messages, note);
                }
            };
            fill(0);
            const auto firstStart = std::chrono::steady_clock::now();
            REQUIRE(n.run() == RP_OK);
            const double firstUs =
                std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - firstStart).count();
            const rp_script_heap_use first = subject.script ? n.heap(true) : rp_script_heap_use{};
            for (std::uint32_t block = 1; block < 64; ++block) {
                fill(block);
                REQUIRE(n.run() == RP_OK);
            }
            const auto start = std::chrono::steady_clock::now();
            for (std::uint32_t block = 0; block < kBlocks; ++block) {
                fill(block);
                n.run();
            }
            const double ns =
                std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count() /
                (double{kBlocks} * messages);
            const rp_script_heap_use warm = subject.script ? n.heap(true) : rp_script_heap_use{};
            std::printf("%s: %.0f ns a message (%u a block), first block %.0f us", subject.name, ns, messages, firstUs);
            if (subject.script)
                std::printf("; heap: context %zu, fixed %zu, live %zu after the first block, %zu after %u more",
                            warm.bytes, warm.fixed, first.heap, warm.heap, kBlocks + 63);
            std::printf("\n");
        }
    }
}

// ---- the engine's two fixes (script/rp_mqjs_engine.c, cmake/mquickjs.cmake's header) --------------------------------

TEST_CASE("a loaded pack's names stay the engine's after a collection: its definitions are found from C",
          "[device-mode][script-pack][debug-gc]") {
    // The pack's bytecode loaded as the host loads it, into a context of its own: relocated, its strings kept
    // (rp_mqjs_keep_bytecode_names), run. Its definitions are under a name of the bytecode's own,
    // `__rp_scriptNodes`, which the code used as a property name and so made unique where it lies; a collection then
    // runs, and a lookup from C makes the same name again. Without the strings kept, the collection drops the
    // bytecode's string from the engine's weak table of names, the lookup makes a second string of that text, and
    // finds nothing.
    const rp_mqjs_pack_source& source = rp_script_devtest;
    std::vector<std::uint64_t> memory((std::size_t{1} << 20) / sizeof(std::uint64_t));
    std::vector<std::uint64_t> code(source.bytecode_size / sizeof(std::uint64_t) + 1);
    std::memcpy(code.data(), source.bytecode, source.bytecode_size);
    auto* bytes = reinterpret_cast<std::uint8_t*>(code.data());
    JSContext* ctx = JS_NewContext(memory.data(), memory.size() * sizeof(std::uint64_t), &rp_mqjs_stdlib);
    REQUIRE(ctx);
    REQUIRE(JS_RelocateBytecode(ctx, bytes, source.bytecode_size) == 0);
    rp_mqjs_keep_bytecode_names(bytes, source.bytecode_size);
    const JSValue main = JS_LoadBytecode(ctx, bytes);
    REQUIRE_FALSE(JS_IsException(main));
    REQUIRE_FALSE(JS_IsException(JS_Run(ctx, main)));
    JS_GC(ctx);
    const JSValue defs = JS_GetPropertyStr(ctx, JS_GetGlobalObject(ctx), "__rp_scriptNodes");
    CHECK(JS_IsArray(ctx, defs));
    JS_FreeContext(ctx);
}

namespace {

// A runner's line for one pack (retroplug-mqjs-run pack: its nodes from the device's runtime, registered from the whole
// pack), or the run's exit status when it failed.
std::string runnerLine(const std::string& runner, const std::string& dir, const rp_mqjs_pack_source& source,
                       std::uint32_t heapBytes, std::uint32_t blocks) {
    std::string types;
    for (std::uint32_t i = 0; i < source.type_count; ++i) types += (i ? "," : "") + std::string(source.type_ids[i]);
    const std::string name = source.name;
    const std::string out = dir + "/." + name + ".runner-" + std::to_string(heapBytes) + ".txt";
    std::string command = "\"" + runner + "\" pack \"" + dir + "/" + name + ".device64.bin\" --control \"" + dir + "/" +
                          name + ".mqjs64.bin\" --name " + name + " --heap-bytes " + std::to_string(heapBytes) +
                          " --types " + types + " --blocks " + std::to_string(blocks) +
                          " --least no --deadline-us 3000000 > \"" + out + "\" 2>&1";
#ifdef _WIN32
    command = "\"" + command + "\"";  // cmd /c drops a command line's outer quotes when it starts with one
#endif
    const int status = std::system(command.c_str());
    std::ifstream in(out, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::remove(out.c_str());
    if (status != 0) return "exit " + std::to_string(status) + ": " + text;
    const std::size_t at = text.find("ok least=");
    return at == std::string::npos ? text : text.substr(at);
}

std::string digestOf(const std::string& line) {
    const std::size_t at = line.find("digest=");
    return at == std::string::npos ? line : line.substr(at);
}

}  // namespace

TEST_CASE("the pack check runs the same in the engine's DEBUG_GC build, a collection at every allocation",
          "[device-mode][script-pack][debug-gc]") {
    // retroplug-mqjs-run-gc is the toolchain's runner over the engine built with DEBUG_GC (graph-runtime's CMakeLists):
    // every allocation collects first, and every collection moves what it keeps. Each pack's check, its nodes run from
    // the device's runtime over the runner's whole stimulus, must digest to what the ordinary runner's does (the
    // declared heap's run alone: the debug build's runs are slow). The debug build keeps a block of up to half the
    // context aside to move the heap's addresses, so it runs at twice the declared heap. A bytecode string dropped from
    // the weak table of names (the second fix, reverted) fails here at registration, and scripttest's sends node (data
    // that is no Uint8Array, whose every read can collect) holds the host to reading its data through a slot the
    // collector updates: a copy of the value, held across those reads, sends what the collector moved away.
    // Each pack's run: the runner's 16 blocks, scripttest's one cycle of five (a block of each kind, its sends node's
    // among them), as its many nodes make the debug build's run long. Both runners give a call 3 s by the wall
    // clock, so a slow build's node (a sanitizer's, under DEBUG_GC) meets the deadline only where an ordinary one does.
    struct Pack {
        const rp_mqjs_pack_source* source;
        std::string                dir;
        std::uint32_t              blocks;
    };
    const std::string build = RP_BUILD_DIR;
    for (const Pack& p : {Pack{&rp_script_bliptoaster, build + "/packages/bliptoaster/script", 16},
                          Pack{&rp_script_devtest, build + "/packages/devtest/script", 16},
                          Pack{&rp_script_hog, build + "/device-mode/script-packs/hog", 16},
                          Pack{&rp_script_scripttest, build + "/device-mode/script-packs/scripttest", 5}}) {
        INFO(p.source->name);
        const std::string plain = runnerLine(RP_MQJS_RUN, p.dir, *p.source, p.source->heap_bytes, p.blocks);
        const std::string gc = runnerLine(RP_MQJS_RUN_GC, p.dir, *p.source, 2 * p.source->heap_bytes, p.blocks);
        INFO(plain);
        INFO(gc);
        REQUIRE(plain.starts_with("ok least=none"));
        REQUIRE(gc.starts_with("ok least=none"));
        CHECK(digestOf(gc) == digestOf(plain));
    }
}
