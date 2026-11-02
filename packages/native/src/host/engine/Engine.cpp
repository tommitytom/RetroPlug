#include "host/engine/Engine.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <utility>

#include "host/engine/OfflineJobs.hpp"  // the pool Engine holds behind a forward declaration
#include "system/ChannelTap.hpp"  // the capability SyncTap arms
#include "system/SystemBase.hpp"
#include "system/SystemCapabilities.hpp"
#include "system/SystemTypes.hpp"   // AudioBlockInfo, SystemId
#include "system/CoreBackends.hpp"  // registerCoreBackends: the engine owns the one factory every build goes through
#include "system/SystemFactory.hpp"
#include "system/InputCapacity.hpp"

#include "transport/FrameBufferTriple.hpp"
#include "native/core/img/png/lodepng.h"

#include "graph/Descriptor.hpp"
#include "graph/GraphHost.hpp"
#include "graph/HostNodes.hpp"
#include "graph/Queue.hpp"
#include "graph/nodes/CoreNodes.hpp"
#include "graph/nodes/DspNodes.hpp"
#include "graph/nodes/DspResampleNodes.hpp"
#include "host/dsp/ScriptCompiler.hpp"
#include "host/devices/DeviceRegistry.hpp"
#include "host/engine/DspCommand.hpp"
#include "host/graph/HostIoNodes.hpp"
#include "host/graph/LaunchpadNodes.hpp"
#include "host/graph/N8Nodes.hpp"
#include "host/graph/HostServices.hpp"
#include "host/graph/SystemNodes.hpp"
#include "host/graph/SystemTable.hpp"

namespace {

// A press queue is sized from the two doors a block can press through: its buttons input and this command ring.
static_assert(rp::kCommandRingCapacity == rp::graph::GraphHost::commandCapacity(),
              "InputCapacity.hpp's command-ring bound must match GraphHost's ring");

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

// The id a system node's settings name its system by: their `system`, the id its core is built under and its snapshot
// slot's key (beside the core's knobs and construct-time fields, which the node reads:
// docs/modular-graph-phase-c2-design.md section 3). The field must be a system id (1 to UINT32_MAX); a node without one is
// left to its own create, which refuses it with a reason naming what it must name.
struct SystemTarget {
    std::optional<SystemId> id;
    bool                    namesAny = false;  // carries `system` at all
};

SystemTarget systemTargetOf(const rp::graph::NodeDoc& n) {
    SystemTarget out;
    if (!n.settings) return out;
    const auto object = n.settings->to_object();
    if (!object) return out;
    const auto systemField = object->get("system");
    out.namesAny = systemField.has_value();
    if (!systemField) return out;
    const auto id = systemField->to_int64();
    if (id && *id > 0 && *id <= static_cast<std::int64_t>(UINT32_MAX)) out.id = static_cast<SystemId>(*id);
    return out;
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

// Whether `document` places a node of one of `package`'s types. A document that does not parse places nothing; one
// published always parses.
bool placesAny(const std::string& document, const rp_package& package) {
    const auto doc = rp::graph::parseGraphDocument(document);
    if (!doc) return false;
    for (const auto& n : doc->nodes)
        for (std::uint32_t k = 0; k < package.node_type_count; ++k)
            if (n.type == package.node_types[k]->id) return true;
    return false;
}

// The planner's errors as one line: the first, where it is when it is at a node (a refusal of the whole document, busy
// among them, is at none), and how many more.
std::string describeErrors(const std::vector<Engine::GraphError>& errors) {
    if (errors.empty()) return "no reason given";
    const auto& e = errors.front();
    std::string text = e.code;
    if (!e.nodeId.empty()) text += " at " + e.nodeId + (e.port.empty() ? "" : "." + e.port);
    text += ": " + e.message;
    if (errors.size() > 1) text += " (and " + std::to_string(errors.size() - 1) + " more)";
    return text;
}

// The embedded script-node bundle (src/script/bundle.ts) every Engine loads unless it is built with another.
std::span<const rp::host::ScriptHost::Chunk> embeddedScripts() {
    static const rp::host::ScriptHost::Chunk chunks[] = {rp::host::ScriptHost::embeddedBundle()};
    return chunks;
}

} // namespace

// The Engine's graph: the script host, the node packages and the GraphHost running the plan. Destroyed first (graph_ is
// the Engine's last member), and with it the host: a system node giving its core up from its destructor calls
// destroySystem while the engine services (the system builder, the host I/O, the sync trace) its nodes call into are
// still there, and before the script host its script nodes release their instances through.
struct Engine::Graph {
    // First, so it goes last: the script nodes the host destroys hand their instances back to it, and its packs' types
    // are the registry's script entries.
    rp::host::ScriptHost                   script;
    // Filled at construction and by loadScriptPack (a pack's package added, or a reload's swapped in, and swapped back
    // when its replan is refused), and a reload's retired entries dropped as the script host frees their generation
    // (its pack-freed callback, from the reclaims), all on the control thread, which is where every plan is made too
    // (setGraph, and reactivate, quiescent), so no change overlaps a find (NodeRegistry's threading rule). Nothing on
    // the audio thread reads it: a plan's instances carry their entries, which only the control thread reads (to
    // destroy a node, as it frees the plan that held it), and an entry never moves, nor is dropped while an instance
    // holds it: a retired generation is freed only once no record of its types' instances is alive, each one's planned
    // instance gone first.
    rp::graph::NodeRegistry                registry;
    rp::graph::ResourceTable               resources;
    rp::host::HostSystems                  systems{};
    rp::host::EngineHostIo                 hostIo{};
    rp::host::EngineSyncTrace              syncTrace{};
    // The links this host's engine hands out by name (rp.host.devices/1), and the struct the nodes read them through.
    // The registry is declared before the GraphHost below, so it outlives the plans whose nodes give their handles back
    // as the graph goes.
    rp::host::DeviceRegistry               devices;
    rp::host::HostDevices                  devicesApi{};
    std::unique_ptr<rp::graph::GraphHost>  host;
    std::uint32_t                          maxFrames = kDefaultMaxBlockFrames;
};

Engine::Engine(double sampleRate) : Engine(sampleRate, embeddedScripts()) {}

Engine::Engine(double sampleRate, std::span<const rp::host::ScriptHost::Chunk> scriptBundle)
    : sampleRate_(sampleRate), pendingMidi_(rp::host::kHostMidiInMessages, rp::host::kHostMidiInBytes) {
    graph_ = std::make_unique<Graph>();
    // The one factory a system node's build goes through, with every core backend on it: a host that composes an Engine
    // builds cores, whatever else it keeps.
    factory_ = std::make_unique<SystemFactory>();
    registerCoreBackends(*factory_);
    // The script node types first: the bundle runs in both of the built-in pack's contexts, and its definitions become
    // one more package. One that will not load registers no type, so a document naming one is refused (unknown-type).
    std::string why;
    // A retired generation's entries go as the script host frees it, on whichever reclaim frees it.
    graph_->script.setPackFreed(
        [](void* ctx, const rp_package* package) { static_cast<Graph*>(ctx)->registry.dropRetired(package); },
        graph_.get());
    if (!graph_->script.load(scriptBundle, &why))
        std::fprintf(stderr, "[engine] the script-node bundle did not load (%s); no script node type is registered\n", why.c_str());
    // The routing, the controller and the protocol nodes are the script package's (src/script/nodes/).
    for (const rp_package* p : {rp::graph::corePackage(), rp::graph::dspPackage(), rp::graph::dspResamplePackage(),
                                rp::graph::hostPackage(), rp::host::sameboyPackage(), rp::host::mesenPackage(),
                                rp::host::hostIoPackage(), rp::host::n8Package(), rp::host::launchpadPackage()})
        if (!graph_->registry.addPackage(p, &why))
            std::fprintf(stderr, "[engine] a node package was refused (%s); its types are not registered\n", why.c_str());
    if (!graph_->registry.addPackage(graph_->script.package(), &why))
        std::fprintf(stderr, "[engine] the script node types were refused (%s); none is registered\n", why.c_str());
    // rp.host.systems/1: a system node that builds its own core, on the control thread, inside the setGraph that
    // activates it. The id it names must not be live; the core it builds claims its snapshot slot and joins the live
    // cores, and the table of the plan being built learns it (buildSystem).
    graph_->systems = {
        this,
        [](void* ctx, std::uint64_t id, SystemBuildRequest request, rp_error* err) {
            auto* self = static_cast<Engine*>(ctx);
            std::string why;
            SystemBase* core = self->buildSystem(id, std::move(request), &why);
            if (!core && err) {
                const std::size_t n = std::min(why.size(), sizeof err->message - 1);
                std::memcpy(err->message, why.data(), n);
                err->message[n] = '\0';
            }
            return core;
        },
        [](void* ctx, SystemBase* core) { static_cast<Engine*>(ctx)->destroySystem(core); }};
    // rp.engine.host-io/3: the staging the hosts fill and the buffer they drain, on the block thread.
    graph_->hostIo = {
        this,
        [](void* ctx, rp_queue* out) { static_cast<Engine*>(ctx)->pendingMidi_.takeSortedInto(*out); },
        [](void* ctx, std::uint32_t system, std::uint32_t frame, const std::uint8_t* data, std::size_t size) {
            static_cast<Engine*>(ctx)->midiOut_.push_back({system, frame, std::vector<std::uint8_t>(data, data + size)});
        }};
    graph_->syncTrace = {this, [](void* ctx, std::uint32_t frame, const std::uint8_t* data, std::size_t size) {
                             auto* self = static_cast<Engine*>(ctx);
                             if (!self->blockTracing_) return;
                             for (std::size_t i = 0; i < size; ++i) self->syncTrace_.byte(frame, data[i]);
                         }};
    // rp.host.devices/1: the links the host installed, by name, handed out for the life of a plan. A host with no
    // device never installs one, and its nodes get the never-connected handle instead (design section 3).
    graph_->devicesApi = {
        this,
        [](void* ctx, const char* name) {
            return static_cast<Engine*>(ctx)->graph_->devices.acquireCart(name ? name : "");
        },
        [](void* ctx, rp::host::CartLink* link) { static_cast<Engine*>(ctx)->graph_->devices.release(link); },
        [](void* ctx, const char* name) {
            return static_cast<Engine*>(ctx)->graph_->devices.acquireSurface(name ? name : "");
        },
        [](void* ctx, rp::host::SurfaceLink* link) { static_cast<Engine*>(ctx)->graph_->devices.release(link); }};
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

// The offline pool first: it cancels its jobs and joins its workers, so no job thread outlives the engine, and each
// job's host and nodes (a resampler among them), and each render's runtime, Engine and cores, are gone before the
// engine's own members start to go.
Engine::~Engine() { offline_.reset(); }

rp::host::OfflineSubmit Engine::submitOfflineJob(rp::host::OfflineRequest request) {
    if (!offline_) offline_ = std::make_unique<rp::host::OfflineJobs>();
    return offline_->submit(std::move(request));
}

rp::host::OfflineSubmit Engine::submitEngineJob(rp::host::EngineJobRequest request) {
    if (!offline_) offline_ = std::make_unique<rp::host::OfflineJobs>();
    return offline_->submitEngine(std::move(request));
}

std::vector<rp::host::OfflineCompletion> Engine::drainOfflineJobs(std::chrono::milliseconds  wait,
                                                                  std::size_t*               inFlight,
                                                                  rp::host::EngineJobsDrain* engine) {
    if (offline_) return offline_->drain(wait, inFlight, engine);
    if (inFlight) *inFlight = 0;
    return {};
}

bool Engine::cancelOfflineJob(std::uint32_t id) { return offline_ && offline_->cancel(id); }

std::size_t Engine::offlineJobsOutstanding() const { return offline_ ? offline_->outstanding() : 0; }

const void* Engine::graphInterface(void* ctx, const char* id) {
    auto* self = static_cast<Engine*>(ctx);
    if (std::strcmp(id, RP_HOST_SYSTEMS_INTERFACE) == 0) return &self->graph_->systems;
    if (std::strcmp(id, RP_ENGINE_HOST_IO_INTERFACE) == 0) return &self->graph_->hostIo;
    if (std::strcmp(id, RP_ENGINE_SYNC_TRACE_INTERFACE) == 0) return &self->graph_->syncTrace;
    if (std::strcmp(id, RP_HOST_DEVICES_INTERFACE) == 0) return &self->graph_->devicesApi;
    return nullptr;
}

bool Engine::pushCommand(const DspCommand& c) { return graph_->host->pushHostCommand(&c, sizeof c); }

SystemBase* Engine::buildSystem(std::uint64_t system, SystemBuildRequest request, std::string* why) {
    const auto refuse = [why](std::string reason) {
        if (why) *why = std::move(reason);
        return nullptr;
    };
    // The plan being built is where the id's slot is filled (below); a build with no setGraph in progress would join the
    // live cores and claim a snapshot slot with no plan to publish it, and a later plan's slot for that id would stay
    // null while processBlock dereferences it. Refused, rather than left to the comment on this method.
    if (!building_) return refuse("a system is built only inside the setGraph that activates it (rp.host.systems/1)");
    if (system == 0) return refuse("0 is not a system id");
    if (system > UINT32_MAX) return refuse("system " + std::to_string(system) + " is beyond a system id");
    const SystemId id = static_cast<SystemId>(system);
    // Checked before the claim: the registry finds a slot by id, and a second slot under a live id would free the live
    // core's. A node of a plan not yet reclaimed still holds its core, so a construct-time edit under the id it was
    // built with is refused here rather than quietly sharing it.
    if (coreLive(id)) return refuse("system " + std::to_string(id) + " is live");
    std::unique_ptr<SystemBase> core = factory_->build(id, std::move(request), sampleRate_, why);
    if (!core) {
        if (why && why->empty()) *why = "the core did not build";
        return nullptr;
    }
    // The slot is claimed after the build, not before, because the claim seeds the slot from the core: a GBA pays its
    // whole boot (its 8 MiB ROM included) before a full pool can refuse it. That is the price of one claim that both
    // keeps the slot and seeds it, and only a document that finds the pool full pays it.
    if (!registry_.claim(id, *core)) return refuse("the snapshot pool is full");
    SystemBase* built = core.release();
    liveCores_[id] = built;
    // The plan being planned learns its slot's core here: its table was built with a null entry for a system no node of
    // an older plan is reused for, and the table is published with the plan once every node has activated. buildSystem
    // only runs inside a setGraph, so the table is there, and publishGraph has already refused a document whose system
    // nodes and slots name different systems, so an id the slots do not name is a host bug. Asserted, and refused with
    // what the claim took given back, so that a broken invariant costs a refused activation rather than a core and a
    // snapshot slot the process then holds for ever under an id no plan can free.
    bool found = false;
    for (auto& e : building_->slots)
        if (e.id == id) {
            e.core = built;
            e.tap = built->channelTap();
            found = true;
        }
    assert(found);
    if (!found) {
        destroySystem(built);  // the slot, the live-core entry and the core itself
        return refuse("system " + std::to_string(id) + " is not in the plan's slots");
    }
    return built;
}

void Engine::destroySystem(SystemBase* core) {
    if (!core) return;
    const SystemId id = core->id();
    registry_.release(id);
    liveCores_.erase(id);
    delete core;
}

Engine::GraphResult Engine::setGraph(const GraphRequest& request) { return publishGraph(request, false); }

Engine::GraphResult Engine::publishGraph(const GraphRequest& request, bool replan) {
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

    // The system nodes and the slots name the same systems, each node as sys-<id>, its settings naming the `system` it
    // builds its core for. A node naming none is left to its own create, which refuses it with a reason naming what it
    // must name (so the whole check waits: a node with no id cannot be lined up against the slots).
    std::vector<SystemId> named;
    bool malformed = false;
    for (const auto& n : doc->nodes) {
        if (!isSystemNodeType(n.type)) continue;
        const SystemTarget target = systemTargetOf(n);
        if (!target.id) {
            // The field is there and is not a system id: that is the document's mistake, said here. A node without one
            // is its own create refusal, which says what it must name.
            if (target.namesAny)
                return refuse(n.id, "slots", "a system node's system is a system id, 1 to 4294967295");
            malformed = true;
            continue;
        }
        if (n.id != "sys-" + std::to_string(*target.id))
            return refuse(n.id, "slots",
                          "a system node is sys-<id>, its settings naming its system as {\"system\": <id>}");
        named.push_back(*target.id);
    }
    std::vector<SystemId> slots = request.slots;
    std::sort(named.begin(), named.end());
    std::sort(slots.begin(), slots.end());
    if (!malformed) {
        // Two nodes naming one system would build or take one core twice.
        const auto duplicate = std::adjacent_find(named.begin(), named.end());
        if (duplicate != named.end())
            return refuse("", "slots", "two system nodes name system " + std::to_string(*duplicate));
        if (std::adjacent_find(slots.begin(), slots.end()) != slots.end() || slots != named)
            return refuse("", "slots", "the slots and the document's system nodes name different systems");
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
        // A slot whose core is live already (its node of an older plan is reused, its rebuild_key unchanged) is in the
        // plan from here; a new instance's has a null entry until its activation, and buildSystem fills it.
        const auto it = liveCores_.find(id);
        SystemBase* core = it == liveCores_.end() ? nullptr : it->second;
        table->slots.push_back({id, core, core ? core->channelTap() : nullptr});
    }
    // The host inputs, one of each at most: each takes everything staged, wired or not, so a second would take it from
    // the first whenever it ran first. The host MIDI out, one at most: it frame-sorts what it merges, and a second
    // would append its own run behind it, so midiOut_ would step back in frame (buildGraph places one).
    bool takesHostMidi = false, sendsHostMidi = false;
    for (const auto& n : doc->nodes) {
        if (n.type == "rp.host.midi-in") {
            if (takesHostMidi)
                return refuse(n.id, "midi-in", "a document has one rp.host.midi-in at most: it takes all the staged host MIDI");
            takesHostMidi = true;
        }
        if (n.type == "rp.host.midi-out") {
            if (sendsHostMidi)
                return refuse(n.id, "midi-out",
                              "a document has one rp.host.midi-out at most: it merges all the host MIDI out by frame");
            sendsHostMidi = true;
        }
    }

    rp::graph::HostCommand trailer{};
    if (request.rearmTap) {
        const DspCommand c = syncTapCommand(request.arm, request.armId, request.arm ? request.armMode : 0);
        std::memcpy(trailer.bytes, &c, sizeof c);
    }
    // The window a plan is built in. A node that builds its core adds it to the plan's table here (buildSystem), so
    // that table is complete before it is published. If the plan is then refused (a later node fails to activate, the
    // ring has no room for its swap), the nodes the planner made are destroyed before the host's setGraph returns and
    // each destroys the core it built, so nothing of the refused plan is left alive.
    struct InSetGraph {
        Engine& e;
        InSetGraph(Engine& engine, rp::host::SystemTable* table) : e(engine) { e.building_ = table; }
        ~InSetGraph() { e.building_ = nullptr; }
    };
    rp::graph::SetGraphResult result;
    {
        const InSetGraph window(*this, table.get());
        // The table is handed on by copy: this thread filled its slots (buildSystem, as the plan activated) and reads
        // them below, where the plan's own view belongs to whichever thread adopts it.
        result = graph_->host->setGraph(*doc, table, request.rearmTap ? &trailer : nullptr);
    }
    graph_->script.reclaim();  // a quiescent swap released its script nodes in there: what they left in cycles goes now
    for (const auto& e : result.errors) out.errors.push_back({e.nodeId, e.port, e.code, e.message});
    if (!result.ok) return out;
    out.ok = true;
    // Every slot has a core by now: one already live, or one a node built while the plan activated (the planner
    // activates every node it plans). A null one would be a table the plan's per-block work dereferences, so it is an
    // invariant rather than a diagnostic.
    for (const auto& e : table->slots) assert(e.core);
    hostWidth_ = hostWidthOf(*doc);
    publishedTable_ = table;
    // A replan plans a copy of the request setGraph last kept (its re-arm cleared), so it must not replace it.
    if (replan) return out;
    lastRequest_ = request;
    return out;
}

bool Engine::setParam(std::string_view nodeId, std::string_view paramId, double value) {
    return graph_->host->setParam(nodeId, paramId, value);
}

// The table is Graph's, so it outlives every plan that pins one of its resources and every node that names one.
std::uint32_t Engine::putResource(const std::string& type, std::vector<std::uint8_t> bytes, std::string* error) {
    return graph_->resources.createBytes(type, std::move(bytes), error);
}

bool Engine::releaseResource(std::uint32_t id) { return graph_->resources.drop(id); }

const rp_resource* Engine::resource(std::uint32_t id) const { return graph_->resources.get(id); }

std::uint32_t Engine::resourcePinCount(std::uint32_t id) const { return graph_->resources.pinCount(id); }

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
    return n;
}

void Engine::discardPending() { graph_->host->discardPending(); }

std::uint32_t Engine::faultCount() const { return graph_->host->faultCount(); }

std::uint32_t Engine::droppedMessages() const { return graph_->host->droppedMessages(); }

std::uint32_t Engine::scriptThrows() const { return graph_->script.throwCount(); }

Engine::ScriptPackResult Engine::loadScriptPack(const std::string& id, const std::string& source,
                                                std::size_t memoryLimit) {
    ScriptPackResult out;
    rp::host::ScriptHost& script = graph_->script;
    rp::graph::NodeRegistry& registry = graph_->registry;
    // What the audio thread has handed back is reclaimed first, as setGraph does: the plans it no longer
    // runs, and each retired or discarded generation whose last record is back. A host with no idle tick reclaims only
    // in those calls, so there each load, a refused one or one that replans nothing among them, frees the generations
    // earlier reloads replaced once blocks have released their nodes.
    graph_->host->reclaim();
    script.reclaim();
    // The id first: a refused one is refused whatever its source says, and costs no compile.
    if (!script.canLoadPack(id, &out.error)) return out;
    const auto bytecode = dsp::compileToBytecode(source, rp::host::ScriptHost::kStackBytes, &out.error);
    if (!bytecode) return out;
    const rp::host::ScriptHost::Chunk chunk{*bytecode};
    // Whether this is a reload (docs/modular-graph-phase-c1-design.md section 5): the script host knows whether it
    // holds a current generation of `id`, having built it, where the registry could one day hold a package of another
    // kind under the same id (a downloaded native one), which a reload must not swap out. Asked before the load,
    // which makes the new generation current too.
    const rp_package* current = script.currentPack(id);
    const rp_package* package = script.loadPack(id, {&chunk, 1}, memoryLimit, &out.error);
    if (!package) return out;
    std::string why;
    if (!current) {
        // Checked here as every package is: a descriptor the planner could not use, or a type id the registry holds
        // already (one whose namespace holds another's: "a" and "a.b" can both name a type "a.b.c"). Refused, the pack
        // goes: nothing placed it.
        if (!registry.addPackage(package, &why)) {
            script.discardPack(package);
            out.error = "the registry refused the pack's types: " + why;
            return out;
        }
    } else {
        // The new generation's entries in place of the old one's, checked as an addition is. The old entries stay valid
        // for the running plan's instances, which no longer match them, so a replan of the document last published
        // rebuilds the pack's nodes on the new code, under the same node ids, and keeps every other node's instance.
        // Its tap is carried as it is: a request that does not re-arm leaves the tap the running plan has, neither
        // restored nor armed again. A document that places none of the pack's nodes would plan as it runs, so it is not
        // replanned.
        const rp_package* previous = registry.replacePackage(id, package, &why);
        if (!previous) {
            script.discardPack(package);
            out.error = "the registry refused the pack's types: " + why;
            return out;
        }
        assert(previous == current && "the registry holds the generation the script host holds as current");
        if (lastRequest_ && placesAny(lastRequest_->document, *previous)) {
            GraphRequest replan = *lastRequest_;
            replan.rearmTap = false;
            GraphResult planned = publishGraph(replan, true);
            if (!planned.ok) {
                // Nothing has changed: the old entries are found again, the same ones, so the running plan's instances
                // still match them, and the new generation goes.
                [[maybe_unused]] const bool restored = registry.restorePackage(previous);
                assert(restored && "the swap is the package's last: the replan's reclaim drops only older generations");
                script.discardPack(package);
                out.error =
                    "the running document does not plan with the reloaded pack: " + describeErrors(planned.errors);
                out.errors = std::move(planned.errors);
                return out;
            }
        }
        // Freed with its runtimes once the plan holding its nodes is handed back and their records come back: at once
        // when no audio thread runs, as the replan released them there and then.
        script.retirePack(previous);
    }
    for (std::uint32_t k = 0; k < package->node_type_count; ++k) {
        const auto* entry = registry.find(package->node_types[k]->id);
        out.types.push_back({entry->descriptor.id, rp::graph::descriptorJson(entry->descriptor)});
    }
    out.ok = true;
    return out;
}

void Engine::setScriptCallDeadline(std::chrono::milliseconds limit) { graph_->script.setCallDeadline(limit); }

bool Engine::addNodePackage(const rp_package* package, std::string* why) {
    if (package == nullptr) {
        if (why) *why = "no package";
        else std::fprintf(stderr, "[engine] a node package was refused (no package)\n");
        return false;
    }
    std::string reason;
    if (!graph_->registry.addPackage(package, &reason)) {
        if (why) *why = reason;
        else std::fprintf(stderr, "[engine] a node package was refused (%s)\n", reason.c_str());
        return false;
    }
    return true;
}

void Engine::setDeviceLinks(rp::host::CartLink* cart, rp::host::SurfaceLink* surface) {
    // Both back at once is the host uninstalling its links, which is what the registry calls `clear`.
    if (cart == nullptr && surface == nullptr) graph_->devices.clear();
    else graph_->devices.install(cart, surface);
}

void Engine::clearGraph() {
    // The caller stopped the audio thread: make the engine quiescent so the swap below and the reclaim that follows it
    // run inline, and the plan a host's nodes are in is freed here rather than by a block thread that no longer runs.
    setAudioThreadOwns(false);
    const GraphResult empty = setGraph({kEmptyDocument, {}});
    if (!empty.ok)
        for (const auto& e : empty.errors)
            std::fprintf(stderr, "[engine] the empty graph was refused (%s) at %s%s%s: %s\n", e.code.c_str(),
                         e.nodeId.c_str(), e.port.empty() ? "" : ".", e.port.c_str(), e.message.c_str());
    // The constructor published this same document and nothing can remove a type from the registry, so a refusal
    // cannot happen here. Every caller assumes it did not: the hosts uninstall their links on the next line, and the
    // plan a refused clear left alive would then hand its nodes' releases to objects that are already gone.
    assert(empty.ok && "clearGraph's empty document was refused: a plan outlived the links it uses");
    // The plan the publish replaced is in the release ring: freeing it here destroys its nodes, whose device handles
    // this engine still holds.
    reclaim();
}

std::vector<rp::host::ScriptHost::PackInfo> Engine::scriptPacks() const { return graph_->script.packs(); }

std::size_t Engine::nodeTypeCount() const { return graph_->registry.size(); }
std::size_t Engine::retiredNodeTypeCount() const { return graph_->registry.retiredSize(); }

std::uint32_t Engine::scriptInstances() const { return graph_->script.liveInstances(); }

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
    // The running plan's cores, which are every live core while quiescent, re-rated in place: in slot order, then the
    // plan's buffers at the new rate. Quiescent, so the running plan is the one last published.
    for (const auto& e : currentTable().slots) e.core->onSampleRateChanged(sr);
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
        case DspCommand::Kind::CapabilityWrite: {
            // The record is the control thread's, which frees it; this is that thread (the audio thread has stopped).
            rp::host::Job& job = *c.capabilityWrite.job;
            job.failure.store(rp::host::Job::Discarded, std::memory_order_relaxed);
            job.state.store(rp::host::Job::Failed, std::memory_order_release);
            break;
        }
        case DspCommand::Kind::StageMidi:       delete c.stageMidi.ext; break;
        case DspCommand::Kind::StageNodeMidi:   delete c.stageNodeMidi.ext; break;
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
        case DspCommand::Kind::StageNodeMidi:
            if (cmd.stageNodeMidi.ext) {
                stageNodeMidi(cmd.stageNodeMidi.ext->node, cmd.stageNodeMidi.ext->bytes.data(),
                              cmd.stageNodeMidi.ext->bytes.size());
                delete cmd.stageNodeMidi.ext;
            }
            break;
        case DspCommand::Kind::SetBpm:          setBpm(cmd.setBpm.value); break;
        case DspCommand::Kind::SetTransport:    setTransport(cmd.setTransport.value); break;
        case DspCommand::Kind::SetPpq:          setPpq(cmd.setPpq.value); break;
        case DspCommand::Kind::PressButton:
            if (SystemBase* sys = core(cmd.pressButton.id))
                // A Mesen core's press queue is applied whole at the next block's prepare, before the emulator polls
                // its controller; a Game Boy schedules the press ahead instead.
                sys->pressButton(cmd.pressButton.button, cmd.pressButton.down);
            break;
        case DspCommand::Kind::CapabilityWrite:
            applyJob(*cmd.capabilityWrite.job, table);
            break;
        case DspCommand::Kind::SyncTap:
            syncTap(cmd.syncTap.arm, cmd.syncTap.id, cmd.syncTap.mode, table);
            break;
        default:
            break;
    }
}

// The audio tap arming at every structural change, against `table`, the plan just adopted (design section 3.2):
// restore whatever was armed, then arm `id` to `mode` if `arm`. It runs even when the wanted mode is
// unchanged, and each run resets the capture streams. A core that has gone (removed, or replaced under
// its id or a new one) is not restored, and its record is dropped; a core that offers no tap
// (channelTap() null) has nothing to arm, so the document's arm reaches nothing.
void Engine::syncTap(bool arm, SystemId id, std::uint32_t mode, const rp::host::SystemTable& table) {
    if (armedTap_.active) {
        if (const auto* e = table.find(armedTap_.id); e && e->tap == armedTap_.tap)
            e->tap->setExportMode(armedTap_.prevMode);
        armedTap_ = {};
    }
    if (!arm) return;
    const auto* e = table.find(id);
    ChannelTap* tap = e ? e->tap : nullptr;
    if (!tap) return;
    if (tap->exportMode() != mode) {
        armedTap_ = { true, e->id, tap, tap->exportMode() };
        tap->setExportMode(mode);
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

void Engine::setBpm(double bpm) { bpm_ = bpm; }
void Engine::setTransport(bool playing) { transport_ = playing; }
void Engine::setPpq(double ppq) { ppq_ = ppq < 0.0 ? 0.0 : ppq; }

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
        oversizedFrames_.store(frames, std::memory_order_relaxed);
        oversizedBlocks_.fetch_add(1, std::memory_order_release);
        return;
    }
    blockTracing_ = syncTrace_.beginBlock(frames, sampleRate_, bpm_, ppq_, transport_);
    // The block's host output starts empty here, and the plan's rp.host.midi-out appends to it.
    midiOut_.clear();
    const rp_transport transport{sampleRate_, bpm_, ppq_, transport_ ? std::uint32_t{RP_TRANSPORT_PLAYING} : 0u};
#ifdef RETROPLUG_PROFILE
    rp::host::ScriptHost& script = graph_->script;
    const double planStart = script.traceNow();
#endif
    // The host inputs, the script nodes, every system, the mixing and the host outputs (the host drained before this).
    graph_->host->run(frames, transport, outputs, static_cast<std::uint32_t>(numOutputs));
#ifdef RETROPLUG_PROFILE
    // apu-render: the plan's whole run, from where it starts to where it ends. Each script node's process span lies
    // inside it (Chrome trace events nest), so it holds wherever the planner puts a script node, before a system or
    // after one, and its figure includes the script nodes' few microseconds beside the cores'.
    script.spanSince(rp::host::DSP_SPAN_APU, planStart);
#endif
    // The allocation window's block counts (a profile build): the script nodes' promise jobs ran at their calls'
    // checkpoints, inside the plan's run.
    graph_->script.endBlock();
    // What was staged for this block and no node of the plan took (a plan with no rp.host.midi-in) is dropped, not
    // carried into the next block.
    pendingMidi_.clear();
    const rp::host::SystemTable& table = currentTable();
#ifdef RETROPLUG_PROFILE
    script.spanBegin(rp::host::DSP_SPAN_PUBLISH);
#endif
    for (const auto& e : table.slots) registry_.publishSystem(*e.core, frames, sampleRate_);
    // The writes applied at this block's drain (or an earlier one, before a block that ran nothing) are published now.
    confirmApplied(table);
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

// --- capability jobs (CapabilityJobs.hpp) ---------------------------------------------------------------------------

const rp::host::SystemEntry* Engine::publishedSystem(const std::string& nodeId) const {
    constexpr std::string_view prefix = "sys-";
    if (!publishedTable_ || nodeId.size() <= prefix.size() || nodeId.compare(0, prefix.size(), prefix) != 0)
        return nullptr;
    std::uint64_t id = 0;
    for (std::size_t i = prefix.size(); i < nodeId.size(); ++i) {
        const char c = nodeId[i];
        if (c < '0' || c > '9' || id > UINT32_MAX) return nullptr;
        id = id * 10 + static_cast<std::uint64_t>(c - '0');
    }
    if (id == 0 || id > UINT32_MAX) return nullptr;
    const auto* e = publishedTable_->find(static_cast<SystemId>(id));
    // The canonical spelling only: "sys-01" is not the node of system 1.
    return e && nodeId == "sys-" + std::to_string(e->id) ? e : nullptr;
}

std::size_t Engine::jobsOutstanding() const {
    std::size_t n = completions_.size();
    for (const auto& j : jobs_)
        if (!j->detached) ++n;
    return n;
}

rp::host::JobSubmit Engine::submitJob(rp::host::JobRequest r) {
    using rp::host::Job;
    using rp::host::JobCompletion;
    const auto refuse = [](std::string why) { return rp::host::JobSubmit{0, std::move(why)}; };
    settleQuiescent();
    sweepJobs();
    if (jobsOutstanding() >= kMaxJobs)
        return refuse("busy: " + std::to_string(kMaxJobs) + " jobs are outstanding; drain their completions first");
    if (!graph_->host->node(r.nodeId)) return refuse("the running graph has no node " + r.nodeId);
    const rp::host::SystemEntry* entry = publishedSystem(r.nodeId);
    if (!entry) return submitDeviceJob(r);
    SystemBase* core = entry->core;
    const auto presentsNo = [&] { return refuse("node " + r.nodeId + " presents no " + r.capability); };
    const auto noOp = [&] { return refuse(r.capability + " has no operation '" + r.op + "'"); };
    // A read answered from what is published (or from the ROM, which never changes): its completion is queued here.
    const auto completeNow = [&](std::vector<std::uint8_t> bytes) {
        const std::uint32_t id = nextJobId_++;
        JobCompletion c;
        c.id = id;
        c.done = c.ok = true;
        c.bytes = std::move(bytes);
        completions_.push_back(std::move(c));
        return rp::host::JobSubmit{id, {}};
    };

    if (r.capability == RP_SYSTEM_MEMORY_INTERFACE) {
        rp::MemoryCapability* memory = core ? core->memory() : nullptr;
        if (!memory) return presentsNo();
        const auto regions = memory->regions();
        // A region's size is its published copy's; only the work RAM is published (SnapshotRegistry::publishRam).
        const auto publishedSize = [&](const rp::MemoryCapability::Region& region) -> std::size_t {
            return region.type == rp::MemoryType::Ram ? registry_.ramSize(entry->id) : 0;
        };
        if (r.op == "regions") {
            std::string json = "[";
            for (std::size_t i = 0; i < regions.size(); ++i) {
                if (i) json += ",";
                json += std::string("{\"name\":\"") + regions[i].name + "\",\"size\":" +
                        std::to_string(publishedSize(regions[i])) + "}";
            }
            json += "]";
            return completeNow(std::vector<std::uint8_t>(json.begin(), json.end()));
        }
        if (r.op != "read" && r.op != "write") return noOp();
        std::size_t index = regions.size();
        for (std::size_t i = 0; i < regions.size(); ++i)
            if (r.region == regions[i].name) index = i;
        if (index == regions.size()) return refuse("node " + r.nodeId + " has no memory region '" + r.region + "'");
        const auto notPublished = [&] {
            return refuse("region " + r.region + " of node " + r.nodeId + " is not published");
        };
        if (r.op == "read") {
            // One read of the published copy, which is its own bound: a block republishing it at a new size between
            // a size query and the read would otherwise refuse a read that was in range.
            auto ram = regions[index].type == rp::MemoryType::Ram ? registry_.readRam(entry->id) : std::nullopt;
            if (!ram || ram->empty()) return notPublished();
            const std::size_t size = ram->size();
            const std::size_t length = r.length != 0 ? r.length : (r.offset < size ? size - r.offset : 0);
            if (static_cast<std::size_t>(r.offset) + length > size || length == 0)
                return refuse("the read runs past region " + r.region + " (" + std::to_string(size) + " bytes)");
            if (r.offset == 0 && length == size) return completeNow(std::move(*ram));
            return completeNow(std::vector<std::uint8_t>(ram->begin() + r.offset, ram->begin() + r.offset + length));
        }
        const std::size_t size = publishedSize(regions[index]);
        if (size == 0) return notPublished();
        // The answerable failures are answered here, from the published copy (race-free, no live-core read): the
        // block thread re-checks the bounds as it applies, since a model switch could shrink the region between.
        if (r.bytes.empty()) return refuse("an empty write");
        if (static_cast<std::size_t>(r.offset) + r.bytes.size() > size)
            return refuse("the write runs past region " + r.region + " (" + std::to_string(size) + " bytes)");
        auto job = std::make_unique<Job>();
        job->id = nextJobId_;
        job->nodeId = r.nodeId;
        job->system = entry->id;
        job->region = index;
        job->offset = r.offset;
        job->bytes = std::move(r.bytes);
        DspCommand c;
        c.kind = DspCommand::Kind::CapabilityWrite;
        c.capabilityWrite = {job.get()};
        // Pushed before the record joins jobs_: a quiescent push applies it inline, which leaves it in applied_ for
        // settleQuiescent below, and a refused one is freed here.
        if (!pushCommand(c)) return refuse("busy: the command ring is full");
        ++nextJobId_;
        jobs_.push_back(std::move(job));
        settleQuiescent();
        return {jobs_.back()->id, {}};
    }
    if (r.capability == RP_SYSTEM_SRAM_INTERFACE) {
        rp::SramCapability* sram = core ? core->sram() : nullptr;
        if (!sram) return presentsNo();
        if (r.op == "dump") {
            auto bytes = registry_.readSram(entry->id);
            if (!bytes) return refuse("node " + r.nodeId + " publishes no battery");
            return completeNow(std::move(*bytes));
        }
        if (r.op == "restore")
            return refuse("node " + r.nodeId + " restores no battery in place: an emulated system's battery is "
                          "restored by rebuilding it with the bytes as its seed");
        return noOp();
    }
    if (r.capability == RP_SYSTEM_ROM_INTERFACE) {
        rp::RomCapability* rom = core ? core->rom() : nullptr;
        if (!rom) return presentsNo();
        if (r.op == "header") {
            const auto image = rom->image();
            const std::size_t n = r.length != 0 ? std::min<std::size_t>(r.length, image.size()) : image.size();
            return completeNow(std::vector<std::uint8_t>(image.begin(), image.begin() + n));
        }
        if (r.op == "title") {
            const std::string title = rom->title();
            return completeNow(std::vector<std::uint8_t>(title.begin(), title.end()));
        }
        return noOp();
    }
    return refuse("unknown capability " + r.capability);
}

rp::host::JobSubmit Engine::submitDeviceJob(const rp::host::JobRequest& r) {
    // A node that is no system node: a device's (docs/modular-graph-phase-c5-design.md sections 4 and 6), asked
    // through the node for the capability. It takes the job into a record the engine then keeps, or refuses it
    // synchronously (busy, not connected); the engine's bound was checked by submitJob, so a taken job always has a
    // place here. The record outlives the node's use of it: a device's node abandons its job as it leaves the plan
    // (DeviceJobSlot), failing the record and keeping the device off it, so the engine frees only terminal records,
    // and frees them on this thread whenever a sweep finds them.
    //
    // A node is asked only by an id the engine knows: an answer to get_interface is only that id's struct, and the
    // capability came from outside (the RPC), so an id the node answers for something else (rp.step/1) must never
    // reach the cast below.
    if (r.capability != RP_SYSTEM_MEMORY_INTERFACE && r.capability != RP_SYSTEM_SRAM_INTERFACE &&
        r.capability != RP_SYSTEM_ROM_INTERFACE)
        return {0, "unknown capability " + r.capability};
    rp_node* node = graph_->host->node(r.nodeId);
    const auto* jobs =
        node ? static_cast<const rp::host::DeviceJobs*>(node->vt->get_interface(node, r.capability.c_str())) : nullptr;
    if (!jobs) return {0, "node " + r.nodeId + " presents no " + r.capability};
    auto job = std::make_unique<rp::host::Job>();
    job->nodeId = r.nodeId;
    std::string error;
    if (!jobs->submit(jobs->ctx, r, *job, &error)) return {0, error.empty() ? "the job was refused" : error};
    job->id = nextJobId_++;
    const std::uint32_t id = job->id;
    jobs_.push_back(std::move(job));
    return {id, {}};
}

rp::host::JobSubmit Engine::adoptJob(std::unique_ptr<rp::host::Job> job) {
    settleQuiescent();
    sweepJobs();
    if (!job) return {0, "no job"};
    if (jobsOutstanding() >= kMaxJobs)
        return {0, "busy: " + std::to_string(kMaxJobs) + " jobs are outstanding; drain their completions first"};
    job->id = nextJobId_++;
    job->detached = false;
    const std::uint32_t id = job->id;
    jobs_.push_back(std::move(job));
    return {id, {}};
}

bool Engine::writeRam(SystemId id, std::uint32_t offset, std::vector<std::uint8_t> bytes) {
    sweepJobs();
    if (bytes.empty() || !publishedTable_) return false;
    const auto* entry = publishedTable_->find(id);
    rp::MemoryCapability* memory = entry ? entry->core->memory() : nullptr;
    if (!memory) return false;
    const auto regions = memory->regions();
    std::size_t index = regions.size();
    for (std::size_t i = 0; i < regions.size(); ++i)
        if (regions[i].type == rp::MemoryType::Ram) index = i;
    if (index == regions.size()) return false;
    auto job = std::make_unique<rp::host::Job>();
    job->detached = true;
    job->system = id;
    job->region = index;
    job->offset = offset;
    job->bytes = std::move(bytes);
    DspCommand c;
    c.kind = DspCommand::Kind::CapabilityWrite;
    c.capabilityWrite = {job.get()};
    if (!pushCommand(c)) return false;  // full ring: drop the poke, freeing the record here
    jobs_.push_back(std::move(job));
    return true;
}

std::vector<rp::host::JobCompletion> Engine::drainJobs() {
    settleQuiescent();
    sweepJobs();
    std::vector<rp::host::JobCompletion> out(std::make_move_iterator(completions_.begin()),
                                             std::make_move_iterator(completions_.end()));
    completions_.clear();
    // A running job's progress, once per change: what a device thread reports through the record.
    for (const auto& j : jobs_) {
        if (j->detached) continue;
        const std::uint32_t progress = j->progress.load(std::memory_order_acquire);
        const std::uint32_t total = j->total.load(std::memory_order_relaxed);
        if (total == 0 || progress == j->reported) continue;
        j->reported = progress;
        rp::host::JobCompletion c;
        c.id = j->id;
        c.progress = progress;
        c.total = total;
        out.push_back(std::move(c));
    }
    return out;
}

void Engine::sweepJobs() {
    using rp::host::Job;
    for (auto it = jobs_.begin(); it != jobs_.end();) {
        Job& j = **it;
        if (!j.finished()) {
            ++it;
            continue;
        }
        if (!j.detached) {
            rp::host::JobCompletion c;
            c.id = j.id;
            c.done = true;
            c.ok = j.state.load(std::memory_order_relaxed) == Job::Done;
            c.progress = j.progress.load(std::memory_order_relaxed);
            c.total = j.total.load(std::memory_order_relaxed);
            if (c.ok) {
                c.bytes = std::move(j.result);
            } else {
                switch (j.failure.load(std::memory_order_relaxed)) {
                    case Job::OutOfRange:
                        c.error = "the write ran past the region of node " + j.nodeId + " as it applied";
                        break;
                    case Job::Removed:
                        c.error = "node " + j.nodeId + " left the plan before the job completed";
                        break;
                    case Job::Discarded:  c.error = "the engine stopped before the job ran"; break;
                    case Job::Overflow:   c.error = "the engine's table of unpublished writes was full"; break;
                    default:              c.error = j.error.empty() ? "the job failed" : j.error; break;
                }
            }
            completions_.push_back(std::move(c));
        }
        it = jobs_.erase(it);
    }
}

void Engine::applyJob(rp::host::Job& job, const rp::host::SystemTable& table) {
    using rp::host::Job;
    const rp::host::SystemEntry* e = table.find(job.system);
    rp::MemoryCapability* memory = e ? e->core->memory() : nullptr;
    const auto fail = [&job](Job::Failure why) {
        job.failure.store(why, std::memory_order_relaxed);
        job.state.store(Job::Failed, std::memory_order_release);
    };
    if (!memory) return fail(Job::Removed);
    if (!memory->write(job.region, job.offset, job.bytes.data(), job.bytes.size())) return fail(Job::OutOfRange);
    // An old write is collected by nobody, so it is done once applied; a job's write waits for the publish.
    if (job.detached) {
        job.state.store(Job::Done, std::memory_order_release);
        return;
    }
    // The table has room: every job in it is one of at most kMaxJobs outstanding. A bound broken later must not let a
    // write complete before its region is published, so a full table fails the write rather than finishing it.
    assert(appliedCount_ < applied_.size());
    if (appliedCount_ == applied_.size()) return fail(Job::Overflow);
    job.core = e->core;
    job.state.store(Job::Applied, std::memory_order_relaxed);
    applied_[appliedCount_++] = &job;
}

void Engine::confirmApplied(const rp::host::SystemTable& ran) {
    using rp::host::Job;
    for (std::size_t i = 0; i < appliedCount_; ++i) {
        Job& job = *applied_[i];
        // The id and the core both: an id re-added with a new core since the apply is not the core written.
        const rp::host::SystemEntry* e = ran.find(job.system);
        const bool published = e && e->core == job.core;
        if (!published) job.failure.store(Job::Removed, std::memory_order_relaxed);
        // The job's last touch on this thread: the control thread may free it from here on.
        job.state.store(published ? Job::Done : Job::Failed, std::memory_order_release);
    }
    appliedCount_ = 0;
}

void Engine::settleQuiescent() {
    // Ownership first: applied_ is the block thread's while an audio thread runs.
    if (graph_->host->audioThreadOwns() || appliedCount_ == 0) return;
    const rp::host::SystemTable& table = currentTable();
    for (std::size_t i = 0; i < appliedCount_; ++i)
        if (const auto* e = table.find(applied_[i]->system); e && e->core == applied_[i]->core)
            registry_.publishRam(*e->core);
    confirmApplied(table);
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

bool Engine::stageNodeMidi(const std::string& nodeId, const std::uint8_t* bytes, std::size_t len) {
    if (bytes == nullptr || len == 0) return false;
    // The plan running at this point of the drain, never the control thread's live map: a document may have replaced
    // the node under this id since the push, and only the plan that runs can take the message. The interface is asked
    // by id alone, so this reads no node's type: a system node's hands the bytes to its core, a device node's to its
    // device.
    rp_node* node = graph_->host->runningNode(nodeId);
    if (!node) return false;
    const auto* stage = static_cast<const rp::host::rp_host_stage*>(node->vt->get_interface(node, RP_STAGE_INTERFACE));
    return stage != nullptr && stage->stage(stage->ctx, bytes, len, sampleRate_);
}
