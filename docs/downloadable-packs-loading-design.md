# Downloadable packs, part 1: loading a pack at run time

Every package is built in today: a product's package list is turned into generated registries and linked native
tables at build time ([spec/06](../spec/06-build-test.md#the-packages)), and spec/07's
[Downloadable packs](../spec/07-remaining-work.md#downloadable-packs) is the road that is left. This design is the
first part of that road: a pack, carrying native code and TypeScript, loaded into a running RetroPlug from a folder.

## The road, and where this part sits

The goal (decided 2026-11-22): only SameBoy ships in the RetroPlug binary. Mesen moves to a repo of its own, as do the
Mega Drive core and cart code such as BlipToaster's, so an emulator's licence travels with its pack and a developer
adds a project to RetroPlug without a pull request into this repo. The work is six parts, each its own design, plan
and build:

1. **Loading packs at run time** (this design): the pack format, the loader on every host, live registries.
2. **Mesen moves out** to its own repo (`RetroPlug-mesen2`); this repo's Mesen tests go with it, its own tests move to
   SameBoy, and the web build bakes Mesen in.
3. **The pack SDK and template**: what an outside repo builds against, and a GitHub Actions matrix (Linux, macOS signed
   and notarized, Windows) that tests through `retroplug-cli`.
4. **The index and the installer**: a signed index repo, download, verify and install, `retroplug-cli pack install
   <id>`, the UI's pack browser.
5. **Resolution**: ROM detection rules in the index, platform dependencies (risa needs `nes`, which Mesen provides),
   a project naming packs that are not installed, and the download offer with the licences shown.
6. **Cart code moves to its own repos**, BlipToaster first.

## Decisions

| Question | Decision |
|---|---|
| What stays built in | SameBoy and its Game Boy carts (LSDj, mGB), the shared infrastructure (core, dsp, midi, audio, ui) and the hardware integrations (N8, Launchpad). Everything else becomes a pack: Mesen, the Mega Drive core, risa, smsggdj, BlipToaster. So part 1 loads native nodes, the app, consoles, UI and CLI halves and script nodes, and no device provider |
| The N8 and Mesen | The N8 package stays built in with the USB link, the real cart's `rp.n8.nes` node and the hardware tools; none of it uses Mesen's code. The emulated N8 (its FIFO, SD card and link model) is Mesen's and goes with the Mesen pack. Today `buildGraph` places the real cart's node beside the first NES system, and the opt-out (`driveRealCart`) is a field of Mesen's role, so a real cart is driven only alongside an emulated NES; whether part 2 keeps that or loosens it is part 2's to decide |
| Compatibility | An API level: one integer, bumped by hand in any commit that breaks packs (the C ABI, `SystemBase` and the contract headers, the TS contract modules). A pack records the level it was built against and is refused at any other. The TS contract is not frozen; the API level is what lets it move, and a downstream CI job (part 3) runs the first-party packs' tests against each RetroPlug change |
| When a pack takes effect | Installing takes effect at once, live; updating and removing take effect at the next launch (native code is never unloaded) |
| A project naming a missing pack | It opens; the missing parts become placeholders that keep their data and save it back unchanged, and come alive when the pack arrives |
| How a pack's code joins the host | One package format, two deliveries: the generated registries become the seed of live registries a pack adds to, the same package can still be built in, and a pack's TypeScript links against the host's own contract modules |
| CLI verbs | `retroplug-cli pack install`, `pack list`, `pack remove` (and later `pack update`, `pack search`, `pack for-rom`): one reserved noun, since packs add top-level commands of their own |

Rejected: regenerating the registries and re-bundling inside the app on install (it puts a bundler in the plugin, the
CLI and the web build, and a reload restarts the control plane), and running each pack in a JS context of its own
behind a message protocol (every contribution kind would need one, and the C1 design already put a pack's UI half in
the app's realm; it can be layered on later for untrusted packs).

## 1. The pack on disk

**Where packs live.** In a per-user data directory, so the plugin, the standalone and the CLI share one set. Packs are
installed data, not configuration, and the bytecode compiled from their bundles is a cache that can always be rebuilt,
so each goes where its platform keeps that kind of file (not in the config directory, `HostRpcService::configDir`,
which keeps the user's settings, the Development packs list among them):

| | Packs and `state.json` | Bytecode cache |
|---|---|---|
| Linux | `$XDG_DATA_HOME/retroplug/packs` (`~/.local/share/retroplug/packs`) | `$XDG_CACHE_HOME/retroplug/packs` (`~/.cache/retroplug/packs`) |
| macOS | `~/Library/Application Support/RetroPlug/packs` | `~/Library/Caches/RetroPlug/packs` |
| Windows | `%LOCALAPPDATA%\RetroPlug\packs` | `%LOCALAPPDATA%\RetroPlug\cache\packs` |

Windows takes the local profile, not the roaming `%APPDATA%`: a domain network copies the roaming profile between
machines at each logon, and a pack's native libraries are large and machine-specific.

```
packs/
  state.json              the installed packs, each one's active version, pending updates and removals
  mesen/
    1.2.0/                one folder per version
      pack.json
      native/linux-x64/rp-mesen.so      (a library per platform the archive carries)
      js/app.js  consoles.js  ui.js  cli.js  protocol.js  script.js
      assets/             embeds and other data files
      LICENSE
```

**Versioned folders make an update at restart work.** An update unpacks beside the loaded version and is recorded as
pending in `state.json`; the next launch switches to it. Each process holds a lock file in the version folder it
loaded, and a folder is deleted only once no lock is held. `state.json` is written atomically (a temporary file and a
rename), so a plugin and a standalone installing at the same moment cannot corrupt it.

**The manifest, `pack.json`:**

```json
{
  "id": "mesen", "name": "Mesen", "version": "1.2.0", "apiLevel": 3,
  "license": "GPL-3.0-or-later", "source": "https://github.com/.../RetroPlug-mesen2",
  "requires": { "packs": [], "platforms": [] },
  "provides": { "platforms": ["nes", "sms", "gg", "gba"], "nodeTypes": ["rp.mesen.nes", "..."], "roles": ["mesen"] },
  "halves": { "native": "rp-mesen", "app": "js/app.js", "consoles": "js/consoles.js",
              "ui": "js/ui.js", "cli": "js/cli.js" }
}
```

- `apiLevel` must equal RetroPlug's, or the pack is refused with a message saying which side is out of date.
- `provides` is what part 5 resolves against, so the loader holds the pack to it: a pack that registers a platform, a
  node type or a role it does not declare is refused.
- `requires.platforms` is how a cart pack says "an NES" without naming Mesen; `requires.packs` names packs by id.
  Part 1 loads packs in dependency order and refuses one whose requirements no loaded pack (or built-in package)
  meets.
- A node type, role kind, kit format id or CLI command already registered is refused; naming conventions are the
  index's review (part 4).

**Development packs.** `retroplug-cli --pack <dir>`, a `RETROPLUG_PACK_PATH` environment variable and a "Development
packs" list in Settings each load a pack straight from a build folder (a version folder's layout). A development pack
overrides an installed one with the same id, and Settings > Packs says so.

**Pack archives.** A `.rppack` is a zip of one version folder, made by the SDK's packaging step (part 3). Unpacking
uses a small vendored zip reader (miniz, MIT): Mesen carries a copy today, which leaves with it.

## 2. The native half

**The entry.** A pack's library exports one C function:

```c
const rp_package* const* rp_pack_entry(uint32_t* api_level, size_t* count);
```

It returns the same `rp_package` list a built-in package's generated table holds today
([Packages.hpp](../packages/graph-runtime/native/src/graph/Packages.hpp)). The loader reads `api_level` first and
uses nothing else from a pack at any other level.

**One load per process, never unloaded.** A process-wide table opens each pack's library once, and every engine in the
process uses it: every plugin instance, render jobs, the standalone. An engine created after a pack loaded registers
the built-in packages (`rp_register_packages`, in their load-bearing order) and then the loaded packs, in dependency
order. A pack loaded live is added to the existing engines through the path `Engine::loadScriptPack` takes (the
control thread that plans), so its node types exist from the next graph change.

**The boundary is C and C++.** Nodes cross through the C ABI (`rp_node.h`), but a console pack's systems are C++
objects: the engine, the snapshot registry and the debug and audio RPC services call a `SystemBase` through its
virtual functions. Wrapping `SystemBase` in C would be a large refactor, so the C++ boundary is accepted and pinned:

- **The SDK fixes the toolchain per platform**, the release ones: gcc 13 with libstdc++ on Linux at the glibc 2.35
  floor, Apple clang with libc++ on macOS, MSVC 14.5x with the dynamic C runtime on Windows. `SystemBase` and the
  contract headers are covered by the API level.
- **Crossing rules.** No RTTI cast across the boundary (there is none today; a test keeps it so). An object is freed by
  the side that made it (a system is already destroyed through the `rp.host.systems` build service that built it). No
  exception crosses the C entry.
- **A pack links its own static copies** of what it compiles against (node-sdk, system, n8-wire, reflect-cpp) and
  imports no symbol from RetroPlug: everything reaches the host through `rp_host` interfaces and vtables. Its symbols
  are hidden but for the entry, so two packs' copies, or a pack's and the plugin's, cannot clash.

**Platforms:** `linux-x64`, `linux-arm64`, `macos-universal`, `windows-x64`; the loader takes `native/<platform>/`. A
pack with a native half and no library for this platform is refused, naming the platform.

**The Pico does not change**: a package's `device: true` native target still compiles into the firmware at build time.

## 3. The TypeScript halves

**What a pack ships.** The SDK's build makes each half one ES module bundle (`app.js`, `consoles.js`, `protocol.js`,
`ui.js`, `cli.js`, `script.js`), leaving every `@retroplug/*` import and the host modules (`react`,
`react/jsx-runtime`, `lvgljs`, `lvgljs-ui`, `retroplug-cli`) external, so a pack never carries its own copy of the
contract. Bundles are JavaScript source, not bytecode: source runs on txiki's QuickJS, the web's QuickJS and Node alike. This holds for the `script` half too: its ES5 source is
compiled to MicroQuickJS bytecode at load, never shipped as bytecode.
The host compiles a bundle to bytecode at load and keeps it in the bytecode cache (section 1), keyed by the pack, its
version and the QuickJS version.

**How the imports resolve.** Each runtime serves the contract modules by their specifiers:

- **QuickJS** (the desktop, the CLI, the web): a module-loader hook answers each `@retroplug/*` specifier with the
  host's own module, as the CLI serves the bare `retroplug-cli` import to test files today. The host's bundles
  therefore publish those modules' namespaces in a table instead of only inlining them.
- **Node**: a `module.register` loader hook does the same.
- **The exposed set** is exactly what the boundary check already calls contracts (`SHELL_IMPORTS`, `UI_IMPORTS` and
  the host modules of [host-modules.json](../packages/retroplug/host-modules.json)). A pack importing anything else is
  refused by the SDK's build and again at load. That list is the contract the API level guards.

**Where each half loads.** `app`, `consoles` and `protocol` into the control plane; `ui` into the UI's realm (the same
txiki runtime, as the [C1 design](modular-graph-phase-c1-design.md) section 8 set out); `cli` into the CLI; `script`
is a device script pack (MicroQuickJS), shipped as ES5 source and compiled at load
([docs/downloadable-packs-m2d-plan.md](downloadable-packs-m2d-plan.md)), and registered in every Engine; a pack's
QuickJS script nodes are its protocol nodes, which reach the engine through `loadScriptPack`'s path (M2e).

**Live registries.** A small `Registry` replaces each generated constant consumers read today: the ROM providers, the
roles, the tracker integrations, the kit formats, the config and project sections, the project services, the embeds,
the consoles, the placers, the menus, the views and the CLI tools (`romProviders.ts`, `trackerRegistry.ts`,
`configSections.ts`, `kitFormats.ts`, `embeds.ts`, `projectStore.ts`, `projectConfig.ts`, the userConfig modules, the
UI's generated menus and views, the CLI dispatcher). The generated modules seed it with the built-in entries;
`add(pack, items)` appends in dependency order after the built-ins, which keeps today's ROM-provider order (a cart pack
after the platform it needs); and a registry notifies on change, so the menus and views re-render when a pack arrives.

**One set of checks.** The generator's checks ([packages.mjs](../packages/retroplug/scripts/lib/packages.mjs)) move
into one module that both the build-time generator and the run-time loader call: a role kind, node type, kit format id,
embed id or marker role declared twice, a role naming a node no package wires, an embed's two halves unpaired, the
capture rules. The same mistake fails the same way at build time and at load.

**All or nothing.** A pack registers into a staging copy of every registry, and the copy is committed only once every
half and the native library have loaded and passed. A refused pack registers nothing and shows its reason in
Settings > Packs.

**Script-node span labels.** The built-in script nodes keep 16 to N. A loaded pack's script nodes take the next free
labels at load, fixed for the life of the process.

## 4. Missing packs, and managing packs

**Placeholders.** A project can name what no loaded pack provides: a node type, a role kind, a platform, an embed (the
pack is missing, refused for its API level, or waiting on an update).

- **The graph document.** A node whose type is not registered becomes a `rp.core.missing` node holding the original
  type and its settings untouched. The engine plans it as nothing (no ports, no work); its edges stay in the document
  and are left out of the plan.
- **The project config.** A system whose platform or roles nothing provides keeps its raw config exactly as read: its
  roles, its ROM path, its settings. The UI shows it as a card, "Needs the Mesen pack" (or "Mesen 1.2 needs updating
  for this RetroPlug"), with no core built.
- **Saving** writes every such raw part back byte for byte. Today's stores assume every role kind is registered, so this
  is a raw passthrough beside the parsed config, new work.
- **When the pack arrives** (installed live, or a development pack added), the project re-resolves: the placeholder
  systems are built from their kept config as a project load builds them, and the placeholder nodes become real nodes
  at the next graph change.
- **A project-level notice** lists the missing packs; part 5 hangs the download offer on it.

**Settings > Packs** lists every pack, installed and development: its version, licence and source link, and its status
(loaded; refused, with the reason; update or removal pending a restart). From there a pack is removed (at the next
launch), a development folder is added, and a `.rppack` is installed ("Install from file...").

**Install from a file is part 1's**, not part 4's: offline studios need it, it is how this repo's CI installs a pack,
and part 4's downloader becomes fetch, verify, install. `retroplug-cli pack install <file.rppack>` and Settings unpack
into `packs/<id>/<version>/` and load the pack live.

**The CLI.** `pack install <file>`, `pack list`, `pack remove <id>`, `--pack <dir>`; a pack's CLI tools appear as
top-level commands, and one that collides with a built-in command is refused.

## 5. Tests

**The proof is real packages.** Part 1 builds Mesen and BlipToaster from this tree as packs and loads them at run time.
Between them they carry every half part 1 promises: Mesen native, app, consoles, ui and cli; BlipToaster app, ui,
script and cli, and `requires.platforms: ["nes"]`. A test product (the desktop list without `mesen` and
`bliptoaster`; the shared `n8` stays built in) loads both from `build/packs/`, and Mesen's and BlipToaster's existing
cases run against it and must pass unchanged: the parity baselines, the kits, the renders, the emulated N8,
BlipToaster's views and ROM patcher. A pack loaded at run time behaves byte for byte as a built-in package does. The
shipped desktop product does not change in part 1; part 2 drops Mesen from it.

**A fixture pack** in the tree, built in deliberately broken variants: the wrong API level, a duplicate role kind or
node type, a registration its `provides` does not declare, no library for this platform, a CLI command colliding with a
built-in, a half that throws while loading. Each is refused with its message and leaves nothing registered.

**Placeholders.** A project naming a missing pack saves back byte for byte; installing the pack live turns the
placeholder into a running system with its ROM and roles intact; a refused pack's systems show the "needs updating"
card.

**Each host.** The plugin: two instances in one process share one load (a `test:plugin` case), and a pack installed live
reaches both. The standalone, DPF and SDL. The CLI: `pack install`, `list`, `remove`, `--pack`, a pack's tools as
commands. The Node addon loads the same packs, its PCM matching QuickJS's as today. The web is unchanged: a build-time
distribution, with no loader built.

**Concurrency and sanitizers.** Two processes installing into one `packs/` folder at once leave a valid `state.json`;
a live load while audio runs is clean under TSAN and ASAN.

## 6. Not in part 1

Device providers (N8 and Launchpad stay built in). The index, downloads and signing (part 4); ROM and project
resolution and the download offer (part 5); the SDK template, the CI matrix and notarization (part 3); moving source to
other repos (parts 2 and 6). Unloading native code, wasm packs and isolating packs from each other are not planned.
