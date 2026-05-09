# Step 12 — TS extension framework

**Status:** Pending.

## Goal

Establish a pattern for user-authored views and workflows in TypeScript without
recompiling C++. Manifest-based registration, lifecycle hooks, hot-reload via
esbuild. Closes the gap from the legacy Lua-based extensibility — typed,
hot-reloadable, audio-thread-safe.

## Depends on

- [Step 11](./11-memory-snapshots.md) (extensions need access to emulator
  state).

## Architecture introduced

- **Manifest convention.** Every extension is a directory at
  `ui/extensions/<name>/` containing `index.tsx`. The bundle build
  (`tools/build-ui.js`) auto-discovers and imports them. No dynamic loading
  at runtime.
- **`Extension` interface.** TS:
  ```ts
  export interface Extension {
    id: string;
    displayName: string;
    appliesTo?: (system: SystemSlot) => boolean;
    renderTile?: (system: SystemSlot) => React.ReactNode;
    renderOverlay?: (system: SystemSlot) => React.ReactNode;
    renderMenuItems?: () => React.ReactNode;
  }
  export default function defineExtension(ext: Extension): Extension;
  ```
- **Extension registry.** A small TS module collects exports from each
  extension, the React tree consults it when rendering tiles/menus.
- **`SystemSlot` props.** Extensions get the slot's id, ROM kind, name, plus
  the standard `useMemory(...)` and `useFrameBuffer(...)` hooks (the latter
  arrives whenever the React-side framebuffer hook does — see step 13).
- **Per-tile overlay slot.** Tiles render the framebuffer plus
  `<ExtensionsForSlot slot={...}/>` which iterates active extensions and
  composites their `renderTile` / `renderOverlay` returns.
- **Built-in vs user extensions.** Built-ins (LSDJ HD, kit editor) live at
  `ui/extensions/`; user extensions live at the same path but are convention
  the user maintains. `.gitignore` for user folders if desired.
- **Hot-reload.** Already works for the bundle as a whole via
  `LVGL_PLUGIN_BUNDLE_PATH`; documenting the workflow is enough.

## Tasks

1. Create `ui/extensions/` and a TS index that auto-discovers via a
   build-time scan in `tools/build-ui.js` (write a JSON manifest at build time
   listing all `index.tsx` files; the entry bundle imports them).
2. Define the `Extension` interface in `runtime/lvgljs/extensions.ts`.
3. Refactor the React tree so tiles delegate optional rendering to the
   registry.
4. Migrate the kit editor (built in step 10 as an inline panel) into a real
   extension at `ui/extensions/lsdj-kit-editor/`.
5. Document the surface in a top-level `EXTENSIONS.md` (one-page user guide).

## Verification

- Two extensions can register and both render their UI in the right slots.
- Adding a new extension dir + restarting `node tools/build-ui.js` picks it up
  without C++ rebuild.
- An extension can subscribe to `useMemory` and re-render only when the data
  changes.
- An extension errors during rendering — error boundary contains the failure
  to that extension's slot, the rest of the UI keeps working.

## Risks / open questions

- **Extension API stability.** Once users write extensions, breaking the API
  costs them work. Version the surface from day one (`Extension.apiVersion =
  1`) and provide a compat shim path for older versions.
- **Performance.** Extensions running `useMemory` at 60 Hz × N
  systems × M extensions can add up. Encourage opt-in subscription rates.
- **Sandboxing.** TS extensions run with full access to the QuickJS context.
  An untrusted extension could cause bad behavior. For now, document this as
  "trust your extensions like you'd trust a VST" — full sandboxing is a much
  larger project.
- **Discovery vs registration.** Build-time scan is simple but means any
  added extension needs a bundle rebuild. Acceptable trade-off; document it.
