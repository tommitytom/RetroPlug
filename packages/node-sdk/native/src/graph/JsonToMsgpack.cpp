#include "graph/JsonToMsgpack.hpp"

#include <cstring>
#include <string>

#include <yyjson.h>

namespace rp::graph {

namespace {

constexpr int kMaxDepth = 512;  // as CanonicalJson's parseJson: deeper is refused rather than recursed into

struct Writer {
    std::vector<std::uint8_t>& out;

    void byte(std::uint8_t b) { out.push_back(b); }
    void be(std::uint64_t v, int bytes) {
        for (int k = bytes - 1; k >= 0; --k) out.push_back(static_cast<std::uint8_t>(v >> (8 * k)));
    }

    void uint(std::uint64_t v) {
        if (v <= 0x7f) byte(static_cast<std::uint8_t>(v));
        else if (v <= 0xff) byte(0xcc), be(v, 1);
        else if (v <= 0xffff) byte(0xcd), be(v, 2);
        else if (v <= 0xffffffffu) byte(0xce), be(v, 4);
        else byte(0xcf), be(v, 8);
    }
    void sint(std::int64_t v) {
        if (v >= 0) return uint(static_cast<std::uint64_t>(v));
        const auto bits = static_cast<std::uint64_t>(v);
        if (v >= -32) byte(static_cast<std::uint8_t>(bits));
        else if (v >= -128) byte(0xd0), be(bits, 1);
        else if (v >= -32768) byte(0xd1), be(bits, 2);
        else if (v >= -2147483648LL) byte(0xd2), be(bits, 4);
        else byte(0xd3), be(bits, 8);
    }
    void float64(double v) {
        std::uint64_t bits;
        std::memcpy(&bits, &v, sizeof bits);
        byte(0xcb);
        be(bits, 8);
    }
    void str(const char* s, std::size_t n) {
        if (n <= 31) byte(static_cast<std::uint8_t>(0xa0 | n));
        else if (n <= 0xff) byte(0xd9), be(n, 1);
        else if (n <= 0xffff) byte(0xda), be(n, 2);
        else byte(0xdb), be(n, 4);
        out.insert(out.end(), s, s + n);
    }
    void header(std::size_t n, std::uint8_t fix, std::uint8_t wide16) {
        if (n <= 15) byte(static_cast<std::uint8_t>(fix | n));
        else if (n <= 0xffff) byte(wide16), be(n, 2);
        else byte(static_cast<std::uint8_t>(wide16 + 1)), be(n, 4);
    }

    // False only when the value nests deeper than kMaxDepth.
    bool value(yyjson_val* v, int depth) {
        if (depth > kMaxDepth) return false;
        switch (yyjson_get_type(v)) {
            case YYJSON_TYPE_BOOL: byte(yyjson_get_bool(v) ? 0xc3 : 0xc2); return true;
            case YYJSON_TYPE_NUM:
                if (yyjson_is_uint(v)) uint(yyjson_get_uint(v));
                else if (yyjson_is_sint(v)) sint(yyjson_get_sint(v));
                else float64(yyjson_get_real(v));
                return true;
            case YYJSON_TYPE_STR: str(yyjson_get_str(v), yyjson_get_len(v)); return true;
            case YYJSON_TYPE_ARR: {
                header(yyjson_arr_size(v), 0x90, 0xdc);
                yyjson_val*     item;
                yyjson_arr_iter it = yyjson_arr_iter_with(v);
                while ((item = yyjson_arr_iter_next(&it)))
                    if (!value(item, depth + 1)) return false;
                return true;
            }
            case YYJSON_TYPE_OBJ: {
                header(yyjson_obj_size(v), 0x80, 0xde);
                yyjson_val*     key;
                yyjson_obj_iter it = yyjson_obj_iter_with(v);
                while ((key = yyjson_obj_iter_next(&it))) {
                    str(yyjson_get_str(key), yyjson_get_len(key));
                    if (!value(yyjson_obj_iter_get_val(key), depth + 1)) return false;
                }
                return true;
            }
            case YYJSON_TYPE_NULL:
            default: byte(0xc0); return true;
        }
    }
};

}  // namespace

rfl::Result<std::vector<std::uint8_t>> settingsToMsgpack(std::string_view json) {
    yyjson_read_err err{};
    yyjson_doc* doc = yyjson_read_opts(const_cast<char*>(json.data()), json.size(), 0, nullptr, &err);
    if (!doc) return rfl::Unexpected<rfl::Error>(rfl::Error(std::string("not JSON: ") + err.msg));
    std::vector<std::uint8_t> out;
    out.reserve(json.size());
    const bool ok = Writer{out}.value(yyjson_doc_get_root(doc), 0);
    yyjson_doc_free(doc);
    if (!ok) {
        return rfl::Unexpected<rfl::Error>(
            rfl::Error("JSON nests deeper than " + std::to_string(kMaxDepth) + " levels"));
    }
    return out;
}

}  // namespace rp::graph
