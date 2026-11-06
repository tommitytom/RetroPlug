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
//       Holds a pack's 64-bit bytecode to its declared heap: in a context of n bytes, set up as a host sets a pack up
//       (its definitions run and checked against the ids), it creates one node of each type from its default settings,
//       runs it up as a plan does (activate, then its connected outputs) and runs every node for --blocks blocks of 256
//       frames (default 16) under the host's deadline. Each events or bytes input is a queue of a plan's default
//       capacity (graph/PortTypes.hpp: 256 messages, 4096 bytes), full every block: note messages on even blocks, 64
//       SysEx of 64 bytes on odd ones; each control stream holds a plan's 64 changes and gets two a block. A node's
//       ports are sized to those queues at its first block (script/MqjsHost.hpp), so this is the heap the node needs in
//       a plan whose sources declare no smaller capacity, and the most a burst can ask of it. Then it finds the least
//       context that does the same, to 64 bytes. Prints "ok least=<bytes> declared=<n> live=<bytes> fixed=<bytes>"
//       (live: the live heap after the last block, collected; fixed: what MicroQuickJS takes of the context before its
//       heap) and exits 0; or says
//       what failed at the declared heap on stderr ("define", "create" or "run", the node and its words) and exits 1.
//       A node that faults for any reason but the heap running out (one that throws by design, or wants settings other
//       than its defaults) is reported on stdout as a note, and the run goes on without it.
//       The check is at 64 bits, the desktop's width: a 64-bit context needs more of its heap than the box's 32-bit
//       one does for the same objects, so a pack that runs in its heap here runs in it on the box.

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
#include "graph/rp_node.h"
#include "script/MqjsHost.hpp"

namespace {

[[noreturn]] void usage(const char* why) {
    std::fprintf(stderr,
                 "retroplug-mqjs-run: %s\n"
                 "usage: retroplug-mqjs-run script <bytecode> [--context-bytes <n>]\n"
                 "       retroplug-mqjs-run pack <bytecode> --name <pack> --heap-bytes <n> --types <id,...> "
                 "[--blocks <n>]\n",
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

// The host the nodes are created under: the pack's memory (rp.host.packages/1), nothing else.
struct Host {
    const rp_package* package = nullptr;
    void*             memory = nullptr;
    rp_host_packages  service{this, &Host::give};
    rp_host           host{RP_ABI_VERSION, this, nullptr, nullptr, &Host::log, &Host::interfaceOf};
    static void* give(void* ctx, const rp_package* p, rp_error* err) {
        auto* h = static_cast<Host*>(ctx);
        if (p == h->package) return h->memory;
        if (err) std::snprintf(err->message, sizeof err->message, "%s", "not the pack under test");
        return nullptr;
    }
    static void        log(void*, int, const char*) {}
    static const void* interfaceOf(const rp_host* h, const char* id) {
        return std::strcmp(id, RP_HOST_PACKAGES_INTERFACE) == 0 ? &static_cast<Host*>(h->ctx)->service : nullptr;
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
};

const rp_script_fault* faultOf(rp_node* node) {
    return static_cast<const rp_script_fault*>(node->vt->get_interface(node, RP_SCRIPT_FAULT_INTERFACE));
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
                    v.u.queue = rig->queue(rp::graph::kDefaultQueueMessages, rp::graph::kDefaultQueueBytes);
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
            for (rp_queue* q : rig->feeds) {
                rp::graph::clearQueue(*q);
                if (b % 2 == 0) {
                    for (std::uint32_t k = 0; k < q->capacity; ++k) {
                        const std::uint8_t note[3] = {static_cast<std::uint8_t>(k % 2 ? 0x80 : 0x90),
                                                      static_cast<std::uint8_t>(48 + (b + k) % 24), 100};
                        rp::graph::pushMessage(*q, k * kFrames / q->capacity, note);
                    }
                } else {
                    std::uint8_t sysex[64] = {0xF0, 0x7D};
                    sysex[63] = 0xF7;
                    for (std::uint32_t k = 0; k < q->bytes_capacity / 64; ++k)
                        rp::graph::pushMessage(*q, k * kFrames * 64 / q->bytes_capacity, sysex);
                }
            }
            for (rp_control_queue* c : rig->moves) {
                c->count = 0;
                rp::graph::pushControl(*c, 0, 0.25 * (b % 4));
                rp::graph::pushControl(*c, kFrames / 2, 0.5);
            }
            for (rp_queue* q : rig->drains) rp::graph::clearQueue(*q);
            for (rp_control_queue* c : rig->rolls) rp::graph::rollControl(*c);
            rp_process block{kFrames,
                             &transport,
                             rig->inputs.data(),
                             static_cast<std::uint32_t>(rig->inputs.size()),
                             rig->outputs.data(),
                             static_cast<std::uint32_t>(rig->outputs.size()),
                             rig->params.data(),
                             static_cast<std::uint32_t>(rig->params.size())};
            if (rig->faulted || rig->node->vt->process(rig->node, &block) == RP_OK) continue;
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
    return finish({});
}

int pack(int argc, char** argv) {
    if (argc < 3) usage("pack needs a bytecode file");
    std::string name, types;
    std::uint32_t heap = 0, blocks = 16;
    for (int i = 3; i < argc; ++i) {
        const std::string a = argv[i];
        if (i + 1 >= argc) usage(("no value for " + a).c_str());
        const std::string v = argv[++i];
        if (a == "--name") name = v;
        else if (a == "--heap-bytes") heap = static_cast<std::uint32_t>(std::stoul(v));
        else if (a == "--types") types = v;
        else if (a == "--blocks") blocks = static_cast<std::uint32_t>(std::stoul(v));
        else usage(("unknown option " + a).c_str());
    }
    if (name.empty() || !heap || types.empty()) usage("pack needs --name, --heap-bytes and --types");
    std::vector<std::string> ids;
    std::stringstream        list(types);
    for (std::string id; std::getline(list, id, ',');) ids.push_back(id);
    std::vector<const char*> idPointers;
    for (const auto& id : ids) idPointers.push_back(id.c_str());

    std::size_t size = 0;
    const std::vector<std::uint64_t> words = readFile(argv[2], &size);
    rp_mqjs_pack_source source{name.c_str(), reinterpret_cast<const std::uint8_t*>(words.data()),
                               static_cast<std::uint32_t>(size), heap, static_cast<std::uint32_t>(ids.size()),
                               idPointers.data()};
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
    std::uint32_t lo = rp::mqjs::kLeastContextBytes, hi = heap;
    source.heap_bytes = lo;
    if (runPack(package, blocks).stage.empty()) hi = lo;
    while (hi - lo > 64) {
        const std::uint32_t mid = (lo + (hi - lo) / 2) & ~7u;
        source.heap_bytes = mid;
        (runPack(package, blocks).stage.empty() ? hi : lo) = mid;
    }
    std::printf("ok least=%u declared=%u live=%zu fixed=%zu\n", hi, heap, declared.use.heap, declared.use.fixed);
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
