# Step 11 — Memory snapshot API

**Status:** Pending.

## Goal

Expose emulator memory regions to the UI for inspection. RAM/VRAM/SRAM
snapshots flow DSP→UI on request via rpcpp; a throttled push channel feeds
live polling for things like LSDJ phrase/chain detection. Foundation for the
rest of Phase 4 (extension framework, LSDJ HD, sample matcher).

## Depends on

- [Step 10](./10-lsdj-kit-patching.md) (the `MemoryAccessor` was first used
  here; this step extends it).

## Architecture introduced

- **`MemoryType`** — `enum class { Rom, Ram, Sram, Vram, IORegisters }`. Mirror
  of [old/src/core/CoreComponents.h](../old/src/core/CoreComponents.h).
- **`MemoryAccessor`** — already prototyped in step 10. Refines into a
  read-only or read-write view of an emulator memory region with bank
  awareness. Lives at [src/system/MemoryAccessor.hpp](../src/system/MemoryAccessor.hpp).
- **`SystemBase::getMemory(MemoryType, AccessType)`** — virtual already
  declared in step 01; concrete on `SameBoySystem` since step 10. Now exposed
  to the UI.
- **rpcpp method `getMemory(systemId, type, offset, length) -> ArrayBuffer`**
  for cold-path one-shot reads.
- **Live snapshot push channel.** A new "memory" event fired from
  `LvglJsEngine::tick()` at a configurable cadence (default 60 Hz). Payload:
  `{ systemId, type, hash, bytes }`. UI components subscribe via
  `on("memory", ...)` and filter by systemId/type. Hash check on the UI side
  cheaply rejects unchanged snapshots.
- **Snapshot subscription.** `plugin.subscribeMemory(systemId, type, hz)`
  registers a live feed; `unsubscribeMemory` tears it down. The DSP-side
  subscription registry maintains "wants this type at this rate" per system;
  the UI-side `LvglJsEngine::tick` reads the right offsets and emits.
- **Bank handling for SameBoy.** `GB_get_direct_access` returns the
  *currently-banked* memory pointer; for ROM/SRAM snapshots that span banks,
  we may need to walk banks. Defer the multi-bank case until something
  actually needs it; flag in this step.

## Tasks

1. Promote `MemoryAccessor` from step 10's local helper to a first-class type.
2. Implement the rpcpp one-shot `getMemory` method.
3. Implement the subscription registry on the DSP, one entry per
   `{systemId, type, hz}` triple.
4. Wire `LvglJsEngine::tick` to walk active subscriptions, snapshot, hash
   (xxhash, already in deps), emit `"memory"` event with binary payload only
   when the hash changes.
5. Document the JS-side convention in `runtime/lvgljs/memory.ts` (or extend
   `index.ts`): a `useMemory(systemId, type, hz)` React hook that returns
   `{ bytes, version }` and re-renders only on actual change.
6. Smoke-test by building a tiny "RAM viewer" panel as a built-in dev tool.

## Verification

- Load LSDJ. Open the dev RAM viewer. Watch the phrase/chain bytes change as
  LSDJ plays.
- Subscribe at 60 Hz. CPU usage goes up modestly (~2-3% on UI thread for
  Game Boy-sized RAM); subscribing+unsubscribing toggles cleanly.
- Verify hash-based suppression: a static screen produces zero `"memory"`
  emissions after the first hash settles.

## Risks / open questions

- **Snapshot timing vs DSP block boundary.** Snapshots from the UI thread
  read DSP-thread memory without locks. Use a triple-buffered "latest
  snapshot per type" pattern similar to the framebuffer, or just accept that
  snapshots can read torn state on the rare cycle a transfer crosses a block
  boundary. For polling at 60 Hz over megabit-range memory this is fine.
- **Larger memory types.** SRAM can be 32 KiB+; ROM is up to 2 MiB. Don't
  push these on the live channel by default — `getMemory` (one-shot) for
  those, plus a "this snapshot is too big to live-stream" guard.
- **Race with kit patching.** Step 10 writes ROM. Live ROM subscriptions
  see those writes. That's actually desired — kit-editor UI wants to confirm
  the patch landed. Just document it.
