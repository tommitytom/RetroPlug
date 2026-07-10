#pragma once

// Per-JSContext target routing for C-functions bound with JS_NewCFunctionData.
//
// A QuickJS C-function carries no C++ `this`; the naive recovery is a process-global, which breaks the
// moment several JSContexts share the process — e.g. one plugin instance's editor per control-plane context,
// and a DAW opens many at once: every context's hook then routes to whichever owner registered last (and
// goes dead when that one leaves). Instead, bind a stable per-context Slot into the function's func-data and
// follow Slot::ptr at call time, so each context reaches its own owner. The Slot is an indirection cell:
// bound once, then re-pointed / nulled as the owner comes and goes while the hook persists on the (longer
// lived) context. This mirrors TjsHostRuntime::bindRpcSend, which carries its RpcSendFn* the same way.

#include <cstdint>
#include <cstring>
#include <memory>
#include <unordered_map>

#include <quickjs.h>

namespace retroplug {

template <class T>
class ContextTargetTable {
public:
    struct Slot {
        T* ptr = nullptr;
    };

    // The stable slot for `ctx` (created on first use). Its address never changes, so a pointer packed into
    // func-data stays valid across re-points and across the owner coming and going.
    Slot* slotFor(JSContext* ctx) {
        auto& slot = slots_[ctx];
        if (!slot) slot = std::make_unique<Slot>();
        return slot.get();
    }

    // Null `ctx`'s slot iff it still points at `expect`, so a newer owner on the same context isn't clobbered.
    void clear(JSContext* ctx, T* expect) {
        auto it = slots_.find(ctx);
        if (it != slots_.end() && it->second->ptr == expect) it->second->ptr = nullptr;
    }

private:
    std::unordered_map<JSContext*, std::unique_ptr<Slot>> slots_;
};

// Pack a Slot* into an ArrayBuffer JSValue for JS_NewCFunctionData's funcData (its pointer bytes, exactly as
// rpcSendThunk carries its RpcSendFn*). Free the returned value after JS_NewCFunctionData copies it.
inline JSValue packContextTarget(JSContext* ctx, const void* slot) {
    return JS_NewArrayBufferCopy(ctx, reinterpret_cast<const std::uint8_t*>(&slot), sizeof(slot));
}

// Recover the owner a hook was bound with. Null when the slot was cleared (owner gone) or the func-data is
// malformed — callers no-op.
template <class T>
T* contextTargetFromData(JSContext* ctx, JSValue* funcData) {
    using Slot = typename ContextTargetTable<T>::Slot;
    std::size_t len   = 0;
    std::uint8_t* raw = JS_GetArrayBuffer(ctx, &len, funcData[0]);
    if (!raw || len != sizeof(Slot*)) return nullptr;
    Slot* slot = nullptr;
    std::memcpy(&slot, raw, sizeof(slot));
    return slot ? slot->ptr : nullptr;
}

} // namespace retroplug
