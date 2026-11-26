#pragma once

// What crosses a pack's boundary in place of a returned container (docs/downloadable-packs-m5a-plan.md). A pack's
// library has a heap of its own wherever its C runtime is its own (Windows' static CRT, a runtime linked statically),
// so no heap ownership crosses the boundary: a block one side allocated is never freed by the other. A virtual a pack
// implements therefore returns no owning type. It is handed a sink the caller made over its own container and gives the
// sink what it has, by const reference or by pointer and size; the sink copies, and the callee keeps and frees what it
// gave. The helpers below are inline, so the sink's function is compiled in the module that makes it, and its copy
// allocates in that module's heap. A sink is valid for the call it is passed to: a callee never keeps one.
//
// A sink's function may throw (std::bad_alloc from the caller's copy) through the callee's frames; a C system ABI would
// need it noexcept. A Sink<ChannelStream>'s string_view items point into the callee's read-only data, valid while its
// module is loaded (a pack never unloads).
//
// Both are C-shaped (a context pointer and a plain function pointer), as a C system ABI would need them.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace rp {

// Bytes, appended in order: a callee may write more than once.
struct ByteSink {
    void* ctx = nullptr;
    void (*write)(void* ctx, const std::uint8_t* data, std::size_t size) = nullptr;
    void operator()(const std::uint8_t* data, std::size_t size) const {
        if (write != nullptr && size != 0) write(ctx, data, size);
    }
};

// Items, one at a time, each lent for the call: the sink copies it before it returns.
template <class T>
struct Sink {
    void* ctx = nullptr;
    void (*put)(void* ctx, const T& item) = nullptr;
    void operator()(const T& item) const {
        if (put != nullptr) put(ctx, item);
    }
};

// A byte sink appending to `out`.
inline ByteSink appendBytesTo(std::vector<std::uint8_t>& out) {
    return ByteSink{&out, [](void* ctx, const std::uint8_t* data, std::size_t size) {
                        auto& bytes = *static_cast<std::vector<std::uint8_t>*>(ctx);
                        bytes.insert(bytes.end(), data, data + size);
                    }};
}

// A sink appending each item to `out`.
template <class T>
Sink<T> appendTo(std::vector<T>& out) {
    return Sink<T>{&out, [](void* ctx, const T& item) { static_cast<std::vector<T>*>(ctx)->push_back(item); }};
}

// A sink assigning the item it is given to `out`: a single result that holds memory (a later item replaces it).
template <class T>
Sink<T> assignTo(T& out) {
    return Sink<T>{&out, [](void* ctx, const T& item) { *static_cast<T*>(ctx) = item; }};
}

}  // namespace rp
