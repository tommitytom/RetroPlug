// What TS has to boot a system from (docs/modular-graph-phase-c3-design.md section 6), a type only:
// graph/systemResources.ts (the shell's) turns it into the engine's resources, and nothing native reads it. It lives
// here rather than in the shell's backend because the role contract's onConstruct hook takes and returns one
// (docs/modular-graph-phase-f1-design.md section 9 question 1), and a package's role may not name the shell.
import type { Core, Platform } from "./platform";

export interface ConstructSpec {
  /** The ROM file to read; `""` when an embedded ROM is used instead. */
  romPath: string;
  /** What the ROM targets (TS classifies it via platform.ts). It picks the node the system is placed as
   *  (systemNodeTypeFor), and with it the console its core is built as. */
  platform: Platform;
  /** The kind of core whose role config `coreConfig` is (platform.ts's `Core`): the store keys a system's roles by it.
   *  It selects no core: the platform picks the node type the system is placed as (systemNodeTypeFor), and with it the
   *  console its core is built as. */
  core: Core;
  /** A binary-baked ROM marker (e.g. `"mgb"`); `""` for a file-backed ROM. */
  embeddedRom: string;
  /** Exact battery file: its bytes seed the battery when the file is there, and it is the auto-save target. `null` =
   *  fresh, and a path whose file is absent is fresh too (a missing save is not a failure). */
  savPath: string | null;
  /** Exact savestate file to boot from when the file is there. `null` = cold boot (the systems domain always passes
   *  null; present for the later Project domain). */
  statePath: string | null;
  /** Effective ROM bytes to boot INSTEAD of reading `romPath` - a load-time role's patched image (e.g. LSDj asset
   *  overrides applied non-destructively). `romPath` still names the system (its stem, its sav resolution); only the
   *  bytes differ. */
  romBytes?: Uint8Array;
  /** Seed SRAM bytes (a zip-import blob, a carried battery, or a load-time role's synthesized sav). When set they seed
   *  the battery instead of `savPath`'s file, which stays the auto-save target. */
  sramBytes?: Uint8Array;
  /** Seed savestate bytes (a zip-import blob or a duplicate's captured state). When set the core boots from these
   *  instead of `statePath`'s file; a savestate wins over a battery, as it does at the core. */
  stateBytes?: Uint8Array;
  /** The system's backend ("system"-role) config, in the labels its node reads (the role's schema, which a console
   *  package owns): its construct-time fields (a NES's SD root and FIFO, an SMS's FM) and its live knobs, each left out
   *  of the node's settings where it is absent or a value the node would refuse. Only a caller placing a literal spec
   *  (DirectGraph) writes it; the store reads the core role's config from the entry's own roles and never this field.
   *  Omitted for a system with no core-config role. */
  coreConfig?: Readonly<Record<string, unknown>>;
}
