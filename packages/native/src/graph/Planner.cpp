#include "graph/Planner.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>
#include <numeric>
#include <set>

#include "graph/CanonicalJson.hpp"
#include "graph/Queue.hpp"

namespace rp::graph {

namespace {

constexpr int kNone = -1;
constexpr int kEndOfBlock = std::numeric_limits<int>::max();

// Document text quoted for a message: escaped, so whatever it holds prints intact.
std::string q(std::string_view s) { return jsonString(s); }


struct NodePlan {
    const NodeDoc*             doc = nullptr;
    const NodeRegistry::Entry* entry = nullptr;
    std::shared_ptr<Instance>  inst;
    std::shared_ptr<PortState> ports;     // the instance's for `layout`: its current ones, or a reshape's new ones
    bool                       fresh = false;
    void*                      prepared = nullptr;
    std::string                settings;
    Layout                     layout;    // the type's layout_for these settings: the instance's, once it exists
    std::vector<int>           inPorts;   // layout port indices of non-output ports, in order
    std::vector<int>           outPorts;  // layout port indices of output ports, in order
    std::vector<int>           ordinal;   // layout port index -> input or output ordinal
    int                        group = 0;
    int                        order = 0;
};

struct DataWire {
    int         from = 0, fromPort = 0;           // node, layout port index (an output)
    int         to = 0, toPort = kNone, toParam = kNone;
    bool        feedback = false;
    PortKind    kind = PortKind::Audio;
    std::string key;
};

struct Resolved {
    enum class State : std::uint8_t { Unvisited, Visiting, Failed };
    std::uint32_t count = 0;
    std::string   layout;
    bool          ok = false;
    State         state = State::Unvisited;  // meaningful until ok
};

int outputOrdinal(const Layout& l, const PortDesc* port) {
    int o = 0;
    for (const auto& p : l.ports) {
        if (&p == port) return o;
        if (p.dir == PortDir::Out) ++o;
    }
    return kNone;
}

// Is this queue laid out for that capacity?
bool laidOutFor(const QueueStore& q, const QueueCapacity& c) {
    return q.queue.capacity == c.messageCount() && q.queue.bytes_capacity == c.byteCount();
}

// What a queue of that capacity takes: its message items and its payload bytes.
std::size_t queueMemory(const QueueCapacity& c) {
    return std::size_t{c.messageCount()} * sizeof(rp_message) + c.byteCount();
}

// A value means what it meant: the same type, range and enum labels (a name, unit or default may change).
bool sameValues(const ParamDesc& a, const ParamDesc& b) {
    return a.type == b.type && a.min == b.min && a.max == b.max && a.values == b.values;
}

// An instance's ports for a layout. A reshape passes the instance's current ones as `carry`, which lends every
// stream and slot whose param or port persists by id (a param whose values mean the same, a control or resource
// output of the same kind and type), with the document value last sent to it; the rest start afresh. Lent, never
// copied: the running plan keeps writing them while this one is built, and this thread only takes addresses.
std::shared_ptr<PortState> portState(const Layout& layout, const PortState* carry) {
    auto st = std::make_shared<PortState>();
    st->layout = layout;
    for (const auto& param : layout.params) {
        const ParamDesc* was = carry ? findParam(carry->layout, param.id) : nullptr;
        if (was && sameValues(*was, param)) {
            st->params.push_back(carry->params[static_cast<std::size_t>(was - carry->layout.params.data())]);
            const auto applied = carry->appliedParams.find(param.id);
            if (applied != carry->appliedParams.end()) st->appliedParams.insert(*applied);
        } else {
            st->params.push_back(std::make_shared<StreamStore>(kStreamChanges, param.def));
        }
    }
    for (const auto& port : layout.ports) {
        if (port.dir != PortDir::Out) continue;
        const PortDesc* was = carry ? findPort(carry->layout, port.id) : nullptr;
        const bool kept =
            was && was->dir == PortDir::Out && was->kind == port.kind && was->resourceType == port.resourceType;
        const auto o = static_cast<std::size_t>(kept ? outputOrdinal(carry->layout, was) : 0);
        std::shared_ptr<StreamStore>  control;
        std::shared_ptr<rp_resource*> resource;
        if (port.kind == PortKind::Control)
            control = kept ? carry->controlOutputs[o] : std::make_shared<StreamStore>(kStreamChanges, 0.0);
        else if (port.kind == PortKind::Resource)
            resource = kept ? carry->resourceOutputs[o] : std::make_shared<rp_resource*>(nullptr);
        st->controlOutputs.push_back(std::move(control));
        st->resourceOutputs.push_back(std::move(resource));
    }
    return st;
}

class Builder {
public:
    explicit Builder(const PlanInput& in) : in_(in) {}
    Builder(const Builder&) = delete;
    Builder& operator=(const Builder&) = delete;
    // An allocation failure leaves run() by its exception (plan() reports it), so what it prepared is released here.
    ~Builder() { releasePrepared(); }

    PlanOutcome run() {
        try {
            // Every check that needs a layout runs on the types' layouts before any instance exists, so a
            // document refused here creates nothing.
            if (structure() && params() && wires() && groups() && order() && channels() && exclusive() &&
                resources() && queueBudget() && reconcile() && linkedSteps()) {
                build();
            }
        } catch (const std::bad_alloc&) {
            throw;  // plan() refuses it, once this Builder and all it took are freed
        } catch (const std::exception& e) {
            error("", "", "internal", std::string("planning failed: ") + e.what());
        }
        if (!out_.errors.empty()) rollback();
        return std::move(out_);
    }

private:
    const PlanInput&                      in_;
    PlanOutcome                           out_;
    std::vector<NodePlan>                 nodes_;
    std::map<std::string, int, std::less<>> index_;
    std::vector<DataWire>                 wires_;
    std::vector<std::pair<int, int>>      links_;
    std::vector<int>                      parent_;    // union-find over link wires
    std::vector<std::vector<int>>         groupMembers_;  // in topological order once ordered
    std::vector<std::vector<Resolved>>    resolved_;  // node -> layout port index -> resolution
    std::vector<std::map<int, int>>       inputWire_; // node -> layout port index -> wire
    std::vector<std::map<int, int>>       paramWire_; // node -> param index -> wire
    std::vector<std::vector<std::pair<int, double>>> paramValues_;  // node -> (param index, value)
    std::vector<std::vector<std::pair<int, ResourceTable::Id>>> bindings_;  // node -> (port, resource)

    bool error(std::string node, std::string port, std::string code, std::string message) {
        out_.errors.push_back({std::move(node), std::move(port), std::move(code), std::move(message)});
        return false;
    }

    // ---- structure ---------------------------------------------------------------------------------

    bool structure() {
        for (const auto& n : in_.doc->nodes) {
            if (!isPlainId(n.id) || n.id.find('.') != std::string::npos)
                error(n.id, "", "node-id",
                      "node id " + q(n.id) + " must be non-empty, with no '.', whitespace or control characters");
            else if (index_.count(n.id))
                error(n.id, "", "node-id", "node id " + q(n.id) + " is used twice");
            const auto* entry = in_.registry->find(n.type);
            if (!entry) {
                error(n.id, "", "unknown-type", "no node type " + q(n.type) + " is registered");
                continue;
            }
            if (in_.target == Target::Pico && (entry->descriptor.heap || entry->descriptor.threads))
                error(n.id, "", "capability",
                      "node type " + q(n.type) + " needs " + (entry->descriptor.heap ? "a heap" : "threads") +
                          ", which the Pico target does not provide");
            NodePlan p;
            p.doc = &n;
            p.entry = entry;
            p.settings = n.settings ? canonicalJson(*n.settings) : std::string("{}");
            if (!typeLayout(p)) continue;
            index_.emplace(n.id, static_cast<int>(nodes_.size()));
            for (std::size_t i = 0; i < p.layout.ports.size(); ++i) {
                const bool isOut = p.layout.ports[i].dir == PortDir::Out;
                auto& list = isOut ? p.outPorts : p.inPorts;
                p.ordinal.push_back(static_cast<int>(list.size()));
                list.push_back(static_cast<int>(i));
            }
            out_.layouts[p.doc->id] = layoutJson(p.layout);
            nodes_.push_back(std::move(p));
        }
        return out_.errors.empty();
    }

    // The node type's layout for these settings, asked of the type (no instance needed).
    bool typeLayout(NodePlan& p) {
        const rp_settings s{p.settings.data(), p.settings.size(), RP_SETTINGS_JSON};
        rp_error err{};
        std::string text(256, '\0');
        std::size_t n = p.entry->type->layout_for(&s, text.data(), text.size(), &err);
        if (n >= text.size()) {  // too long for the first guess: it said how long, so ask once more
            text.assign(n + 1, '\0');
            n = p.entry->type->layout_for(&s, text.data(), text.size(), &err);
        }
        if (n == 0) return error(p.doc->id, "", "settings-invalid", errorText(err, "the node refused its settings"));
        if (n >= text.size())
            return error(p.doc->id, "", "node-contract", "layout_for answered a length it then would not fit");
        text.resize(n);
        auto layout = parseLayout(text);
        if (!layout) return error(p.doc->id, "", "layout-invalid", layout.error().what());
        p.layout = std::move(*layout);
        return true;
    }

    // ---- queue memory -------------------------------------------------------------------------------

    // Every queue the plan will hold, against kPlanQueueBudget: the queue of each wired events or bytes output (one,
    // however many wires it feeds) and the front of each feedback wire, sized as its source's queue. A document past
    // it is refused here, on the types' layouts, so it creates nothing.
    bool queueBudget() {
        std::size_t total = 0;
        std::set<std::pair<int, int>> counted;  // outputs whose queue is in `total`
        for (const auto& w : wires_) {
            if (w.kind != PortKind::Events && w.kind != PortKind::Bytes) continue;
            const std::string& id = nodes_[w.from].doc->id;
            const PortDesc& port = nodes_[w.from].layout.ports[w.fromPort];
            auto charge = [&](const std::string& queue) {
                total += queueMemory(port.capacity);
                if (total <= kPlanQueueBudget) return true;
                return error(id, port.id, "queue-budget",
                             queue + " takes the plan's events and bytes queues to " + std::to_string(total) +
                                 " bytes, past their " + std::to_string(kPlanQueueBudget >> 20) +
                                 " MiB budget; declare smaller capacities");
            };
            if (counted.emplace(w.from, w.fromPort).second && !charge("the queue of " + q(id + "." + port.id)))
                return false;
            if (w.feedback && !charge("the feedback front of " + q(w.key))) return false;
        }
        return true;
    }

    // ---- reconcile instances -------------------------------------------------------------------------

    std::shared_ptr<Instance> create(NodePlan& p) {
        const rp_settings s{p.settings.data(), p.settings.size(), RP_SETTINGS_JSON};
        rp_error err{};
        rp_memory_req req{};
        if (!p.entry->type->query_memory(&s, &req, &err)) {
            error(p.doc->id, "", "settings-invalid", errorText(err, "the node refused its settings"));
            return nullptr;
        }
        if (req.align & (req.align - 1)) {  // aligned operator new requires a power of two
            error(p.doc->id, "", "node-memory",
                  "node asked for an alignment of " + std::to_string(req.align) + ", which is not a power of two");
            return nullptr;
        }
        auto inst = std::make_shared<Instance>();
        inst->id = p.doc->id;
        inst->entry = p.entry;
        inst->align = std::max(req.align, alignof(std::max_align_t));
        inst->memory =
            ::operator new(std::max<std::size_t>(req.size, 1), std::align_val_t(inst->align), std::nothrow);
        if (!inst->memory) {
            error(p.doc->id, "", "node-memory",
                  "could not allocate the " + std::to_string(req.size) + " bytes the node asked for");
            return nullptr;
        }
        inst->node = p.entry->type->create(in_.host, inst->memory, &s, &err);
        if (!inst->node) {
            error(p.doc->id, "", "create-failed", errorText(err, "the node could not be created"));
            return nullptr;  // ~Instance frees the memory (node is null, so nothing to destroy)
        }
        inst->settings = p.settings;
        inst->ports = portState(p.layout, nullptr);  // a fresh instance is no one else's until it is published
        if (!inst->node->vt->activate(inst->node, in_.sampleRate, in_.maxFrames, &err)) {
            error(p.doc->id, "", "activate-failed", errorText(err, "the node could not be activated"));
            return nullptr;
        }
        inst->active = true;
        return inst;
    }

    void createFresh(NodePlan& p) {
        p.inst = create(p);
        p.fresh = true;
        if (p.inst) p.ports = p.inst->ports;
    }

    // A same-type node whose settings changed is asked how it takes them, whether or not its layout moved: only
    // the instance knows whether it can keep up with a new layout.
    bool reconcile() {
        for (auto& p : nodes_) {
            const auto it = in_.live->find(p.doc->id);
            if (it == in_.live->end() || it->second->entry != p.entry) {
                createFresh(p);
                continue;
            }
            const std::shared_ptr<Instance>& live = it->second;
            const bool sameLayout = layoutJson(live->ports->layout) == out_.layouts.at(p.doc->id);
            if (sameLayout && live->settings == p.settings) {
                p.inst = live;
                p.ports = live->ports;
            } else {
                const rp_settings s{p.settings.data(), p.settings.size(), RP_SETTINGS_JSON};
                rp_error err{};
                const rp_prepare_result result = live->node->vt->prepare_settings(live->node, &s, &p.prepared, &err);
                switch (result) {
                    case RP_PREPARE_SWAP:
                    case RP_PREPARE_RESHAPE:
                        if (!p.prepared) {
                            error(p.doc->id, "", "node-contract",
                                  "prepare_settings kept the instance but prepared nothing");
                        } else if (result == RP_PREPARE_SWAP && !sameLayout) {
                            // The instance's ports are laid out by the layout it has; only a reshape moves them.
                            live->node->vt->release_settings(live->node, p.prepared);
                            p.prepared = nullptr;
                            error(p.doc->id, "", "node-contract",
                                  "prepare_settings chose a swap for a change to the node's layout, which needs a "
                                  "reshape or a rebuild");
                        } else {
                            // Kept. A reshape gets ports for its new layout; one that leaves the layout as it
                            // was is a swap, and keeps the ports it has.
                            p.inst = live;
                            p.ports = sameLayout ? live->ports : portState(p.layout, live->ports.get());
                        }
                        break;
                    case RP_PREPARE_REBUILD:
                        p.prepared = nullptr;
                        createFresh(p);
                        break;
                    case RP_PREPARE_INVALID:
                        error(p.doc->id, "", "settings-invalid", errorText(err, "the node refused its settings"));
                        break;
                    default:
                        p.prepared = nullptr;  // an unknown result promises nothing about it
                        error(p.doc->id, "", "node-contract",
                              "prepare_settings returned " + std::to_string(result) + ", which is not a result");
                        break;
                }
            }
        }
        return out_.errors.empty();
    }

    // ---- document param values --------------------------------------------------------------------------

    bool params() {
        paramValues_.resize(nodes_.size());
        for (std::size_t n = 0; n < nodes_.size(); ++n) {
            const auto& p = nodes_[n];
            if (!p.doc->params) continue;
            for (const auto& [id, value] : *p.doc->params) {
                const auto& ps = p.layout.params;
                const auto it = std::find_if(ps.begin(), ps.end(), [&](const ParamDesc& d) { return d.id == id; });
                if (it == ps.end()) {
                    error(p.doc->id, id, "unknown-param", "node " + q(p.doc->id) + " has no param " + q(id));
                    continue;
                }
                double v = 0.0;
                bool ok = false;
                if (it->type == ParamType::Enum) {
                    if (auto label = value.to_string()) {
                        for (std::size_t i = 0; i < it->values.size(); ++i)
                            if (it->values[i] == *label) {
                                v = static_cast<double>(i);
                                ok = true;
                            }
                    }
                } else if (it->type == ParamType::Bool) {
                    if (auto b = value.to_bool()) {
                        v = *b ? 1.0 : 0.0;
                        ok = true;
                    }
                } else if (auto d = value.to_double()) {
                    v = std::clamp(*d, it->min, it->max);
                    if (it->type == ParamType::Int) v = std::round(v);
                    ok = true;
                }
                if (!ok) {
                    error(p.doc->id, id, "param-value",
                          "param " + q(p.doc->id + "." + id) + " got a value of the wrong type");
                    continue;
                }
                paramValues_[n].emplace_back(static_cast<int>(it - ps.begin()), v);
            }
        }
        return out_.errors.empty();
    }

    // ---- wires ------------------------------------------------------------------------------------------

    bool wires() {
        inputWire_.resize(nodes_.size());
        paramWire_.resize(nodes_.size());
        for (const auto& w : in_.doc->wires) {
            const auto from = parseEndpoint(w.from);
            const auto to = parseEndpoint(w.to);
            if (!from || !to) {
                error("", "", "wire-endpoint", "wire " + q(w.from) + " -> " + q(w.to) + " needs node.port endpoints");
                continue;
            }
            const auto fi = index_.find(from->node);
            const auto ti = index_.find(to->node);
            if (fi == index_.end() || ti == index_.end()) {
                const std::string_view missing = fi == index_.end() ? from->node : to->node;
                error(std::string(missing), "", "unknown-node",
                      "wire " + q(w.from) + " -> " + q(w.to) + " names node " + q(missing) + ", which does not exist");
                continue;
            }
            const NodePlan& src = nodes_[fi->second];
            const NodePlan& dst = nodes_[ti->second];
            const PortDesc* sp = findPort(src.layout, from->port);
            const PortDesc* dp = findPort(dst.layout, to->port);
            const ParamDesc* dparam = dp ? nullptr : findParam(dst.layout, to->port);
            if (!sp) {
                error(src.doc->id, std::string(from->port), "unknown-port", q(w.from) + " is not a port");
                continue;
            }
            if (!dp && !dparam) {
                error(dst.doc->id, std::string(to->port), "unknown-port", q(w.to) + " is not a port or param");
                continue;
            }
            const bool feedback = w.feedback.value_or(false);
            if (sp->kind == PortKind::Link || (dp && dp->kind == PortKind::Link)) {
                if (!dp || sp->kind != PortKind::Link || dp->kind != PortKind::Link)
                    error(dst.doc->id, std::string(to->port), "kind-mismatch",
                          "a link port can only be wired to another link port");
                else if (feedback)
                    error(dst.doc->id, std::string(to->port), "feedback-kind", "a link wire cannot be a feedback wire");
                else
                    links_.emplace_back(fi->second, ti->second);
                continue;
            }
            if (sp->dir != PortDir::Out) {
                error(src.doc->id, sp->id, "wire-direction", q(w.from) + " is an input; a wire starts at an output");
                continue;
            }
            const PortKind dkind = dp ? dp->kind : PortKind::Control;
            if (dp && dp->dir != PortDir::In) {
                error(dst.doc->id, dp->id, "wire-direction", q(w.to) + " is an output; a wire ends at an input");
                continue;
            }
            if (sp->kind != dkind) {
                error(dst.doc->id, std::string(to->port), "kind-mismatch",
                      q(w.from) + " is " + std::string(kindName(sp->kind)) + " but " + q(w.to) + " is " +
                          std::string(kindName(dkind)));
                continue;
            }
            if (sp->kind == PortKind::Resource && sp->resourceType != dp->resourceType) {
                error(dst.doc->id, dp->id, "resource-type-mismatch",
                      q(w.from) + " carries " + sp->resourceType + " but " + q(w.to) + " wants " + dp->resourceType);
                continue;
            }
            if (sp->kind == PortKind::Resource && dp->write) {
                // One writer per mutable resource is counted over bindings, so a writer must be bound.
                error(dst.doc->id, dp->id, "resource-write-wired",
                      q(w.to) + " writes its resource, so it is bound to one, never wired");
                continue;
            }
            if (feedback && sp->kind == PortKind::Resource) {
                error(dst.doc->id, dp->id, "feedback-kind", "a resource wire cannot be a feedback wire");
                continue;
            }
            DataWire dw;
            dw.from = fi->second;
            dw.fromPort = static_cast<int>(sp - src.layout.ports.data());
            dw.to = ti->second;
            dw.feedback = feedback;
            dw.kind = sp->kind;
            dw.key = w.from + "->" + w.to;
            auto& taken = dp ? inputWire_[dw.to] : paramWire_[dw.to];
            const int slot = dp ? static_cast<int>(dp - dst.layout.ports.data())
                                : static_cast<int>(dparam - dst.layout.params.data());
            if (taken.count(slot)) {
                error(dst.doc->id, std::string(to->port), "input-taken",
                      q(w.to) + " already has a wire; combine signals with an explicit node (+, merge)");
                continue;
            }
            if (!dp) {
                const auto& values = paramValues_[dw.to];
                if (std::any_of(values.begin(), values.end(), [&](const auto& v) { return v.first == slot; })) {
                    error(dst.doc->id, std::string(to->port), "input-taken",
                          q(w.to) + " has a document value, so it cannot also take a wire");
                    continue;
                }
            }
            (dp ? dw.toPort : dw.toParam) = slot;
            taken[slot] = static_cast<int>(wires_.size());
            wires_.push_back(std::move(dw));
        }
        return out_.errors.empty();
    }

    // ---- coupled groups -----------------------------------------------------------------------------------

    int root(int n) {
        while (parent_[n] != n) n = parent_[n] = parent_[parent_[n]];
        return n;
    }

    bool groups() {
        parent_.resize(nodes_.size());
        std::iota(parent_.begin(), parent_.end(), 0);
        for (const auto& [a, b] : links_) parent_[root(a)] = root(b);
        std::map<int, int> rootToGroup;
        std::vector<std::vector<int>> members;
        for (std::size_t n = 0; n < nodes_.size(); ++n) {
            const int r = root(static_cast<int>(n));
            auto [it, inserted] = rootToGroup.emplace(r, static_cast<int>(members.size()));
            if (inserted) members.emplace_back();
            members[it->second].push_back(static_cast<int>(n));
            nodes_[n].group = it->second;
        }
        groupMembers_ = std::move(members);
        return out_.errors.empty();
    }

    // An interface is the instance's to report, so this one check runs after reconcile; a refusal here
    // rolls back the instances it made, like a create or activate that fails.
    bool linkedSteps() {
        for (const auto& g : groupMembers_) {
            if (g.size() < 2) continue;
            for (int n : g)
                if (!nodes_[n].inst->node->vt->get_interface(nodes_[n].inst->node, RP_STEP_INTERFACE))
                    error(nodes_[n].doc->id, "", "link-without-step",
                          "node " + q(nodes_[n].doc->id) + " is linked but does not implement " RP_STEP_INTERFACE);
        }
        return out_.errors.empty();
    }

    // ---- order and cycles ------------------------------------------------------------------------

    bool order() {
        const int g = static_cast<int>(groupMembers_.size());
        std::vector<std::set<int>> next(g);
        std::vector<int> indegree(g, 0);
        for (const auto& w : wires_) {
            if (w.feedback) continue;
            const int a = nodes_[w.from].group, b = nodes_[w.to].group;
            if (a == b) {
                return error(nodes_[w.to].doc->id, "", "cycle",
                             "wire " + w.key + " loops back into its own " +
                                 (groupMembers_[a].size() > 1 ? "coupled group" : "node") + "; mark it feedback");
            }
            if (next[a].insert(b).second) ++indegree[b];
        }
        // Kahn's algorithm, breaking ties by document order so plans are deterministic.
        std::vector<int> sorted;
        std::vector<bool> done(g, false);
        for (int step = 0; step < g; ++step) {
            int pick = kNone;
            for (int c = 0; c < g; ++c)
                if (!done[c] && indegree[c] == 0 && (pick == kNone || groupMembers_[c][0] < groupMembers_[pick][0]))
                    pick = c;
            if (pick == kNone) break;
            done[pick] = true;
            sorted.push_back(pick);
            for (int b : next[pick]) --indegree[b];
        }
        if (static_cast<int>(sorted.size()) < g) return reportCycle(next, done);
        std::vector<std::vector<int>> ordered;
        for (std::size_t i = 0; i < sorted.size(); ++i) {
            for (int n : groupMembers_[sorted[i]]) {
                nodes_[n].group = static_cast<int>(i);
                nodes_[n].order = static_cast<int>(i);
            }
            ordered.push_back(groupMembers_[sorted[i]]);
        }
        groupMembers_ = std::move(ordered);
        return true;
    }

    bool reportCycle(const std::vector<std::set<int>>& next, const std::vector<bool>& done) {
        // Walk BACK from any unsorted group until one repeats. Every unsorted group still has an unsorted
        // predecessor (that is why Kahn's algorithm left it), so the walk cannot dead-end; walking forward
        // could, from a group that is merely downstream of the loop.
        std::vector<std::vector<int>> prev(next.size());
        for (std::size_t a = 0; a < next.size(); ++a)
            if (!done[a])
                for (int b : next[a])
                    if (!done[b]) prev[b].push_back(static_cast<int>(a));
        int at = 0;
        while (done[at]) ++at;
        std::vector<int> path;
        std::vector<int> seenAt(next.size(), kNone);
        while (seenAt[at] == kNone) {
            seenAt[at] = static_cast<int>(path.size());
            path.push_back(at);
            at = prev[at].front();
        }
        // path[seenAt[at]..] is the loop walked backwards; name it forwards, from `at` back to itself.
        auto name = [&](int group) { return nodes_[groupMembers_[group][0]].doc->id; };
        std::string loop = name(at);
        for (std::size_t i = path.size(); i-- > static_cast<std::size_t>(seenAt[at]);) loop += " -> " + name(path[i]);
        return error(name(at), "", "cycle", "the graph loops (" + loop + "); mark one wire in the loop feedback");
    }

    // ---- channel resolution ---------------------------------------------------------------------------------

    const DataWire* wireInto(int node, int port) const {
        const auto it = inputWire_[node].find(port);
        return it == inputWire_[node].end() ? nullptr : &wires_[it->second];
    }

    // A port's channel count and layout, following match specs and wires upstream. Each port is resolved
    // once: a success is cached, and so is a failure, so one bad port is reported once however many
    // others depend on it.
    Resolved resolve(int node, int port) {
        Resolved& r = resolved_[node][port];
        if (r.ok || r.state == Resolved::State::Failed) return r;
        const PortDesc& d = nodes_[node].layout.ports[port];
        const std::string& id = nodes_[node].doc->id;
        auto fail = [&](const char* code, std::string message) {
            r.state = Resolved::State::Failed;
            if (code) error(id, d.id, code, std::move(message));
            return Resolved{};
        };
        if (r.state == Resolved::State::Visiting)
            return fail("channel-match", q(d.id) + "'s \"match\" specs form a loop");
        r.state = Resolved::State::Visiting;
        Resolved out;
        switch (d.channels.mode) {
            case ChannelSpec::Mode::Fixed:
                out = {d.channels.count, d.channels.layout, true};
                break;
            case ChannelSpec::Mode::Match: {
                const PortDesc* other = findPort(nodes_[node].layout, d.channels.match);
                if (!other || other->kind != PortKind::Audio)
                    return fail("channel-match",
                                q(d.id) + " matches " + q(d.channels.match) + ", which is not an audio port");
                out = resolve(node, static_cast<int>(other - nodes_[node].layout.ports.data()));
                break;
            }
            case ChannelSpec::Mode::Any: {
                if (d.dir == PortDir::Out)
                    return fail("channel-any", "an output cannot be \"any\"; use match or a fixed count");
                const DataWire* w = wireInto(node, port);
                if (!w) out = {1, "mono", true};
                else if (w->feedback)
                    return fail("feedback-inference",
                                q(d.id) + " infers its channels from a feedback wire; give it a fixed count");
                else out = resolve(w->from, w->fromPort);
                break;
            }
        }
        if (!out.ok) return fail(nullptr, {});  // upstream already said why
        if (out.count == 0 || out.count > kMaxChannels)
            return fail(out.count == 0 ? "channel-count" : "too-many-channels",
                        q(d.id) + " resolves to " + std::to_string(out.count) + " channels (1 to " +
                            std::to_string(kMaxChannels) + ")");
        r = out;
        return r;
    }

    static bool compatible(const Resolved& a, const Resolved& b) {
        return a.count == b.count && (a.layout == b.layout || a.layout == "any" || b.layout == "any");
    }

    bool channels() {
        resolved_.resize(nodes_.size());
        for (std::size_t n = 0; n < nodes_.size(); ++n) resolved_[n].resize(nodes_[n].layout.ports.size());
        for (const auto& g : groupMembers_)
            for (int n : g)
                for (std::size_t p = 0; p < nodes_[n].layout.ports.size(); ++p)
                    if (nodes_[n].layout.ports[p].kind == PortKind::Audio) resolve(n, static_cast<int>(p));
        if (!out_.errors.empty()) return false;
        for (const auto& w : wires_) {
            if (w.kind != PortKind::Audio) continue;
            const Resolved& s = resolved_[w.from][w.fromPort];
            const Resolved& d = resolved_[w.to][w.toPort];
            if (!compatible(s, d)) {
                const auto describe = [](const Resolved& r) { return r.layout + " (" + std::to_string(r.count) + ")"; };
                error(nodes_[w.to].doc->id, nodes_[w.to].layout.ports[w.toPort].id, "channel-mismatch",
                      w.key + ": the output is " + describe(s) + ", the input wants " + describe(d) +
                          "; convert explicitly (join, split, downmix, relabel)");
            }
        }
        return out_.errors.empty();
    }

    // ---- exclusive groups ---------------------------------------------------------------------------------

    bool connectedOutput(int node, int port) const {
        for (const auto& w : wires_)
            if (w.from == node && w.fromPort == port) return true;
        return false;
    }

    bool exclusive() {
        for (std::size_t n = 0; n < nodes_.size(); ++n) {
            const Layout& l = nodes_[n].layout;
            for (const auto& group : l.exclusive) {
                std::vector<std::string> used;  // the first connected port matching each member
                for (const auto& pattern : group)
                    for (int p : nodes_[n].outPorts)
                        if (matchesPattern(pattern, l.ports[p].id) && connectedOutput(static_cast<int>(n), p)) {
                            used.push_back(l.ports[p].id);
                            break;
                        }
                if (used.size() > 1)
                    error(nodes_[n].doc->id, used[1], "exclusive-ports",
                          q(nodes_[n].doc->id) + " cannot use " + q(used[0]) + " and " + q(used[1]) + " at the same time");
            }
        }
        return out_.errors.empty();
    }

    // ---- resource bindings --------------------------------------------------------------------------------

    bool resources() {
        bindings_.resize(nodes_.size());
        std::map<ResourceTable::Id, int> writers;
        std::map<ResourceTable::Id, std::pair<std::string, std::string>> readers;  // id -> (node, port)
        for (std::size_t n = 0; n < nodes_.size(); ++n) {
            const auto& p = nodes_[n];
            if (!p.doc->resources) continue;
            for (const auto& [portId, ref] : *p.doc->resources) {
                const PortDesc* d = findPort(p.layout, portId);
                if (!d || d->kind != PortKind::Resource || d->dir != PortDir::In) {
                    error(p.doc->id, portId, "resource-port", q(p.doc->id + "." + portId) + " is not a resource input");
                    continue;
                }
                const int port = static_cast<int>(d - p.layout.ports.data());
                if (wireInto(static_cast<int>(n), port)) {
                    error(p.doc->id, portId, "input-taken", q(p.doc->id + "." + portId) + " is both wired and bound");
                    continue;
                }
                const auto id = parseResourceRef(ref);
                rp_resource* r = id ? in_.resources->get(*id) : nullptr;
                if (!r) {
                    error(p.doc->id, portId, "unknown-resource", q(ref) + " is not a live resource");
                    continue;
                }
                if (d->resourceType != r->type) {
                    error(p.doc->id, portId, "resource-type-mismatch",
                          q(ref) + " is " + std::string(r->type) + " but " + q(portId) + " wants " + d->resourceType);
                    continue;
                }
                if (d->write) {
                    if (++writers[*id] > 1)
                        error(p.doc->id, portId, "resource-multiple-writers",
                              q(ref) + " is bound to more than one writer");
                } else {
                    readers[*id] = {p.doc->id, portId};
                }
                bindings_[n].emplace_back(port, *id);
            }
        }
        for (const auto& [id, count] : writers) {
            const auto r = readers.find(id);
            if (r != readers.end())
                error(r->second.first, r->second.second, "resource-reader-unordered",
                      q("res:" + std::to_string(id)) + " has a writer, so " +
                          q(r->second.first + "." + r->second.second) +
                          " must read it through a wire from the writer's output, not a binding");
        }
        return out_.errors.empty();
    }

    // ---- buffers and the flat plan ---------------------------------------------------------------------

    float* const* channelArray(PlanStorage& s, std::vector<float*> pointers) {
        auto arr = std::make_unique<float*[]>(std::max<std::size_t>(pointers.size(), 1));
        std::copy(pointers.begin(), pointers.end(), arr.get());
        float* const* raw = arr.get();
        s.channelArrays.push_back(std::move(arr));
        return raw;
    }

    // The wire's front buffer. An existing one is reused when it still fits (same kind and channels, a queue
    // of the capacity its source declares, and for audio at least maxFrames long), which is what carries the
    // block in flight through a swap. One that no longer fits is replaced, and what it holds is carried into
    // the new one only while quiescent: the audio thread writes the old front every block, so with it running
    // the control thread must not read it, and the new front starts silent or empty instead. Carried, an audio
    // front that has grown too short (the host's block size went up) keeps its frames, and a queue front whose
    // source's capacity changed keeps its messages, those the new capacity cannot hold counted as the source's
    // drops. A front whose channel count changed always starts silent: its frames are for other channels.
    std::shared_ptr<FeedbackFront> front(const DataWire& w, std::uint32_t channelCount) {
        // A front is filled from its source's queue, so it is sized as that queue is.
        const QueueCapacity& capacity = nodes_[w.from].layout.ports[w.fromPort].capacity;
        const auto it = in_.feedback->find(w.key);
        const FeedbackFront* old = it != in_.feedback->end() ? it->second.get() : nullptr;
        // What the old front holds still means the same: the same kind of data, in as many channels.
        const bool sameData = old && old->kind == w.kind && old->channels == channelCount;
        if (sameData && (!old->queue || laidOutFor(*old->queue, capacity)) &&
            (w.kind != PortKind::Audio || old->frames >= in_.maxFrames))
            return it->second;
        const bool carry = sameData && in_.quiescent;
        auto f = std::make_shared<FeedbackFront>();
        f->kind = w.kind;
        f->channels = channelCount;
        if (w.kind == PortKind::Audio) {
            f->frames = in_.maxFrames;
            f->samples.assign(static_cast<std::size_t>(channelCount) * in_.maxFrames, 0.0f);
            for (std::uint32_t c = 0; c < channelCount; ++c)
                f->pointers.push_back(f->samples.data() + std::size_t(c) * in_.maxFrames);
            if (carry)
                for (std::uint32_t c = 0; c < channelCount; ++c)
                    std::copy_n(old->pointers[c], std::min(old->frames, in_.maxFrames), f->pointers[c]);
        } else if (w.kind == PortKind::Control) {
            f->stream = std::make_unique<StreamStore>(kStreamChanges, 0.0);
        } else {
            f->queue = std::make_unique<QueueStore>(capacity.messageCount(), capacity.byteCount());
            if (carry) carryMessages(*old->queue, *f->queue, w);
        }
        return f;
    }

    // A queue front's messages, onto its replacement. Those that do not fit are dropped as any full queue drops
    // them, and charged to the wire's source (the instance its id names in this plan) when the plan is published:
    // a refused plan keeps the old front, messages and all.
    void carryMessages(const QueueStore& from, QueueStore& to, const DataWire& w) {
        appendQueue(to.queue, from.queue);
        const std::uint32_t lost = to.queue.dropped;
        to.queue.dropped = 0;  // a front's own count is never published
        if (lost == 0) return;
        const std::shared_ptr<Instance> source = nodes_[w.from].inst;
        std::atomic<std::uint32_t>* const total = in_.drops;
        out_.onPublished.push_back([source, total, lost] {
            source->drops.fetch_add(lost, std::memory_order_relaxed);
            if (total) total->fetch_add(lost, std::memory_order_release);  // released after it, as the executor's is
        });
    }

    // Producer side: every output's view, with audio buffers reused by lifetime.
    void assignOutputs(PlanStorage& st) {
        std::vector<int> lastUse;  // per audio buffer
        std::vector<int> freeList;
        auto takeBuffer = [&](int lastReader) {
            int id;
            if (!freeList.empty()) {
                id = freeList.back();
                freeList.pop_back();
            } else {
                id = static_cast<int>(st.audio.size());
                st.audio.push_back(std::make_unique<float[]>(in_.maxFrames));
                std::fill_n(st.audio.back().get(), in_.maxFrames, 0.0f);
                lastUse.push_back(0);
            }
            lastUse[id] = lastReader;
            return id;
        };

        for (int g = 0; g < static_cast<int>(groupMembers_.size()); ++g) {
            for (int n : groupMembers_[g]) {
                NodePlan& p = nodes_[n];
                auto& views = st.outputViews[n];
                for (std::size_t o = 0; o < p.outPorts.size(); ++o) {
                    const int port = p.outPorts[o];
                    const PortDesc& d = p.layout.ports[port];
                    rp_port_view v{};
                    v.kind = static_cast<std::uint32_t>(d.kind);
                    int lastReader = kNone;
                    for (const auto& w : wires_)
                        if (w.from == n && w.fromPort == port)
                            lastReader = std::max(lastReader, w.feedback ? kEndOfBlock : nodes_[w.to].order);
                    switch (d.kind) {
                        case PortKind::Audio: {
                            const std::uint32_t ch = resolved_[n][port].count;
                            std::vector<float*> ptrs;
                            for (std::uint32_t c = 0; c < ch; ++c) {
                                if (lastReader == kNone) {
                                    ptrs.push_back(st.scratch.get());
                                } else {
                                    const int id = takeBuffer(lastReader);
                                    ptrs.push_back(st.audio[id].get());
                                }
                            }
                            v.u.audio = rp_audio_view{channelArray(st, std::move(ptrs)), ch};
                            break;
                        }
                        case PortKind::Events:
                        case PortKind::Bytes:
                            if (lastReader == kNone) {
                                v.u.queue = &st.scratchQueue->queue;
                            } else {
                                // Sized as the port declares; what it drops is charged to this instance.
                                st.queues.push_back(
                                    std::make_unique<QueueStore>(d.capacity.messageCount(), d.capacity.byteCount()));
                                v.u.queue = &st.queues.back()->queue;
                                st.queuesToClear.push_back({v.u.queue, &p.inst->drops});
                            }
                            break;
                        case PortKind::Control:
                            v.u.control = &p.ports->controlOutputs[o]->queue;
                            st.controlsToRoll.push_back(v.u.control);
                            break;
                        case PortKind::Resource:
                            v.u.resource = p.ports->resourceOutputs[o].get();
                            break;
                        case PortKind::Link:
                            break;
                    }
                    views.push_back(v);
                }
            }
            // Free buffers whose last reader is this group - AFTER allocating its outputs, so a node's
            // output never aliases one of its own inputs.
            for (std::size_t id = 0; id < lastUse.size(); ++id)
                if (lastUse[id] == g) {
                    freeList.push_back(static_cast<int>(id));
                    lastUse[id] = kNone;
                }
        }
    }

    // Feedback fronts, keyed by wire, and the copies that fill them at the end of each block.
    std::map<std::string, std::shared_ptr<FeedbackFront>> feedbackFronts(PlanStorage& st) {
        std::map<std::string, std::shared_ptr<FeedbackFront>> fronts;
        for (const auto& w : wires_) {
            if (!w.feedback) continue;
            const rp_port_view& back = st.outputViews[w.from][nodes_[w.from].ordinal[w.fromPort]];
            const std::uint32_t ch = w.kind == PortKind::Audio ? back.u.audio.channel_count : 0;
            auto f = front(w, ch);
            FeedbackCopy copy;
            copy.kind = static_cast<rp_port_kind>(w.kind);
            if (w.kind == PortKind::Audio) {
                copy.backChannels = back.u.audio.channels;
                copy.frontChannels = f->pointers.data();
                copy.channels = ch;
                copy.capacity = f->frames;
            } else if (w.kind == PortKind::Control) {
                copy.backControl = back.u.control;
                copy.frontControl = &f->stream->queue;
            } else {
                copy.backQueue = back.u.queue;
                copy.frontQueue = &f->queue->queue;
            }
            st.feedback.push_back(copy);
            st.feedbackFronts.push_back(f);
            fronts[w.key] = f;
        }
        return fronts;
    }

    // Consumer side: every input, param and resource slot.
    void assignInputs(PlanStorage& st, std::map<std::string, std::shared_ptr<FeedbackFront>>& fronts) {
        const std::size_t count = nodes_.size();
        for (std::size_t n = 0; n < count; ++n) {
            NodePlan& p = nodes_[n];
            auto& views = st.inputViews[n];
            for (const int port : p.inPorts) {
                const PortDesc& d = p.layout.ports[port];
                rp_port_view v{};
                v.kind = static_cast<std::uint32_t>(d.kind);
                const DataWire* w = wireInto(static_cast<int>(n), port);
                const rp_port_view* src = w ? &st.outputViews[w->from][nodes_[w->from].ordinal[w->fromPort]] : nullptr;
                FeedbackFront* f = (w && w->feedback) ? fronts[w->key].get() : nullptr;
                switch (d.kind) {
                    case PortKind::Audio: {
                        const std::uint32_t ch = resolved_[n][port].count;
                        if (f) v.u.audio = rp_audio_view{f->pointers.data(), ch};
                        else if (src) v.u.audio = src->u.audio;
                        else v.u.audio = rp_audio_view{channelArray(st, std::vector<float*>(ch, st.silence.get())), ch};
                        break;
                    }
                    case PortKind::Events:
                    case PortKind::Bytes:
                        v.u.queue = f ? &f->queue->queue : src ? src->u.queue : &st.emptyQueue->queue;
                        break;
                    case PortKind::Control:
                        v.u.control = f ? &f->stream->queue : src ? src->u.control : &st.emptyStream->queue;
                        break;
                    case PortKind::Resource: {
                        if (src) {
                            v.u.resource = src->u.resource;
                        } else {
                            rp_resource* bound = nullptr;
                            for (const auto& [bp, id] : bindings_[n])
                                if (bp == port) {
                                    bound = in_.resources->get(id);  // checked to exist when bindings_ was built
                                    if (in_.resources->pin(id)) st.pins.push_back(id);
                                }
                            st.resourceSlots.push_back(std::make_unique<rp_resource*>(bound));
                            v.u.resource = st.resourceSlots.back().get();
                        }
                        break;
                    }
                    case PortKind::Link:
                        break;
                }
                views.push_back(v);
            }
            auto& pv = st.paramViews[n];
            for (std::size_t k = 0; k < p.layout.params.size(); ++k) {
                rp_control_queue* q = &p.ports->params[k]->queue;
                st.controlsToRoll.push_back(q);
                const auto it = paramWire_[n].find(static_cast<int>(k));
                if (it != paramWire_[n].end()) {
                    const DataWire& w = wires_[it->second];
                    q = w.feedback ? &fronts[w.key]->stream->queue
                                   : st.outputViews[w.from][nodes_[w.from].ordinal[w.fromPort]].u.control;
                }
                pv.push_back(q);
            }
        }
    }

    // Steps, in order; a coupled group's members run consecutively behind its first.
    void flattenSteps(PlanStorage& st) {
        for (const auto& g : groupMembers_) {
            for (std::size_t m = 0; m < g.size(); ++m) {
                const int n = g[m];
                PlanStep step;
                step.node = nodes_[n].inst->node;
                step.faulted = &nodes_[n].inst->faulted;
                step.process.inputs = st.inputViews[n].data();
                step.process.input_count = static_cast<std::uint32_t>(st.inputViews[n].size());
                step.process.outputs = st.outputViews[n].data();
                step.process.output_count = static_cast<std::uint32_t>(st.outputViews[n].size());
                step.process.params = st.paramViews[n].data();
                step.process.param_count = static_cast<std::uint32_t>(st.paramViews[n].size());
                if (g.size() > 1) {
                    step.step = static_cast<const rp_step_interface*>(
                        step.node->vt->get_interface(step.node, RP_STEP_INTERFACE));
                    if (m == 0) step.groupSize = static_cast<std::uint32_t>(g.size());
                }
                st.steps.push_back(step);
            }
        }
    }

    // What the audio thread applies when it adopts the plan, and the control-side records that follow
    // once it is published.
    void scheduleAdoption(PlanStorage& st) {
        const std::size_t count = nodes_.size();
        for (std::size_t n = 0; n < count; ++n) {
            NodePlan& p = nodes_[n];
            const std::shared_ptr<Instance>  inst = p.inst;
            const std::shared_ptr<PortState> ports = p.ports;
            // A reshape's ports become the instance's once its plan is published. That is the whole switch: the
            // audio thread reaches ports only through the plan it runs, and this plan holds the new ones while
            // the one it replaces holds the old ones until it is reclaimed.
            if (ports != inst->ports) out_.onPublished.push_back([inst, ports] { inst->ports = ports; });
            if (p.prepared) {
                st.commits.push_back({inst->node, p.prepared, nullptr, false});
                p.prepared = nullptr;  // the plan owns it now
                if (!(p.entry->type->flags & RP_TYPE_STATELESS_PREPARE)) st.awaitsAdoption = true;
                const std::string settings = p.settings;
                out_.onPublished.push_back([inst, settings] { inst->settings = settings; });
            }
            // New ports have sent no mask, so a reshape always brings one: its outputs' ordinals may all have
            // moved, whatever the mask's bytes are.
            std::vector<std::uint8_t> mask;
            for (int port : p.outPorts) mask.push_back(connectedOutput(static_cast<int>(n), port) ? 1 : 0);
            if (!ports->maskSent || ports->lastMask != mask) {
                st.portsChanged.push_back({inst->node, mask});
                out_.onPublished.push_back([ports, mask] {
                    ports->lastMask = mask;
                    ports->maskSent = true;
                });
            }
            std::vector<std::uint8_t> wired(ports->layout.params.size(), 0);
            for (const auto& [k, w] : paramWire_[n]) wired[static_cast<std::size_t>(k)] = 1;
            if (ports->wiredParams != wired) out_.onPublished.push_back([ports, wired] { ports->wiredParams = wired; });
            for (const auto& [k, value] : paramValues_[n]) {
                const std::string& id = ports->layout.params[k].id;
                const auto it = ports->appliedParams.find(id);
                if (p.fresh || it == ports->appliedParams.end() || it->second != value) {
                    st.paramSets.push_back({&ports->params[k]->queue, value});
                    out_.onPublished.push_back([ports, id, value] { ports->appliedParams[id] = value; });
                }
            }
        }
        for (const auto& g : groupMembers_) {
            for (int n : g) {
                const std::shared_ptr<Instance> inst = nodes_[n].inst;
                const auto* link = static_cast<const rp_link_interface*>(
                    inst->node->vt->get_interface(inst->node, RP_LINK_INTERFACE));
                if (!link) continue;
                std::vector<rp_node*> peers;
                for (int m : g)
                    if (m != n) peers.push_back(nodes_[m].inst->node);
                if (!inst->peersSent || inst->lastPeers != peers) {
                    st.peers.push_back({inst->node, link, peers});
                    out_.onPublished.push_back([inst, peers] {
                        inst->lastPeers = peers;
                        inst->peersSent = true;
                    });
                }
            }
        }
    }

    void build() {
        auto s = std::make_unique<PlanStorage>();
        PlanStorage& st = *s;
        st.maxFrames = in_.maxFrames;
        st.resources = in_.resources;
        st.silence = std::make_unique<float[]>(in_.maxFrames);
        st.scratch = std::make_unique<float[]>(in_.maxFrames);
        std::fill_n(st.silence.get(), in_.maxFrames, 0.0f);
        std::fill_n(st.scratch.get(), in_.maxFrames, 0.0f);
        st.emptyQueue = std::make_unique<QueueStore>(0, 0);
        // Every unconnected output shares it, at the default size. Nothing reads it, so nothing it drops is lost
        // and none of that is counted.
        st.scratchQueue = std::make_unique<QueueStore>(kDefaultQueueMessages, kDefaultQueueBytes);
        st.emptyStream = std::make_unique<StreamStore>(0, 0.0);
        st.queuesToClear.push_back({&st.scratchQueue->queue, nullptr});

        st.inputViews.resize(nodes_.size());
        st.outputViews.resize(nodes_.size());
        st.paramViews.resize(nodes_.size());
        for (const auto& p : nodes_) {
            st.instances.push_back(p.inst);
            st.ports.push_back(p.ports);
        }
        assignOutputs(st);
        auto fronts = feedbackFronts(st);
        assignInputs(st, fronts);
        flattenSteps(st);
        scheduleAdoption(st);

        st.view = PlanView{st.steps, st.queuesToClear, st.controlsToRoll, st.feedback, in_.faults, in_.drops};
        for (const auto& p : nodes_) out_.instances[p.doc->id] = p.inst;
        out_.feedback = std::move(fronts);
        out_.storage = std::move(s);
    }

    // ---- failure ------------------------------------------------------------------------------------------

    void releasePrepared() {
        for (auto& p : nodes_)
            if (p.prepared && p.inst) {
                p.inst->node->vt->release_settings(p.inst->node, p.prepared);
                p.prepared = nullptr;
            }
    }

    void rollback() {
        releasePrepared();
        out_.storage.reset();
        out_.instances.clear();
        out_.feedback.clear();
        out_.onPublished.clear();
    }
};

}  // namespace

std::string errorText(const rp_error& err, const char* fallback) {
    const std::size_t n = strnlen(err.message, sizeof err.message);
    return n ? std::string(err.message, n) : std::string(fallback);
}

// Nothing may escape setGraph: an allocation failure (a huge maxFrames, queues within the budget the machine still
// cannot back) is a refused plan, rolled back like any other. Caught here, outside the Builder, so everything it
// took is freed before the refusal is spelt, and that spelling's own few bytes can be had.
PlanOutcome plan(const PlanInput& in) {
    try {
        return Builder(in).run();
    } catch (const std::bad_alloc&) {
        PlanOutcome refused;
        refused.errors.push_back({"", "", "out-of-memory", "planning ran out of memory"});
        return refused;
    }
}

}  // namespace rp::graph
