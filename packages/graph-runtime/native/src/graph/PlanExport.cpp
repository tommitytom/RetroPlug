#include "graph/PlanExport.hpp"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <utility>

#include "graph/CanonicalJson.hpp"
#include "graph/Executor.hpp"
#include "graph/JsonToMsgpack.hpp"
#include "graph/PackageMemory.hpp"
#include "graph/PlanBlob.hpp"
#include "graph/PlanStorage.hpp"
#include "graph/ResourceTable.hpp"
#include "graph/ThreadClock.hpp"
#include "graph/node.hpp"
#include "graph/rp_device.h"
#include "graph/rp_device_midi_in.h"
#include "graph/rp_script.h"

namespace rp::graph {

namespace {

using namespace blob;

constexpr const char* kNotExported = "resources are not exported to rp2350 yet";
// What the exporter's resource table answers a node whose settings name a resource; a create that fails with it is a
// "resource" refusal. The node's own words come first: `rom: "res:7" is named, and resources are ...`.
constexpr const char* kNamed = "is named, and resources are not exported to rp2350 yet";

// ---- the exporter's host --------------------------------------------------------------------------------------------

// Its devices interface answers every acquire with null, so a node that binds a device (rp.n8.nes) runs on its own
// disconnected stub, as in any host that owns no device; one that finds no devices interface at all refuses to
// activate, so the exporter offers this one rather than none. Its MIDI input (rp.device.midi-in/1) has nothing staged,
// as a box's has before its first byte, so rp.device.midi-in plans and loads here as it does on the box. Its resource
// table holds nothing and says why.
void* acquireNoDevice(void*, const char*, const char*) { return nullptr; }
void  releaseNoDevice(void*, void*) {}
const rp_host_devices kDevices{nullptr, &acquireNoDevice, &releaseNoDevice};

bool                    nothingStaged(void*, std::uint32_t, rp_device_midi_message*) { return false; }
const rp_device_midi_in kMidiIn{nullptr, &nothingStaged};

const rp_resource* refuseResource(void*, std::uint32_t, const char*, rp_error* err) {
    if (err) std::snprintf(err->message, sizeof err->message, "%s", kNamed);
    return nullptr;
}
bool releaseNoResource(void*, std::uint32_t) { return false; }
const rp_host_resources kResources{nullptr, &refuseResource, &releaseNoResource};

// Its package memory (rp.host.packages/1) is the exporter's own for the plan it makes, its host's ctx: what the
// desktop's builds of a package with memory (a script pack's) run their planned nodes in. The trial load below sets the
// device's builds up in the arena, as the device does. Its clock (rp.host.clock/1) is the thread's CPU time at the
// desktop's deadline, so a script node whose create or first block runs on is faulted there, not left to hang the
// export.
const rp_host_clock kTrialClock{nullptr, &threadCpuMicros, RP_SCRIPT_DESKTOP_DEADLINE_MICROS};
void        ignoreLog(void*, int, const char*) {}
const void* hostInterface(const rp_host* host, const char* id) {
    if (std::strcmp(id, RP_HOST_CLOCK_INTERFACE) == 0) return &kTrialClock;
    if (std::strcmp(id, RP_HOST_DEVICES_INTERFACE) == 0) return &kDevices;
    if (std::strcmp(id, RP_DEVICE_MIDI_IN_INTERFACE) == 0) return &kMidiIn;
    if (std::strcmp(id, RP_HOST_RESOURCES_INTERFACE) == 0) return &kResources;
    if (std::strcmp(id, RP_HOST_PACKAGES_INTERFACE) == 0) return static_cast<PackageMemory*>(host->ctx)->service();
    return nullptr;
}

// ---- the plan, as records -------------------------------------------------------------------------------------------

// Writes a PlanStorage the planner built as the blob's records. Every pool is numbered in the planner's own order (its
// buffers, queues and streams as it made them, the instances' port states in the document's order, the feedback fronts
// in wire order), and every table follows the plan's own (its steps, its queues to clear, its adoption lists), so the
// bytes depend on the document and the types alone: the maps below are keyed by address for lookup, and never walked.
class Writer {
public:
    Writer(const PlanStorage& st, std::vector<PlanError>& errors) : st_(st), errors_(errors) {}

    bool write(const ExportOptions& options, std::vector<std::uint8_t>& out) {
        numberPools();
        if (!nodes() || !steps() || !afterBlock() || !atAdoption()) return false;
        assemble(options, out);
        return true;
    }

private:
    struct Run {
        std::uint32_t offset = 0;
        std::uint32_t count = 0;
    };

    const PlanStorage&                                   st_;
    std::vector<PlanError>&                              errors_;
    std::map<const rp_node*, std::uint32_t>              nodeIndex_;
    std::map<const std::atomic<std::uint32_t>*, std::uint32_t> dropsIndex_;
    std::map<const float*, std::uint32_t>                buffers_;
    std::map<float* const*, Run>                         channelRuns_;
    std::map<const rp_queue*, std::uint32_t>             queueIndex_;
    std::map<const rp_control_queue*, std::uint32_t>     streamIndex_;
    std::map<rp_resource* const*, std::uint32_t>         cellIndex_;
    std::map<std::string, Bytes>                         stringAt_;

    std::vector<std::uint8_t>     strings_, settings_, maskBytes_;
    std::vector<NodeRecord>       nodes_;
    std::vector<StepRecord>       steps_;
    std::vector<ViewRecord>       views_;
    std::vector<ParamViewRecord>  paramViews_;
    std::vector<std::uint32_t>    channels_, rolls_, peerNodes_;
    std::vector<QueueRecord>      queues_;
    std::vector<StreamRecord>     streams_;
    std::vector<ClearRecord>      clears_;
    std::vector<FeedbackRecord>   feedback_;
    std::vector<MaskRecord>       masks_;
    std::vector<PeerRecord>       peers_;
    std::vector<ParamStartRecord> paramStarts_;

    bool internal(const std::string& what) {
        errors_.push_back({"", "", "export-internal", "the planned plan cannot be written: " + what});
        return false;
    }

    // ---- pools ----

    void addBuffer(const float* b) { buffers_.emplace(b, static_cast<std::uint32_t>(buffers_.size())); }
    void addQueue(const rp_queue& q) {
        if (queueIndex_.emplace(&q, static_cast<std::uint32_t>(queues_.size())).second)
            queues_.push_back({q.capacity, q.bytes_capacity});
    }
    void addStream(const rp_control_queue& q) {
        if (!streamIndex_.emplace(&q, static_cast<std::uint32_t>(streams_.size())).second) return;
        StreamRecord r{q.capacity, {}};
        splitDouble(q.start, r.start);
        streams_.push_back(r);
    }

    void numberPools() {
        for (std::size_t i = 0; i < st_.instances.size(); ++i) {
            nodeIndex_[st_.instances[i]->node] = static_cast<std::uint32_t>(i);
            dropsIndex_[&st_.instances[i]->drops] = static_cast<std::uint32_t>(i);
        }
        addBuffer(st_.silence.get());
        addBuffer(st_.scratch.get());
        for (const auto& a : st_.audio) addBuffer(a.get());
        addQueue(st_.emptyQueue->queue);
        addQueue(st_.scratchQueue->queue);
        for (const auto& q : st_.queues) addQueue(q->queue);
        addStream(st_.emptyStream->queue);
        for (const auto& ports : st_.ports) {
            for (const auto& p : ports->params) addStream(p->queue);
            for (const auto& c : ports->controlOutputs)
                if (c) addStream(c->queue);
        }
        for (const auto& f : st_.feedbackFronts) {
            for (float* channel : f->pointers) addBuffer(channel);
            if (f->queue) addQueue(f->queue->queue);
            if (f->stream) addStream(f->stream->queue);
        }
    }

    bool channelRun(float* const* channels, std::uint32_t count, std::uint32_t* offset) {
        const auto it = channelRuns_.find(channels);
        if (it != channelRuns_.end()) {
            if (count > it->second.count) return internal("one channel array is read at two widths");
            *offset = it->second.offset;
            return true;
        }
        const Run run{static_cast<std::uint32_t>(channels_.size()), count};
        for (std::uint32_t c = 0; c < count; ++c) {
            const auto b = buffers_.find(channels[c]);
            if (b == buffers_.end()) return internal("an audio channel is no buffer of the plan's");
            channels_.push_back(b->second);
        }
        channelRuns_.emplace(channels, run);
        *offset = run.offset;
        return true;
    }
    bool queue(const rp_queue* q, std::uint32_t* index) {
        const auto it = queueIndex_.find(q);
        if (it == queueIndex_.end()) return internal("a queue is none of the plan's");
        *index = it->second;
        return true;
    }
    bool stream(const rp_control_queue* q, std::uint32_t* index) {
        const auto it = streamIndex_.find(q);
        if (it == streamIndex_.end()) return internal("a control stream is none of the plan's");
        *index = it->second;
        return true;
    }
    bool node(const rp_node* n, std::uint32_t* index) {
        const auto it = nodeIndex_.find(n);
        if (it == nodeIndex_.end()) return internal("a node is none of the plan's instances");
        *index = it->second;
        return true;
    }

    bool view(const rp_port_view& v, ViewRecord& r) {
        r = ViewRecord{v.kind, 0, 0};
        switch (v.kind) {
            case RP_PORT_AUDIO:
                r.count = v.u.audio.channel_count;
                return channelRun(v.u.audio.channels, r.count, &r.index);
            case RP_PORT_EVENTS:
            case RP_PORT_BYTES: return queue(v.u.queue, &r.index);
            case RP_PORT_CONTROL: return stream(v.u.control, &r.index);
            case RP_PORT_RESOURCE:
                // A cell a resource wire shares, or an unbound input's: either holds null in an exported plan, since a
                // resource is refused before planning and no node has run to write one.
                if (*v.u.resource) return internal("a resource slot holds a resource");
                r.index = cellIndex_.emplace(v.u.resource, static_cast<std::uint32_t>(cellIndex_.size())).first->second;
                return true;
            case RP_PORT_LINK: return true;
            default: return internal("a port has kind " + std::to_string(v.kind));
        }
    }

    // ---- tables ----

    Bytes string(const std::string& s) {
        const auto it = stringAt_.find(s);
        if (it != stringAt_.end()) return it->second;
        const Bytes b{static_cast<std::uint32_t>(strings_.size()), static_cast<std::uint32_t>(s.size())};
        strings_.insert(strings_.end(), s.begin(), s.end());
        stringAt_.emplace(s, b);
        return b;
    }

    // Each instance's settings as it parsed them (rp.sdk.settings-json/1), as msgpack.
    bool settingsOf(const Instance& inst, Bytes& out) {
        const auto* writer =
            static_cast<const SettingsJsonInterface*>(inst.node->vt->get_interface(inst.node, kSettingsJsonInterface));
        if (!writer) {
            errors_.push_back({inst.id, "", "settings-export",
                               "node type " + jsonString(inst.entry->descriptor.id) +
                                   " cannot write its settings back (" + kSettingsJsonInterface +
                                   "), so it cannot be exported"});
            return false;
        }
        std::string json(256, '\0');
        std::size_t n = writer->write(inst.node, json.data(), json.size());
        if (n >= json.size()) {
            json.assign(n + 1, '\0');
            n = writer->write(inst.node, json.data(), json.size());
        }
        if (n == 0 || n >= json.size()) {
            errors_.push_back({inst.id, "", "settings-export", "the node could not write its settings back"});
            return false;
        }
        json.resize(n);
        auto bytes = settingsToMsgpack(json);
        if (!bytes) {
            errors_.push_back({inst.id, "", "settings-export", bytes.error().what()});
            return false;
        }
        out = Bytes{static_cast<std::uint32_t>(settings_.size()), static_cast<std::uint32_t>(bytes->size())};
        settings_.insert(settings_.end(), bytes->begin(), bytes->end());
        return true;
    }

    bool nodes() {
        for (const auto& inst : st_.instances) {
            NodeRecord r{};
            r.id = string(inst->id);
            r.type = string(inst->entry->descriptor.id);
            if (!settingsOf(*inst, r.settings)) return false;
            nodes_.push_back(r);
        }
        return true;
    }

    bool steps() {
        std::uint32_t group = 0, groupEnd = 0;
        for (std::size_t i = 0; i < st_.steps.size(); ++i) {
            const PlanStep& s = st_.steps[i];
            StepRecord r{};
            if (!node(s.node, &r.node)) return false;
            r.groupSize = s.groupSize;
            // A coupled group is one group however many steps it takes; every other step is a group of its own.
            if (i >= groupEnd) {
                if (i > 0) ++group;
                groupEnd = static_cast<std::uint32_t>(i) + (s.groupSize > 1 ? s.groupSize : 1);
            }
            nodes_[r.node].group = group;
            nodes_[r.node].step = static_cast<std::uint32_t>(i);
            r.firstInput = static_cast<std::uint32_t>(views_.size());
            r.inputCount = s.process.input_count;
            for (std::uint32_t k = 0; k < s.process.input_count; ++k) {
                ViewRecord v;
                if (!view(s.process.inputs[k], v)) return false;
                views_.push_back(v);
            }
            r.firstOutput = static_cast<std::uint32_t>(views_.size());
            r.outputCount = s.process.output_count;
            for (std::uint32_t k = 0; k < s.process.output_count; ++k) {
                ViewRecord v;
                if (!view(s.process.outputs[k], v)) return false;
                views_.push_back(v);
            }
            r.firstParam = static_cast<std::uint32_t>(paramViews_.size());
            r.paramCount = s.process.param_count;
            const PortState& ports = *st_.ports[r.node];
            if (ports.params.size() != s.process.param_count) return internal("a step's params are not its node's");
            for (std::uint32_t k = 0; k < s.process.param_count; ++k) {
                // A param reads its own stream unless a wire drives it, when it reads the wire's source instead.
                ParamViewRecord v{0, s.process.params[k] != &ports.params[k]->queue ? 1u : 0u};
                if (!stream(s.process.params[k], &v.stream)) return false;
                paramViews_.push_back(v);
            }
            steps_.push_back(r);
        }
        return true;
    }

    bool afterBlock() {
        for (const OutputQueue& o : st_.queuesToClear) {
            ClearRecord r{0, kNoNode};
            if (!queue(o.queue, &r.queue)) return false;
            if (o.drops) {
                const auto it = dropsIndex_.find(o.drops);
                if (it == dropsIndex_.end()) return internal("a queue's drops are charged to no instance");
                r.node = it->second;
            }
            clears_.push_back(r);
        }
        for (const rp_control_queue* c : st_.controlsToRoll) {
            std::uint32_t index = 0;
            if (!stream(c, &index)) return false;
            rolls_.push_back(index);
        }
        for (const FeedbackCopy& f : st_.feedback) {
            FeedbackRecord r{static_cast<std::uint32_t>(f.kind), 0, 0, 0, 0};
            bool ok = true;
            if (f.kind == RP_PORT_AUDIO) {
                r.channels = f.channels;
                r.frames = f.capacity;
                ok = channelRun(f.backChannels, f.channels, &r.back) &&
                     channelRun(f.frontChannels, f.channels, &r.front);
            } else if (f.kind == RP_PORT_CONTROL) {
                ok = stream(f.backControl, &r.back) && stream(f.frontControl, &r.front);
            } else {
                ok = queue(f.backQueue, &r.back) && queue(f.frontQueue, &r.front);
            }
            if (!ok) return false;
            feedback_.push_back(r);
        }
        return true;
    }

    bool atAdoption() {
        if (!st_.commits.empty()) return internal("a fresh plan commits settings");
        for (const auto& p : st_.peers) {
            PeerRecord r{0, static_cast<std::uint32_t>(peerNodes_.size()), static_cast<std::uint32_t>(p.peers.size())};
            if (!node(p.node, &r.node)) return false;
            for (const rp_node* peer : p.peers) {
                std::uint32_t index = 0;
                if (!node(peer, &index)) return false;
                peerNodes_.push_back(index);
            }
            peers_.push_back(r);
        }
        for (const auto& p : st_.portsChanged) {
            MaskRecord r{0, static_cast<std::uint32_t>(maskBytes_.size()), static_cast<std::uint32_t>(p.mask.size())};
            if (!node(p.node, &r.node)) return false;
            maskBytes_.insert(maskBytes_.end(), p.mask.begin(), p.mask.end());
            masks_.push_back(r);
        }
        for (const auto& s : st_.paramSets) {
            ParamStartRecord r{};
            if (!stream(s.queue, &r.stream)) return false;
            splitDouble(s.value, r.value);
            paramStarts_.push_back(r);
        }
        return true;
    }

    // ---- the bytes ----

    template <class T>
    static void append(std::vector<std::uint8_t>& out, const T* data, std::size_t count) {
        const auto* p = reinterpret_cast<const std::uint8_t*>(data);
        out.insert(out.end(), p, p + count * sizeof(T));
        while (out.size() % 4) out.push_back(0);
    }

    void assemble(const ExportOptions& options, std::vector<std::uint8_t>& out) {
        Header h{};
        std::memcpy(h.magic, kMagic, sizeof kMagic);
        h.version = kVersion;
        h.target = kTargetRp2350;
        splitDouble(options.sampleRate, h.sampleRate);
        h.maxFrames = options.maxFrames;
        h.arenaBytes = static_cast<std::uint32_t>(options.arenaBytes);
        h.buffers = static_cast<std::uint32_t>(buffers_.size());
        h.cells = static_cast<std::uint32_t>(cellIndex_.size());
        h.sectionCount = kSectionCount;
        out.assign(sizeof(Header), 0);
        auto section = [&](Section s, const auto& records) {
            SectionEntry& e = h.sections[static_cast<std::uint32_t>(s)];
            e.offset = static_cast<std::uint32_t>(out.size());
            e.count = static_cast<std::uint32_t>(records.size());
            e.recordBytes = recordBytes(s);
            append(out, records.data(), records.size());
        };
        section(Section::Strings, strings_);
        section(Section::Settings, settings_);
        section(Section::Nodes, nodes_);
        section(Section::Steps, steps_);
        section(Section::Views, views_);
        section(Section::ParamViews, paramViews_);
        section(Section::Channels, channels_);
        section(Section::Queues, queues_);
        section(Section::Streams, streams_);
        section(Section::Clears, clears_);
        section(Section::Rolls, rolls_);
        section(Section::Feedback, feedback_);
        section(Section::Masks, masks_);
        section(Section::MaskBytes, maskBytes_);
        section(Section::Peers, peers_);
        section(Section::PeerNodes, peerNodes_);
        section(Section::ParamStarts, paramStarts_);
        h.size = static_cast<std::uint32_t>(out.size() + sizeof(std::uint32_t));
        std::memcpy(out.data(), &h, sizeof h);
        const std::uint32_t crc = crc32(out.data(), out.size());
        append(out, &crc, 1);
    }
};

std::string text(const char* s, std::size_t capacity) {
    std::size_t n = 0;
    while (n < capacity && s[n]) ++n;
    return std::string(s, n);
}

}  // namespace

ExportResult exportPlan(const GraphDocument& document, const NodeRegistry& registry,
                        std::span<const rp_package* const> deviceTypes, const ExportOptions& options) {
    ExportResult result;
    if (options.target != Target::Pico) {
        result.errors.push_back({"", "", "target", std::string("a plan is exported for ") + kTargetName + " alone"});
        return result;
    }
    if (options.maxFrames == 0 || !std::isfinite(options.sampleRate) || options.sampleRate <= 0.0) {
        result.errors.push_back({"", "", "export-options", "a plan's sample rate and maxFrames must be positive"});
        return result;
    }
    if (options.arenaBytes > 0xFFFFFFFFu) {
        result.errors.push_back({"", "", "export-options", "an arena of 4 GiB or more is no device's"});
        return result;
    }
    // A binding names a resource outright; a settings field that names one is refused at create, by the host's table.
    for (const auto& n : document.nodes)
        if (n.resources)
            for (const auto& binding : *n.resources)
                result.errors.push_back({n.id, binding.first, "resource", kNotExported});
    if (!result.errors.empty()) return result;

    // Declared before the plan, so the packages its nodes were made under are torn down after them.
    PackageMemory                                         packageMemory;
    const rp_host host{RP_ABI_VERSION, &packageMemory, nullptr, nullptr, &ignoreLog, &hostInterface};
    ResourceTable                                         resources;
    const std::map<std::string, std::shared_ptr<Instance>>      live;
    const std::map<std::string, std::shared_ptr<FeedbackFront>> fronts;
    std::atomic<std::uint32_t>                            faults{0}, drops{0};
    PlanInput in;
    in.doc = &document;
    in.registry = &registry;
    in.resources = &resources;
    in.live = &live;
    in.feedback = &fronts;
    in.host = &host;
    in.sampleRate = options.sampleRate;
    in.maxFrames = options.maxFrames;
    in.target = options.target;
    in.faults = &faults;
    in.drops = &drops;
    PlanOutcome planned = plan(in);
    for (auto& e : planned.errors) {
        if (e.code == "create-failed" && e.message.find(kNamed) != std::string::npos) e.code = "resource";
        result.errors.push_back(std::move(e));
    }
    if (!result.errors.empty()) return result;

    std::vector<std::uint8_t> blob;
    if (!Writer(*planned.storage, result.errors).write(options, blob)) return result;
    PlanLoadError err{};
    auto refuse = [&] {
        result.errors.push_back({text(err.node, sizeof err.node), "", text(err.code, sizeof err.code),
                                 text(err.message, sizeof err.message)});
        return result;
    };
    // Sized for the target: its packages' own memory as an rp2350 build takes it (a script pack's context is
    // word-sized, about half what these 64-bit builds take), and the rest with these builds, an upper bound on the
    // device's own. That is the figure the arena is held to and the estimate an image is given.
    if (!sizePlan(blob, deviceTypes, options.arenaBytes, &result.size, &err, kTargetPointerBytes)) return refuse();
    // With no budget the sizing pass refuses nothing below 2^48; a plan past what a 32-bit arena addresses is no
    // device's, and the trial load below would allocate it here.
    if (result.size.bytes > 0xFFFFFFFFull) {
        std::snprintf(err.code, sizeof err.code, "arena");
        std::snprintf(err.message, sizeof err.message, "the plan needs %llu bytes, more than any rp2350 arena holds",
                      static_cast<unsigned long long>(result.size.bytes));
        return refuse();
    }
    // The trial load: the blob laid out, created and activated by the device's builds on the exporter's host, in an
    // arena of exactly the size these builds measure (their own width's, the packages' memory included), then
    // unloaded. What the device's create or activate would refuse at boot (a refusal a device build makes and the
    // desktop's does not) is refused here instead, in the loader's words.
    if (options.trialLoad) {
        PlanSize own;
        if (!sizePlan(blob, deviceTypes, 0, &own, &err)) return refuse();
        std::vector<std::max_align_t> arena(own.bytes / sizeof(std::max_align_t) + 1);
        LoadedPlan* loaded = loadPlan(blob, std::span<std::byte>(reinterpret_cast<std::byte*>(arena.data()),
                                                                 own.bytes),
                                      deviceTypes, &host, &err);
        if (!loaded) return refuse();
        // One block, every input empty: a script node is made at its first block (and its pack's heap sized there for
        // the plan's queues), so what only that refuses is seen here, as its fault.
        const rp_transport transport{options.sampleRate, 120.0, 0.0, 0};
        runBlock(loaded->view, options.maxFrames, transport);
        for (const LoadedNode& n : loaded->nodes) {
            const auto* fault =
                static_cast<const rp_script_fault*>(n.node->vt->get_interface(n.node, RP_SCRIPT_FAULT_INTERFACE));
            const char* words = fault ? fault->reason(n.node) : nullptr;
            if (words) {
                result.errors.push_back({std::string(n.name()), "", "create-failed",
                                         std::string("its first block faulted on the device's build: ") + words});
            }
        }
        unloadPlan(loaded);
        if (!result.errors.empty()) return result;
    }
    result.blob = std::move(blob);
    return result;
}

ExportResult exportPlan(std::string_view documentJson, const NodeRegistry& registry,
                        std::span<const rp_package* const> deviceTypes, const ExportOptions& options) {
    auto doc = parseGraphDocument(documentJson);
    if (!doc) {
        ExportResult r;
        r.errors.push_back({"", "", "document", std::string("not a graph document: ") + doc.error().what()});
        return r;
    }
    return exportPlan(*doc, registry, deviceTypes, options);
}

}  // namespace rp::graph
