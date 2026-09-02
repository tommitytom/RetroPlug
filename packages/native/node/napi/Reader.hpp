#pragma once

// reflect-cpp Reader that decodes C++ values directly from live Node-API values, mirroring
// rpcpp's src/qjs/Reader.hpp but over the N-API C surface instead of the QuickJS C API. It plugs
// into rfl::parsing::Parser unchanged (see Parser.hpp).
//
// This lives in RetroPlug for now so the spike doesn't churn two nested submodule pointers, but it
// is deliberately written to drop into rpcpp as src/napi/ verbatim (same namespace shape, same file
// layout, same interface as the qjs twin).
//
// Ownership model - SIMPLER than the QuickJS twin, and this is the whole reason:
//   * napi_values are not individually reference-counted. They are owned by the innermost enclosing
//     napi_handle_scope and stay valid until it closes. Our reads run inside the synchronous
//     __rpcSend callback's implicit scope, which spans the entire decode.
//   * So there is no per-value free, no `owned_` tracking vector, and no steal-on-attach dance.
//     Every value we fetch is simply borrowed and dies with the callback.
//   * Consequence to be aware of: handles ACCUMULATE for the duration of one call. A decode of a
//     very large array holds one handle per element until the callback returns. Bounded by a single
//     RPC request, so fine here; a streaming decode would want per-iteration escapable scopes.
//
// N-API predicates (napi_is_array / napi_is_typedarray / ...) never throw, unlike QuickJS's
// JS_GetUint8Array, so the reader needs none of the twin's pending-exception clearing.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <node_api.h>

#include <rfl/Bytestring.hpp>
#include <rfl/Result.hpp>
#include <rfl/always_false.hpp>

namespace rpcpp::napi {

// Byte width of one element of each typed-array flavour - napi_get_typedarray_info reports `length`
// in ELEMENTS, so a Bytestring read has to scale by this to get the byte count right. (The canonical
// path is a Uint8Array, where the factor is 1, but accepting the rest costs one switch.)
inline size_t typedArrayElementSize(napi_typedarray_type _type) noexcept {
    switch (_type) {
        case napi_int8_array:
        case napi_uint8_array:
        case napi_uint8_clamped_array: return 1;
        case napi_int16_array:
        case napi_uint16_array:        return 2;
        case napi_int32_array:
        case napi_uint32_array:
        case napi_float32_array:       return 4;
        case napi_float64_array:
        case napi_bigint64_array:
        case napi_biguint64_array:     return 8;
        default:                       return 1;
    }
}

class Reader {
 public:
    struct InputArrayType {
        napi_value val_;
    };
    struct InputObjectType {
        napi_value val_;
    };
    struct InputVarType {
        napi_value val_;
    };

    template <class T>
    static constexpr bool has_custom_constructor = false;

    explicit Reader(napi_env _env) : env_(_env) {}

    Reader(const Reader&)            = delete;
    Reader& operator=(const Reader&) = delete;
    Reader(Reader&&)                 = delete;
    Reader& operator=(Reader&&)      = delete;

    rfl::Result<InputVarType> get_field_from_array(
        const size_t _idx, const InputArrayType _arr) const noexcept {
        napi_value v = nullptr;
        if (napi_get_element(env_, _arr.val_, static_cast<uint32_t>(_idx), &v) != napi_ok ||
            isUndefined(v)) {
            return rfl::error("Index " + std::to_string(_idx) + " out of bounds.");
        }
        return InputVarType{v};
    }

    rfl::Result<InputVarType> get_field_from_object(
        const std::string& _name, const InputObjectType _obj) const noexcept {
        napi_value v = nullptr;
        if (napi_get_named_property(env_, _obj.val_, _name.c_str(), &v) != napi_ok ||
            isUndefined(v)) {
            return rfl::error("Object contains no field named '" + _name + "'.");
        }
        return InputVarType{v};
    }

    bool is_empty(const InputVarType _var) const noexcept {
        napi_valuetype t = napi_undefined;
        if (napi_typeof(env_, _var.val_, &t) != napi_ok) return true;
        return t == napi_null || t == napi_undefined;
    }

    template <class ArrayReader>
    std::optional<rfl::Error> read_array(const ArrayReader& _array_reader,
                                         const InputArrayType _arr) const noexcept {
        uint32_t len = 0;
        if (napi_get_array_length(env_, _arr.val_, &len) != napi_ok) {
            return rfl::Error("Could not determine array length.");
        }
        for (uint32_t i = 0; i < len; ++i) {
            napi_value v = nullptr;
            if (napi_get_element(env_, _arr.val_, i, &v) != napi_ok) {
                return rfl::Error("Could not read array element " + std::to_string(i) + ".");
            }
            const auto err = _array_reader.read(InputVarType{v});
            if (err) {
                return err;
            }
        }
        return std::nullopt;
    }

    template <class ObjectReader>
    std::optional<rfl::Error> read_object(const ObjectReader& _object_reader,
                                          const InputObjectType _obj) const noexcept {
        // Own enumerable string-keyed properties - the N-API equivalent of the twin's
        // JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY.
        napi_value names = nullptr;
        if (napi_get_property_names(env_, _obj.val_, &names) != napi_ok) {
            return rfl::Error("Could not enumerate object properties.");
        }
        uint32_t plen = 0;
        if (napi_get_array_length(env_, names, &plen) != napi_ok) {
            return rfl::Error("Could not enumerate object properties.");
        }
        for (uint32_t i = 0; i < plen; ++i) {
            napi_value key = nullptr;
            if (napi_get_element(env_, names, i, &key) != napi_ok) continue;

            const auto name = toStdString(key);
            if (!name) continue;

            napi_value v = nullptr;
            if (napi_get_property(env_, _obj.val_, key, &v) != napi_ok) continue;

            // `*name` outlives the call, so the string_view handed to the parser stays valid.
            _object_reader.read(std::string_view(*name), InputVarType{v});
        }
        return std::nullopt;
    }

    template <class T>
    rfl::Result<T> to_basic_type(const InputVarType _var) const noexcept {
        using U = std::remove_cvref_t<T>;

        if constexpr (std::is_same_v<U, std::string>) {
            if (typeOf(_var.val_) != napi_string) {
                return rfl::error("Could not cast to string.");
            }
            const auto s = toStdString(_var.val_);
            if (!s) {
                return rfl::error("Could not cast to string.");
            }
            return *s;

        } else if constexpr (std::is_same_v<U, rfl::Bytestring>) {
            // Binary buffers arrive as a JS Uint8Array (canonical - what the Writer emits), a
            // node Buffer (a Uint8Array subclass, so the typedarray probe catches it), or a bare
            // ArrayBuffer. Mirrors the twin's JS_GetUint8Array / JS_GetArrayBuffer pair.
            void*  data = nullptr;
            size_t size = 0;

            bool isTyped = false;
            if (napi_is_typedarray(env_, _var.val_, &isTyped) == napi_ok && isTyped) {
                napi_typedarray_type type   = napi_uint8_array;
                size_t               length = 0;
                napi_value           ab     = nullptr;
                size_t               offset = 0;
                if (napi_get_typedarray_info(env_, _var.val_, &type, &length, &data, &ab, &offset) !=
                    napi_ok) {
                    return rfl::error("Could not cast to bytestring (expected Uint8Array).");
                }
                size = length * typedArrayElementSize(type);
            } else {
                bool isAb = false;
                if (napi_is_arraybuffer(env_, _var.val_, &isAb) == napi_ok && isAb) {
                    if (napi_get_arraybuffer_info(env_, _var.val_, &data, &size) != napi_ok) {
                        data = nullptr;
                    }
                }
            }

            if (!data && size != 0) {
                return rfl::error("Could not cast to bytestring (expected Uint8Array).");
            }
            if (!data) {
                // A zero-length typed array can legitimately report a null data pointer.
                bool isTypedOrAb = isTyped;
                if (!isTypedOrAb) {
                    bool isAb = false;
                    napi_is_arraybuffer(env_, _var.val_, &isAb);
                    isTypedOrAb = isAb;
                }
                if (!isTypedOrAb) {
                    return rfl::error("Could not cast to bytestring (expected Uint8Array).");
                }
                return rfl::Bytestring();
            }
            const auto* bytes = reinterpret_cast<const std::byte*>(data);
            return rfl::Bytestring(bytes, bytes + size);

        } else if constexpr (std::is_same_v<U, bool>) {
            if (typeOf(_var.val_) != napi_boolean) {
                return rfl::error("Could not cast to boolean.");
            }
            bool b = false;
            napi_get_value_bool(env_, _var.val_, &b);
            return b;

        } else if constexpr (std::is_floating_point_v<U>) {
            if (typeOf(_var.val_) != napi_number) {
                return rfl::error("Could not cast to double.");
            }
            double d = 0.0;
            napi_get_value_double(env_, _var.val_, &d);
            return static_cast<T>(d);

        } else if constexpr (std::is_integral_v<U>) {
            if (typeOf(_var.val_) != napi_number) {
                return rfl::error("Could not cast to integer.");
            }
            int64_t n = 0;
            napi_get_value_int64(env_, _var.val_, &n);
            return static_cast<T>(n);

        } else {
            static_assert(rfl::always_false_v<T>, "Unsupported type.");
        }
    }

    rfl::Result<InputArrayType> to_array(const InputVarType _var) const noexcept {
        bool isArr = false;
        if (napi_is_array(env_, _var.val_, &isArr) != napi_ok || !isArr) {
            return rfl::error("Could not cast to array!");
        }
        return InputArrayType{_var.val_};
    }

    rfl::Result<InputObjectType> to_object(const InputVarType _var) const noexcept {
        bool isArr = false;
        napi_is_array(env_, _var.val_, &isArr);
        if (typeOf(_var.val_) != napi_object || isArr) {
            return rfl::error("Could not cast to object!");
        }
        return InputObjectType{_var.val_};
    }

    template <class T>
    rfl::Result<T> use_custom_constructor(const InputVarType) const noexcept {
        return rfl::error("Custom constructors are not supported by the Node-API reader.");
    }

 private:
    napi_valuetype typeOf(napi_value _v) const noexcept {
        napi_valuetype t = napi_undefined;
        if (napi_typeof(env_, _v, &t) != napi_ok) return napi_undefined;
        return t;
    }

    bool isUndefined(napi_value _v) const noexcept { return typeOf(_v) == napi_undefined; }

    // Two-pass utf8 extraction: N-API reports the byte length when handed a null buffer. Coerces
    // nothing - the caller has already checked the value is a string (or is a property key, which
    // napi_get_property_names guarantees is one).
    std::optional<std::string> toStdString(napi_value _v) const noexcept {
        size_t len = 0;
        if (napi_get_value_string_utf8(env_, _v, nullptr, 0, &len) != napi_ok) {
            return std::nullopt;
        }
        std::string out(len, '\0');
        size_t written = 0;
        if (napi_get_value_string_utf8(env_, _v, out.data(), len + 1, &written) != napi_ok) {
            return std::nullopt;
        }
        out.resize(written);
        return out;
    }

    napi_env env_;
};

}  // namespace rpcpp::napi
