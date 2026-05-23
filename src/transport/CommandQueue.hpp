#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "project/ProjectConfig.hpp"
#include "system/InputTypes.hpp"
#include "system/MemoryType.hpp"
#include "system/SystemTypes.hpp"
#include "system/sameboy/SameBoyConfig.hpp"

class SystemBase;

// TODO: Ensure DSP thread is running (sometimes isn't if the host doesn't have a valid audio device)

// SPSC command queue: UI thread → DSP thread.
//
// Hand-rolled bounded ring of POD Command records; no allocation on either
// thread once constructed. Commands are small tagged-union structs so a
// variant-of-trivially-copyable types isn't needed.
//
// Audio-thread invariant: the DSP must never allocate, free, or block when
// processing commands. `LoadRom` therefore carries a fully-constructed
// `SystemBase*` (the UI thread did `make_unique` + onActivate then released
// the unique_ptr); the DSP just swaps it into the project. The displaced
// system, if any, is shipped back through the EventQueue for the UI to
// `delete`.

struct ButtonPressCommand {
    SystemId     systemId;
    std::uint8_t button;   // SameBoy: GameboyButton; Mesen: NesButton (cast)
    bool         down;
};

struct LoadRomCommand {
    // Ownership transferred to DSP — DSP must either install via
    // Project::swapSystem (which doesn't alloc/free) or, on failure, ship it
    // back through the EventQueue for the UI thread to delete. Replaces
    // slot 0 if the project is empty; otherwise replaces the focused
    // system (or, if none focused, slot 0 fallback).
    SystemBase* newSystem;
};

// Append a new system to the project. UI built it (full make_unique +
// onActivate); DSP adopts the raw pointer with no allocation.
struct AddSystemCommand {
    SystemBase* newSystem;
};

// Remove the system identified by `id`. The displaced pointer is shipped
// back through the EventQueue (`SystemReleased`) for off-thread delete.
struct RemoveSystemCommand {
    SystemId id;
};

// Replace one specific system by id (used by "Replace ROM" on a focused
// tile). Same ownership rules as LoadRom; the displaced ptr returns via
// the EventQueue.
struct ReplaceSystemCommand {
    SystemId    id;
    SystemBase* newSystem;
};

// Move a system into a serial-link group (0 = standalone). The DSP
// rebuilds link-group membership on receipt; UI gets a ConfigChanged so
// the menu chrome updates.
struct SetLinkGroupCommand {
    SystemId     id;
    std::uint8_t groupId;
};

// Replace the current project from a fully-parsed ProjectConfig. The UI
// thread reads the .rplg zip blob from disk, decompresses it, and
// heap-allocates a ProjectConfig; ownership transfers to the DSP, which
// applies it and deletes. Used by the standalone "Load project" feature so
// a fresh launch can restore a 2-instance link configuration without
// re-doing the whole menu dance.
// TODO: Maybe do the delete on the UI thread? std::unique_ptr too?
struct LoadProjectCommand {
    ProjectConfig* config;
};

// Project-wide MIDI routing change. Applied to ProjectConfig::settings on
// the DSP thread; UI is notified via Event::ConfigChanged so menus refresh.
struct SetMidiRoutingCommand {
    MidiRouting routing;
};

// Project-wide zoom change (1..6). Applied to ProjectConfig::settings on
// the DSP thread; UI is notified via Event::ConfigChanged so layout and
// window size update.
struct SetZoomCommand {
    std::uint8_t zoom;
};

// Project-wide layout change. Same pattern as SetMidiRouting.
struct SetLayoutCommand {
    SystemLayout layout;
};

// Project-wide audio routing change. Same pattern as SetMidiRouting.
struct SetAudioRoutingCommand {
    AudioRouting routing;
};

// Soft-reset of a single system (the GB equivalent of pressing the reset
// button). DSP thread calls SystemBase::onReset on the matching slot.
struct ResetSystemCommand {
    SystemId id;
};

// Zero the cartridge battery RAM and reload it into the emulator. Only
// SameBoy systems carry battery RAM; the handler no-ops for others.
struct NewSramCommand {
    SystemId id;
};

// Toggle SameBoyConfig::fastBoot. Mutation only — applies on the next
// reset / boot since the boot ROM is only consulted then.
struct SetFastBootCommand {
    SystemId id;
    bool     enabled;
};

// Change SameBoyConfig::model. Triggers an emulator restart on the DSP
// thread so the new model takes effect immediately. SRAM is preserved
// across the restart; savestate is dropped (model-specific).
struct SetModelCommand {
    SystemId     id;
    SameBoyModel model;
};

// Toggle the per-system "watch the ROM file for changes" flag. The actual
// watcher lives on the UI thread; this command just mutates config so the
// state persists.
struct SetReloadOnRomChangeCommand {
    SystemId id;
    bool     enabled;
};

// Edit the LSDJ sync mode + tempo divisor on a specific system's LsdjSyncRole
// config. Keeping the command narrow (rather than passing a whole RoleConfig)
// preserves the union's POD-trivial property; if more role kinds need
// per-system editors a generic mechanism arrives later.
struct SetLsdjSyncConfigCommand {
    SystemId      id;
    std::uint32_t mode;          // matches LsdjSyncMode underlying type
    std::uint8_t  tempoDivisor;  // 1/2/4/8
};

// Patch one LSDJ kit slot on a specific system. `bytes` is heap-allocated
// on the UI thread (rpcpp `compileAndPatchKit` handler) and ownership
// transfers to the DSP, which applies the patch via the system's
// `LsdjKitPatchRole` and frees the vector. Same ownership model as
// `LoadProjectCommand::json`.
struct PatchKitCommand {
    SystemId                   id;
    std::uint8_t               kitIndex;
    std::vector<std::uint8_t>* bytes;       // exactly Kit::kSize on the wire
};

// Refcounted live-memory-snapshot subscription. The UI-side registry on
// PluginRpcService sends Subscribe on the 0→1 refcount transition and
// Unsubscribe on the 1→0 transition; the DSP allocates / frees the
// per-(system, type) MemorySnapshotTriple between blocks. After this lands
// the system's onProcess publishes the region to UI-readable triple-buffers.
struct SubscribeMemoryCommand {
    SystemId       systemId;
    rp::MemoryType type;
};

struct UnsubscribeMemoryCommand {
    SystemId       systemId;
    rp::MemoryType type;
};

struct Command {
    enum class Kind : std::uint8_t {
        None              = 0,
        ButtonPress       = 1,
        LoadRom           = 2,
        AddSystem         = 3,
        RemoveSystem      = 4,
        ReplaceSystem     = 5,
        SetLinkGroup      = 6,
        LoadProject       = 7,
        SetMidiRouting    = 8,
        SetLsdjSyncConfig = 9,
        PatchKit          = 10,
        SubscribeMemory   = 11,
        UnsubscribeMemory = 12,
        SetZoom           = 13,
        SetLayout         = 14,
        ResetSystem       = 15,
        NewSram           = 16,
        SetFastBoot       = 17,
        SetModel          = 18,
        SetReloadOnRomChange = 19,
        SetAudioRouting   = 20,
    };

    Kind kind = Kind::None;
    union Payload {
        ButtonPressCommand       buttonPress;
        LoadRomCommand           loadRom;
        AddSystemCommand         addSystem;
        RemoveSystemCommand      removeSystem;
        ReplaceSystemCommand     replaceSystem;
        SetLinkGroupCommand      setLinkGroup;
        LoadProjectCommand       loadProject;
        SetMidiRoutingCommand    setMidiRouting;
        SetLsdjSyncConfigCommand setLsdjSyncConfig;
        PatchKitCommand          patchKit;
        SubscribeMemoryCommand   subscribeMemory;
        UnsubscribeMemoryCommand unsubscribeMemory;
        SetZoomCommand           setZoom;
        SetLayoutCommand         setLayout;
        ResetSystemCommand       resetSystem;
        NewSramCommand           newSram;
        SetFastBootCommand       setFastBoot;
        SetModelCommand          setModel;
        SetReloadOnRomChangeCommand setReloadOnRomChange;
        SetAudioRoutingCommand   setAudioRouting;
        Payload() : buttonPress{} {}
    } payload;

    Command() = default;

    static Command makeButtonPress(SystemId id, std::uint8_t b, bool down) {
        Command c;
        c.kind = Kind::ButtonPress;
        c.payload.buttonPress = ButtonPressCommand{id, b, down};
        return c;
    }

    static Command makeLoadRom(SystemBase* newSystem) {
        Command c;
        c.kind = Kind::LoadRom;
        c.payload.loadRom = LoadRomCommand{newSystem};
        return c;
    }

    static Command makeAddSystem(SystemBase* newSystem) {
        Command c;
        c.kind = Kind::AddSystem;
        c.payload.addSystem = AddSystemCommand{newSystem};
        return c;
    }

    static Command makeRemoveSystem(SystemId id) {
        Command c;
        c.kind = Kind::RemoveSystem;
        c.payload.removeSystem = RemoveSystemCommand{id};
        return c;
    }

    static Command makeReplaceSystem(SystemId id, SystemBase* newSystem) {
        Command c;
        c.kind = Kind::ReplaceSystem;
        c.payload.replaceSystem = ReplaceSystemCommand{id, newSystem};
        return c;
    }

    static Command makeSetLinkGroup(SystemId id, std::uint8_t groupId) {
        Command c;
        c.kind = Kind::SetLinkGroup;
        c.payload.setLinkGroup = SetLinkGroupCommand{id, groupId};
        return c;
    }

    static Command makeLoadProject(ProjectConfig* config) {
        Command c;
        c.kind = Kind::LoadProject;
        c.payload.loadProject = LoadProjectCommand{config};
        return c;
    }

    static Command makeSetMidiRouting(MidiRouting routing) {
        Command c;
        c.kind = Kind::SetMidiRouting;
        c.payload.setMidiRouting = SetMidiRoutingCommand{routing};
        return c;
    }

    static Command makeSetLsdjSyncConfig(SystemId id,
                                         std::uint32_t mode,
                                         std::uint8_t tempoDivisor) {
        Command c;
        c.kind = Kind::SetLsdjSyncConfig;
        c.payload.setLsdjSyncConfig = SetLsdjSyncConfigCommand{id, mode, tempoDivisor};
        return c;
    }

    static Command makePatchKit(SystemId id, std::uint8_t kitIndex,
                                std::vector<std::uint8_t>* bytes) {
        Command c;
        c.kind = Kind::PatchKit;
        c.payload.patchKit = PatchKitCommand{id, kitIndex, bytes};
        return c;
    }

    static Command makeSubscribeMemory(SystemId id, rp::MemoryType type) {
        Command c;
        c.kind = Kind::SubscribeMemory;
        c.payload.subscribeMemory = SubscribeMemoryCommand{id, type};
        return c;
    }

    static Command makeUnsubscribeMemory(SystemId id, rp::MemoryType type) {
        Command c;
        c.kind = Kind::UnsubscribeMemory;
        c.payload.unsubscribeMemory = UnsubscribeMemoryCommand{id, type};
        return c;
    }

    static Command makeSetZoom(std::uint8_t zoom) {
        Command c;
        c.kind = Kind::SetZoom;
        c.payload.setZoom = SetZoomCommand{zoom};
        return c;
    }

    static Command makeSetLayout(SystemLayout layout) {
        Command c;
        c.kind = Kind::SetLayout;
        c.payload.setLayout = SetLayoutCommand{layout};
        return c;
    }

    static Command makeResetSystem(SystemId id) {
        Command c;
        c.kind = Kind::ResetSystem;
        c.payload.resetSystem = ResetSystemCommand{id};
        return c;
    }

    static Command makeNewSram(SystemId id) {
        Command c;
        c.kind = Kind::NewSram;
        c.payload.newSram = NewSramCommand{id};
        return c;
    }

    static Command makeSetFastBoot(SystemId id, bool enabled) {
        Command c;
        c.kind = Kind::SetFastBoot;
        c.payload.setFastBoot = SetFastBootCommand{id, enabled};
        return c;
    }

    static Command makeSetModel(SystemId id, SameBoyModel model) {
        Command c;
        c.kind = Kind::SetModel;
        c.payload.setModel = SetModelCommand{id, model};
        return c;
    }

    static Command makeSetReloadOnRomChange(SystemId id, bool enabled) {
        Command c;
        c.kind = Kind::SetReloadOnRomChange;
        c.payload.setReloadOnRomChange = SetReloadOnRomChangeCommand{id, enabled};
        return c;
    }

    static Command makeSetAudioRouting(AudioRouting routing) {
        Command c;
        c.kind = Kind::SetAudioRouting;
        c.payload.setAudioRouting = SetAudioRoutingCommand{routing};
        return c;
    }
};

// Power-of-two bounded SPSC ring. Single producer (UI thread), single
// consumer (DSP thread). Lock-free on both sides; tryPush returns false when
// full so the UI can drop or coalesce as it sees fit. 1024 entries handles
// chord-style key spam comfortably without ever touching the heap.
class CommandQueue {
public:
    static constexpr std::size_t kCapacity = 1024;
    static_assert((kCapacity & (kCapacity - 1)) == 0,
                  "kCapacity must be a power of two");

    CommandQueue() = default;
    CommandQueue(const CommandQueue&)            = delete;
    CommandQueue& operator=(const CommandQueue&) = delete;

    bool tryPush(const Command& c) {
        const std::size_t w = writeIdx.load(std::memory_order_relaxed);
        const std::size_t next = (w + 1) & (kCapacity - 1);
        if (next == readIdx.load(std::memory_order_acquire))
            return false; // full
        slots[w] = c;
        writeIdx.store(next, std::memory_order_release);
        return true;
    }

    bool tryPop(Command& out) {
        const std::size_t r = readIdx.load(std::memory_order_relaxed);
        if (r == writeIdx.load(std::memory_order_acquire))
            return false; // empty
        out = slots[r];
        readIdx.store((r + 1) & (kCapacity - 1), std::memory_order_release);
        return true;
    }

private:
    alignas(64) std::atomic<std::size_t> writeIdx{0};
    alignas(64) std::atomic<std::size_t> readIdx{0};
    Command slots[kCapacity]{};
};
