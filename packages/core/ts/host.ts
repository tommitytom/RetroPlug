// The host contract (docs/modular-graph-phase-f4b-design.md section 3): one object of named groups, one per capability
// a surface has, which the shell builds once per surface - the UI, the CLI, the render worker - out of the stores,
// backend and driver it already has. A hook types itself by the GROUPS it uses (`Host<"files" | "png">`, say), so a
// hook given a surface that lacks one is a compile error rather than a member that is undefined when the hook runs, and
// the manifest of a surface's host is its group list (ui/host/uiHost.ts, cli/cliHost.ts, src/render/renderHost.ts).
//
// Every member exists because code outside the shell calls it today; the shell's own generic machinery keeps using its
// stores directly. A type a group names that the shell owns (a PNG image, a file dialog's options, a MIDI client, the
// capability jobs' operation table) lives here as a type only, with the shell's module re-exporting it, so a cart or a
// console's TS can name it without importing the shell.
import type { Core, Platform } from "./platform";
import type { RoleInstance } from "./systemRoles";
import type {
  CapabilityArgs,
  CapabilityOp,
  CapabilityProgress,
  CapabilityResults,
} from "./capability";

/** A raw RGBA8888 image: `rgba` is `width*height*4` bytes, row-major, top-to-bottom (the output of a PNG decode and
 *  the shape a font override maps to and from tiles). */
export interface PngImageData {
  width: number;
  height: number;
  rgba: Uint8Array;
}

/** Presentation for an OS file dialog: its title and the glob patterns to show. */
export interface FileBrowserOpts {
  title: string;
  patterns: string[];
  /** True for a SAVE dialog (a filename can be typed and an overwrite confirmed); absent/false opens for reading. */
  saving?: boolean;
  /** A suggested filename for a save dialog (e.g. `"project.rplg"`); ignored when opening. */
  defaultName?: string;
  /** Directory the dialog opens in; the host's own default (its working directory) when absent or empty. Used to
   *  reopen a dialog where the user last saved (the render output dir today). */
  startDir?: string;
  /** Pick a FOLDER instead of a file: `patterns`/`defaultName` are then ignored. Supported on Linux + macOS; on Windows
   *  the OS opens a file dialog instead, and the caller takes the dirname. */
  directory?: boolean;
}

/** A compiled kit bank: the bytes, and each spec sample's reason in spec order, null exactly when the bank holds that
 *  sample whole. The dsp package's own `KitCompileResult` is this shape member for member; core declares it because it
 *  cannot import dsp, and a test holds the two assignable both ways. */
export interface KitBank {
  bytes: Uint8Array;
  errors: (string | null)[];
}

/** The synchronous JSON-RPC caller every native client reaches the host through: the method name and its params, the
 *  reply's result, a throw when the reply is an error. */
export type Call = (method: string, ...params: unknown[]) => unknown;

/** A live-MIDI client (mounted CLI-only today), for the TS N8 bridges and the Launchpad probe. `poll()` drains the
 *  messages queued since the last call (each = raw bytes; MIDI carries no timing). Synchronous like every other facet.
 *  `listInputs` returns [] on a host that did not mount it. The output half exists because a control surface has to be
 *  LIT as well as read, and `send` takes any length - a bulk-LED sysex runs to hundreds of bytes. */
export interface MidiClient {
  listInputs(): string[];
  listOutputs(): string[];
  /** Open a virtual "<clientName> In/Out" + the selected hardware input ("" = all inputs). False if no MIDI system. */
  open(clientName: string, input: string): boolean;
  /** Mirror sends to this hardware output ("" = the virtual port only). Applied immediately. */
  selectOutput(output: string): void;
  poll(): Uint8Array[];
  send(bytes: Uint8Array | number[]): void;
  close(): void;
}

/** The part of a system a hook may read: the shell's system view less its live `focused`/`settings` fields, which a
 *  cart has no business reading, so the view satisfies it structurally. */
export interface SystemInfo {
  readonly id: number;
  readonly platform: Platform;
  readonly core: Core;
  readonly romPath: string;
  readonly savPath: string;
  readonly savSuffix: number;
  readonly embedded: boolean;
  readonly battery: boolean;
  readonly missing: boolean;
  readonly roles: readonly RoleInstance[];
}

/** The control surface this machine uses (Settings > MIDI's scan and the instance menu's controller submenu). HOST
 *  state rather than project state, because port names belong to the machine: a project opened elsewhere would carry
 *  names that do not exist there, and one process holds one device for every project it has open.
 *
 *  The port names are opaque - often an interface's, not the device's, since a Launchpad also speaks TRS/DIN - and
 *  `device` is what a scan read off the hardware ("" = never scanned), shown and never interpreted. */
export interface ControlSurfaceSettings {
  input: string; // "" = none chosen
  output: string;
  enabled: boolean; // the user's Connect: claim the pair now, and again at the next launch
  device: string;
}

/** The file I/O every surface has, and the backend's own methods one for one. */
export interface FilesHost {
  readFile(path: string): Uint8Array | null;
  /** The first `bytes` bytes of the file, or null when it cannot be read. A header read does not copy the whole
   *  image: the backend truncates to the file's real size. */
  readPrefix(path: string, bytes: number): Uint8Array | null;
  writeFile(path: string, bytes: Uint8Array): boolean;
  writeFileAtomic(path: string, bytes: Uint8Array): boolean;
  fileExists(path: string): boolean;
}

/** The PNG codec: a font override decodes a linked image, a font export encodes one. */
export interface PngHost {
  encode(width: number, height: number, rgba: Uint8Array): Uint8Array | null;
  decode(bytes: Uint8Array): PngImageData | null;
}

/** Compiling a kit bank, which only a CLI tool does today. */
export interface KitsHost {
  /** The bank format `id` names among the product's registered kit formats, the spec compiled by its compiler. A
   *  per-sample failure leaves that slot empty and comes back in `errors`; an unknown id rejects, naming it. */
  compile<S>(id: string, spec: S): Promise<KitBank>;
}

/** The live systems of a project: their view, their battery, their roles and the nodes the graph places them at. */
export interface SystemsHost {
  /** The live systems, in slot order. */
  view(): readonly SystemInfo[];
  setRoleConfig(id: number, kind: string, config: Record<string, unknown>): boolean;
  /** Reboot a system in place from what it holds (the one rebuild a keep-open row ends in). */
  reload(id: number): unknown;
  /** The system's live battery, or null when its core has none to read. */
  readSram(id: number): Uint8Array | null;
  /** The system's live work RAM, or null when it has none to read. A UI-only read: the render surface reaches work RAM
   *  through its one emulator group instead. */
  readRam(id: number): Uint8Array | null;
  /** Reboot a system from the `.sav` at `path`: the one rebuild an import ends in. */
  loadSram(id: number, path: string): unknown;
  /** The `.sav` a save of system `id` writes: its paired-save override when it has one, else the file beside its ROM.
   *  Null when the system is gone, so a caller writes nothing rather than an empty path. */
  savPathOf(id: number): string | null;
  /** The graph id of a system's own node (`sys-<id>`), which a node aimed message names. */
  nodeIdOf(id: number): string;
  /** The system the document places a real cart for, and that cart's node id, or null when no system drives one. The
   *  one rule the document and every caller read, so they cannot disagree about which system's cart it is. */
  drivenCart(): { id: number; nodeId: string } | null;
  /** Deliver MIDI bytes straight to the input of one node of the running graph, outside the project's musical
   *  routing. */
  stageNodeMidi(nodeId: string, bytes: number[]): boolean;
  /** Run capability job `op` on node `nodeId` and settle with its completion (a read at once, a write once the block
   *  after it published, a device's job on its own thread). Rejects with native's reason when it is refused. */
  capability<O extends CapabilityOp>(
    nodeId: string,
    op: O,
    args: CapabilityArgs,
    onProgress?: (p: CapabilityProgress) => void,
  ): Promise<CapabilityResults[O]>;
}

/** The saved songs of a battery image as LINES to print: a CLI host prints them as they are, so a tool's output stays
 *  the tool's own while what a cart's battery means stays the cart's. */
export interface SongsHost {
  /** The header and one row per song (or the "unrecognized battery image" line) for `bytes`. */
  list(bytes: Uint8Array): string[];
}

/** The UI's half of a cart's songs: the live battery, and the picker an import from a `.sav` opens. A CLI host cannot
 *  offer either (it has no running system and no overlay), which is why the listing and these are two groups. */
export interface LiveSongsHost {
  /** Apply a byte transform to a running system's battery and cold-boot it, all or nothing. False when the transform
   *  declined (`fn` returned null) or the write/boot did not take, so a caller that CHAINS edits can stop at the first
   *  one that did not land. */
  mutate(sys: SystemInfo, fn: (sav: Uint8Array) => Uint8Array | null): boolean;
  /** Open the Songs "import from a `.sav`" picker on `source`, validated against `sys`'s console. */
  beginImport(sys: SystemInfo, source: Uint8Array): void;
}

/** What a render hook drives of the booted system. The render's own output stream is NOT here: it is the request's, so
 *  it is passed to the one hook that captures into it and a hook that runs before it exists cannot call it. */
export interface EmulatorHost {
  /** The audio a hook discards while it works (a boot wait, a prep press): whole milliseconds, in the render's mix. */
  renderAudio(ms: number): void;
  /** A CPU register's value, or null when the core cannot be read. */
  readCpu(id: number, addr: number): number | null;
  /** The system's work RAM, or null when a snapshot is not available this block. */
  readRam(id: number): Uint8Array | null;
  /** Write into the system's work RAM; false when the range is refused (a wrong layout, a full command ring). */
  writeRam(id: number, offset: number, bytes: Uint8Array): boolean;
  pressButton(id: number, button: number, down: boolean): void;
}

/** The OS file dialog. */
export interface DialogsHost {
  browse(opts: FileBrowserOpts): Promise<string | null>;
}

/** The native facets a tool reaches directly rather than through a store: the RPC channel, and the live-MIDI client. */
export interface DevicesHost {
  /** The JSON-RPC caller: what a client of a native facet this host does not model is built over. */
  rpc: Call;
  /** A live-MIDI client. A FACTORY, because a tool opens as many as it needs (a bridge calls it twice per run). */
  midi(): MidiClient;
}

/** The process a tool runs in. */
export interface ProcessHost {
  /** The tool's own arguments, exactly the `args` its `run(host, args)` was given: the dispatcher strips the command
   *  name, so a flag a tool reads from `args` reads the same here. A session run by path (no dispatcher) has none. */
  args(): string[];
  /** Keep the launcher pumping after the tool returns: what a long-running tool (a live bridge) asks for. */
  keepAlive(): void;
  exit(code: number): void;
}

/** The window views a package registers, by the id it names one with: what a menu row or an action opens. The id names a
 *  registered WINDOW view (an unknown id, or a tile view's id, is reported and opens nothing), and the shell refuses one
 *  whose `when` rejects the system, or a system that is gone, the same way (design 3.3). */
export interface ViewsHost {
  open(id: string, systemId: number): void;
}

/** The project's own settings, as a package's UI reads and edits them (docs/modular-graph-phase-f5a-design.md section
 *  6.4): the sections its packages contribute, by key, and what their project services published. It is how a package
 *  owns a piece of the project - the Launchpad's submenu edits `controller` through it - without the shell knowing the
 *  key's shape or which package declared it. */
export interface ProjectHost {
  /** The section as its contributor parses it (raw JSON for a key no package contributes). */
  section<T = Record<string, unknown>>(key: string): T;
  /** Apply a patch through the contributor's `merge`, mark the project dirty, publish the graph and notify. THROWS when
   *  the contributor refuses the patch, and does nothing for a section stamped newer than its contributor. */
  setSection(key: string, patch: Readonly<Record<string, unknown>>): void;
  /** What a project service published (the controller cart's polled SYNC), or undefined for no such service. */
  serviceState<T = unknown>(key: string): T | undefined;
}

/** `config.json`'s control surface: the machine's port pair, which a scan fills and the user's choices edit. */
export interface ConfigHost {
  controlSurface(): ControlSurfaceSettings;
  setControlSurface(patch: Partial<ControlSurfaceSettings>): void;
}

/** Every group a host can offer. A surface's host names the subset it has, and what it cannot do it does not offer. */
export interface HostGroups {
  files: FilesHost;
  png: PngHost;
  kits: KitsHost;
  systems: SystemsHost;
  songs: SongsHost;
  liveSongs: LiveSongsHost;
  emulator: EmulatorHost;
  dialogs: DialogsHost;
  devices: DevicesHost;
  process: ProcessHost;
  views: ViewsHost;
  project: ProjectHost;
  config: ConfigHost;
}

/** A surface's host, narrowed to the groups `G` names: `Host<"files" | "png">` is what a construct hook takes, and a
 *  host offering more satisfies it. */
export type Host<G extends keyof HostGroups = keyof HostGroups> = { readonly [K in G]: HostGroups[K] };
