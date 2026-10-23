#include "graph/CanonicalJson.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <utility>
#include <vector>

#include <rfl/json.hpp>

namespace rp::graph {

namespace {

void appendString(std::string& out, std::string_view s) {
    out += '"';
    for (const char ch : s) {
        const auto c = static_cast<unsigned char>(ch);
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", c);
                    out += buf;
                } else {
                    out += ch;
                }
        }
    }
    out += '"';
}

void append(std::string& out, const rfl::Generic& value);

void appendObject(std::string& out, const rfl::Generic::Object& object) {
    std::vector<std::pair<std::string_view, const rfl::Generic*>> entries;
    entries.reserve(object.size());
    for (const auto& [key, v] : object) entries.emplace_back(key, &v);
    std::sort(entries.begin(), entries.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    out += '{';
    bool first = true;
    for (const auto& [key, v] : entries) {
        if (!first) out += ',';
        first = false;
        appendString(out, key);
        out += ':';
        append(out, *v);
    }
    out += '}';
}

void append(std::string& out, const rfl::Generic& value) {
    std::visit(
        [&](const auto& v) {
            using V = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<V, bool>) {
                out += v ? "true" : "false";
            } else if constexpr (std::is_same_v<V, std::int64_t>) {
                out += std::to_string(v);
            } else if constexpr (std::is_same_v<V, double>) {
                out += jsNumber(v);
            } else if constexpr (std::is_same_v<V, std::string>) {
                appendString(out, v);
            } else if constexpr (std::is_same_v<V, rfl::Generic::Object>) {
                appendObject(out, v);
            } else if constexpr (std::is_same_v<V, rfl::Generic::Array>) {
                out += '[';
                for (std::size_t i = 0; i < v.size(); ++i) {
                    if (i) out += ',';
                    append(out, v[i]);
                }
                out += ']';
            } else {
                out += "null";
            }
        },
        value.variant());
}

}  // namespace

std::string jsNumber(double value) {
    if (!std::isfinite(value)) return "null";
    if (value == 0.0) return "0";  // also -0, which JavaScript prints as "0"

    // Shortest round-trip digits, read back out of scientific form: d.ddde[+-]x.
    char buf[64];
    const auto res = std::to_chars(buf, buf + sizeof buf, std::abs(value), std::chars_format::scientific);
    const std::string_view sci(buf, static_cast<std::size_t>(res.ptr - buf));
    const auto ePos = sci.find('e');
    std::string digits;
    for (const char c : sci.substr(0, ePos))
        if (c != '.') digits += c;
    const int exponent = std::atoi(std::string(sci.substr(ePos + 1)).c_str());

    // ECMAScript Number::toString: k significant digits, n = position of the decimal point.
    const int k = static_cast<int>(digits.size());
    const int n = exponent + 1;
    std::string out = value < 0 ? "-" : "";
    if (k <= n && n <= 21) {
        out += digits;
        out.append(static_cast<std::size_t>(n - k), '0');
    } else if (0 < n && n <= 21) {
        out += digits.substr(0, static_cast<std::size_t>(n));
        out += '.';
        out += digits.substr(static_cast<std::size_t>(n));
    } else if (-6 < n && n <= 0) {
        out += "0.";
        out.append(static_cast<std::size_t>(-n), '0');
        out += digits;
    } else {
        out += digits[0];
        if (k > 1) {
            out += '.';
            out += digits.substr(1);
        }
        out += 'e';
        out += (n - 1 >= 0) ? '+' : '-';
        out += std::to_string(std::abs(n - 1));
    }
    return out;
}

std::string canonicalJson(const rfl::Generic& value) {
    std::string out;
    append(out, value);
    return out;
}

rfl::Result<std::string> canonicalizeJson(std::string_view json) {
    return rfl::json::read<rfl::Generic>(json).transform(
        [](const rfl::Generic& g) { return canonicalJson(g); });
}

}  // namespace rp::graph
