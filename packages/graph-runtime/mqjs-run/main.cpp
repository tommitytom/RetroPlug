// retroplug-mqjs-run: the device-pack toolchain's runner (packages/retroplug/scripts/build-device-pack.mjs,
// docs/modular-graph-phase-e4-design.md section 6). It runs MicroQuickJS bytecode on RetroPlug's own standard library
// and the MicroQuickJS script host (script/MqjsHost.hpp), the desktop's build of what the box runs, so what the
// toolchain checks a pack against is the engine as the hosts configure it, not upstream's REPL (mqjs), whose globals a
// pack could otherwise lean on unseen.
//
//   retroplug-mqjs-run script <bytecode> [--context-bytes <n>]
//       Runs the bytecode in a context of n bytes (default 1 MiB), its console.log on stdout. Exits 1, saying what
//       stopped it on stderr, when it throws.
//
//   retroplug-mqjs-run pack <bytecode> --name <pack> --heap-bytes <n> --types <id,...> [--blocks <n>]
//                           [--control <bytecode>] [--least no] [--deadline-us <n>]
//       Holds a pack's 64-bit bytecode to its declared heap, by the toolchain's heuristic: in a context of 3/2 of n
//       bytes (the manifest's heapBytes is the box's 32-bit figure; script/MqjsHost.hpp's kCheckHalves says why 3/2,
//       and why it is no guarantee), set up as a host sets a pack up (its definitions run and checked against the ids),
//       it creates one node of each type from its default settings, runs it up as a plan does (activate, then its
//       connected outputs) and runs every node for --blocks blocks of 256 frames (default 16) under the host's
//       deadline. <bytecode> is what the nodes run: the device's runtime of the pack, as every host runs it, or the
//       whole pack, for the toolchain's reference run. --control is the whole pack, which registers the types (the
//       desktop's control context); without it <bytecode> does both. Each events or bytes input is a queue of the
//       device's MIDI input's capacity (graph/rp_device_midi_in.h: 64 messages, 4096 bytes), filled block by block in a
//       cycle of five: 64 note messages; 64 SysEx of 64 bytes (the queue's bytes full); two blocks that sweep the first
//       byte, an empty message then 63 more whose first bytes run on from the last sweep's (0x00 to 0xFF and round
//       again, each in the length a parser gives it: a data byte's two, a channel message's two or three, a short
//       SysEx, a system common's, a lone F7 and the real-time bytes alone), tied two to a frame, with flags set on
//       every third, so a filter's every branch runs; and a block that brings nothing at all, so a node the host skips
//       when idle is skipped there (and one that differs on that between the bundles is caught by the digest). Each
//       control stream holds a plan's 64 changes and gets two a block, but for that idle one. A node's ports are sized
//       to those queues at its first block (script/MqjsHost.hpp), so this is the heap the node needs behind the
//       device's MIDI, and the most a burst can ask of it there; a plan whose sources declare larger queues makes
//       larger pools, which the exporter's trial load makes and holds to the heap by the same 3/2. Then it finds the
//       least heapBytes that does the same, to 64 bytes (unless --least no, which prints least=none: a run that wants
//       the declared heap's result alone, as a DEBUG_GC build's, whose every run is slow). Prints "ok least=<bytes>
//       declared=<n> live=<bytes> fixed=<bytes> digest=<hex>" (least: in heapBytes, what this check would accept, not
//       what the box needs; live: the live heap after the last block, collected; fixed: what MicroQuickJS takes of the
//       context before its heap, both at this width; digest: every output's messages and changes over the run) and
//       exits 0; or says what failed at the declared heap on stderr ("define", "create" or "run", the node and its
//       words) and exits 1. A node that faults for any reason but the heap running out (one that throws by design, or
//       wants settings other than its defaults) is reported on stdout as a note, and the run goes on without it.
//       --deadline-us gives each call into a node, and into the pack's control context (its load, its settings
//       checks), that long by the wall clock, in place of the desktop's own deadlines: a build whose every run is slow
//       (DEBUG_GC, a sanitizer) holds its pack to the same outcome as an ordinary one.

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <new>
#include <sstream>
#include <string>
#include <vector>

#include "graph/Descriptor.hpp"
#include "graph/PortTypes.hpp"
#include "graph/Queue.hpp"
#include "graph/rp_device_midi_in.h"
#include "graph/rp_node.h"
#include "script/MqjsHost.hpp"

namespace {

[[noreturn]] void usage(const char* why) {
    std::fprintf(stderr,
                 "retroplug-mqjs-run: %s\n"
                 "usage: retroplug-mqjs-run script <bytecode> [--context-bytes <n>]\n"
                 "       retroplug-mqjs-run pack <bytecode> --name <pack> --heap-bytes <n> --types <id,...> "
                 "[--blocks <n>] [--control <bytecode>] [--least no] [--deadline-us <n>]\n",
                 why);
    std::exit(2);
}

// A file's bytes, in storage aligned for MicroQuickJS's words (a script's bytecode is relocated where it lies).
std::vector<std::uint64_t> readFile(const std::string& path, std::size_t* size) {
    std::ifstream in(path, std::ios::binary);
    if (!in) usage(("cannot read " + path).c_str());
    const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::vector<std::uint64_t> words(bytes.size() / 8 + 1);
    std::memcpy(words.data(), bytes.data(), bytes.size());
    *size = bytes.size();
    return words;
}

void writeStdout(void*, const char* text, std::size_t size) { std::fwrite(text, 1, size, stdout); }

// ---- pack -----------------------------------------------------------------------------------------------------------

// The deadline a run's host offers its nodes (--deadline-us), by the wall clock: 0 offers no clock, so the host's own
// desktop default holds (the thread's CPU time, script/MqjsHost.hpp's kDesktopDeadlineMicros).
std::uint64_t gDeadlineMicros = 0;
std::uint64_t wallMicros(void*) {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                          std::chrono::steady_clock::now().time_since_epoch())
                                          .count());
}

// The host the nodes are created under: the pack's memory (rp.host.packages/1), and a clock when --deadline-us asks.
struct Host {
    const rp_package* package = nullptr;
    void*             memory = nullptr;
    rp_host_packages  service{this, &Host::give};
    rp_host_clock     clock{nullptr, &wallMicros, gDeadlineMicros};
    rp_host           host{RP_ABI_VERSION, this, nullptr, nullptr, &Host::log, &Host::interfaceOf};
    static void* give(void* ctx, const rp_package* p, rp_error* err) {
        auto* h = static_cast<Host*>(ctx);
        if (p == h->package) return h->memory;
        if (err) std::snprintf(err->message, sizeof err->message, "%s", "not the pack under test");
        return nullptr;
    }
    static void        log(void*, int, const char*) {}
    static const void* interfaceOf(const rp_host* h, const char* id) {
        auto* self = static_cast<Host*>(h->ctx);
        if (std::strcmp(id, RP_HOST_PACKAGES_INTERFACE) == 0) return &self->service;
        if (gDeadlineMicros && std::strcmp(id, RP_HOST_CLOCK_INTERFACE) == 0) return &self->clock;
        return nullptr;
    }
};

// One node under test, with the ports and params its default layout gives it.
struct Rig {
    const rp_node_type*                type = nullptr;
    std::vector<std::uint64_t>         memory;
    rp_node*                           node = nullptr;
    std::vector<rp_port_view>          inputs, outputs;
    std::vector<rp_control_queue*>     params;
    std::vector<std::unique_ptr<rp_queue>>         queues;
    std::vector<std::unique_ptr<rp_control_queue>> streams;
    std::vector<std::vector<rp_message>>           items;
    std::vector<std::vector<std::uint8_t>>         bytes;
    std::vector<std::vector<rp_control_change>>    changes;
    std::vector<rp_queue*>         feeds;    // the inputs the run fills
    std::vector<rp_control_queue*> moves;    // the control inputs and params it moves
    std::vector<rp_queue*>         drains;   // the outputs it empties
    std::vector<rp_control_queue*> rolls;    // the control outputs it rolls
    rp_resource*                   slot = nullptr;
    bool                           faulted = false;
    rp_process                     step{};

    rp_queue* queue(std::uint32_t messages, std::uint32_t capacity) {
        items.emplace_back(messages);
        bytes.emplace_back(capacity);
        queues.push_back(std::make_unique<rp_queue>(
            rp_queue{items.back().data(), 0, messages, bytes.back().data(), 0, capacity, 0}));
        return queues.back().get();
    }
    rp_control_queue* stream(double start) {
        changes.emplace_back(64);
        streams.push_back(std::make_unique<rp_control_queue>(rp_control_queue{start, changes.back().data(), 0, 64, 0}));
        return streams.back().get();
    }
};

struct Outcome {
    std::string stage;  // empty: it ran
    std::string reason;
    rp_script_heap_use use{};
    std::uint64_t      digest = 0xcbf29ce484222325ull;  // FNV-1a 64 over every output, block by block
};

void fold(std::uint64_t& h, const void* data, std::size_t size) {
    const auto* p = static_cast<const std::uint8_t*>(data);
    for (std::size_t i = 0; i < size; ++i) h = (h ^ p[i]) * 0x100000001b3ull;
}

const rp_script_fault* faultOf(rp_node* node) {
    return static_cast<const rp_script_fault*>(node->vt->get_interface(node, RP_SCRIPT_FAULT_INTERFACE));
}

// The bytes a parser gives a message whose first byte is `first`: a data byte's two, a channel message's two or three,
// a short SysEx, a system common's, and a real-time byte or a lone F7 alone.
std::size_t messageOf(std::uint8_t first, std::uint8_t* out) {
    out[0] = first;
    if (first < 0x80) return out[1] = 0x40, 2;
    if (first < 0xC0 || (first >= 0xE0 && first < 0xF0)) return out[1] = 0x3C, out[2] = 0x64, 3;
    if (first < 0xE0) return out[1] = 0x05, 2;
    if (first == 0xF0) return out[1] = 0x7D, out[2] = 0x01, out[3] = 0xF7, 4;
    if (first == 0xF1 || first == 0xF3) return out[1] = 0x10, 2;
    if (first == 0xF2) return out[1] = 0x10, out[2] = 0x20, 3;
    return 1;
}

// Block b's messages on an input (the header comment): notes, SysEx, two sweeps of the first byte, then none.
constexpr std::uint32_t kCycle = 5;
void fill(rp_queue& q, std::uint32_t b, std::uint32_t frames) {
    rp::graph::clearQueue(q);
    switch (b % kCycle) {
        case 0:
            for (std::uint32_t k = 0; k < q.capacity; ++k) {
                const std::uint8_t note[3] = {static_cast<std::uint8_t>(k % 2 ? 0x80 : 0x90),
                                              static_cast<std::uint8_t>(48 + (b + k) % 24), 100};
                rp::graph::pushMessage(q, k * frames / q.capacity, note);
            }
            return;
        case 1: {
            std::uint8_t sysex[64] = {0xF0, 0x7D};
            sysex[63] = 0xF7;
            for (std::uint32_t k = 0; k < q.bytes_capacity / 64; ++k)
                rp::graph::pushMessage(q, k * frames * 64 / q.bytes_capacity, sysex);
            return;
        }
        case 4: return;
        default: {
            const std::uint32_t sweep = b / kCycle * 2 + (b % kCycle - 2);
            rp::graph::pushMessage(q, 0, {});
            for (std::uint32_t k = 1; k < q.capacity; ++k) {
                std::uint8_t bytes[4];
                const std::size_t n = messageOf(static_cast<std::uint8_t>((sweep * (q.capacity - 1) + k - 1) % 256),
                                                bytes);
                rp::graph::pushMessage(q, k / 2 * (frames / 32), {bytes, n}, k % 3 == 0 ? 1u : 0u);
            }
            return;
        }
    }
}

Outcome runPack(const rp_package* package, std::uint32_t blocks, std::vector<std::string>* notes = nullptr) {
    Outcome out;
    const rp_package_memory& hooks = *package->memory;
    rp_memory_req req{};
    rp_error err{};
    if (!hooks.query(package, &req, &err)) return {"define", err.message, {}};
    std::vector<std::uint64_t> memory(req.size / 8 + 1);
    if (!hooks.setup(package, memory.data(), &err)) return {"define", err.message, {}};
    Host host;
    host.package = package;
    host.memory = memory.data();

    constexpr std::uint32_t kFrames = 256;
    std::uint64_t digest = Outcome{}.digest;
    std::vector<std::unique_ptr<Rig>> rigs;
    auto finish = [&](Outcome o) {
        // The pack's heap after the run, collected, through a node that made its instance.
        for (auto& r : rigs) {
            const void* found = r->node ? r->node->vt->get_interface(r->node, RP_SCRIPT_HEAP_INTERFACE) : nullptr;
            const auto* heap = static_cast<const rp_script_heap*>(found);
            if (heap && heap->use(r->node, true, &o.use)) break;
        }
        for (auto& r : rigs)
            if (r->node) r->type->destroy(r->node);
        hooks.teardown(package, memory.data());
        return o;
    };
    for (std::uint32_t t = 0; t < package->node_type_count; ++t) {
        auto rig = std::make_unique<Rig>();
        rig->type = package->node_types[t];
        const rp_settings none{nullptr, 0, RP_SETTINGS_JSON};
        std::string layoutJson(4096, '\0');
        std::size_t n = rig->type->layout_for(rig->type, &none, layoutJson.data(), layoutJson.size(), &err);
        if (n >= layoutJson.size()) {
            layoutJson.assign(n + 1, '\0');
            n = rig->type->layout_for(rig->type, &none, layoutJson.data(), layoutJson.size(), &err);
        }
        if (n == 0) return finish({"create", std::string(rig->type->id) + ": " + err.message, {}});
        layoutJson.resize(n);
        auto layout = rp::graph::parseLayout(layoutJson);
        if (!layout) return finish({"create", std::string(rig->type->id) + ": its layout does not parse", {}});
        rp_memory_req nodeReq{};
        if (!rig->type->query_memory(rig->type, &none, &nodeReq, &err))
            return finish({"create", std::string(rig->type->id) + ": " + err.message, {}});
        rig->memory.resize(nodeReq.size / 8 + 1);
        rig->node = rig->type->create(rig->type, &host.host, rig->memory.data(), &none, &err);
        if (!rig->node) return finish({"create", std::string(rig->type->id) + ": " + err.message, {}});
        if (!rig->node->vt->activate(rig->node, 48000.0, kFrames, &err))
            return finish({"create", std::string(rig->type->id) + ": " + err.message, {}});
        for (const auto& p : layout->ports) {
            rp_port_view v{};
            v.kind = static_cast<std::uint32_t>(p.kind);
            const bool out = p.dir == rp::graph::PortDir::Out;
            switch (p.kind) {
                case rp::graph::PortKind::Events:
                case rp::graph::PortKind::Bytes:
                    v.u.queue = out ? rig->queue(rp::graph::kDefaultQueueMessages, rp::graph::kDefaultQueueBytes)
                                    : rig->queue(RP_DEVICE_MIDI_IN_MESSAGES, RP_DEVICE_MIDI_IN_BYTES);
                    (out ? rig->drains : rig->feeds).push_back(v.u.queue);
                    break;
                case rp::graph::PortKind::Control:
                    v.u.control = rig->stream(0.0);
                    (out ? rig->rolls : rig->moves).push_back(v.u.control);
                    break;
                case rp::graph::PortKind::Resource: v.u.resource = &rig->slot; break;
                default: break;
            }
            (out ? rig->outputs : rig->inputs).push_back(v);
        }
        for (const auto& p : layout->params) {
            rig->params.push_back(rig->stream(p.def));
            rig->moves.push_back(rig->params.back());
        }
        std::vector<std::uint8_t> connected(rig->outputs.size(), 1);
        rig->node->vt->ports_changed(rig->node, connected.data(), static_cast<std::uint32_t>(connected.size()));
        rigs.push_back(std::move(rig));
    }
    const rp_transport transport{48000.0, 120.0, 0.0, RP_TRANSPORT_PLAYING};
    for (std::uint32_t b = 0; b < blocks; ++b) {
        for (auto& rig : rigs) {
            for (rp_queue* q : rig->feeds) fill(*q, b, kFrames);
            for (rp_control_queue* c : rig->moves) {
                c->count = 0;
                if (b % kCycle == 4) continue;
                rp::graph::pushControl(*c, 0, 0.25 * (b % 4));
                rp::graph::pushControl(*c, kFrames / 2, 0.5);
            }
            for (rp_queue* q : rig->drains) rp::graph::clearQueue(*q);
            for (rp_control_queue* c : rig->rolls) rp::graph::rollControl(*c);
            // The node's plan step: one rp_process for the whole run, as an executor's is.
            rig->step = rp_process{kFrames,
                                   &transport,
                                   rig->inputs.data(),
                                   static_cast<std::uint32_t>(rig->inputs.size()),
                                   rig->outputs.data(),
                                   static_cast<std::uint32_t>(rig->outputs.size()),
                                   rig->params.data(),
                                   static_cast<std::uint32_t>(rig->params.size())};
            if (rig->faulted) continue;
            if (rig->node->vt->process(rig->node, &rig->step) == RP_OK) {
                for (const rp_queue* q : rig->drains) {
                    for (std::uint32_t k = 0; k < q->count; ++k) {
                        const rp_message& m = q->items[k];
                        fold(digest, &m.frame, sizeof m.frame);
                        fold(digest, &m.flags, sizeof m.flags);
                        fold(digest, &m.size, sizeof m.size);
                        fold(digest, q->bytes + m.offset, m.size);
                    }
                }
                for (const rp_control_queue* c : rig->rolls) {
                    for (std::uint32_t k = 0; k < c->count; ++k) {
                        fold(digest, &c->changes[k].frame, sizeof c->changes[k].frame);
                        fold(digest, &c->changes[k].value, sizeof c->changes[k].value);
                    }
                }
                continue;
            }
            // The heap running out fails the check; any other fault is the node's own (one that throws by design, or
            // wants settings other than its defaults), which the run reports and goes on without.
            const rp_script_fault* f = faultOf(rig->node);
            const std::string why = f && f->reason(rig->node) ? f->reason(rig->node) : "it faulted";
            if (!f || f->kind(rig->node) == RP_SCRIPT_FAULT_HEAP)
                return finish({"run", std::string(rig->type->id) + ": " + why, {}});
            rig->faulted = true;
            if (notes) notes->push_back(std::string(rig->type->id) + " faulted on its default settings: " + why);
        }
    }
    Outcome done;
    done.digest = digest;
    return finish(done);
}

int pack(int argc, char** argv) {
    if (argc < 3) usage("pack needs a bytecode file");
    std::string name, types, control;
    std::uint32_t heap = 0, blocks = 16;
    bool          least = true;
    for (int i = 3; i < argc; ++i) {
        const std::string a = argv[i];
        if (i + 1 >= argc) usage(("no value for " + a).c_str());
        const std::string v = argv[++i];
        if (a == "--name") name = v;
        else if (a == "--heap-bytes") heap = static_cast<std::uint32_t>(std::stoul(v));
        else if (a == "--types") types = v;
        else if (a == "--blocks") blocks = static_cast<std::uint32_t>(std::stoul(v));
        else if (a == "--control") control = v;
        else if (a == "--least" && (v == "yes" || v == "no")) least = v == "yes";
        else if (a == "--deadline-us") gDeadlineMicros = std::stoull(v);
        else usage(("unknown option " + a).c_str());
    }
    if (name.empty() || !heap || types.empty()) usage("pack needs --name, --heap-bytes and --types");
    std::vector<std::string> ids;
    std::stringstream        list(types);
    for (std::string id; std::getline(list, id, ',');) ids.push_back(id);
    std::vector<const char*> idPointers;
    for (const auto& id : ids) idPointers.push_back(id.c_str());

    std::size_t size = 0, controlSize = 0;
    const std::vector<std::uint64_t> words = readFile(argv[2], &size);
    const std::vector<std::uint64_t> controlWords =
        control.empty() ? std::vector<std::uint64_t>{} : readFile(control, &controlSize);
    rp_mqjs_pack_source source{name.c_str(),
                               reinterpret_cast<const std::uint8_t*>(words.data()),
                               static_cast<std::uint32_t>(size),
                               heap,
                               static_cast<std::uint32_t>(ids.size()),
                               idPointers.data(),
                               0,
                               control.empty() ? nullptr : reinterpret_cast<const std::uint8_t*>(controlWords.data()),
                               static_cast<std::uint32_t>(controlSize)};
    // The pack's registration (its control context: its load, its settings checks) held to the run's deadline too.
    if (gDeadlineMicros) rp::mqjs::setControlClock(&wallMicros, gDeadlineMicros);
    const rp_package* package = rp::mqjs::exactPackage(source);
    if (!package) {
        std::fprintf(stderr, "define: its bytecode is not a pack this host registers (said above)\n");
        return 1;
    }
    // The declared heap first: what the toolchain refuses or passes. Then the least that does the same, by halving.
    std::vector<std::string> notes;
    const Outcome declared = runPack(package, blocks, &notes);
    if (!declared.stage.empty()) {
        std::fprintf(stderr, "%s: %s\n", declared.stage.c_str(), declared.reason.c_str());
        return 1;
    }
    for (const auto& note : notes) std::printf("note: %s\n", note.c_str());
    if (!least) {
        std::printf("ok least=none declared=%u live=%zu fixed=%zu digest=%016llx\n", heap, declared.use.heap,
                    declared.use.fixed, static_cast<unsigned long long>(declared.digest));
        return 0;
    }
    // In heapBytes, the box's figure: from the least context the box sets itself up in.
    std::uint32_t lo = rp::mqjs::kBoxLeastContextBytes, hi = heap;
    source.heap_bytes = lo;
    if (runPack(package, blocks).stage.empty()) hi = lo;
    while (hi - lo > 64) {
        const std::uint32_t mid = (lo + (hi - lo) / 2) & ~7u;
        source.heap_bytes = mid;
        (runPack(package, blocks).stage.empty() ? hi : lo) = mid;
    }
    std::printf("ok least=%u declared=%u live=%zu fixed=%zu digest=%016llx\n", hi, heap, declared.use.heap,
                declared.use.fixed, static_cast<unsigned long long>(declared.digest));
    return 0;
}

int script(int argc, char** argv) {
    if (argc < 3) usage("script needs a bytecode file");
    std::size_t contextBytes = std::size_t{1} << 20;
    for (int i = 3; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--context-bytes" && i + 1 < argc) contextBytes = std::stoul(argv[++i]);
        else usage(("unknown option " + a).c_str());
    }
    std::size_t size = 0;
    std::vector<std::uint64_t> words = readFile(argv[2], &size);
    const std::string failed =
        rp::mqjs::runScript(reinterpret_cast<std::uint8_t*>(words.data()), size, contextBytes, &writeStdout, nullptr);
    std::fflush(stdout);
    if (failed.empty()) return 0;
    std::fprintf(stderr, "%s\n", failed.c_str());
    return 1;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) usage("no command");
    const std::string command = argv[1];
    if (command == "script") return script(argc, argv);
    if (command == "pack") return pack(argc, argv);
    usage(("unknown command " + command).c_str());
}
