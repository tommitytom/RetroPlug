#pragma once

// The desktop's JSON side of the settings types (settings_types.hpp): reflect-cpp parsers that read and write each one
// as the JSON the type it stands in for was, with that type's schema. Part of the desktop build of node.hpp only; a
// device-mode build reads settings from msgpack (settings_msgpack.hpp) and never sees reflect-cpp's parsing layer.

#ifdef RP_DEVICE
#error "settings_json.hpp is the desktop's: a device-mode build reads settings with settings_msgpack.hpp"
#endif

#include <cstddef>
#include <map>
#include <string>
#include <vector>

#include <rfl/Literal.hpp>
#include <rfl/Result.hpp>
#include <rfl/json.hpp>
#include <rfl/parsing/Parser.hpp>

#include "graph/CanonicalJson.hpp"
#include "graph/ResourceId.hpp"
#include "graph/settings_types.hpp"

// A text is the JSON string it holds, and its schema is the string's.
template <class R, class W, std::size_t N, class ProcessorsType>
    requires rfl::parsing::AreReaderAndWriter<R, W, rp::text<N>>
struct rfl::parsing::Parser<R, W, rp::text<N>, ProcessorsType> {
    using InputVarType = typename R::InputVarType;
    using String = Parser<R, W, std::string, ProcessorsType>;

    static Result<rp::text<N>> read(const R& r, const InputVarType& var) noexcept {
        auto s = String::read(r, var);
        if (!s) return rfl::Unexpected<rfl::Error>(s.error());
        rp::text<N> out;
        if (!out.assign(*s))
            return rfl::Unexpected<rfl::Error>(rfl::Error("a string of " + std::to_string(s->size()) +
                                                          " bytes is over the " + std::to_string(N) +
                                                          " the field holds"));
        return out;
    }
    template <class P>
    static void write(const W& w, const rp::text<N>& field, const P& parent) {
        String::write(w, field.str(), parent);
    }
    static schema::Type to_schema(std::map<std::string, schema::Type>* definitions) {
        return String::to_schema(definitions);
    }
};

// A list is the JSON array its elements make, and its schema is the array's. The elements are read as a std::vector
// would be, so each one reports its own error exactly as it did when the field was one.
template <class R, class W, class T, std::size_t N, class ProcessorsType>
    requires rfl::parsing::AreReaderAndWriter<R, W, rp::list<T, N>>
struct rfl::parsing::Parser<R, W, rp::list<T, N>, ProcessorsType> {
    using InputVarType = typename R::InputVarType;
    using Vector = Parser<R, W, std::vector<T>, ProcessorsType>;

    static Result<rp::list<T, N>> read(const R& r, const InputVarType& var) noexcept {
        auto v = Vector::read(r, var);
        if (!v) return rfl::Unexpected<rfl::Error>(v.error());
        if (v->size() > N)
            return rfl::Unexpected<rfl::Error>(rfl::Error("an array of " + std::to_string(v->size()) +
                                                          " elements is over the " + std::to_string(N) +
                                                          " the field holds"));
        rp::list<T, N> out;
        for (const T& e : *v) out.push_back(e);
        return out;
    }
    template <class P>
    static void write(const W& w, const rp::list<T, N>& field, const P& parent) {
        Vector::write(w, std::vector<T>(field.begin(), field.end()), parent);
    }
    static schema::Type to_schema(std::map<std::string, schema::Type>* definitions) {
        return Vector::to_schema(definitions);
    }
};

namespace rp::detail {
// The rfl::Literal a choice stands in for, label for label.
template <rp::fixed_string... Labels>
using LiteralOf = rfl::Literal<rfl::internal::StringLiteral<sizeof(Labels.v)>(Labels.v)...>;
}  // namespace rp::detail

// A choice is the rfl::Literal over the same labels: the same JSON string, the same refusal of a string that is none
// of them, and the same schema.
template <class R, class W, rp::fixed_string... Labels, class ProcessorsType>
    requires rfl::parsing::AreReaderAndWriter<R, W, rp::choice<Labels...>>
struct rfl::parsing::Parser<R, W, rp::choice<Labels...>, ProcessorsType> {
    using InputVarType = typename R::InputVarType;
    using Lit = rp::detail::LiteralOf<Labels...>;
    using Literal = Parser<R, W, Lit, ProcessorsType>;

    static Result<rp::choice<Labels...>> read(const R& r, const InputVarType& var) noexcept {
        auto lit = Literal::read(r, var);
        if (!lit) return rfl::Unexpected<rfl::Error>(lit.error());
        rp::choice<Labels...> out;
        out.assign_index(lit->value());
        return out;
    }
    template <class P>
    static void write(const W& w, const rp::choice<Labels...>& field, const P& parent) {
        Literal::write(w, Lit::from_value(static_cast<typename Lit::ValueType>(field.value())).value(), parent);
    }
    static schema::Type to_schema(std::map<std::string, schema::Type>* definitions) {
        return Literal::to_schema(definitions);
    }
};

// A resource_ref is the plain string the document carries, read and written as one, and its schema is a string
// INLINE rather than a $defs entry. rfl names a $defs entry after the C++ type, and a class template over a
// fixed_string has no spelling compilers agree on (GCC writes the argument's type, Clang drops it), so the default
// would put a different key into the descriptor under each compiler; a Settings struct is a named type for the same
// reason (node.hpp's hook list).
template <class R, class W, rp::fixed_string Type, class ProcessorsType>
    requires rfl::parsing::AreReaderAndWriter<R, W, rp::resource_ref<Type>>
struct rfl::parsing::Parser<R, W, rp::resource_ref<Type>, ProcessorsType> {
    using InputVarType = typename R::InputVarType;
    using String = Parser<R, W, std::string, ProcessorsType>;

    // The empty string is "none" (id 0); anything else must be "res:N", the one spelling of an id parseResourceId
    // accepts. A ref that is not is refused here, with the string it carried, where the settings are read.
    static Result<rp::resource_ref<Type>> read(const R& r, const InputVarType& var) noexcept {
        auto text = String::read(r, var);
        if (!text) return rfl::Unexpected<rfl::Error>(text.error());
        rp::resource_ref<Type> out;
        if (text->empty()) return out;
        if (!rp::graph::parseResourceId(*text, out.id))
            return rfl::Unexpected<rfl::Error>(
                rfl::Error(rp::graph::jsonString(*text) + " is not a resource reference (res:N)"));
        return out;
    }
    template <class P>
    static void write(const W& w, const rp::resource_ref<Type>& field, const P& parent) {
        String::write(w, field.ref(), parent);
    }
    static schema::Type to_schema(std::map<std::string, schema::Type>* definitions) {
        return String::to_schema(definitions);
    }
};
