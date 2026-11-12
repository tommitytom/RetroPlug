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
  /** The kinds of `roles` the system's graph places, in role order (the shell's protocol pipeline), and every present
   *  kind with no protocol node (a core's config role, a tracker marker such as risa), which the pipeline never leaves
   *  out: a stored role the graph leaves out, a stale one after a ROM swap, is in `roles` and not here. A view asks it
   *  through `isPlaced` (systemRoles) before it treats a system as its cart's. */
  readonly placed: readonly string[];
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
  /** How many whole projects have been adopted (a new one, a load). A service that resolved a section's names against
   *  the machine re-resolves when this moves, even if the new project's section bytes are the same. */
  revision(): number;
}

/** `config.json`'s sections (docs/modular-graph-phase-f5b-design.md section 6.1): the machine state a package owns by
 *  root key - the audio device, the control surface - with the shape, the defaults and the refusals kept by whoever
 *  contributed it. Mirrors the `project` group, at the file's root rather than a project's `settings`.
 *
 *  The file is shared with builds of other ages (a release plugin beside a newer standalone), so the rules a writer
 *  keeps are: a block THIS session did not set goes back exactly as the file carried it, additive fields and all; a key
 *  that is ABSENT reads its contributor's defaults but is not written until something sets it; and every write is
 *  computed over a re-read of the file rather than over the in-memory copy. */
export interface ConfigHost {
  /** The section as its contributor parses it (raw JSON for a root key no package contributes). A key the shell owns is
   *  refused, not answered. */
  section<T = Record<string, unknown>>(key: string): T;
  /** Apply a patch through the contributor's `merge`, written through a re-read of the file (two processes may share
   *  it). THROWS when it refuses the patch; does nothing for a section stamped newer than its contributor. */
  setSection(key: string, patch: Readonly<Record<string, unknown>>): void;
}

/** One choice of the audio device: the `audio` config section, exactly as it is saved - the backend and driver by
 *  their ids (never a library's number), the device by the name the driver knows, and the frames and width asked
 *  for. `backend: "auto"` takes the first backend the product names that opens. */
export interface AudioDeviceChoice {
  backend: string;
  driver: string;
  device: string;
  sampleRate: number;
  blockSize: number;
  outChannels: number;
}

/** What the device is actually running: the resolved backend, driver and device, the frames and width it OBTAINED
 *  (a driver may round), and `error` - empty for a real device, else the reason the null fallback is playing. */
export interface AudioDeviceStatus {
  backend: string;
  driver: string;
  device: string;
  sampleRate: number;
  blockFrames: number;
  channels: number;
  error: string;
  live: boolean;
}

/** One driver as the Settings > Audio rows read it: its id in its backend's vocabulary, the label a row shows, and the
 *  output devices it offers (the first entry of `names` is the driver's default, named by `defaultName`). */
export interface AudioDriverDescription {
  id: string;
  label: string;
  devices: { names: string[]; defaultName: string };
}

/** One backend: what it is called, the driver its Auto resolves to right now, and its drivers. Both of the last two are
 *  filled for the backend IN FORCE only: a backend initialises its library to answer them (the driver its Auto resolves
 *  to is a probe), so every other backend in the list carries its id, its label, `autoDriver: ""` and no drivers.
 *  Asking for another backend's drivers is `backendDrivers`, because that call is its library's first use. */
export interface AudioBackendDescription {
  id: string;
  label: string;
  autoDriver: string;
  drivers: AudioDriverDescription[];
}

/** What a RUN'S flags pin regardless of the saved section (`--audio-backend`, `--block-size`): the rows name it, so a
 *  user sees why the saved choice is not the one running. Empty/0 when no flag is set. */
export interface AudioRunOverrides {
  backend: string;
  blockSize: number;
}

/** Everything the Settings > Audio rows show: each backend (the one in force with its drivers, the others by name), what
 *  is running, and what this run's flags pinned. */
export interface AudioDescription {
  backends: AudioBackendDescription[];
  status: AudioDeviceStatus;
  overrides: AudioRunOverrides;
}

/** The audio device the standalone owns. A host that has no audio device of its own (the plugin, the DPF standalone,
 *  the UI harness) does not offer the group at all, so the rows that read it disappear as they always did. */
export interface AudioDeviceHost {
  describe(): AudioDescription | null;
  /** One backend's drivers and devices, on demand: the sibling of the backend in force, whose library this call is the
   *  first use of (the Driver row asks when the user opens it). */
  backendDrivers(backend: string): AudioDriverDescription[];
  /** The whole reconfigure: the sequence opens the device, shapes the engine for what it obtained and starts it. The
   *  answer is the status adopted (or why the null fallback is playing). */
  apply(choice: AudioDeviceChoice): AudioDeviceStatus | null;
  /** A cheap read for the status row and the UI frame. */
  status(): AudioDeviceStatus | null;
}

/** What the Settings > MIDI rows and the control surface see: the hardware ports this machine lists (filtered: no
 *  virtual port, no test-only hidden one), what the stream actually opened, whether the backend can create virtual
 *  ports at all (false on Windows), and the pair a control surface holds out of this stream. */
export interface MidiDescription {
  inputs: string[];
  outputs: string[];
  openedInputs: string[];
  openedOutputs: string[];
  supportsVirtual: boolean;
  reserved: { input: string; output: string };
}

/** The selection a project's `midi` section resolves to: EXACT port names (the package matched the saved names against
 *  the list itself), whether every hardware input is wanted, and the outputs to mirror to. */
export interface MidiSelection {
  inputs: string[];
  allInputs: boolean;
  outputs: string[];
}

/** The standalone's transport, as the MIDI Clock submenu shows it: what the engine is being told, whether an external
 *  clock master is driving it, the tempo in force and our own. */
export interface TransportState {
  playing: boolean;
  external: boolean;
  bpm: number;
  localBpm: number;
}

/** The MIDI device the standalone owns: its stream and the transport the clock drives. A host with no MIDI device
 *  (the plugin, whose DAW routes MIDI) does not offer the group. */
export interface MidiDeviceHost {
  describe(): MidiDescription | null;
  /** Apply a selection to the stream. `mode` is the stream's: "diff" keeps the ports an unchanged selection already
   *  holds open (a project load), "reopen" lets every port go before opening it again (the Rescan row and a re-pick,
   *  the one remedy for a port that was unplugged). */
  apply(selection: MidiSelection, mode?: "diff" | "reopen"): void;
  transport(): TransportState | null;
  setTransport(playing: boolean): void;
  setClockBpm(bpm: number): void;
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
  /** The standalone's audio device and MIDI stream. A surface offers them only where the host provides one, so a
   *  package's UI reads them optionally and a plugin (whose DAW owns both) draws no row for either. */
  audioDevice: AudioDeviceHost;
  midiDevice: MidiDeviceHost;
}

/** A surface's host, narrowed to the groups `G` names: `Host<"files" | "png">` is what a construct hook takes, and a
 *  host offering more satisfies it. */
export type Host<G extends keyof HostGroups = keyof HostGroups> = { readonly [K in G]: HostGroups[K] };
