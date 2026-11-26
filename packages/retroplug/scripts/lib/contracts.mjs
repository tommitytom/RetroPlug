// The contract modules (docs/downloadable-packs-loading-design.md section 3): every @retroplug/* specifier the shell may
// import, with the reason it is the shell's to reach. The boundary check (check-boundaries.mjs, rule 9) holds the shell
// to these lists, and the shared module sets (tools/build-shared-modules.mjs) build each one as a module named by its
// specifier, which is what a pack's own modules may import (M2c holds a pack to the same set). One list, two readers.

/** Rule 9's list for `src/` and `cli/`: every @retroplug/<package> specifier they may import, with the reason it is the
 *  shell's to reach. Each is a contract module (or a library that is not a cart), so a second console or a second
 *  tracker still needs no edit here; a cart's or a console's own module is reached through the generated registries
 *  (src/generated/packages.ts, protocol.ts and consoles.ts for the control plane, cli/generated/tools.ts for the CLI),
 *  which this rule does not judge. One list serves both roots on purpose: they are one program's two halves, so a
 *  contract module only cli/ imports today stays reachable from src/ without a second entry, and an entry per root
 *  would claim a precision the rule does not need. An entry no import of either root uses fails the check, so this
 *  shrinks with the shell. */
export const SHELL_IMPORTS = [
  // The contract every package implements: the shell is its host, not a participant.
  { specifier: "@retroplug/core/canonicalJson", reason: "the document encoder every protocol driver writes through" },
  { specifier: "@retroplug/core/capability", reason: "the capability jobs' operation table a host's systems group names and a cart types a job by" },
  { specifier: "@retroplug/core/cliTool", reason: "the CLI tool contract the dispatcher and the generated tool table implement" },
  { specifier: "@retroplug/core/configFields", reason: "the schema field helpers a role config and the stores' configs build with" },
  { specifier: "@retroplug/core/consoles", reason: "the console descriptor contract the generated registry is read through" },
  { specifier: "@retroplug/core/constructSpec", reason: "the construct spec a backend and a role hook carry" },
  { specifier: "@retroplug/core/defineKind", reason: "the contribution-kind contract a declaring package's control module implements and the generated registry calls" },
  { specifier: "@retroplug/core/definePackage", reason: "the contribution contract an app module (and the generated registry) implements" },
  { specifier: "@retroplug/core/descriptor", reason: "a node's span label and category, which the shell's node definitions state" },
  { specifier: "@retroplug/core/graphDocument", reason: "the graph document the shell's planner writes and the engine reads" },
  { specifier: "@retroplug/core/host", reason: "the host contract a cart's or a console's hook asks its surface for, and every surface's builder returns" },
  { specifier: "@retroplug/core/midiRouting", reason: "the MIDI routing model the project settings store and the planner share" },
  { specifier: "@retroplug/core/offlineJob", reason: "the offline job's document and result types the render and kit drivers carry" },
  { specifier: "@retroplug/core/placer", reason: "the graph placer contract buildGraph reads a package's slots through" },
  { specifier: "@retroplug/core/path", reason: "the pure path and save-file naming helpers a project store, a menu and a cart's own file actions all derive names with" },
  { specifier: "@retroplug/core/platform", reason: "the platform and core ids, open strings since the descriptors register them" },
  { specifier: "@retroplug/core/romParameters", reason: "the ROM parameter lane a role's parameters hook answers in" },
  { specifier: "@retroplug/core/systemRoles", reason: "the role contract every system role implements, the shell's role registry included" },
  // The kit compile driver's contract and its shared half: dsp is a library of graph nodes, not a cart.
  { specifier: "@retroplug/dsp/kitContract", reason: "the kit format contract the shell's format registry dispatches through" },
  { specifier: "@retroplug/dsp/packageContributions", reason: "the contribution shape an app module declares its kit formats through" },
  { specifier: "@retroplug/dsp/wav", reason: "the pure WAV codec the render library and its tests share with a package's CLI tool" },
  // The tracker contract: the neutral catalogs, the integration shape, the hooks the shell's pollers and renderer ask,
  // and the working-song image the store carries.
  { specifier: "@retroplug/tracker/assetCatalog", reason: "the asset catalog and the neutral effective-asset merge" },
  { specifier: "@retroplug/tracker/packageContributions", reason: "the contribution shape an app module declares its tracker integration through" },
  { specifier: "@retroplug/tracker/songCatalog", reason: "the song catalog and the working-song targets the store and the render read" },
  { specifier: "@retroplug/tracker/trackerIntegration", reason: "the tracker integration contract an app module contributes" },
  { specifier: "@retroplug/tracker/projectService", reason: "the project-service contract the store builds and polls a package's instances through" },
  { specifier: "@retroplug/tracker/trackerRender", reason: "the render hooks the render pipeline asks a ROM's integration for" },
  { specifier: "@retroplug/tracker/workingSongBlob", reason: "the working-song image a project carries across a reboot" },
  // The test harness itself: the SDK serves it to a consumer's tests, and the ROM-test session imports it.
  { specifier: "@retroplug/testing", reason: "the TAP harness a session's own tests run through (the SDK serves it to a consumer)" },
  // The SDK's Everdrive N8: a session script's device is the n8 pack's, over the process's RPC call.
  { specifier: "@retroplug/n8/device", reason: "the N8 device the SDK hands a session script (connectN8)" },
];

/** Rule 9's list for `ui/`: a list of its own, because the kit and the view contract are the UI's alone (src/ and cli/
 *  must never reach `@retroplug/ui-react`), and because the control plane needs no menu model or contribution type.
 *  Each entry is a contract module rather than a cart's or a console's code, which the UI reaches through the
 *  generated `ui/generated/menus.ts` and `ui/generated/views.ts`; an entry no `ui/` file imports fails the check, as an
 *  unused SHELL_IMPORTS entry does. */
export const UI_IMPORTS = [
  { specifier: "@retroplug/ui/menu", reason: "the menu model every composer and the Menu draw" },
  { specifier: "@retroplug/ui/contribution", reason: "the contribution and view types the generated registry and the composers read" },
  { specifier: "@retroplug/ui-react", reason: "the kit the shell's own components and the three renderers are built from" },
  { specifier: "@retroplug/core/host", reason: "the host contract the UI host implements" },
  { specifier: "@retroplug/core/systemRoles", reason: "the role contract the UI reads a system's roles through" },
  { specifier: "@retroplug/core/midiRouting", reason: "the routing model the MIDI settings menu edits" },
  { specifier: "@retroplug/tracker/songCatalog", reason: "the song catalog the shell's Songs menu reads through the integration" },
  { specifier: "@retroplug/tracker/assetCatalog", reason: "the asset catalog the shell's Assets menus read" },
  { specifier: "@retroplug/tracker/trackerIntegration", reason: "the integration contract the shell resolves a cart's tracker by" },
];

/** The libraries every shared module set carries as one named module, which a pack's halves import rather than bundle
 *  (docs/downloadable-packs-m5b-plan.md): a role config's schema a pack builds composes with the host's configFields,
 *  and one schema built from two zod instances is two type systems; one copy also keeps a pack's app half small. Rule 9
 *  does not judge these (it names @retroplug/* specifiers), and a kinds half still refuses them (its script contexts
 *  carry no schema library). Their versions are part of the API level (graph/rp_pack.h). */
export const SHARED_LIBRARIES = [
  { specifier: "zod", reason: "the schema library role configs, node settings and the stores' configs are built with" },
];

/** The host modules a pack's UI half may import (host-modules.json), minus the CLI binary's own `retroplug-cli`, which
 *  the CLI serves from its own embedded module and no shared set builds. */
export function hostModuleSpecifiers(hostModulesJson) {
  return hostModulesJson.map((entry) => entry.module).filter((name) => name !== "retroplug-cli");
}
