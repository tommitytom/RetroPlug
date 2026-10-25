#include "host/engine/Engine.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
#include <utility>

#include "system/SystemBase.hpp"
#include "system/SystemTypes.hpp"   // AudioBlockInfo, SystemId
#include "system/sameboy/SameBoySystem.hpp"  // live-apply cast target (model/highpass/gain/…)
#include "system/mesen/MesenNesSystem.hpp"    // live-apply cast target (NES region / sprite limit)

#include "transport/FrameBufferTriple.hpp"
#include "native/core/img/png/lodepng.h"

#include "graph/GraphHost.hpp"
#include "graph/HostNodes.hpp"
#include "graph/nodes/CoreNodes.hpp"
#include "host/engine/DspCommand.hpp"
#include "host/graph/HostServices.hpp"
#include "host/graph/KernelNode.hpp"
#include "host/graph/SystemNodes.hpp"
#include "host/graph/SystemTable.hpp"
#include "host/graph/Translator.hpp"

namespace {

// Read-only empties the kernel's per-block button/key inputs default to. The host
// doesn't yet feed the kernel per-block buttons/keys.
const std::vector<DspRuntime::ButtonIn> kNoButtons;
const std::vector<DspRuntime::KeyIn>    kNoKeys;

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

// What the translator's routing arms against `table`, the systems of the plan it rides with: the pins (1) or
// the five channels (3) of a lone NES under a split routing; a restore only otherwise.
DspCommand syncTapFor(rp::host::AudioRouting routing, const rp::host::SystemTable& table) {
    using rp::host::AudioRouting;
    const bool pins  = routing == AudioRouting::PinSplit || routing == AudioRouting::StereoPinSplit;
    const bool split = pins || routing == AudioRouting::ChannelSplit;
    if (!split || table.slots.size() != 1 || !table.slots.front().nes) return syncTapCommand(false, 0, 0);
    return syncTapCommand(true, table.slots.front().id, pins ? 1u : 3u);
}

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

} // namespace

// The Engine's graph: the node packages, the GraphHost running the plan, and the translator that turns
// structure into its documents. Destroyed first (graph_ is the Engine's last member), the translator before
// the host it plans for, and the host (whose system nodes call cores->released from their destructors)
// before the core handoff and the kernel interface those nodes call into.
struct Engine::Graph {
    rp::graph::NodeRegistry                registry;
    rp::graph::ResourceTable               resources;
    rp::host::HostCores                    cores{};
    rp::host::EngineKernel                 kernel{};
    std::unique_ptr<rp::graph::GraphHost>  host;
    std::unique_ptr<rp::host::Translator>  translator;
    std::uint32_t                          maxFrames = kDefaultMaxBlockFrames;
};

Engine::Engine(double sampleRate) : sampleRate_(sampleRate) {
    graph_ = std::make_unique<Graph>();
    for (const rp_package* p : {rp::graph::corePackage(), rp::graph::hostPackage(), rp::host::sameboyPackage(),
                                rp::host::mesenPackage(), rp::host::dspPackage()})
        graph_->registry.addPackage(p);
    graph_->cores = {this, [](void* ctx, std::uint64_t token) { return static_cast<Engine*>(ctx)->takeCore(token); },
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
    graph_->kernel = {this, [](void* ctx, std::uint32_t frames) { static_cast<Engine*>(ctx)->runKernelStage(frames); }};
    rp::graph::HostConfig config;
    config.sampleRate = sampleRate;
    config.maxFrames = graph_->maxFrames;
    config.interfaces = &Engine::graphInterface;
    config.interfacesCtx = this;
    graph_->host = std::make_unique<rp::graph::GraphHost>(graph_->registry, graph_->resources, config);
    graph_->host->setHostCommandHandler(&Engine::onHostCommand, this);
    graph_->translator = std::make_unique<rp::host::Translator>(
        [this](const std::string& json, std::shared_ptr<const rp::host::SystemTable> table, bool rearmsTap) {
            auto doc = rp::graph::parseGraphDocument(json);
            if (!doc) {
                std::fprintf(stderr, "[engine] the translator built a document that does not parse: %s\n", doc.error().what().c_str());
                return false;
            }
            // An edit that re-arms the NES tap carries its SyncTap in the plan's swap, so the wiring and the
            // tap land in one drain or not at all. routing() already holds the mode this edit set.
            rp::graph::HostCommand trailer{};
            if (rearmsTap) {
                const DspCommand c = syncTapFor(graph_->translator->routing(), *table);
                std::memcpy(trailer.bytes, &c, sizeof c);
            }
            const auto result = graph_->host->setGraph(*doc, std::move(table), rearmsTap ? &trailer : nullptr);
            for (const auto& e : result.errors)
                std::fprintf(stderr, "[engine] a graph was refused (%s) at %s.%s: %s\n", e.code.c_str(), e.nodeId.c_str(),
                             e.port.c_str(), e.message.c_str());
            if (result.ok) hostWidth_ = hostWidthOf(*doc);
            return result.ok;
        },
        [this](SystemBase* core) { registry_.release(core->id()); });
}

Engine::~Engine() = default;

const void* Engine::graphInterface(void* ctx, const char* id) {
    auto* self = static_cast<Engine*>(ctx);
    if (std::strcmp(id, RP_HOST_CORES_INTERFACE) == 0) return &self->graph_->cores;
    if (std::strcmp(id, RP_ENGINE_KERNEL_INTERFACE) == 0) return &self->graph_->kernel;
    return nullptr;
}

rp::host::Translator& Engine::translator() { return *graph_->translator; }

bool Engine::pushCommand(const DspCommand& c) { return graph_->host->pushHostCommand(&c, sizeof c); }

bool Engine::pushSyncTap() {
    // Behind every plan already pushed, so the table it is applied against is the last one published: the
    // translator's own.
    return pushCommand(syncTapFor(graph_->translator->routing(), graph_->translator->table()));
}

SystemBase* Engine::takeCore(std::uint64_t token) {
    const auto it = parked_.find(static_cast<SystemId>(token));
    if (token <= UINT32_MAX && it != parked_.end()) {
        SystemBase* core = it->second.release();
        parked_.erase(it);
        if (inSetGraph_) takenInSetGraph_.insert(core->id());
        return core;
    }
    return graph_->translator->take(token);
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
        table->slots.push_back({id, core, dynamic_cast<SameBoySystem*>(core), dynamic_cast<MesenNesSystem*>(core)});
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

void Engine::setAudioThreadOwns(bool owns) { graph_->host->setAudioThreadOwns(owns); }

std::size_t Engine::reclaim() {
    const std::size_t n = graph_->host->reclaim();
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
        case DspCommand::Kind::SetSystems:      delete c.setSystems.json; break;
        case DspCommand::Kind::LoadKernel:      delete c.loadKernel.bytecode; break;
        case DspCommand::Kind::WriteRam:        delete c.writeRam.bytes; break;
        case DspCommand::Kind::StageMidi:       delete c.stageMidi.ext; break;
        case DspCommand::Kind::StageSystemMidi: delete c.stageSystemMidi.ext; break;
        default: break;
    }
}

// Every kind that rides the ring, SyncTap included, a per-system one aimed at the core `table` names. Structure
// never arrives here: the translator turns it into plans.
void Engine::applyCommand(DspCommand& cmd, const rp::host::SystemTable& table) {
    auto core = [&](std::uint32_t id) -> SystemBase* {
        const auto* e = table.find(id);
        return e ? e->core : nullptr;
    };
    switch (cmd.kind) {
        case DspCommand::Kind::SetSystems:
            setSystems(std::vector<std::uint8_t>(cmd.setSystems.json->begin(), cmd.setSystems.json->end()));
            delete cmd.setSystems.json;
            break;
        case DspCommand::Kind::LoadKernel:
            loadKernel(*cmd.loadKernel.bytecode);
            delete cmd.loadKernel.bytecode;
            break;
        case DspCommand::Kind::StageMidi:
            if (cmd.stageMidi.ext) {
                stageMidi(std::move(*cmd.stageMidi.ext));
                delete cmd.stageMidi.ext;
            } else {
                stageMidi(std::vector<std::uint8_t>(cmd.stageMidi.data, cmd.stageMidi.data + cmd.stageMidi.len));
            }
            break;
        case DspCommand::Kind::StageControllerMidi:
            stageControllerMidi(std::vector<std::uint8_t>(cmd.stageMidi.data, cmd.stageMidi.data + cmd.stageMidi.len));
            break;
        case DspCommand::Kind::StageSystemMidi:
            if (cmd.stageSystemMidi.ext) {
                stageSystemMidi(core(cmd.stageSystemMidi.id), cmd.stageSystemMidi.ext->data(), cmd.stageSystemMidi.ext->size());
                delete cmd.stageSystemMidi.ext;
            } else {
                stageSystemMidi(core(cmd.stageSystemMidi.id), cmd.stageSystemMidi.data, cmd.stageSystemMidi.len);
            }
            break;
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

bool Engine::loadKernel(const std::vector<std::uint8_t>& bytecode) {
    dspActive_ = dsp_.loadKernel(bytecode);
    return dspActive_;
}

bool Engine::setSystems(const std::vector<std::uint8_t>& json) {
    return dsp_.setSystems(json);
}

void Engine::stageMidi(std::uint32_t frame, std::vector<std::uint8_t> bytes) {
    pendingMidi_.push_back({ frame, std::move(bytes) });
}

void Engine::stageControllerMidi(std::vector<std::uint8_t> bytes) {
    // Frame 0: a control surface's timing is not musical. A pad press is quantised by the controller app
    // against the block clock, not placed by sample, so an intra-block offset would carry no information.
    pendingControllerMidi_.push_back({ 0, std::move(bytes) });
}

void Engine::setBpm(double bpm) { bpm_ = bpm; }
void Engine::setTransport(bool playing) { transport_ = playing; }
void Engine::setPpq(double ppq) { ppq_ = ppq < 0.0 ? 0.0 : ppq; }
// The DSP kernel stage, run by rp.dsp.kernel as the plan's first step: run the kernel (if one is loaded),
// then hand its output to the cores, all of serialIn, then coreMidi, then coreBytes, then buttons, each in
// emission order. findSystem reads the running plan's table.
void Engine::runKernelStage(std::uint32_t frames) {
    if (dspActive_) {
#ifdef RETROPLUG_PROFILE
        dsp_.spanBegin(DSP_SPAN_KERNEL);  // the whole DSP-kernel stage (marshal + JS + sink fan-out)
#endif
        const DspRuntime::BlockInfo dInfo{ frames, sampleRate_, bpm_, ppq_, transport_, controllerConnected_ };
        dsp_.processBlock(pendingMidi_, kNoButtons, kNoKeys, pendingSerialOut_, pendingControllerMidi_, dInfo);
        pendingMidi_.clear();       // staged host MIDI consumed this block
        pendingControllerMidi_.clear();
        pendingSerialOut_.clear();  // last block's serial-out consumed by the kernel this block
        // serial-in sink → the addressed system's serial FIFO.
        for (const auto& sv : dsp_.serialIn_)
            if (SystemBase* t = findSystem(sv.system)) t->pushSerialIn(sv.frame, sv.byte);
        // core-MIDI sink → the addressed core's onMidi (e.g. the NES N8 FIFO). One ::MidiEvent per entry.
        // A message longer than the inline data[4] (a SysEx, or several messages staged as one run) goes
        // down the core's RAW byte path instead: for the NES that is the same FIFO onMidi feeds, unframed
        // and uncapped, and a core with no byte device (SameBoy's mGB speaks 3-byte messages only) ignores
        // it - which also keeps a dangling MidiEvent::dataExt out of SameBoy's queued copy.
        for (const auto& cm : dsp_.coreMidi_) {
            if (cm.data.empty()) continue;
            SystemBase* t = findSystem(cm.system);
            if (!t) continue;
            if (cm.data.size() > ::MidiEvent::kDataSize) {
                t->pushCoreBytes(cm.frame, cm.data.data(), cm.data.size(), false);
                continue;
            }
            ::MidiEvent ev{};
            ev.frame = cm.frame;
            ev.size  = static_cast<std::uint32_t>(cm.data.size());
            for (std::size_t j = 0; j < cm.data.size(); ++j) ev.data[j] = cm.data[j];
            t->onMidi(&ev, 1);
        }
        // raw core-bytes sink → the addressed core's byte device (e.g. the NES N8 FIFO). Un-framed: no
        // MidiEvent, so NO length cap — a byte protocol (a tracker's host sync) can exceed 4 bytes.
        for (const auto& cb : dsp_.coreBytes_) {
            if (cb.data.empty()) continue;
            if (blockTracing_) for (std::uint8_t b : cb.data) syncTrace_.byte(cb.frame, b);
            // Mirror the message to any external sink (the standalone's physical-N8 link) BEFORE the core
            // dispatch, so the real cart receives byte-identical data to the emulated FIFO. No-op unless set.
            if (coreByteSink_) coreByteSink_(cb.frame, cb.data.data(), cb.data.size(), cb.flush);
            if (SystemBase* t = findSystem(cb.system))
                t->pushCoreBytes(cb.frame, cb.data.data(), cb.data.size(), cb.flush);
        }
        // role-generated button presses → the addressed core.
        for (const auto& bo : dsp_.buttonOut_)
            if (SystemBase* t = findSystem(bo.system))
                t->pressButton(static_cast<std::uint8_t>(bo.button), bo.down);
#ifdef RETROPLUG_PROFILE
        dsp_.spanEnd();  // dsp-kernel
#endif
    }
#ifdef RETROPLUG_PROFILE
    dsp_.spanBegin(DSP_SPAN_APU);  // the plan's systems after this step (ended in processBlock)
#endif
}

// The translator's host shape decides who goes where: the caller set it before rendering.
void Engine::processBlock(std::uint32_t frames, float* const* outputs, std::size_t numOutputs) {
    // An empty block runs nothing, and the tail must not either: the cores never ran, so the MI.OUT gather
    // would collect the last block's log a second time. There are no frames to silence.
    if (frames == 0) return;
    // Longer than the plan's buffers: the GraphHost would refuse it, so nothing runs, and the tail must not
    // either (no MI.OUT gather of a log the cores never cleared, no publish, no ppq advance). A host that
    // reported its block size (setMaxBlockFrames) never gets here; one that did not is counted, for
    // reclaim() to report on the control thread.
    if (frames > graph_->maxFrames) {
        for (std::size_t c = 0; c < numOutputs; ++c)
            if (outputs[c]) std::memset(outputs[c], 0, frames * sizeof(float));
        oversizedFrames_.store(frames, std::memory_order_relaxed);
        oversizedBlocks_.fetch_add(1, std::memory_order_release);
        return;
    }
    blockTracing_ = syncTrace_.beginBlock(frames, sampleRate_, bpm_, ppq_, transport_);
    const rp_transport transport{sampleRate_, bpm_, ppq_, transport_ ? std::uint32_t{RP_TRANSPORT_PLAYING} : 0u};
    // The kernel stage, every system, the mixing and the host outputs (the host drained before this).
    graph_->host->run(frames, transport, outputs, static_cast<std::uint32_t>(numOutputs));
#ifdef RETROPLUG_PROFILE
    dsp_.spanEnd();  // apu-render, begun at the end of the kernel stage
#endif
    const rp::host::SystemTable& table = currentTable();
    // MI.OUT: in slot order with each byte's frame dropped, for the kernel's next block.
    if (dspActive_)
        for (const auto& e : table.slots) {
            if (!e.sameboy || e.sameboy->serialOutLog_.empty()) continue;
            for (const auto& entry : e.sameboy->serialOutLog_) pendingSerialOut_.push_back({ e.id, entry.second });
        }
#ifdef RETROPLUG_PROFILE
    dsp_.spanBegin(DSP_SPAN_PUBLISH);  // the audio->control-plane state pump (per-block frame + timed savestate)
#endif
    for (const auto& e : table.slots) registry_.publishSystem(*e.core, frames, sampleRate_);
#ifdef RETROPLUG_PROFILE
    dsp_.spanEnd();  // state-publish
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

bool Engine::stageSystemMidi(SystemBase* sys, const std::uint8_t* bytes, std::size_t len) {
    if (!bytes || len == 0 || !sys) return false;
    // Frame 0: a knob has no musical position, exactly as stageControllerMidi argues for a pad press.
    //
    // pushCoreBytes, not onMidi, and the difference is load-bearing: onMidi is the MIDI-FRAMED door, and the
    // NES role's implementation clamps every event to MidiEvent::kDataSize and ignores dataExt
    // (NesN8FifoRole.cpp), so a SysEx through it arrives as its first four bytes and the cart waits forever
    // for an F7 that was thrown away. pushCoreBytes is the raw door the DSP kernel itself uses for this
    // transport (see the CoreBytes sink below), with no framing and no cap - which is what a rig message
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
                // the translator rewires the cable (QueuedInvoker::applyConfigField)
            }
            break;
        }
        case ConfigField::FastBoot:
            sb->setFastBoot(value != 0.0);  // deferred to the next restart
            break;
        case ConfigField::SerialOutCapture:
            sb->setSerialOutCapture(value != 0.0);  // live — arms LSDj MI.OUT serial-out capture
            break;
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
