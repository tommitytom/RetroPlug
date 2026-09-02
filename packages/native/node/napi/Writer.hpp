#pragma once

// reflect-cpp Writer that builds a live Node-API value tree, mirroring rpcpp's src/qjs/Writer.hpp
// but emitting napi_values instead of JSValues. Plugs into rfl::parsing::Parser unchanged.
//
// Ownership model - again simpler than the QuickJS twin:
//   * napi_set_named_property / napi_set_element do NOT steal a reference (there is no per-value
//     refcount to steal). A child is simply created and attached; both the parent's slot and our
//     local handle stay valid until the enclosing handle scope closes.
//   * So there is no "the parent now owns it, don't free it again" rule, and no root to hand back
//     for freeing - write() returns the root purely as a value.
//   * JS arrays have no append primitive here either, so OutputArrayType tracks the next index.
//
// Errors: napi failures throw std::runtime_error, matching the twin. RpcServer runs the write inside
// the dispatch try/catch, and the binding's callback catches anything that escapes and converts it
// to a JS exception.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

#include <node_api.h>

#include <rfl/Bytestring.hpp>
#include <rfl/always_false.hpp>

namespace rpcpp::napi {

class Writer {
 public:
    struct OutputArrayType {
        napi_value val_;
        uint32_t   length_ = 0;
    };
    struct OutputObjectType {
        napi_value val_;
    };
    struct OutputVarType {
        napi_value val_;
        OutputVarType(napi_value _v) : val_(_v) {}
        OutputVarType(OutputArrayType _arr) : val_(_arr.val_) {}
        OutputVarType(OutputObjectType _obj) : val_(_obj.val_) {}
    };

    explicit Writer(napi_env _env) : env_(_env) {}

    // The root-producing methods record the root as a side effect, because rfl::parsing::Parser::write
    // returns void; write() reads it back via root().
    OutputArrayType array_as_root(const size_t) const {
        napi_value v = newArray();
        root_        = v;
        return OutputArrayType{v};
    }

    OutputObjectType object_as_root(const size_t) const {
        napi_value v = newObject();
        root_        = v;
        return OutputObjectType{v};
    }

    OutputVarType null_as_root() const {
        napi_value v = newNull();
        root_        = v;
        return OutputVarType{v};
    }

    template <class T>
    OutputVarType value_as_root(const T& _var) const {
        napi_value v = from_basic_type(_var);
        root_        = v;
        return OutputVarType{v};
    }

    // The root napi_value, valid after Parser::write completes (and for as long as the enclosing
    // handle scope lives). Undefined if the parser produced nothing.
    napi_value root() const {
        if (root_) return root_;
        napi_value undef = nullptr;
        napi_get_undefined(env_, &undef);
        return undef;
    }

    OutputArrayType add_array_to_array(const size_t, OutputArrayType* _parent) const {
        napi_value child = newArray();
        set_in_array(_parent, child);
        return OutputArrayType{child};
    }

    OutputArrayType add_array_to_object(const std::string_view& _name, const size_t,
                                        OutputObjectType* _parent) const {
        napi_value child = newArray();
        set_in_object(_parent, _name, child);
        return OutputArrayType{child};
    }

    OutputObjectType add_object_to_array(const size_t, OutputArrayType* _parent) const {
        napi_value child = newObject();
        set_in_array(_parent, child);
        return OutputObjectType{child};
    }

    OutputObjectType add_object_to_object(const std::string_view& _name, const size_t,
                                          OutputObjectType* _parent) const {
        napi_value child = newObject();
        set_in_object(_parent, _name, child);
        return OutputObjectType{child};
    }

    template <class T>
    OutputVarType add_value_to_array(const T& _var, OutputArrayType* _parent) const {
        napi_value child = from_basic_type(_var);
        set_in_array(_parent, child);
        return OutputVarType{child};
    }

    template <class T>
    OutputVarType add_value_to_object(const std::string_view& _name, const T& _var,
                                      OutputObjectType* _parent) const {
        napi_value child = from_basic_type(_var);
        set_in_object(_parent, _name, child);
        return OutputVarType{child};
    }

    OutputVarType add_null_to_array(OutputArrayType* _parent) const {
        napi_value child = newNull();
        set_in_array(_parent, child);
        return OutputVarType{child};
    }

    OutputVarType add_null_to_object(const std::string_view& _name,
                                     OutputObjectType* _parent) const {
        napi_value child = newNull();
        set_in_object(_parent, _name, child);
        return OutputVarType{child};
    }

    void end_array(OutputArrayType*) const noexcept {}
    void end_object(OutputObjectType*) const noexcept {}

 private:
    napi_value newArray() const {
        napi_value v = nullptr;
        if (napi_create_array(env_, &v) != napi_ok) {
            throw std::runtime_error("Could not create Node-API array.");
        }
        return v;
    }

    napi_value newObject() const {
        napi_value v = nullptr;
        if (napi_create_object(env_, &v) != napi_ok) {
            throw std::runtime_error("Could not create Node-API object.");
        }
        return v;
    }

    napi_value newNull() const {
        napi_value v = nullptr;
        if (napi_get_null(env_, &v) != napi_ok) {
            throw std::runtime_error("Could not create Node-API null.");
        }
        return v;
    }

    void set_in_array(OutputArrayType* _parent, napi_value _child) const {
        if (napi_set_element(env_, _parent->val_, _parent->length_, _child) != napi_ok) {
            throw std::runtime_error("Could not append value to Node-API array.");
        }
        ++_parent->length_;
    }

    void set_in_object(OutputObjectType* _parent, const std::string_view& _name,
                       napi_value _child) const {
        // The name must be null-terminated, so materialize the view into a std::string.
        const std::string name(_name);
        if (napi_set_named_property(env_, _parent->val_, name.c_str(), _child) != napi_ok) {
            throw std::runtime_error("Could not set field '" + name + "' on Node-API object.");
        }
    }

    template <class T>
    napi_value from_basic_type(const T& _var) const {
        using U      = std::remove_cvref_t<T>;
        napi_value v = nullptr;

        if constexpr (std::is_same_v<U, std::string>) {
            if (napi_create_string_utf8(env_, _var.data(), _var.size(), &v) != napi_ok) {
                throw std::runtime_error("Could not create Node-API string.");
            }
            return v;

        } else if constexpr (std::is_same_v<U, rfl::Bytestring>) {
            // Binary buffers (rfl::Bytestring) cross the bridge as a JS Uint8Array, matching the
            // QuickJS codec's JS_NewUint8ArrayCopy path - NOT a JS array of numbers. This is what
            // keeps renderAudio / compileKit / readFile off the number-per-byte path.
            napi_value ab   = nullptr;
            void*      dest = nullptr;
            if (napi_create_arraybuffer(env_, _var.size(), &dest, &ab) != napi_ok) {
                throw std::runtime_error("Could not create Node-API ArrayBuffer.");
            }
            if (_var.size() != 0) {
                std::memcpy(dest, _var.data(), _var.size());
            }
            if (napi_create_typedarray(env_, napi_uint8_array, _var.size(), ab, 0, &v) != napi_ok) {
                throw std::runtime_error("Could not create Node-API Uint8Array.");
            }
            return v;

        } else if constexpr (std::is_same_v<U, bool>) {
            if (napi_get_boolean(env_, _var, &v) != napi_ok) {
                throw std::runtime_error("Could not create Node-API boolean.");
            }
            return v;

        } else if constexpr (std::is_floating_point_v<U>) {
            if (napi_create_double(env_, static_cast<double>(_var), &v) != napi_ok) {
                throw std::runtime_error("Could not create Node-API number.");
            }
            return v;

        } else if constexpr (std::is_integral_v<U>) {
            if (napi_create_int64(env_, static_cast<int64_t>(_var), &v) != napi_ok) {
                throw std::runtime_error("Could not create Node-API number.");
            }
            return v;

        } else {
            static_assert(rfl::always_false_v<T>, "Unsupported type.");
        }
    }

    napi_env           env_;
    mutable napi_value root_ = nullptr;
};

}  // namespace rpcpp::napi
