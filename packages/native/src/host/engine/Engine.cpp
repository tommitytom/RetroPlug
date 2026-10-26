#include "host/engine/Engine.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <utility>

#include "system/SystemBase.hpp"
#include "system/SystemTypes.hpp"   // AudioBlockInfo, SystemId
#include "system/sameboy/SameBoySystem.hpp"  // live-apply cast target (model/highpass/gain/…)
#include "system/mesen/MesenNesSystem.hpp"    // live-apply cast target (NES region / sprite limit)

#include "transport/FrameBufferTriple.hpp"
#include "native/core/img/png/lodepng.h"

#include "graph/CanonicalJson.hpp"
#include "graph/GraphHost.hpp"
#include "graph/HostNodes.hpp"
#include "graph/Queue.hpp"
#include "graph/nodes/CoreNodes.hpp"
#include "host/engine/DspCommand.hpp"
#include "host/graph/HostIoNodes.hpp"
#include "host/graph/HostServices.hpp"
#include "host/graph/SystemNodes.hpp"
#include "host/graph/SystemTable.hpp"

namespace {

// The table before the first plan. At namespace scope, not function-local: currentTable() may first run on
// the audio thread, which must not take a static's init guard or register its destructor.
const rp::host::SystemTable kNoSystems;

// `mode` is one the caller has checked: 0 to 3, so it fits the command's byte.
DspCommand syncTapCommand(bool arm, SystemId id, std::uint32_t mode) {
    DspCommand c;
    c.kind = DspCommand::Kind::SyncTap;
    c.syncTap = { arm, static_cast<std::uint8_t>(mode), static_cast<std::uint32_t>(id) };
    return c;
}

// The graph before the first document TS sends: the host's stereo pair, silent.
constexpr const char* kEmptyDocument =
    R"({"nodes":[{"id":"out","type":"rp.host.audio-out","settings":{"buses":[1,1]}}],"wires":[]})";

// A system node is a node of one of the two system packages: every type in them adapts a core.
bool isSystemNodeType(const std::string& type) {
    for (const rp_package* p : {rp::host::sameboyPackage(), rp::host::mesenPackage()})
        for (std::uint32_t k = 0; k < p->node_type_count; ++k)
            if (type == p->node_types[k]->id) return true;
    return false;
}

// The id a system node's settings name its core by, {"core": <id>}; nullopt for anything else.
std::optional<SystemId> coreIdOf(const rp::graph::NodeDoc& n) {
    if (!n.settings) return std::nullopt;
    const auto object = n.settings->to_object();
    if (!object || object->size() != 1) return std::nullopt;
    const auto core = object->get("core");
    if (!core) return std::nullopt;
    const auto id = core->to_int64();
    if (!id || *id <= 0 || *id > static_cast<std::int64_t>(UINT32_MAX)) return std::nullopt;
    return static_cast<SystemId>(*id);
}

// The channels an rp.host.audio-out writes when its settings name no buses: its default buses, {2, 2, 2, 2}
// (AudioOutSettings in graph/HostNodes.cpp).
constexpr std::uint32_t kDefaultHostWidth = 8;

// The host channels a document's rp.host.audio-out writes: the sum of its buses.
std::uint32_t hostWidthOf(const rp::graph::GraphDocument& doc) {
    for (const auto& n : doc.nodes) {
        if (n.type != "rp.host.audio-out") continue;
        if (!n.settings) return kDefaultHostWidth;
        const auto object = n.settings->to_object();
        if (!object) return 0;
        const auto buses = object->get("buses");
        if (!buses) return kDefaultHostWidth;
        const auto array = buses->to_array();
        if (!array) return 0;
        std::uint32_t width = 0;
        for (const auto& b : *array)
            if (const auto v = b.to_int()) width += static_cast<std::uint32_t>(*v);
        return width;
    }
    return 0;
}

// The embedded script-node bundle (src/script/bundle.ts) every Engine loads unless it is built with another.
std::span<const rp::host::ScriptHost::Chunk> embeddedScripts() {
    static const rp::host::ScriptHost::Chunk chunks[] = {rp::host::ScriptHost::embeddedBundle()};
    return chunks;
}

} // namespace

// The Engine's graph: the script host, the node packages and the GraphHost running the plan. Destroyed first (graph_ is
// the Engine's last member), and the host (whose system nodes call cores->released from their destructors)
// before the core handoff and the engine services (the host I/O, the sync trace) its nodes call into, and before the
// script host its script nodes release their instances through.
struct Engine::Graph {
    // First, so it goes last: the script nodes the host destroys hand their instances back to it.
    rp::host::ScriptHost                   script;
    rp::graph::NodeRegistry                registry;
    rp::graph::ResourceTable               resources;
    rp::host::HostCores                    cores{};
    rp::host::EngineHostIo                 hostIo{};
    rp::host::EngineSyncTrace              syncTrace{};
    std::unique_ptr<rp::graph::GraphHost>  host;
    std::uint32_t                          maxFrames = kDefaultMaxBlockFrames;
};

Engine::Engine(double sampleRate) : Engine(sampleRate, embeddedScripts()) {}

Engine::Engine(double sampleRate, std::span<const rp::host::ScriptHost::Chunk> scriptBundle)
    : sampleRate_(sampleRate),
      pendingMidi_(rp::host::kHostMidiInMessages, rp::host::kHostMidiInBytes),
      pendingControllerMidi_(rp::host::kControllerInMessages, rp::host::kControllerInBytes) {
    graph_ = std::make_unique<Graph>();
    // The script node types first: the bundle runs in both of the script host's contexts, and its definitions become
    // one more package. One that will not load registers no type, so a document naming one is refused (unknown-type).
    std::string why;
    if (!graph_->script.load(scriptBundle, &why))
        std::fprintf(stderr, "[engine] the script-node bundle did not load (%s); no script node type is registered\n", why.c_str());
    // The DSP kernel is the script package's rp.dsp.kernel (src/script/kernelNode.ts).
    for (const rp_package* p : {rp::graph::corePackage(), rp::graph::hostPackage(), rp::host::sameboyPackage(),
                                rp::host::mesenPackage(), rp::host::hostIoPackage(), rp::host::n8Package()})
        graph_->registry.addPackage(p);
    if (!graph_->registry.addPackage(graph_->script.package(), &why))
        std::fprintf(stderr, "[engine] the script node types were refused (%s); none is registered\n", why.c_str());
    // rp.host.cores/1, whose tokens are system ids here: buildCore parks a core under its id, and a system node's
    // settings name it as {"core": <id>}.
    graph_->cores = {this, [](void* ctx, std::uint64_t id) { return static_cast<Engine*>(ctx)->takeCore(id); },
                     [](void* ctx, SystemBase* core) {
                         auto* self = static_cast<Engine*>(ctx);
                         const SystemId id = core->id();
                         // A node of a plan setGraph is refusing, giving back the core it took in that call:
                         // parked again, still live, its snapshot slot kept, for a corrected document to place.
                         if (self->inSetGraph_ && self->takenInSetGraph_.erase(id) != 0) {
                             self->parked_[id].reset(core);
                             return true;
                         }
                         self->registry_.release(id);
                         self->liveCores_.erase(id);
                         return false;
                     }};
    // rp.engine.host-io/1: the staging the hosts fill and the buffers they drain, on the block thread.
    graph_->hostIo = {
        this,
        [](void* ctx, rp_queue* out) { static_cast<Engine*>(ctx)->pendingMidi_.takeSortedInto(*out); },
        [](void* ctx, rp_queue* out) { static_cast<Engine*>(ctx)->pendingControllerMidi_.takeInto(*out); },
        [](void* ctx) { return static_cast<Engine*>(ctx)->controllerConnected_; },
        [](void* ctx, std::uint32_t system, std::uint32_t frame, const std::uint8_t* data, std::size_t size) {
            static_cast<Engine*>(ctx)->midiOut_.push_back({system, frame, std::vector<std::uint8_t>(data, data + size)});
        },
        [](void* ctx, const std::uint8_t* data, std::size_t size) {
            static_cast<Engine*>(ctx)->controllerOut_.emplace_back(data, data + size);
        },
        [](void* ctx, std::uint32_t frame, const std::uint8_t* data, std::size_t size, bool flush) {
            if (const auto& sink = static_cast<Engine*>(ctx)->coreByteSink_) sink(frame, data, size, flush);
        },
        [](void* ctx) { static_cast<Engine*>(ctx)->hostIoDrops_.fetch_add(1, std::memory_order_relaxed); }};
    graph_->syncTrace = {this, [](void* ctx, std::uint32_t frame, const std::uint8_t* data, std::size_t size) {
                             auto* self = static_cast<Engine*>(ctx);
                             if (!self->blockTracing_) return;
                             for (std::size_t i = 0; i < size; ++i) self->syncTrace_.byte(frame, data[i]);
                         }};
    rp::graph::HostConfig config;
    config.sampleRate = sampleRate;
    config.maxFrames = graph_->maxFrames;
    config.interfaces = &Engine::graphInterface;
    config.interfacesCtx = this;
    graph_->host = std::make_unique<rp::graph::GraphHost>(graph_->registry, graph_->resources, config);
    graph_->host->setHostCommandHandler(&Engine::onHostCommand, this);
    // The plan every engine starts from. Refused, nothing runs until a document TS sends plans: logged, so a node
    // type renamed in its package cannot leave an engine silent without a word.
    if (const GraphResult empty = setGraph({kEmptyDocument, {}}); !empty.ok)
        for (const auto& e : empty.errors)
            std::fprintf(stderr, "[engine] the empty graph was refused (%s) at %s%s%s: %s\n", e.code.c_str(),
                         e.nodeId.c_str(), e.port.empty() ? "" : ".", e.port.c_str(), e.message.c_str());
}

Engine::~Engine() = default;

const void* Engine::graphInterface(void* ctx, const char* id) {
    auto* self = static_cast<Engine*>(ctx);
    if (std::strcmp(id, RP_HOST_CORES_INTERFACE) == 0) return &self->graph_->cores;
    if (std::strcmp(id, RP_ENGINE_HOST_IO_INTERFACE) == 0) return &self->graph_->hostIo;
    if (std::strcmp(id, RP_ENGINE_SYNC_TRACE_INTERFACE) == 0) return &self->graph_->syncTrace;
    return nullptr;
}

bool Engine::pushCommand(const DspCommand& c) { return graph_->host->pushHostCommand(&c, sizeof c); }

// rp.host.cores/1's take: the core parked under the system id a node's settings name.
SystemBase* Engine::takeCore(std::uint64_t id) {
    if (id > UINT32_MAX) return nullptr;
    const auto it = parked_.find(static_cast<SystemId>(id));
    if (it == parked_.end()) return nullptr;
    SystemBase* core = it->second.release();
    parked_.erase(it);
    if (inSetGraph_) takenInSetGraph_.insert(core->id());
    return core;
}

bool Engine::parkCore(std::unique_ptr<SystemBase> core) {
    const SystemId id = core->id();
    if (id == 0) {  // the snapshot registry's free-slot id
        std::fprintf(stderr, "[engine] a core was built under id 0, which is no system id; it is dropped\n");
        return false;
    }
    // Checked before the claim: the registry finds a slot by id, first match, so a second slot under a live
    // id, released again, would free the live core's.
    if (coreLive(id)) {
        std::fprintf(stderr, "[engine] a core was built under id %u, which is still live; it is dropped\n", static_cast<unsigned>(id));
        return false;
    }
    if (!registry_.claim(id, *core)) return false;  // pool full
    liveCores_[id] = core.get();
    parked_[id] = std::move(core);
    return true;
}

Engine::GraphResult Engine::setGraph(const GraphRequest& request) {
    GraphResult out;
    auto refuse = [&](const std::string& node, const char* code, std::string message) {
        out.errors.push_back({node, "", code, std::move(message)});
        return out;
    };
    // The cores of plans the audio thread has handed back are freed first, so the checks below see them gone.
    graph_->host->reclaim();
    graph_->script.reclaim();
    auto doc = rp::graph::parseGraphDocument(request.document);
    if (!doc) return refuse("", "document", std::string("not a graph document: ") + doc.error().what());

    // The system nodes and the slots name the same systems, each node as sys-<id> with {"core": <id>}.
    std::vector<std::pair<SystemId, const rp::graph::NodeDoc*>> systemNodes;
    std::vector<SystemId> named;
    for (const auto& n : doc->nodes) {
        if (!isSystemNodeType(n.type)) continue;
        const auto id = coreIdOf(n);
        if (!id || n.id != "sys-" + std::to_string(*id))
            return refuse(n.id, "slots", "a system node is sys-<id>, its settings {\"core\": <id>}");
        systemNodes.emplace_back(*id, &n);
        named.push_back(*id);
    }
    std::vector<SystemId> slots = request.slots;
    std::sort(named.begin(), named.end());
    std::sort(slots.begin(), slots.end());
    if (std::adjacent_find(slots.begin(), slots.end()) != slots.end() || slots != named)
        return refuse("", "slots", "the slots and the document's system nodes name different systems");

    // Each names a live core, and is the type that core is placed as: a node of another type would take the
    // core, find it the wrong kind, and give it up.
    for (const auto& [id, n] : systemNodes) {
        const auto it = liveCores_.find(id);
        if (it == liveCores_.end())
            return refuse(n->id, "unknown-core", "no core is built under id " + std::to_string(id));
        const char* type = rp::host::systemNodeType(it->second->kind());
        if (n->type != type)
            return refuse(n->id, "core-type", "the core under id " + std::to_string(id) + " is placed as " + type +
                                                  ", not " + n->type);
    }

    // The tap arms one of the NES's export modes (1 to 3), on one of the document's systems.
    if (request.rearmTap && request.arm) {
        if (request.armMode < 1 || request.armMode > 3)
            return refuse("", "tap", "the tap arms mode 1, 2 or 3, not " + std::to_string(request.armMode));
        if (std::find(request.slots.begin(), request.slots.end(), request.armId) == request.slots.end())
            return refuse("", "tap", "the tap arms system " + std::to_string(request.armId) + ", which is not in the slots");
    }

    auto table = std::make_shared<rp::host::SystemTable>();
    for (const SystemId id : request.slots) {
        SystemBase* core = liveCores_.at(id);
        table->slots.push_back({id, core, dynamic_cast<MesenNesSystem*>(core)});
    }
    // The host inputs, one of each at most: each takes everything staged, wired or not, so a second would take it from
    // the first whenever it ran first. And the one system a physical N8 can mirror, of the document's.
    bool takesHostMidi = false, takesControllerMidi = false;
    for (const auto& n : doc->nodes) {
        if (n.type == "rp.host.midi-in") {
            if (takesHostMidi)
                return refuse(n.id, "midi-in", "a document has one rp.host.midi-in at most: it takes all the staged host MIDI");
            takesHostMidi = true;
        }
        if (n.type == "rp.host.controller-in") {
            if (takesControllerMidi)
                return refuse(n.id, "controller-in",
                              "a document has one rp.host.controller-in at most: it takes all the staged control-surface MIDI");
            takesControllerMidi = true;
        }
        if (n.type != "rp.n8.cart-mirror") continue;
        if (table->cartMirror != 0)
            return refuse(n.id, "cart-mirror", "a document has one rp.n8.cart-mirror at most: there is one cart to mirror to");
        // Read as the node reads its settings, so the two accept the same ones.
        const auto id = rp::host::cartMirrorSystem(n.settings ? rp::graph::canonicalJson(*n.settings) : std::string("{}"));
        if (!id || std::find(request.slots.begin(), request.slots.end(), *id) == request.slots.end())
            return refuse(n.id, "cart-mirror", "a cart mirror's settings are {\"system\": <id>}, naming one of the slots");
        if (liveCores_.at(*id)->kind() != SystemKind::MesenNes)
            return refuse(n.id, "cart-mirror", "a cart mirror names a NES, the one system an N8 is a cart for: system " +
                                                   std::to_string(*id) + " is placed as " +
                                                   rp::host::systemNodeType(liveCores_.at(*id)->kind()));
        table->cartMirror = *id;
    }

    rp::graph::HostCommand trailer{};
    if (request.rearmTap) {
        const DspCommand c = syncTapCommand(request.arm, request.armId, request.arm ? request.armMode : 0);
        std::memcpy(trailer.bytes, &c, sizeof c);
    }
    // The nodes the planner makes take their cores out of parked_. If the plan is then refused (a later node
    // fails to activate, the ring has no room for its swap), those nodes are destroyed before the host's
    // setGraph returns, and each puts its core back (the cores lambda). A core only an older plan held, freed
    // by a reclaim in there, was not taken here, so it is freed as usual.
    struct InSetGraph {
        Engine& e;
        explicit InSetGraph(Engine& engine) : e(engine) {
            e.takenInSetGraph_.clear();
            e.inSetGraph_ = true;
        }
        ~InSetGraph() {
            e.inSetGraph_ = false;
            e.takenInSetGraph_.clear();
        }
    };
    rp::graph::SetGraphResult result;
    {
        const InSetGraph window(*this);
        result = graph_->host->setGraph(*doc, std::move(table), request.rearmTap ? &trailer : nullptr);
    }
    graph_->script.reclaim();  // a quiescent swap released its script nodes in there: what they left in cycles goes now
    for (const auto& e : result.errors) out.errors.push_back({e.nodeId, e.port, e.code, e.message});
    if (!result.ok) return out;
    out.ok = true;
    hostWidth_ = hostWidthOf(*doc);
    // Each node the document names took its core when it was activated: a core still parked is one no
    // document placed.
    for (auto& [id, core] : parked_) {
        registry_.release(id);
        liveCores_.erase(id);
    }
    parked_.clear();
    return out;
}

void Engine::drainCommands() { graph_->host->drain(); }

// The script host knows first that an audio thread will run blocks, and last that it has stopped: between the two, a
// destroyed script node's release waits for the block thread.
void Engine::setAudioThreadOwns(bool owns) {
    if (owns) graph_->script.setAudioThreadOwns(true);
    graph_->host->setAudioThreadOwns(owns);
    if (!owns) graph_->script.setAudioThreadOwns(false);
}

std::size_t Engine::reclaim() {
    const std::size_t n = graph_->host->reclaim();
    graph_->script.reclaim();  // the script instances the block thread released since, and the first throw, once
    // The block thread only counts a block too long to run (it must not log); say so here, once per new one.
    if (const auto seen = oversizedBlocks_.load(std::memory_order_acquire); seen != oversizedReported_) {
        std::fprintf(stderr, "[engine] %u block(s) longer than the plan's %u frames rendered silence (the last was %u frames)\n",
                     static_cast<unsigned>(seen - oversizedReported_), static_cast<unsigned>(graph_->maxFrames),
                     static_cast<unsigned>(oversizedFrames_.load(std::memory_order_relaxed)));
        oversizedReported_ = seen;
    }
    // A host output node's drops, named the first time only: a document that sends one may send one every block.
    if (!hostIoDropsReported_ && hostIoDrops_.load(std::memory_order_relaxed) != 0) {
        std::fprintf(stderr, "[engine] rp.host.midi-out has dropped a message too short to carry its system id "
                             "(Engine::droppedMessages counts each)\n");
        hostIoDropsReported_ = true;
    }
    return n;
}

void Engine::discardPending() { graph_->host->discardPending(); }

std::uint32_t Engine::faultCount() const { return graph_->host->faultCount(); }

std::uint32_t Engine::droppedMessages() const {
    return graph_->host->droppedMessages() + hostIoDrops_.load(std::memory_order_relaxed);
}

std::uint32_t Engine::scriptThrows() const { return graph_->script.throwCount(); }

rp::host::DspAllocStats Engine::dspAllocStats() const { return graph_->script.allocStats(); }
bool Engine::resetDspAllocStats(bool disableAutoGc) { return graph_->script.resetAllocStats(disableAutoGc); }
rp::host::DspGcResult Engine::dspRunGc() { return graph_->script.runGc(); }
bool Engine::dspTraceReset(bool arm) { return graph_->script.traceReset(arm); }
std::vector<rp::host::DspTraceSpan> Engine::dspTrace() const { return graph_->script.traceSpans(); }
std::vector<std::string> Engine::dspTraceNames() const { return graph_->script.traceNames(); }

void Engine::setMaxBlockFrames(std::uint32_t frames) {
    if (frames <= graph_->maxFrames) return;
    std::string why;
    if (graph_->host->reactivate(sampleRate_, frames, &why)) graph_->maxFrames = frames;
    else std::fprintf(stderr, "[Engine] could not grow the block size to %u: %s\n", static_cast<unsigned>(frames), why.c_str());
}

void Engine::setSampleRate(double sr) {
    sampleRate_ = sr;
    // Every live core re-rated in place: the running plan's in slot order, then the parked ones a later
    // document places; then the plan's buffers at the new rate. Quiescent, so the running plan is the one
    // last published.
    for (const auto& e : currentTable().slots) e.core->onSampleRateChanged(sr);
    for (const auto& [id, core] : parked_) core->onSampleRateChanged(sr);
    std::string why;
    if (!graph_->host->reactivate(sr, graph_->maxFrames, &why))
        std::fprintf(stderr, "[Engine] could not re-activate the graph at %g Hz: %s\n", sr, why.c_str());
}

const rp::host::SystemTable& Engine::currentTable() const {
    const auto* t = static_cast<const rp::host::SystemTable*>(graph_->host->hostData());
    return t ? *t : kNoSystems;
}

void Engine::onHostCommand(void* ctx, const rp::graph::HostCommand& cmd, const void* hostData, bool discard) {
    static_assert(sizeof(DspCommand) <= rp::graph::kHostCommandBytes, "a DspCommand rides the ring as a HostCommand");
    static_assert(std::is_trivially_copyable_v<DspCommand>, "a DspCommand is copied as bytes");
    DspCommand c;
    std::memcpy(&c, cmd.bytes, sizeof c);
    if (!discard) {
        // Against the table it was handed, the plan current at this point of the drain, never through
        // currentTable(): a handler must not call back into the GraphHost.
        const auto* table = static_cast<const rp::host::SystemTable*>(hostData);
        static_cast<Engine*>(ctx)->applyCommand(c, table ? *table : kNoSystems);
        return;
    }
    switch (c.kind) {  // dropped at teardown (QueuedInvoker::freePending): free what it owns, never apply it
        case DspCommand::Kind::WriteRam:        delete c.writeRam.bytes; break;
        case DspCommand::Kind::StageMidi:       delete c.stageMidi.ext; break;
        case DspCommand::Kind::StageSystemMidi: delete c.stageSystemMidi.ext; break;
        default: break;
    }
}

// Every kind that rides the ring, SyncTap included, a per-system one aimed at the core `table` names. Structure
// never arrives here: TS sends it as graph documents.
void Engine::applyCommand(DspCommand& cmd, const rp::host::SystemTable& table) {
    auto core = [&](std::uint32_t id) -> SystemBase* {
        const auto* e = table.find(id);
        return e ? e->core : nullptr;
    };
    switch (cmd.kind) {
        case DspCommand::Kind::StageMidi:
            if (cmd.stageMidi.ext) {
                stageMidi(cmd.stageMidi.frame, *cmd.stageMidi.ext);
                delete cmd.stageMidi.ext;
            } else {
                stageMidi(cmd.stageMidi.frame, std::span<const std::uint8_t>(cmd.stageMidi.data, cmd.stageMidi.len));
            }
            break;
        case DspCommand::Kind::StageControllerMidi:
            stageControllerMidi(std::span<const std::uint8_t>(cmd.stageMidi.data, cmd.stageMidi.len));
            break;
        case DspCommand::Kind::StageSystemMidi: {
            // Whether the cart mirrors this system: the rp.n8.cart-mirror of this plan's table, judged like the core
            // lookup against the plan current at this point of the drain (a handler must not read currentTable()).
            const bool toCart = coreByteSink_ && cmd.stageSystemMidi.id != 0 && cmd.stageSystemMidi.id == table.cartMirror;
            if (cmd.stageSystemMidi.ext) {
                stageSystemMidi(core(cmd.stageSystemMidi.id), cmd.stageSystemMidi.ext->data(), cmd.stageSystemMidi.ext->size(), toCart);
                delete cmd.stageSystemMidi.ext;
            } else {
                stageSystemMidi(core(cmd.stageSystemMidi.id), cmd.stageSystemMidi.data, cmd.stageSystemMidi.len, toCart);
            }
            break;
        }
        case DspCommand::Kind::SetBpm:          setBpm(cmd.setBpm.value); break;
        case DspCommand::Kind::SetTransport:    setTransport(cmd.setTransport.value); break;
        case DspCommand::Kind::SetPpq:          setPpq(cmd.setPpq.value); break;
        case DspCommand::Kind::SetConfigField:
            applyConfigField(core(cmd.setConfigField.id), cmd.setConfigField.field, cmd.setConfigField.value);
            break;
        case DspCommand::Kind::PressButton:
            if (SystemBase* sys = core(cmd.pressButton.id))
                sys->pressButton(cmd.pressButton.button, cmd.pressButton.down);  // spread across the block in onProcess
            break;
        case DspCommand::Kind::WriteRam:
            writeRam(core(cmd.writeRam.id), cmd.writeRam.offset, *cmd.writeRam.bytes);
            delete cmd.writeRam.bytes;
            break;
        case DspCommand::Kind::SyncTap:
            syncTap(cmd.syncTap.arm, cmd.syncTap.id, cmd.syncTap.mode, table);
            break;
        default:
            break;
    }
}

// The NES tap arming at every structural change, against `table`, the plan just adopted (design section 3.2):
// restore whatever was armed, then arm `id` to `mode` if `arm`. It runs even when the wanted mode is
// unchanged, and each run resets the capture streams. A core that has gone (removed, or replaced under
// its id or a new one) is not restored, and its record is dropped.
void Engine::syncTap(bool arm, SystemId id, std::uint32_t mode, const rp::host::SystemTable& table) {
    if (armedTap_.active) {
        if (const auto* e = table.find(armedTap_.id); e && e->nes == armedTap_.core)
            e->nes->setChannelExportMode(armedTap_.prevMode);
        armedTap_ = {};
    }
    if (!arm) return;
    const auto* e = table.find(id);
    MesenNesSystem* nes = e ? e->nes : nullptr;
    if (!nes) return;
    if (nes->channelExportMode() != mode) {
        armedTap_ = { true, nes->id(), nes, nes->channelExportMode() };
        nes->setChannelExportMode(mode);
    }
}

std::size_t Engine::systemCount() const { return currentTable().slots.size(); }

SystemBase* Engine::findSystem(SystemId id) {
    const auto* e = currentTable().find(id);
    return e ? e->core : nullptr;
}

Engine::Staging::Staging(std::size_t messageRoom, std::size_t byteRoom) {
    messages.reserve(messageRoom);
    bytes.reserve(byteRoom);
}

void Engine::Staging::add(std::uint32_t frame, std::span<const std::uint8_t> data) {
    messages.push_back({frame, static_cast<std::uint32_t>(bytes.size()), static_cast<std::uint32_t>(data.size())});
    bytes.insert(bytes.end(), data.begin(), data.end());
}

// An insertion sort: stable, and in place, swapping the messages' frames and places in the arena (their bytes stay
// where they are); quadratic, but it allocates nothing.
void Engine::Staging::sortByFrame() {
    for (std::size_t i = 1; i < messages.size(); ++i)
        for (std::size_t j = i; j > 0 && messages[j].frame < messages[j - 1].frame; --j) std::swap(messages[j], messages[j - 1]);
}

void Engine::Staging::push(rp_queue& out, const Message& m) const {
    rp::graph::pushMessage(out, m.frame, std::span<const std::uint8_t>(bytes.data() + m.offset, m.size));
}

void Engine::Staging::takeInto(rp_queue& out) {
    for (const Message& m : messages) push(out, m);
    clear();
}

// What the plugin stages is two runs, each in frame order: a block's DAW events, then each moved automation slot's CC
// at frame 0 (up to 640 of them on a block that resends every slot). Those are merged straight onto the queue, a tie
// going to the earlier run, so the work is linear and nothing moves. More than two runs (a host staging out of order)
// are sorted in place first (sortByFrame). Neither allocates.
void Engine::Staging::takeSortedInto(rp_queue& out) {
    std::size_t second = 0;  // where the second run starts; 0 while there is one
    for (std::size_t i = 1; i < messages.size(); ++i) {
        if (messages[i].frame >= messages[i - 1].frame) continue;
        if (second != 0) {  // a third run
            sortByFrame();
            takeInto(out);
            return;
        }
        second = i;
    }
    if (second == 0) {
        takeInto(out);
        return;
    }
    const std::size_t end = messages.size();
    for (std::size_t a = 0, b = second; a < second || b < end;) {
        const bool later = b < end && (a == second || messages[b].frame < messages[a].frame);
        push(out, messages[later ? b++ : a++]);
    }
    clear();
}

void Engine::Staging::clear() {
    messages.clear();
    bytes.clear();
}

void Engine::stageMidi(std::uint32_t frame, std::span<const std::uint8_t> bytes) { pendingMidi_.add(frame, bytes); }

void Engine::stageControllerMidi(std::span<const std::uint8_t> bytes) {
    // Frame 0: a control surface's timing is not musical. A pad press is quantised by the controller app
    // against the block clock, not placed by sample, so an intra-block offset would carry no information.
    pendingControllerMidi_.add(0, bytes);
}

void Engine::setBpm(double bpm) { bpm_ = bpm; }
void Engine::setTransport(bool playing) { transport_ = playing; }
void Engine::setPpq(double ppq) { ppq_ = ppq < 0.0 ? 0.0 : ppq; }

SystemId Engine::cartMirrorSystemId() const { return currentTable().cartMirror; }

// The graph TS published decides who goes where: it built it for the host's outputs.
void Engine::processBlock(std::uint32_t frames, float* const* outputs, std::size_t numOutputs) {
    // What the control thread's destroyed script nodes left to release, before any of the block's JS runs.
    graph_->script.beginBlock();
    // An empty block runs nothing, and the tail must not either (no publish, no ppq advance). There are no frames
    // to silence.
    if (frames == 0) return;
    // Longer than the plan's buffers: the GraphHost would refuse it, so nothing runs, and the tail must not
    // either (no publish, no ppq advance). A host that reported its block size (setMaxBlockFrames) never gets
    // here; one that did not is counted, for reclaim() to report on the control thread. What was staged for the
    // block goes with it, as a block that ran would leave none of it: no input node took it, and a host whose
    // every block is too long would otherwise grow the staging without end. A host that reaches this loses that
    // block's staged MIDI: its note-offs, and the plugin's automation CCs, which it does not resend until the value
    // changes. A host avoids it by calling setMaxBlockFrames with its buffer size, as the plugin's activate() does.
    if (frames > graph_->maxFrames) {
        for (std::size_t c = 0; c < numOutputs; ++c)
            if (outputs[c]) std::memset(outputs[c], 0, frames * sizeof(float));
        pendingMidi_.clear();
        pendingControllerMidi_.clear();
        oversizedFrames_.store(frames, std::memory_order_relaxed);
        oversizedBlocks_.fetch_add(1, std::memory_order_release);
        return;
    }
    blockTracing_ = syncTrace_.beginBlock(frames, sampleRate_, bpm_, ppq_, transport_);
    // The block's host outputs start empty here, and the plan's rp.host.midi-out / controller-out append to them.
    midiOut_.clear();
    controllerOut_.clear();
    const rp_transport transport{sampleRate_, bpm_, ppq_, transport_ ? std::uint32_t{RP_TRANSPORT_PLAYING} : 0u};
#ifdef RETROPLUG_PROFILE
    rp::host::ScriptHost& script = graph_->script;
    const double planStart = script.traceNow();
#endif
    // The host inputs, the kernel, every system, the mixing and the host outputs (the host drained before this).
    graph_->host->run(frames, transport, outputs, static_cast<std::uint32_t>(numOutputs));
#ifdef RETROPLUG_PROFILE
    // apu-render: the plan from where its script nodes end, which in every document TS builds is the kernel node,
    // run before the systems it feeds.
    script.spanSince(rp::host::DSP_SPAN_APU, std::max(planStart, script.lastProcessEnd()));
#endif
    // The promise jobs the block's script nodes queued, then (a profile build) the allocation window's block counts.
    graph_->script.endBlock();
    // What was staged for this block and no node of the plan took (a plan with no rp.host.midi-in or controller-in)
    // is dropped, not carried into the next block.
    pendingMidi_.clear();
    pendingControllerMidi_.clear();
    const rp::host::SystemTable& table = currentTable();
#ifdef RETROPLUG_PROFILE
    script.spanBegin(rp::host::DSP_SPAN_PUBLISH);
#endif
    for (const auto& e : table.slots) registry_.publishSystem(*e.core, frames, sampleRate_);
#ifdef RETROPLUG_PROFILE
    script.spanEnd();  // state-publish
#endif
    if (blockTracing_) syncTrace_.endBlock();
    if (transport_)
        ppq_ += (bpm_ / 60.0) * (static_cast<double>(frames) / sampleRate_);
}

void Engine::processBlock(std::uint32_t frames, float* outL, float* outR) {
    float* outs[2] = { outL, outR };
    processBlock(frames, outs, 2);
}

std::optional<std::vector<std::uint8_t>> Engine::readState(SystemId id) {
    return registry_.readState(id);   // the owned published copy, never the live core
}

std::optional<std::vector<std::uint8_t>> Engine::readSram(SystemId id) {
    return registry_.readSram(id);    // SRAM sliced from the published savestate, not a live read
}

std::optional<std::vector<std::uint8_t>> Engine::readRam(SystemId id) {
    return registry_.readRam(id);     // the owned per-block WRAM copy, never the live core
}

std::size_t Engine::ramSize(SystemId id) {
    return registry_.ramSize(id);     // published copy, like readRam - no live core
}

bool Engine::writeRam(SystemBase* sys, std::uint32_t offset, const std::vector<std::uint8_t>& bytes) {
    if (bytes.empty() || !sys) return false;
    // ReadWrite, so a backend with a read-only RAM view reports failure rather than silently dropping
    // the poke into a copy nobody reads.
    auto acc = sys->getMemory(rp::MemoryType::Ram, rp::AccessType::ReadWrite);
    if (!acc.valid()) return false;
    // Bounds are the ONE thing this refuses: a short write past the end would corrupt whatever the
    // core keeps after its RAM region, which is a crash rather than the "confuse the ROM" the caller
    // signed up for. Partial writes are refused too - half a song block is not a song.
    if (static_cast<std::size_t>(offset) + bytes.size() > acc.size()) return false;
    std::memcpy(acc.data() + offset, bytes.data(), bytes.size());
    return true;
}

bool Engine::screenshot(SystemId id, const std::string& path) {
    // Encode the owned registry frame (a published copy) — no findSystem walk, no live-core read, so
    // it's safe while the audio thread plays. false until the core has rendered its first frame.
    const SnapshotRegistry::Frame f = registry_.readFrame(id);
    // Distinguish the two failure modes so a false is diagnosable: a caller wanting liveness should query
    // getFrame(id).published (no file I/O); screenshot() is a file-writing action whose bool means "wrote".
    if (!f.published) {
        std::fprintf(stderr, "[Engine] screenshot: system %u has not published a frame yet\n",
                     static_cast<unsigned>(id));
        return false;
    }
    const std::size_t pixels = static_cast<std::size_t>(f.width) * f.height;
    // The frame data is XRGB8888 (little-endian B,G,R,X) → transcode to RGB24 for lodepng.
    std::vector<unsigned char> rgb(pixels * 3);
    const std::uint8_t* src = f.data.data();
    for (std::size_t i = 0; i < pixels; ++i) {
        rgb[i * 3 + 0] = src[i * 4 + 2]; // R
        rgb[i * 3 + 1] = src[i * 4 + 1]; // G
        rgb[i * 3 + 2] = src[i * 4 + 0]; // B
    }
    const unsigned err = lodepng_encode24_file(path.c_str(), rgb.data(), f.width, f.height);
    if (err) {
        std::fprintf(stderr, "[Engine] screenshot: failed to write '%s': %s\n",
                     path.c_str(), lodepng_error_text(err));
        return false;
    }
    return true;
}

EngineFrame Engine::getFrame(SystemId id) {
    // The owned frame copy, by id — no findSystem, no live-core deref. width/height are 0 (published
    // false) for an unknown system; set with published false for a claimed core that hasn't rendered.
    SnapshotRegistry::Frame f = registry_.readFrame(id);
    EngineFrame out;
    out.width = f.width;
    out.height = f.height;
    out.published = f.published;
    out.data = std::move(f.data);   // raw XRGB8888, the Canvas's native format
    return out;
}

bool Engine::stageSystemMidi(SystemBase* sys, const std::uint8_t* bytes, std::size_t len, bool toCart) {
    if (!bytes || len == 0 || !sys) return false;
    // The cart mirrors this system (the plan's rp.n8.cart-mirror): it gets the message too, or a setting changed from the UI would
    // reach the emulated console and not the real one. (Drained on the audio thread, as the cart mirror node calls it.)
    if (toCart) coreByteSink_(0, bytes, len, false);
    // Frame 0: a knob has no musical position, exactly as stageControllerMidi argues for a pad press.
    //
    // pushCoreBytes, not onMidi, and the difference is load-bearing: onMidi is the MIDI-FRAMED door, and the
    // NES role's implementation clamps every event to MidiEvent::kDataSize and ignores dataExt
    // (NesN8FifoRole.cpp), so a SysEx through it arrives as its first four bytes and the cart waits forever
    // for an F7 that was thrown away. pushCoreBytes is the raw door the DSP kernel's bytes reach a system
    // through (the system node's bytes input), with no framing and no cap - which is what a rig message
    // needs. A system with no raw ingress falls back to the framed one, where a channel message still fits.
    if (sys->hasCoreBytesIn()) {
        // flush=false on purpose: flush DISCARDS whatever the device still holds, which for the N8 FIFO is
        // the performance queued behind this. A settings message is not a barrier - it joins the stream.
        sys->pushCoreBytes(0, bytes, len, false);
        return true;
    }
    if (len > ::MidiEvent::kDataSize) return false;
    ::MidiEvent ev;
    ev.frame = 0;
    ev.size = static_cast<std::uint32_t>(len);
    for (std::size_t i = 0; i < len; ++i) ev.data[i] = bytes[i];
    sys->onMidi(&ev, 1);
    return true;
}

void Engine::applyConfigField(SystemBase* sys, std::uint8_t field, double value) {
    if (!sys) return;

    // The two UNIVERSAL settings apply to every backend through the base interface — they must run
    // before the SameBoy-only cast below, or they silently no-op on a Mesen (NES/GBA) system even
    // though its gain slider / reload toggle look live in the UI.
    switch (static_cast<ConfigField>(field)) {
        case ConfigField::Gain:
            sys->setGainDb(static_cast<float>(value));  // live, smoothed
            return;
        case ConfigField::ReloadOnRomChange:
            sys->setRomReload(value != 0.0);  // a UI-thread ROM-watch flag; no core effect
            return;
        default:
            break;
    }

    // The remaining fields are backend-specific emulator knobs, dispatched by concrete type (a system
    // is exactly one, so the order of the casts below doesn't matter). Mesen (NES) first.
    if (auto* mn = dynamic_cast<MesenNesSystem*>(sys)) {
        switch (static_cast<ConfigField>(field)) {
            case ConfigField::NesRegion:
                mn->setRegion(static_cast<std::uint32_t>(value));  // value-guarded; resets the core on change
                break;
            case ConfigField::NesRemoveSpriteLimit:
                mn->setRemoveSpriteLimit(value != 0.0);            // live — the PPU re-reads it per scanline
                break;
            case ConfigField::NesApuLatencyMs:
                mn->setApuLatencyMs(value);                        // live — re-thresholds the APU flush window
                break;
            case ConfigField::NesS5bNoise:
                mn->setS5bNoise(static_cast<std::uint32_t>(value));         // live — read per sample
                break;
            case ConfigField::NesMmc5PhaseReset:
                mn->setMmc5PhaseReset(static_cast<std::uint32_t>(value));   // live — read per register write
                break;
            case ConfigField::NesExpansionVolume:
                mn->setExpansionVolume(static_cast<std::uint32_t>(value));  // live — mixer re-reads per buffer
                break;
            default:
                break;
        }
        return;
    }

    // SameBoy emulator knobs (model / highpass / link group / fast boot).
    auto* sb = dynamic_cast<SameBoySystem*>(sys);
    if (!sb) return;
    switch (static_cast<ConfigField>(field)) {
        case ConfigField::Model: {
            const auto m = static_cast<SameBoyModel>(static_cast<std::uint32_t>(value));
            if (sb->config_.model != m) {
                sb->config_.model = m;
                sb->restartEmulator();  // rebuilds gb_, clears the savestate (a new model can't restore an
                                        // old one), and re-arms the state snapshot onto the new model
                // linkPeers_ hold the systems, which survive the restart, so the cable needs no rewiring
            }
            break;
        }
        case ConfigField::Highpass: {
            const auto hp = static_cast<SameBoyHighpass>(static_cast<std::uint32_t>(value));
            if (sb->config_.highpass != hp) {
                sb->config_.highpass = hp;
                sb->applyHighpassMode();  // live — the filter samples its mode every audio frame
            }
            break;
        }
        case ConfigField::LinkGroup: {
            const auto g = static_cast<std::uint8_t>(static_cast<std::uint32_t>(value));
            if (sb->config_.linkGroupId != g) {
                sb->config_.linkGroupId = g;
                // the cable is wiring: TS republishes the graph with the group (graph/buildGraph.ts)
            }
            break;
        }
        case ConfigField::FastBoot:
            sb->setFastBoot(value != 0.0);  // deferred to the next restart
            break;
        case ConfigField::SerialOutCapture: {
            // Live: the capture mode (LSDj MI.OUT or master sync) as its SerialOutCapture value. Out of range is off.
            const auto mode = static_cast<std::uint32_t>(value);
            sb->setSerialOutCapture(mode <= static_cast<std::uint32_t>(SerialOutCapture::Drive) ? static_cast<SerialOutCapture>(mode)
                                                                                              : SerialOutCapture::Off);
            break;
        }
        // The display group. Each is value-guarded like the knobs above (the store re-sends the whole
        // role config on every edit, so an unguarded apply would re-push all five on every keystroke)
        // and then re-pushes the group, which is a handful of cheap core setters.
        case ConfigField::ColorCorrection: {
            const auto cc = static_cast<SameBoyColorCorrection>(static_cast<std::uint32_t>(value));
            if (sb->config_.colorCorrection != cc) {
                sb->config_.colorCorrection = cc;
                sb->applyDisplayConfig();
            }
            break;
        }
        case ConfigField::DmgPalette: {
            const auto p = static_cast<SameBoyDmgPalette>(static_cast<std::uint32_t>(value));
            if (sb->config_.dmgPalette != p) {
                sb->config_.dmgPalette = p;
                sb->applyDisplayConfig();
            }
            break;
        }
        case ConfigField::LightTemperature:
            if (sb->config_.lightTemperature != value) {
                sb->config_.lightTemperature = value;
                sb->applyDisplayConfig();
            }
            break;
        default:
            break;  // the universal fields (Gain / ReloadOnRomChange) were handled above
    }
}
