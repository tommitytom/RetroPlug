#include "graph/CanonicalJson.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <utility>
#include <vector>

#include <yyjson.h>

namespace rp::graph {

namespace {

constexpr char kHex[] = "0123456789abcdef";

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
                    out += "\\u00";
                    out += kHex[c >> 4];
                    out += kHex[c & 0xf];
                } else {
                    out += ch;
                }
        }
    }
    out += '"';
}

// a < b as JavaScript compares strings: by UTF-16 code unit. That is byte order for UTF-8 except where a
// character in U+E000..U+FFFF (lead byte 0xEE / 0xEF) meets one beyond U+FFFF (lead byte 0xF0..0xF4):
// UTF-16 spells the latter with a 0xD8..0xDB surrogate, which sorts first.
bool utf16Less(std::string_view a, std::string_view b) {
    const auto [ia, ib] = std::mismatch(a.begin(), a.end(), b.begin(), b.end());
    if (ia == a.end() || ib == b.end()) return ia == a.end() && ib != b.end();
    auto i = static_cast<std::size_t>(ia - a.begin());
    while (i > 0 && (static_cast<unsigned char>(a[i]) & 0xC0) == 0x80) --i;  // back to the lead bytes
    const auto la = static_cast<unsigned char>(a[i]);
    const auto lb = static_cast<unsigned char>(b[i]);
    if (la >= 0xEE && la <= 0xEF && lb >= 0xF0) return false;
    if (lb >= 0xEE && lb <= 0xEF && la >= 0xF0) return true;
    return static_cast<unsigned char>(*ia) < static_cast<unsigned char>(*ib);
}

void append(std::string& out, const rfl::Generic& value);

void appendObject(std::string& out, const rfl::Generic::Object& object) {
    std::vector<std::pair<std::string_view, const rfl::Generic*>> entries;
    entries.reserve(object.size());
    for (const auto& [key, v] : object) entries.emplace_back(key, &v);
    // Stable, so duplicates keep their document order and the last one can win, as in JSON.parse.
    std::stable_sort(entries.begin(), entries.end(),
                     [](const auto& a, const auto& b) { return utf16Less(a.first, b.first); });
    out += '{';
    bool first = true;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (i + 1 < entries.size() && entries[i + 1].first == entries[i].first) continue;  // a later twin wins
        if (!first) out += ',';
        first = false;
        appendString(out, entries[i].first);
        out += ':';
        append(out, *entries[i].second);
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
                out += jsNumber(static_cast<double>(v));  // JS holds every number as a double
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

constexpr std::int64_t kMaxExactInteger = 9007199254740992;  // 2^53: every integer up to here is exact in a double
constexpr int          kMaxDepth = 512;  // deeper is refused rather than recursed into, so a small host thread survives

// False only when the document nests deeper than kMaxDepth.
bool fromYyjson(yyjson_val* v, int depth, rfl::Generic& out) {
    if (depth > kMaxDepth) return false;
    switch (yyjson_get_type(v)) {
        case YYJSON_TYPE_BOOL:
            out = rfl::Generic(yyjson_get_bool(v));
            return true;
        case YYJSON_TYPE_NUM: {
            // An integer stays an integer only while a double holds it exactly; beyond that it is the
            // nearest double, as JSON.parse would make it. Never a wrapped uint64 or a truncated value.
            if (yyjson_is_sint(v)) {
                const std::int64_t i = yyjson_get_sint(v);
                if (i >= -kMaxExactInteger && i <= kMaxExactInteger) {
                    out = rfl::Generic(i);
                    return true;
                }
            } else if (yyjson_is_uint(v)) {
                const std::uint64_t u = yyjson_get_uint(v);
                if (u <= static_cast<std::uint64_t>(kMaxExactInteger)) {
                    out = rfl::Generic(static_cast<std::int64_t>(u));
                    return true;
                }
            }
            out = rfl::Generic(yyjson_get_num(v));
            return true;
        }
        case YYJSON_TYPE_STR:
            out = rfl::Generic(std::string(yyjson_get_str(v), yyjson_get_len(v)));
            return true;
        case YYJSON_TYPE_ARR: {
            rfl::Generic::Array arr;
            yyjson_val* item;
            yyjson_arr_iter it = yyjson_arr_iter_with(v);
            while ((item = yyjson_arr_iter_next(&it))) {
                arr.emplace_back();
                if (!fromYyjson(item, depth + 1, arr.back())) return false;
            }
            out = rfl::Generic(std::move(arr));
            return true;
        }
        case YYJSON_TYPE_OBJ: {
            rfl::Generic::Object obj;
            yyjson_val* key;
            yyjson_obj_iter it = yyjson_obj_iter_with(v);
            while ((key = yyjson_obj_iter_next(&it))) {
                rfl::Generic value;
                if (!fromYyjson(yyjson_obj_iter_get_val(key), depth + 1, value)) return false;
                std::string name(yyjson_get_str(key), yyjson_get_len(key));
                // A repeated key keeps its last value, as JSON.parse does - so a struct read from this
                // Generic sees the same value canonicalJson would write.
                auto existing = std::find_if(obj.begin(), obj.end(), [&](const auto& p) { return p.first == name; });
                if (existing != obj.end()) existing->second = std::move(value);
                else obj.insert(std::make_pair(std::move(name), std::move(value)));
            }
            out = rfl::Generic(std::move(obj));
            return true;
        }
        case YYJSON_TYPE_NULL:
        default:
            out = rfl::Generic(rfl::Generic::VariantType(std::nullopt));
            return true;
    }
}

}  // namespace

// libc++ provides the floating-point std::to_chars only for a macOS deployment target of 13.3 or newer.
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
    std::string_view expText = sci.substr(ePos + 1);
    if (!expText.empty() && expText.front() == '+') expText.remove_prefix(1);
    int exponent = 0;
    std::from_chars(expText.data(), expText.data() + expText.size(), exponent);

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

std::string jsonString(std::string_view s) {
    std::string out;
    appendString(out, s);
    return out;
}

std::string canonicalJson(const rfl::Generic& value) {
    std::string out;
    append(out, value);
    return out;
}

rfl::Result<rfl::Generic> parseJson(std::string_view json) {
    yyjson_read_err err{};
    yyjson_doc* doc = yyjson_read_opts(const_cast<char*>(json.data()), json.size(), 0, nullptr, &err);
    if (!doc) return rfl::Unexpected<rfl::Error>(rfl::Error(std::string("not JSON: ") + err.msg));
    rfl::Generic g;
    const bool ok = fromYyjson(yyjson_doc_get_root(doc), 0, g);
    yyjson_doc_free(doc);
    if (!ok) return rfl::Unexpected<rfl::Error>(rfl::Error("JSON nests deeper than " + std::to_string(kMaxDepth) + " levels"));
    return g;
}

rfl::Result<std::string> canonicalizeJson(std::string_view json) {
    return parseJson(json).transform([](const rfl::Generic& g) { return canonicalJson(g); });
}

}  // namespace rp::graph
