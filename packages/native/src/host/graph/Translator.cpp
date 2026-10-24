#include "host/graph/Translator.hpp"

#include <algorithm>
#include <string>

#include "graph/CanonicalJson.hpp"
#include "system/SystemBase.hpp"
#include "system/mesen/MesenNesSystem.hpp"
#include "system/sameboy/SameBoySystem.hpp"

namespace rp::host {

namespace {

// rp.core.add's input limit (CoreNodes.cpp): a longer sum chains adds, which continues the same
// left-to-right sum.
constexpr std::size_t kAddInputs = 16;

std::string sysNode(SystemId id) { return "sys-" + std::to_string(id); }

const char* typeOf(SystemKind k) {
    switch (k) {
        case SystemKind::SameBoy: return "rp.sameboy.gb";
        case SystemKind::MesenNes: return "rp.mesen.nes";
        case SystemKind::MesenGba: return "rp.mesen.gba";
        case SystemKind::MesenSms: return "rp.mesen.sms";
    }
    return "";
}

// One output stream of a system, as channelLayout() reports it: its port on the system node.
struct Stream {
    std::string port;
    bool        stereo = true;
};

// The streams a core reports in `mode` (a NES's export mode; ignored otherwise).
std::vector<Stream> streamsOf(SystemKind kind, std::uint32_t mode) {
    switch (kind) {
        case SystemKind::SameBoy: return {{"ch1"}, {"ch2"}, {"ch3"}, {"ch4"}};
        case SystemKind::MesenNes:
            if (mode == 1) return {{"pins.pulse", false}, {"pins.tnd", false}, {"pins.expansion", false}};
            if (mode == 2) return {{"pins.pulse", false}, {"pins.tnd", false}, {"pins.expansion", false}, {"pins.mixref", false}};
            if (mode == 3)
                return {{"channels.sq1", false}, {"channels.sq2", false}, {"channels.tri", false}, {"channels.noise", false},
                        {"channels.dmc", false}};
            return {{"mix"}};
        default: return {{"mix"}};
    }
}

// rp.host.audio-out's port for the bus starting at host channel `first` (0-based), `n` channels wide.
std::string outPort(std::uint32_t first, std::uint32_t n) {
    return "out.out." + std::to_string(first + 1) + (n == 1 ? "" : "-" + std::to_string(first + n));
}

// The document as JSON, in the order it is built: node order is plan order on ties (section 4.2).
struct Doc {
    std::vector<std::string> nodes;
    std::vector<std::string> wires;
    void node(const std::string& id, const char* type, const std::string& settings = "") {
        nodes.push_back(R"({"id":)" + rp::graph::jsonString(id) + R"(,"type":)" + rp::graph::jsonString(type) +
                        (settings.empty() ? "" : R"(,"settings":)" + settings) + "}");
    }
    void wire(const std::string& from, const std::string& to) {
        wires.push_back(R"({"from":)" + rp::graph::jsonString(from) + R"(,"to":)" + rp::graph::jsonString(to) + "}");
    }
    // A stereo endpoint as its two mono halves, through an rp.core.split named `name`.
    std::pair<std::string, std::string> halves(const std::string& from, const std::string& name) {
        node(name, "rp.core.split");
        wire(from, name + ".in");
        return {name + ".out1", name + ".out2"};
    }
    std::string json() const {
        std::string s = R"({"nodes":[)";
        for (std::size_t i = 0; i < nodes.size(); ++i) s += (i ? "," : "") + nodes[i];
        s += R"(],"wires":[)";
        for (std::size_t i = 0; i < wires.size(); ++i) s += (i ? "," : "") + wires[i];
        return s + "]}";
    }
};

}  // namespace

Translator::Translator(Publish publishFn, std::function<void(SystemBase*)> released)
    : publish_(std::move(publishFn)), released_(std::move(released)) {
    publish(false);  // the empty project: the kernel and the host's outputs
}

Translator::~Translator() {
    for (auto& [token, core] : parked_)
        if (released_) released_(core.get());
}

Translator::System Translator::entryFor(std::unique_ptr<SystemBase> core) {
    System s;
    s.id = core->id();
    s.kind = core->kind();
    s.core = core.get();
    s.token = nextToken_++;
    if (auto* sb = dynamic_cast<SameBoySystem*>(core.get())) s.linkGroup = sb->config_.linkGroupId;
    if (auto* nes = dynamic_cast<MesenNesSystem*>(core.get())) s.buildMode = nes->channelExportMode();
    parked_.emplace(s.token, std::move(core));
    return s;
}

void Translator::unpark(std::uint64_t token) {
    const auto it = parked_.find(token);
    if (it == parked_.end()) return;  // the plan took it, and its node freed it
    if (released_) released_(it->second.get());
    parked_.erase(it);
}

SystemBase* Translator::take(std::uint64_t token) {
    const auto it = parked_.find(token);
    if (it == parked_.end()) return nullptr;
    SystemBase* core = it->second.release();
    parked_.erase(it);
    return core;
}

bool Translator::add(std::unique_ptr<SystemBase> core) {
    if (!core) return false;
    systems_.push_back(entryFor(std::move(core)));
    if (publish(true)) return true;
    const std::uint64_t token = systems_.back().token;
    systems_.pop_back();
    unpark(token);
    return false;
}

bool Translator::replace(SystemId id, std::unique_ptr<SystemBase> core) {
    if (!core) return false;
    const auto it = std::find_if(systems_.begin(), systems_.end(), [&](const System& s) { return s.id == id; });
    if (it == systems_.end()) {
        // Its target is gone, so it is never adopted.
        if (released_) released_(core.get());
        return false;
    }
    const System old = *it;
    *it = entryFor(std::move(core));
    if (publish(true)) return true;
    const std::uint64_t token = it->token;
    *it = old;
    unpark(token);
    return false;
}

bool Translator::remove(SystemId id) {
    const auto it = std::find_if(systems_.begin(), systems_.end(), [&](const System& s) { return s.id == id; });
    if (it == systems_.end()) return true;
    const System old = *it;
    const auto at = systems_.erase(it);
    if (publish(true)) return true;
    systems_.insert(at, old);
    return false;
}

bool Translator::setRouting(AudioRouting mode) {
    const AudioRouting old = routing_;
    routing_ = mode;
    if (publish(true)) return true;
    routing_ = old;
    return false;
}

bool Translator::setLinkGroup(SystemId id, std::uint8_t group) {
    const auto it = std::find_if(systems_.begin(), systems_.end(), [&](const System& s) { return s.id == id; });
    if (it == systems_.end() || it->kind != SystemKind::SameBoy || it->linkGroup == group) return true;
    const std::uint8_t old = it->linkGroup;
    it->linkGroup = group;
    if (publish(false)) return true;
    it->linkGroup = old;
    return false;
}

bool Translator::setShape(HostShape shape) {
    // Multi places systems modulo its channel count, so it needs at least one.
    if (shape.kind == HostShape::Kind::Multi && shape.channels == 0) return false;
    if (shape == shape_) return true;
    const HostShape old = shape_;
    shape_ = shape;
    if (publish(false)) return true;
    shape_ = old;
    return false;
}

SystemBase* Translator::find(SystemId id) const {
    for (const auto& s : systems_)
        if (s.id == id) return s.core;
    return nullptr;
}

std::vector<SystemBase*> Translator::cores() const {
    std::vector<SystemBase*> out;
    for (const auto& s : systems_) out.push_back(s.core);
    return out;
}

// Game Boys with a nonzero link group, bucketed in slot order; a group of fewer than two is dissolved.
std::vector<std::vector<std::size_t>> Translator::linkGroups() const {
    std::vector<std::pair<std::uint8_t, std::vector<std::size_t>>> groups;
    for (std::size_t i = 0; i < systems_.size(); ++i) {
        const System& s = systems_[i];
        if (s.kind != SystemKind::SameBoy || s.linkGroup == 0) continue;
        auto g = std::find_if(groups.begin(), groups.end(), [&](const auto& e) { return e.first == s.linkGroup; });
        if (g == groups.end()) groups.push_back({s.linkGroup, {i}});
        else g->second.push_back(i);
    }
    std::vector<std::vector<std::size_t>> out;
    for (auto& g : groups)
        if (g.second.size() >= 2) out.push_back(std::move(g.second));
    return out;
}

std::vector<std::size_t> Translator::sumOrder() const {
    const auto groups = linkGroups();
    std::vector<bool> linked(systems_.size(), false);
    for (const auto& g : groups)
        for (std::size_t i : g) linked[i] = true;
    std::vector<std::size_t> order;
    for (std::size_t i = 0; i < systems_.size(); ++i)
        if (!linked[i]) order.push_back(i);
    for (const auto& g : groups) order.insert(order.end(), g.begin(), g.end());
    return order;
}

std::uint32_t Translator::nesMode(const System& s) const {
    const bool pins = routing_ == AudioRouting::PinSplit || routing_ == AudioRouting::StereoPinSplit;
    const bool split = pins || routing_ == AudioRouting::ChannelSplit;
    if (split && systems_.size() == 1 && s.kind == SystemKind::MesenNes) return pins ? 1u : 3u;
    return s.buildMode;
}

std::string Translator::documentJson() const {
    Doc d;
    d.node("kernel", "rp.dsp.kernel");
    const std::vector<std::size_t> order = sumOrder();
    for (std::size_t i : order)
        d.node(sysNode(systems_[i].id), typeOf(systems_[i].kind), R"({"core":)" + std::to_string(systems_[i].token) + "}");
    for (const auto& g : linkGroups())
        for (std::size_t k = 1; k < g.size(); ++k)
            d.wire(sysNode(systems_[g[k - 1]].id) + ".link", sysNode(systems_[g[k]].id) + ".link");

    std::vector<std::uint32_t> buses;
    if (shape_.kind == HostShape::Kind::PerSystem) {
        // PerSystem: slot i's mix on channels 2i and 2i + 1.
        for (std::size_t i = 0; i < systems_.size(); ++i) {
            d.wire(sysNode(systems_[i].id) + ".mix", outPort(static_cast<std::uint32_t>(2 * i), 2));
            buses.push_back(2);
        }
    } else if (shape_.kind == HostShape::Kind::PerChannel) {
        // PerChannel: one system's stream s on channels 2s and 2s + 1; a mono stream fills only 2s.
        if (systems_.size() == 1) {
            const System& s = systems_[0];
            const auto streams = streamsOf(s.kind, nesMode(s));
            for (std::size_t k = 0; k < streams.size(); ++k) {
                const std::uint32_t first = static_cast<std::uint32_t>(2 * k);
                if (streams[k].stereo) {
                    d.wire(sysNode(s.id) + "." + streams[k].port, outPort(first, 2));
                    buses.push_back(2);
                } else {
                    d.wire(sysNode(s.id) + "." + streams[k].port, outPort(first, 1));
                    buses.push_back(1);
                    buses.push_back(1);
                }
            }
        }
    } else {
        const std::uint32_t n = shape_.channels;
        std::vector<std::vector<std::string>> lanes(n);
        auto addTo = [&](std::size_t lane, const std::string& from) {
            if (lane < n) lanes[lane].push_back(from);
        };
        const bool pins = routing_ == AudioRouting::PinSplit || routing_ == AudioRouting::StereoPinSplit;
        const bool split = pins || routing_ == AudioRouting::ChannelSplit;
        // A split holds for one system, the pin splits only on a NES, and only with room for its lanes.
        bool splitValid = split && systems_.size() == 1 && !(pins && systems_[0].kind != SystemKind::MesenNes);
        std::vector<Stream> streams;
        std::size_t stride = 2;
        if (splitValid) {
            streams = streamsOf(systems_[0].kind, nesMode(systems_[0]));
            const bool allMono = std::none_of(streams.begin(), streams.end(), [](const Stream& s) { return s.stereo; });
            const bool pairPerMono = allMono && routing_ == AudioRouting::StereoPinSplit;
            stride = allMono && !pairPerMono ? 1 : 2;
            splitValid = stride * streams.size() <= n;
        }
        if (splitValid) {
            // The split: stream k on lane (stride * k) mod n and, for a stereo stream, the next. A mono
            // stream on a pair (StereoPinSplit) fills both lanes.
            const System& s = systems_[0];
            for (std::size_t k = 0; k < streams.size(); ++k) {
                const std::size_t l = (stride * k) % n;
                const std::string from = sysNode(s.id) + "." + streams[k].port;
                if (streams[k].stereo) {
                    const auto [lo, ro] = d.halves(from, "stem-" + std::to_string(s.id) + "-" + std::to_string(k));
                    addTo(l, lo);
                    addTo((l + stride - 1) % n, ro);
                } else {
                    addTo(l, from);
                    if (stride == 2) addTo((l + 1) % n, from);
                }
            }
        } else {
            // Multi-out placement, every split that fell back included: each system's L then R, in sum order.
            for (std::size_t i : order) {
                const System& s = systems_[i];
                const auto [lo, ro] = d.halves(sysNode(s.id) + ".mix", "split-" + std::to_string(s.id));
                if (routing_ == AudioRouting::OnePerInstance) {
                    addTo(i % n, lo);
                    addTo(i % n, ro);
                } else if (routing_ == AudioRouting::TwoPerInstance) {
                    const std::size_t p = (2 * i) % n;
                    addTo(p, lo);
                    addTo((p + 1) % n, ro);
                } else {
                    addTo(0, lo);
                    addTo(1, ro);
                }
            }
        }
        // Each host channel: one contribution wired straight in, several summed by rp.core.add in order.
        for (std::uint32_t c = 0; c < n; ++c) {
            buses.push_back(1);
            const auto& in = lanes[c];
            if (in.empty()) continue;
            if (in.size() == 1) {
                d.wire(in[0], outPort(c, 1));
                continue;
            }
            std::string prev;
            std::size_t at = 0;
            for (int j = 0; at < in.size(); ++j) {
                const std::string name = "lane-" + std::to_string(c) + (j ? "-" + std::to_string(j) : "");
                std::vector<std::string> ins;
                if (!prev.empty()) ins.push_back(prev + ".out");
                while (ins.size() < kAddInputs && at < in.size()) ins.push_back(in[at++]);
                d.node(name, "rp.core.add", R"({"inputs":)" + std::to_string(ins.size()) + "}");
                for (std::size_t k = 0; k < ins.size(); ++k) d.wire(ins[k], name + ".in" + std::to_string(k + 1));
                prev = name;
            }
            d.wire(prev + ".out", outPort(c, 1));
        }
    }
    std::string b = "[";
    for (std::size_t i = 0; i < buses.size(); ++i) b += (i ? "," : "") + std::to_string(buses[i]);
    d.node("out", "rp.host.audio-out", R"({"buses":)" + b + "]}");
    return d.json();
}

SystemTable Translator::table() const {
    SystemTable t;
    for (const auto& s : systems_)
        t.slots.push_back({s.id, s.core, dynamic_cast<SameBoySystem*>(s.core), dynamic_cast<MesenNesSystem*>(s.core)});
    return t;
}

bool Translator::publish(bool rearmsTap) {
    return publish_(documentJson(), std::make_shared<const SystemTable>(table()), rearmsTap);
}

}  // namespace rp::host
