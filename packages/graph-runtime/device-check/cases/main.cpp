// rp-device-check-cases: the device check's case generator (docs/modular-graph-phase-e1-design.md section 7). A desktop
// tool, and a device product of the desktop build: its rp_register_packages() is the device entries of the packages the
// device check lists, built for the desktop (packages/graph-runtime/device-check/cases/CMakeLists.txt). For each type
// a device builds of those, it writes the type's default settings, read back from a desktop instance created with no
// settings (graph::kSettingsJsonInterface) and converted to the msgpack a device reads (graph::settingsToMsgpack), and
// the layout those settings give, resolved for a node with no wires, into one header of constant arrays
// (DeviceCheck.hpp's TypeCase), with the package entries it was made for (kEntries, which the image holds its own
// product's to). The device image is built against that header and nothing else of the desktop's build.
//
// Which types a device builds is the types' own declaration: a device entry registers, in device mode, each of its
// types that declares neither `heap` nor `threads` (node_device.hpp refuses to build one that does), so the generator
// leaves those out and names them in the header. retroplug-device-mode-test holds the device-mode registration to the
// same rule and to this header.
//
// Usage: rp-device-check-cases <out.hpp>. The header is rewritten only when its text changes, so a desktop rebuild
// that moves no default rebuilds no device image.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <new>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "DeviceCheck.hpp"
#include "graph/Descriptor.hpp"
#include "graph/JsonToMsgpack.hpp"
#include "graph/Packages.hpp"
#include "graph/node.hpp"

namespace {

using rp::graph::ChannelSpec;
using rp::graph::Layout;
using rp::graph::PortDesc;
using rp::graph::PortDir;
using rp::graph::PortKind;

[[noreturn]] void fail(const std::string& why) {
    std::fprintf(stderr, "rp-device-check-cases: %s\n", why.c_str());
    std::exit(1);
}

// What a type's defaults lack before it can be created, merged over them: a type whose own validate refuses its
// defaults (a buffer it must read, which no default can name) gets the device check host's one resource here. A type
// whose defaults are refused with no entry fails the generator, naming it.
struct Completion {
    std::string_view id;
    std::string_view json;
};
constexpr Completion kCompletions[] = {
    {"rp.dsp.normalize", R"({"buffer":"res:1"})"},
};

std::string_view completionOf(std::string_view id) {
    for (const Completion& c : kCompletions)
        if (c.id == id) return c.json;
    return {};
}

// A desktop instance of the type, created from `settings` in its own memory, for as long as the object lives.
struct Made {
    const rp_node_type* type;
    rp_memory_req       req{};
    void*               memory = nullptr;
    rp_node*            node = nullptr;

    Made(const rp_node_type* t, const rp_settings& settings) : type(t) {
        rp_error err{};
        if (!t->query_memory(t, &settings, &req, &err))
            fail(std::string(t->id) + ": its defaults are refused (" + err.message +
                 "); give it a completion (kCompletions)");
        // An alignment of 0 asks for none in particular, as DeviceCheck.cpp reads it; align_val_t(0) is undefined.
        if (req.align == 0) req.align = alignof(std::max_align_t);
        memory = ::operator new(req.size, std::align_val_t(req.align));
        node = t->create(t, &rp::device_check::host(), memory, &settings, &err);
        if (!node) fail(std::string(t->id) + ": create: " + err.message);
    }
    ~Made() {
        if (node) type->destroy(node);
        ::operator delete(memory, std::align_val_t(req.align));
    }

    std::string settingsJson() const {
        const auto* writer = static_cast<const rp::graph::SettingsJsonInterface*>(
            node->vt->get_interface(node, rp::graph::kSettingsJsonInterface));
        if (!writer) fail(std::string(type->id) + ": answers no " + rp::graph::kSettingsJsonInterface);
        std::string json(256, '\0');
        std::size_t n = writer->write(node, json.data(), json.size());
        if (n >= json.size()) {
            json.assign(n + 1, '\0');
            n = writer->write(node, json.data(), json.size());
        }
        if (n == 0 || n >= json.size()) fail(std::string(type->id) + ": its settings would not write");
        json.resize(n);
        return json;
    }
};

std::uint32_t channelsOf(const Layout& layout, const PortDesc& port, int depth = 0) {
    if (depth > static_cast<int>(layout.ports.size())) fail("a port's \"match\" specs form a loop");
    switch (port.channels.mode) {
        case ChannelSpec::Mode::Fixed:
            return port.channels.count;
        case ChannelSpec::Mode::Any:
            return 1;  // an unwired "any" input is mono, as the planner resolves one
        case ChannelSpec::Mode::Match: {
            const PortDesc* other = rp::graph::findPort(layout, port.channels.match);
            if (!other) fail("port " + port.id + " matches " + port.channels.match + ", which is not a port");
            return channelsOf(layout, *other, depth + 1);
        }
    }
    return 0;
}

std::uint32_t kindOf(PortKind kind) {
    switch (kind) {
        case PortKind::Audio: return RP_PORT_AUDIO;
        case PortKind::Events: return RP_PORT_EVENTS;
        case PortKind::Bytes: return RP_PORT_BYTES;
        case PortKind::Control: return RP_PORT_CONTROL;
        case PortKind::Link: return RP_PORT_LINK;
        case PortKind::Resource: return RP_PORT_RESOURCE;
    }
    return RP_PORT_AUDIO;
}

const char* kindName(std::uint32_t kind) {
    switch (kind) {
        case RP_PORT_AUDIO: return "RP_PORT_AUDIO";
        case RP_PORT_EVENTS: return "RP_PORT_EVENTS";
        case RP_PORT_BYTES: return "RP_PORT_BYTES";
        case RP_PORT_CONTROL: return "RP_PORT_CONTROL";
        case RP_PORT_LINK: return "RP_PORT_LINK";
        default: return "RP_PORT_RESOURCE";
    }
}

// A C++ comment line may not hold a newline, and a JSON string may: write any control character as an escape.
std::string commentSafe(std::string_view text) {
    std::string out;
    for (const char ch : text) {
        if (static_cast<unsigned char>(ch) < 0x20) {
            char buf[8];
            std::snprintf(buf, sizeof buf, "\\x%02x", static_cast<unsigned>(static_cast<unsigned char>(ch)));
            out += buf;
        } else {
            out += ch;
        }
    }
    return out;
}

std::string typeCase(const rp_node_type* type, std::size_t index, std::ostringstream& arrays) {
    const std::string id = type->id;
    // The defaults: a desktop instance created with no settings (or its completion alone), written back whole.
    const std::string_view completion = completionOf(id);
    const rp_settings      start{completion.data(), completion.size(), RP_SETTINGS_JSON};
    const std::string      json = Made(type, start).settingsJson();
    auto converted = rp::graph::settingsToMsgpack(json);
    if (!converted) fail(id + ": " + converted.error().what());
    const std::vector<std::uint8_t>& msgpack = *converted;

    // The blob read back by the desktop's own msgpack decode must give the same settings, so a field the conversion
    // or the decode would lose fails the generator rather than the device.
    const rp_settings blob{reinterpret_cast<const char*>(msgpack.data()), msgpack.size(), RP_SETTINGS_MSGPACK};
    const std::string back = Made(type, blob).settingsJson();
    if (back != json) fail(id + ": its defaults read back from msgpack as " + back + ", not " + json);

    const rp_settings asJson{json.data(), json.size(), RP_SETTINGS_JSON};
    rp_error          err{};
    std::string       layoutText(4096, '\0');
    std::size_t       n = type->layout_for(type, &asJson, layoutText.data(), layoutText.size(), &err);
    if (n >= layoutText.size()) {
        layoutText.assign(n + 1, '\0');
        n = type->layout_for(type, &asJson, layoutText.data(), layoutText.size(), &err);
    }
    if (n == 0) fail(id + ": layout_for: " + err.message);
    layoutText.resize(n);
    auto layout = rp::graph::parseLayout(layoutText);
    if (!layout) fail(id + ": its layout: " + layout.error().what());

    arrays << "\n// " << id << ": " << commentSafe(json) << "\n";
    arrays << "inline constexpr std::uint8_t kSettings" << index << "[] = {";
    for (std::size_t i = 0; i < msgpack.size(); ++i) {
        arrays << (i % 16 == 0 ? "\n    " : " ") << "0x";
        static const char kHex[] = "0123456789abcdef";
        arrays << kHex[msgpack[i] >> 4] << kHex[msgpack[i] & 0xf] << ",";
    }
    arrays << "\n};\n";
    if (!layout->ports.empty()) {
        arrays << "inline constexpr PortCase kPorts" << index << "[] = {\n";
        for (const PortDesc& p : layout->ports) {
            const std::uint32_t kind = kindOf(p.kind);
            const bool          output = p.dir == PortDir::Out;
            const std::uint32_t channels = p.kind == PortKind::Audio ? channelsOf(*layout, p) : 0;
            const bool          queue = p.kind == PortKind::Events || p.kind == PortKind::Bytes;
            arrays << "    {" << kindName(kind) << ", " << (output ? "true" : "false") << ", " << channels << ", "
                   << (queue ? p.capacity.messageCount() : 0) << ", " << (queue ? p.capacity.byteCount() : 0)
                   << "},  // " << p.id << "\n";
        }
        arrays << "};\n";
    }
    if (!layout->params.empty()) {
        arrays << "inline constexpr double kParams" << index << "[] = {";
        for (const auto& param : layout->params) {
            char buf[64];
            std::snprintf(buf, sizeof buf, "%.17g", param.def);
            arrays << buf << ",";
        }
        arrays << "};\n";
    }
    std::ostringstream row;
    row << "    {\"" << id << "\", kSettings" << index << ", sizeof kSettings" << index << ", "
        << (layout->ports.empty() ? "nullptr, 0" : "kPorts" + std::to_string(index) + ", std::size(kPorts" +
                                                         std::to_string(index) + ")")
        << ", "
        << (layout->params.empty() ? "nullptr, 0" : "kParams" + std::to_string(index) + ", std::size(kParams" +
                                                          std::to_string(index) + ")")
        << "},\n";
    return row.str();
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) fail("usage: rp-device-check-cases <out.hpp>");
    std::ostringstream arrays, rows, leftOut;
    std::size_t        index = 0;
    for (const rp_package* package : rp::graph::productPackages()) {
        for (std::uint32_t t = 0; t < package->node_type_count; ++t) {
            const rp_node_type* type = package->node_types[t];
            auto                d = rp::graph::parseDescriptor(type->descriptor(type));
            if (!d) fail(std::string(type->id) + ": its descriptor: " + d.error().what());
            if (d->heap || d->threads) {
                leftOut << "//   " << type->id << " (declares " << (d->heap ? "heap" : "threads") << ")\n";
                continue;
            }
            rows << typeCase(type, index++, arrays);
        }
    }
    std::ostringstream out;
    out << "#pragma once\n\n"
           "// Generated by rp-device-check-cases (packages/graph-runtime/device-check/cases/main.cpp) from the\n"
           "// desktop's build of the device check's packages: every type a device builds, its default settings as\n"
           "// msgpack and the layout they give. Do not edit.\n";
    if (!leftOut.str().empty())
        out << "//\n// Left out, as no device builds a type that declares heap or threads:\n" << leftOut.str();
    out << "\n#include <cstdint>\n#include <iterator>\n\n#include \"DeviceCheck.hpp\"\n\n"
           "namespace rp::device_check {\n\n"
           "// The package entries the cases were made for, in registration order; the image checks its own\n"
           "// against it.\n"
           "inline constexpr const char kEntries[] = \"" RP_DEVICE_CHECK_ENTRIES "\";\n"
        << arrays.str() << "\ninline constexpr TypeCase kCases[] = {\n"
        << rows.str() << "};\n\n}  // namespace rp::device_check\n";

    const std::string text = out.str();
    {
        std::ifstream existing(argv[1], std::ios::binary);
        const std::string current((std::istreambuf_iterator<char>(existing)), std::istreambuf_iterator<char>());
        if (existing && current == text) return 0;
    }
    std::ofstream file(argv[1], std::ios::binary | std::ios::trunc);
    file << text;
    if (!file) fail(std::string("cannot write ") + argv[1]);
    return 0;
}
