// The TS package generator (packages/retroplug/scripts/lib/packages.mjs, docs/modular-graph-phase-f1-design.md
// section 3): the product list and the packages' manifests become the three generated modules. Here:
//
// - a fixture product list and fixture manifests give the expected modules byte for byte: packages.ts, protocol.ts
//   and scriptNodes.ts, the shell last;
// - the generated entry, bundled and loaded, registers the manifests' controller app runtimes and their nodes in
//   span-label order, and refuses when what the modules define or register is not the manifests' declarations;
// - each of the generator's refusals is exercised with its own message: a list that puts a package before a
// dependency (a cycle is one), a capture on a node that is not wired or whose out decodes nothing, an unlisted
// dependency that would contribute, a dependency on the shell, a node key no protocol node reads, a node wired on
// one side only, a shared span label, a shared node type, a shared role kind, a controller app id two RUNTIMES
// declare and one two control-plane halves declare (each naming both packages), an app entry with a key no runtime
// reads, a controller app with only one of its two halves (each way), a nodes module named with no nodes, a sync
// override whose key, node type or unused off-mode is wrong, two contributions whose generated identifiers mangle
// to one name, a label below 16, a gap, a role naming an undeclared or an unwired type, a named module that does
// not exist, a contribution its exports do not publish, a manifest whose name is not its directory's, a
// contribution outside ts/ and a contributing package the shell does not declare, a views or ui module named
// outside the package's ts/ui/ tree, a view body with no `open` (a session's) or no `component` (a component's), an
// action id twice (one package or two), an action id its package does not prefix, and an action default on a key or
// pad name the shell does not resolve, a single uppercase letter the hosts never send, or one the shell already
// binds. A dependency that contributes nothing, and one whose manifest has no retroplug member, are left out of the
// ordering rather than refused; a package contributing an app and no nodes names no nodes module and still
// registers its app; a tsconfig that must extend the generated host-module paths because it lists an example
// session or declares a host module the UI owns;
// - the generated module itself refuses a duplicate key in the lists the registries look up rather than sequence: a
//   role kind, a kit format's id and a tracker integration's marker role, each with its own message, and an embedded
//   ROM whose declaration and manifest entry do not pair (or name a console this product does not run);
// - an unchanged input leaves the generated files untouched, mtime and all, so a build that changes nothing rewrites
//   nothing.
//
// The generator is Node's (it reads and writes the filesystem), so the test runs it in a node child: the runner's own
// node (RP_NODE, which scripts/run-tests.mjs sets), else the node on the PATH when run by hand. test/packages/
// run-generator.mjs does the copying, mutating and reporting.
import { test, expect } from "@retroplug/testing";

// Just enough of txiki's spawn, a child's pipe and its file reads, declared locally as bundle-no-zod.test.ts does.
interface ByteStream {
  getReader(): { read(): Promise<{ done: boolean; value?: Uint8Array }> };
}
declare const tjs: {
  env: Record<string, string | undefined>;
  readFile(path: string): Promise<Uint8Array>;
  spawn(
    args: string[],
    opts: { stdout: "pipe"; stderr: "pipe" },
  ): {
    stdout: ByteStream | null;
    stderr: ByteStream | null;
    wait(): Promise<{ exit_status: number; term_signal: string | null }>;
  };
};

async function text(stream: ByteStream | null): Promise<string> {
  if (!stream) return "";
  const chunks: Uint8Array[] = [];
  const reader = stream.getReader();
  for (;;) {
    const { done, value } = await reader.read();
    if (done) break;
    if (value) chunks.push(value);
  }
  let total = 0;
  for (const c of chunks) total += c.length;
  const all = new Uint8Array(total);
  let at = 0;
  for (const c of chunks) {
    all.set(c, at);
    at += c.length;
  }
  return new TextDecoder().decode(all);
}

/** The driver's report for one mode, parsed. */
async function drive(mode: string, args: string[] = []): Promise<Record<string, any>> {
  const child = tjs.spawn([tjs.env.RP_NODE || "node", "test/packages/run-generator.mjs", mode, ...args], {
    stdout: "pipe",
    stderr: "pipe",
  });
  // Both pipes drained beside the wait, so a child that fills one cannot block on it.
  const [out, err, status] = await Promise.all([text(child.stdout), text(child.stderr), child.wait()]);
  if (status.term_signal || status.exit_status !== 0) throw new Error(`the generator driver failed: ${err || out}`);
  return JSON.parse(out) as Record<string, any>;
}

/** The expected module a fixture records, read beside the fixtures. */
async function expected(name: string): Promise<string> {
  return new TextDecoder().decode(await tjs.readFile(`test/packages/fixtures/expected/${name}`));
}

/** The first line two modules disagree on, or null: a byte comparison of two ~3 KB files says nothing useful when it
 *  fails, and a line names the mistake. */
function difference(generated: string, want: string): string | null {
  const a = generated.split("\n");
  const e = want.split("\n");
  for (let i = 0; i < Math.max(a.length, e.length); i++) {
    if (a[i] !== e[i]) {
      return `line ${i + 1}: generated ${JSON.stringify(a[i] ?? null)}, expected ${JSON.stringify(e[i] ?? null)}`;
    }
  }
  return null;
}

async function matchesFixture(mode: string, name: string): Promise<boolean> {
  const generated = (await drive(mode)).files as Record<string, string>;
  const diff = difference(generated[name]!, await expected(name));
  if (diff) console.log(`# ${name}: ${diff}`);
  return diff === null;
}

test("the fixture product list and manifests generate the expected packages.ts", async () => {
  const report = await drive("generate");
  // The list is the order: zeta is listed first and depends on nothing, alpha next, then beta (which depends on it);
  // the shell closes the app, so no package may depend on it.
  expect(report.packages).toEqual(["zeta", "alpha", "beta", "retroplug"]);
  expect(await matchesFixture("generate", "packages.ts")).toBe(true);
});

test("the fixture product list and manifests generate the expected consoles.ts", async () => {
  expect(await matchesFixture("generate", "consoles.ts")).toBe(true);
});

test("the fixture product list and manifests generate the expected protocol.ts", async () => {
  expect(await matchesFixture("generate", "protocol.ts")).toBe(true);
});

test("the fixture product list and manifests generate the expected scriptNodes.ts", async () => {
  expect(await matchesFixture("generate", "scriptNodes.ts")).toBe(true);
});

test("the fixture product list and manifests generate the expected menus.ts", async () => {
  expect(await matchesFixture("generate", "menus.ts")).toBe(true);
});

test("the fixture product list and manifests generate the expected placers.ts", async () => {
  // A package's graph placer, in product order: the module buildGraph's slots are filled from, which must carry types
  // and constants alone (test/script/bundle-protocol.test.ts holds the planner's bundle to it).
  expect(await matchesFixture("generate", "placers.ts")).toBe(true);
});

test("the fixture product list and manifests generate the expected views.ts", async () => {
  // The views are a module of their own: apart from menus.ts, which the pure tier bundles, so that a component view's
  // React is reached only from the UI bundle.
  expect(await matchesFixture("generate", "views.ts")).toBe(true);
});

test("the generated menus module carries the packages' UI contributions in product order", async () => {
  // zeta is listed before alpha and beta, so this is the product list's order and not an alphabetical sort.
  const { packages } = await drive("ui");
  expect(packages).toEqual(["zeta", "alpha", "beta"]);
});

test("the generated menus module refuses a row id used twice in a placement", async () => {
  const { error } = await drive("ui-duplicate-row");
  console.log(`# duplicate row id: ${error}`);
  expect(error).toBe("the package alpha declares the row alpha-model twice in the placement system.settings");
});

test("the generated menus module refuses a builder at a placement no placement has", async () => {
  const { error } = await drive("ui-unknown-placement");
  console.log(`# unknown placement: ${error}`);
  expect(error).toBe(
    'the package alpha declares a menu builder at the placement instance.elsewhere, which is no placement (the ' +
      "placements are instance.tracker, instance.devices, instance.footer, system.settings, start, settings.midi)",
  );
});

test("the generated menus module refuses a row whose role kind no registered role type declares", async () => {
  const { error } = await drive("ui-role-undeclared");
  console.log(`# row role undeclared: ${error}`);
  expect(error).toBe(
    "the package alpha declares the row alpha-model over the role nowhere, which no registered role type has",
  );
});

test("the generated menus module refuses a row whose key its role's schema does not declare", async () => {
  const { error } = await drive("ui-unknown-key");
  console.log(`# row key undeclared: ${error}`);
  expect(error).toBe(
    "the package alpha declares the row alpha-model over the key nonsense, which the role alpha-role does not read",
  );
});

test("the generated menus module refuses a row gated on a key its role's schema does not declare", async () => {
  const { error } = await drive("ui-unknown-when-key");
  console.log(`# row gate key undeclared: ${error}`);
  expect(error).toBe(
    "the package alpha declares the row alpha-model gated on the key nonsense, which the role alpha-role does not read",
  );
});

test("the generated menus module refuses a tracker UI id no registered integration has", async () => {
  const { error } = await drive("ui-unknown-tracker");
  console.log(`# tracker id unregistered: ${error}`);
  expect(error).toBe(
    "the package alpha declares a tracker UI for nowhere, which no registered tracker integration has",
  );
});

test("the generated menus module refuses a tracker UI id declared twice", async () => {
  const { error } = await drive("ui-duplicate-tracker");
  console.log(`# tracker id twice: ${error}`);
  expect(error).toBe("the package alpha declares the tracker UI alpha-tracks twice");
});

test("the generated menus module refuses a UI service id declared twice", async () => {
  const { error } = await drive("ui-duplicate-service");
  console.log(`# service id twice: ${error}`);
  expect(error).toBe("the package alpha declares the UI service alpha-watch twice");
});

test("the generated menus module carries the packages' views in product order", async () => {
  const { views, menus, error } = await drive("views");
  if (error) console.log(`# views: ${error}`);
  expect(views).toEqual([
    { pkg: "zeta", id: "zeta-view", placement: "tile", kind: "readout" },
    { pkg: "alpha", id: "alpha-view", placement: "tile", kind: "readout" },
    { pkg: "beta", id: "beta-view", placement: "window", kind: "pixels" },
  ]);
  // zeta is a views-only package in this mode, so the menus module lists no row for it while the views module keeps its
  // view: the two halves read the same manifests and each takes what the package declares.
  expect(menus).toEqual(["alpha"]);
});

test("the generated menus module refuses a view id one package declares twice", async () => {
  const { error } = await drive("views-duplicate-id");
  console.log(`# duplicate view id: ${error}`);
  expect(error).toBe("the package alpha declares the view same-view twice");
});

test("the generated menus module refuses a view id two packages declare", async () => {
  const { error } = await drive("views-duplicate-across");
  console.log(`# view id across packages: ${error}`);
  expect(error).toBe("two packages declare the view same-view: alpha and beta");
});

test("the generated menus module refuses a view at a placement no placement has", async () => {
  const { error } = await drive("views-bad-placement");
  console.log(`# unknown view placement: ${error}`);
  expect(error).toBe(
    "the package alpha declares the view alpha-view at the placement float, which is no placement (the placements " +
      "are window, tile)",
  );
});

test("the generated menus module refuses a view body kind none of the three", async () => {
  const { error } = await drive("views-bad-kind");
  console.log(`# unknown body kind: ${error}`);
  expect(error).toBe(
    "the package alpha declares the view alpha-view with the body kind hologram, which is no body kind (the kinds " +
      "are pixels, readout, component)",
  );
});

test("the generated menus module refuses a view with no body, naming the package", async () => {
  const { error } = await drive("views-no-body");
  console.log(`# missing body: ${error}`);
  expect(error).toBe(
    "the package alpha declares the view alpha-view with the body kind undefined, which is no body kind (the kinds " +
      "are pixels, readout, component)",
  );
});

test("the generated menus module refuses a session body with no open", async () => {
  // A pixels or readout body is opened by the shell, so one with no `open` could never draw; the check names the body
  // kind rather than letting the renderer's first call fail.
  const { error } = await drive("views-no-open");
  console.log(`# view body with no open: ${error}`);
  expect(error).toBe(
    "the package alpha declares the view alpha-view with a readout body and no open (a readout body's open is what the " +
      "shell calls)",
  );
});

test("the generated menus module refuses a component body with no component", async () => {
  const { error } = await drive("views-no-component");
  console.log(`# component body with no component: ${error}`);
  expect(error).toBe(
    "the package alpha declares the view alpha-view with a component body and no component (reactView builds the one a " +
      "component body carries)",
  );
});

test("the generated menus module refuses a visible flag on a window view", async () => {
  const { error } = await drive("views-window-visible");
  console.log(`# window view visible: ${error}`);
  expect(error).toBe("the package alpha declares the window view alpha-view with a visible flag, which only a tile " +
    "view has");
});

test("the generator refuses a views module named outside the package's ts/ui/ tree", async () => {
  // A views module is UI code, and the tree rule 3 admits a React host module from is ts/ui/: a views module beside it
  // would be a second, unjudged door for one.
  const { error } = await drive("views-outside-ui");
  console.log(`# views outside ts/ui: ${error}`);
  expect(error).toBe(
    "alpha: retroplug.views names ts/views.ts, which is outside the package's ts/ui/ tree: a views module is UI " +
      "code, and rule 3 admits a React host module only from there",
  );
});

test("the generator refuses a ui module named outside the package's ts/ui/ tree", async () => {
  // The same placement rule for `retroplug.ui`: every package's entry is ts/ui/index.ts, and one that were not would
  // import React from a file surfaceOf gives no surface.
  const { error } = await drive("ui-outside-ui");
  console.log(`# ui outside ts/ui: ${error}`);
  expect(error).toBe(
    "alpha: retroplug.ui names ts/ui.ts, which is outside the package's ts/ui/ tree: the ui module is the package's " +
      "UI code, and rule 3 admits a React host module only from there",
  );
});

test("the generated registry collects the project sections and services in product order", async () => {
  // Each app module declares them (the shell's ProjectSection contract and the tracker's project service), and the
  // shell reads the list: a section's key is a block of a .rplg's settings, a service's the instance it builds per
  // store.
  const report = await drive("generate");
  expect(report.projectSections).toEqual(["alpha:alpha", "beta:beta", "retroplug:shell"]);
  expect(report.projectServices).toEqual(["alpha", "beta", "shell"]);
});

test("a contribution kind's duplicate item id is refused, naming the package that listed it twice", async () => {
  const { error } = await drive("kind-duplicate-item");
  console.log(`# duplicate kind item: ${error}`);
  expect(error).toBe("the package alpha contributes the widgets item w twice");
});

test("a contribution kind's item with no control-plane half is refused", async () => {
  const { error } = await drive("kind-one-half");
  console.log(`# kind item with no control half: ${error}`);
  expect(error).toBe("the widgets item w has no control-plane half");
});

test("a product list that is the desktop's subsequence gets its own tree, its nodes at its positions", async () => {
  // "solo" is [zeta], so the desktop's node labels (alpha's 16, the shell's 17) are not what it has: the shell's single
  // node is its position 16. Its tree is under the build directory, in the shell's shape, and the desktop's is still
  // where it was (design 7.1, decision 9, E6).
  const report = await drive("product", ["solo"]);
  const files = report.files as Record<string, string>;
  const labels = files["scriptNodes.ts"].match(/"rp\.[a-z.]+": \d+/g) ?? [];
  console.log(`# the product's script-node labels: ${labels.join(", ")}`);
  expect(labels).toEqual(['"rp.fixture.shell": 16']);
  // The desktop still writes its own tree, with both labels.
  expect((report.desktop as Record<string, string>)["scriptNodes.ts"].includes('"rp.alpha.midi": 16')).toBe(true);
  expect((report.desktop as Record<string, string>)["scriptNodes.ts"].includes('"rp.fixture.shell": 17')).toBe(true);
  // Its typecheck program is written beside it, extends the shell's, and takes a paths file of its own.
  expect(files.tsconfig.includes("packages/retroplug/tsconfig")).toBe(true);
  expect(files.tsconfig.includes("./tsconfig.paths.json")).toBe(true);
});

test("the desktop's list must carry every contributing package the shell declares", async () => {
  // The desktop's list is the union: a package the shell declares and that contributes and the list leaves out would be
  // imported by the shell's generated modules and registered by nothing. A product list that is not the desktop's may
  // drop one deliberately, which the `product` case's "solo" list (alpha and beta dropped) holds.
  const { error } = await drive("shell-unlisted");
  console.log(`# desktop list without a contributing shell dependency: ${error}`);
  expect(error).toBe(
    "retroplug depends on @retroplug/zeta, which contributes to the app and the product does not list",
  );
});

test("a product list that is not the desktop's subsequence is refused", async () => {
  const { error } = await drive("product-refused", ["out-of-order"]);
  console.log(`# product list out of order: ${error}`);
  expect(error).toBe(
    "retroplug.products.out-of-order is not a subsequence of retroplug.products.desktop: alpha is in no order there, " +
      "or out of it",
  );
});

test("a product list naming a package the desktop does not is refused", async () => {
  const { error } = await drive("product-refused", ["unknown-package"]);
  console.log(`# product list with an unknown package: ${error}`);
  expect(error).toBe(
    "retroplug.products.unknown-package is not a subsequence of retroplug.products.desktop: nowhere is in no order " +
      "there, or out of it",
  );
});

test("a product list is validated as the desktop's is, and a product the manifest has not is refused", async () => {
  const twice = await drive("product-refused", ["twice"]);
  console.log(`# product list twice: ${twice.error}`);
  expect((twice.error as string).endsWith("lists a package twice in retroplug.products.twice")).toBe(true);
  const shell = await drive("product-refused", ["shell"]);
  console.log(`# product list with the shell: ${shell.error}`);
  expect((shell.error as string).endsWith("lists the shell (retroplug) in retroplug.products.shell")).toBe(true);
  const unknown = await drive("product-refused", ["nowhere"]);
  console.log(`# unknown product: ${unknown.error}`);
  expect(
    (unknown.error as string).endsWith(
      "has no retroplug.products.nowhere list of package names (the products are desktop, solo, out-of-order, " +
        "unknown-package, twice, shell)",
    ),
  ).toBe(true);
});

test("a contribution kind named after an app member is refused", async () => {
  // A kind's items are read from the app module by the kind's own name, so a kind called `roles` would read the app's
  // roles as items.
  const { error } = await drive("kind-app-member");
  console.log(`# kind named after an app member: ${error}`);
  expect(error).toBe(
    "alpha: the contribution kind roles is named after the app member roles, which the generated registry would read " +
      "as that member's list of items (an app's members are roles, romProviders, embeds, projectSections, " +
      "trackerIntegrations, projectServices, kitFormats)",
  );
});

test("a declaring package's check runs over its kind's data at load", async () => {
  // Generic validation cannot know what a kind's data means: the data's own package refuses a mode naming a node type
  // no package declares, with the product's node types handed in.
  const { error } = await drive("kind-check-unknown-node");
  console.log(`# kind check refusing an unknown node type: ${error}`);
  expect(error).toBe('the widgets item w names "rp.nowhere", which no package declares');
});

test("the generated registry refuses a project section two packages declare", async () => {
  const { error } = await drive("duplicate-section");
  console.log(`# duplicate project section: ${error}`);
  expect(error).toBe("two packages declare the project section shared: alpha and beta");
});

test("the generated registry refuses a project section under one of the shell's own keys", async () => {
  const { error } = await drive("shell-section");
  console.log(`# shell-owned project section: ${error}`);
  expect(error).toBe(
    "the package alpha declares the project section layout, which is one of the shell's own settings " +
      "(layout, midiRouting, audioRouting, zoom)",
  );
});

test("the generated registry refuses two project services under one key", async () => {
  const { error } = await drive("duplicate-service");
  console.log(`# duplicate project service: ${error}`);
  expect(error).toBe("two packages declare the project service shared: alpha and beta");
});

test("the generated registry refuses a project service whose key names no section", async () => {
  const { error } = await drive("service-no-section");
  console.log(`# project service with no section: ${error}`);
  expect(error).toBe(
    "the package alpha declares the project service nowhere, which matches no project section " +
      "(the service key is its section's key)",
  );
});

test("the generated registry refuses a project service under one of the shell's own keys", async () => {
  const { error } = await drive("service-shell-key");
  console.log(`# shell-owned project service: ${error}`);
  expect(error).toBe(
    "the package alpha declares the project service layout, which is one of the shell's own settings " +
      "(layout, midiRouting, audioRouting, zoom)",
  );
});

test("the generated registry refuses a graph placer whose key names no section", async () => {
  const { error } = await drive("placer-no-section");
  console.log(`# graph placer with no section: ${error}`);
  expect(error).toBe(
    "the package alpha places the graph's model nowhere, which matches no project section " +
      "(a placer's key is its section's key)",
  );
});

test("the generated registry refuses a graph placer under one of the shell's own keys", async () => {
  const { error } = await drive("placer-shell-key");
  console.log(`# shell-owned graph placer: ${error}`);
  expect(error).toBe(
    "the package alpha places the graph's model layout, which is one of the shell's own settings " +
      "(layout, midiRouting, audioRouting, zoom)",
  );
});

test("the generated placers module refuses two packages placing one graph model", async () => {
  const { error } = await drive("duplicate-placer");
  console.log(`# duplicate graph model: ${error}`);
  expect(error).toBe("two packages place the graph's model alpha: alpha and beta");
});

test("the generator refuses a placer module that does not exist", async () => {
  const { error } = await drive("placer-missing");
  console.log(`# missing placer module: ${error}`);
  expect((error ?? "").includes("retroplug.protocol's placer names ts/nowhere.ts, which does not exist")).toBe(true);
});

test("the generated menus module carries the packages' actions in product order", async () => {
  // One action per package, in the product list's order (zeta first); zeta's and alpha's share a backtick, which two
  // contributions may, and beta's has a pad default and no key.
  const { actions, error } = await drive("actions");
  if (error) console.log(`# actions: ${error}`);
  expect(actions).toEqual([
    { pkg: "zeta", id: "zeta.first", label: "Zeta First", keys: ["`"], pad: [] },
    { pkg: "alpha", id: "alpha.second", label: "Alpha Second", keys: ["`"], pad: [] },
    { pkg: "beta", id: "beta.third", label: "Beta Third", keys: [], pad: ["guide"] },
  ]);
});

test("the generated menus module refuses an action id one package declares twice", async () => {
  const { error } = await drive("actions-duplicate-id");
  console.log(`# duplicate action id: ${error}`);
  expect(error).toBe("the package alpha declares the action alpha.same twice");
});

test("the generated menus module refuses an action id two packages declare", async () => {
  const { error } = await drive("actions-duplicate-across");
  console.log(`# action id across packages: ${error}`);
  expect(error).toBe("two packages declare the action alpha.same: alpha and beta");
});

test("the generated menus module refuses an action id its own package does not prefix", async () => {
  const { error } = await drive("actions-bad-prefix");
  console.log(`# action id prefix: ${error}`);
  expect(error).toBe(
    'the package alpha declares the action zeta.stolen, which is not prefixed by its own name ("alpha.")',
  );
});

test("the generated menus module refuses an action default key name no key resolves", async () => {
  const { error } = await drive("actions-unknown-key");
  console.log(`# action key name: ${error}`);
  expect(error).toBe("the package alpha declares the action alpha.key on the key Nonsense, which is no key name");
});

test("the generated menus module refuses an action default on a single uppercase letter", async () => {
  // The hosts deliver a letter unshifted, so "F" would show in the bindings menu and never fire: the refusal names the
  // lowercase key to bind instead.
  const { error } = await drive("actions-uppercase-key");
  console.log(`# action uppercase key: ${error}`);
  expect(error).toBe(
    "the package alpha declares the action alpha.key on the key F by default, which no host sends (the hosts send " +
      'letters unshifted: bind "f")',
  );
});

test("the generated menus module refuses an action default pad name outside SDL's button names", async () => {
  const { error } = await drive("actions-unknown-pad");
  console.log(`# action pad name: ${error}`);
  expect(error).toBe(
    "the package alpha declares the action alpha.pad on the gamepad button nonsense, which is no SDL button name " +
      "(the names are GAMEPAD_BUTTON_NAMES)",
  );
});

test("the generated menus module refuses an action default on a key a shell action already binds", async () => {
  const { error } = await drive("actions-shell-key");
  console.log(`# action on a shell key: ${error}`);
  expect(error).toBe(
    "the package alpha declares the action alpha.menu on the key Escape by default, which the shell already binds " +
      "(a shell action or a Game Boy button)",
  );
});

test("the generated menus module refuses an action default on a Game Boy button's keyboard key", async () => {
  const { error } = await drive("actions-gb-key");
  console.log(`# action on a GB key: ${error}`);
  expect(error).toBe(
    "the package alpha declares the action alpha.jump on the key Z by default, which the shell already binds " +
      "(a shell action or a Game Boy button)",
  );
});

test("the generated menus module refuses an action default on a key code a Game Boy button already binds", async () => {
  // "Return" is a different name from the Start default "Enter" and the same code (0x0d): the check compares resolved
  // codes, which is what one press sees.
  const { error } = await drive("actions-return-key");
  console.log(`# action on a GB key code: ${error}`);
  expect(error).toBe(
    "the package alpha declares the action alpha.enter on the key Return by default, which the shell already binds " +
      "(a shell action or a Game Boy button)",
  );
});

test("the generated menus module refuses an action default on a Game Boy button's gamepad button", async () => {
  const { error } = await drive("actions-gb-pad");
  console.log(`# action on a GB pad button: ${error}`);
  expect(error).toBe(
    "the package alpha declares the action alpha.jump on the gamepad button a by default, which the shell already " +
      "binds (a shell action or a Game Boy button)",
  );
});

test("a retroplug.ui member naming a module that is not there is refused", async () => {
  const { error } = await drive("ui-missing-module");
  console.log(`# missing ui module: ${error}`);
  expect((error as string).includes("alpha: retroplug.ui names ts/nothing.ts, which does not exist (")).toBe(true);
});

test("the generated cli module carries the packages' tools in product order", async () => {
  // zeta is listed before alpha and beta, so this is the product list's order and not an alphabetical sort.
  const { tools } = await drive("cli");
  expect(tools).toEqual([
    { pkg: "zeta", tools: ["zeta-tool"] },
    { pkg: "alpha", tools: ["alpha-tool"] },
    { pkg: "beta", tools: ["beta-tool"] },
  ]);
});

test("the generated cli module refuses a tool name two packages declare", async () => {
  const { error } = await drive("cli-duplicate-tool");
  console.log(`# duplicate tool name: ${error}`);
  expect(error).toBe("two packages declare the CLI tool same: alpha and beta");
});

test("a retroplug.cli member naming a module that is not there is refused", async () => {
  const { error } = await drive("cli-missing-module");
  console.log(`# missing cli module: ${error}`);
  expect((error as string).includes("alpha: retroplug.cli names ts/nothing.ts, which does not exist (")).toBe(true);
});

test("an example whose entry is outside the package's examples/ tree is refused", async () => {
  const { error } = await drive("examples-outside");
  console.log(`# example outside examples/: ${error}`);
  expect(error).toBe(
    "alpha: the example away's entry ts/away.ts is outside its examples/ tree, where the shell's program includes it",
  );
});

test("a package with examples and no tsconfig extending the generated host modules is refused", async () => {
  const { error } = await drive("examples-tsconfig-missing");
  console.log(`# examples without a tsconfig: ${error}`);
  expect(error).toBe(
    "alpha: it lists examples but has no tsconfig.json, which must extend ../retroplug/tsconfig.host-modules.json so " +
      "an example resolves its host module",
  );
});

test("a package with examples whose tsconfig does not extend the generated host modules is refused", async () => {
  const { error } = await drive("examples-tsconfig-unextended");
  console.log(`# examples tsconfig unextended: ${error}`);
  expect(error).toBe(
    "alpha: it lists examples but its tsconfig.json does not extend ../retroplug/tsconfig.host-modules.json, where " +
      "an example resolves a host module",
  );
});

test("a package with examples whose tsconfig extends the generated host modules is accepted", async () => {
  const { error } = await drive("examples-tsconfig-extended");
  expect(error).toBe(null);
});

test("a package declaring a UI host module without a tsconfig is refused", async () => {
  const { error } = await drive("react-tsconfig-missing");
  console.log(`# a UI host module without a tsconfig: ${error}`);
  expect(error).toBe(
    "alpha: it declares a UI host module but has no tsconfig.json, which must extend " +
      "../retroplug/tsconfig.host-modules.json so a .tsx resolves React",
  );
});

test("a package declaring a UI host module whose tsconfig does not extend the generated host modules is refused", async () => {
  const { error } = await drive("react-tsconfig-unextended");
  console.log(`# a UI host module's tsconfig unextended: ${error}`);
  expect(error).toBe(
    "alpha: it declares a UI host module but its tsconfig.json does not extend " +
      "../retroplug/tsconfig.host-modules.json, where a .tsx resolves React",
  );
});

test("a package declaring a UI host module whose tsconfig extends the generated host modules is accepted", async () => {
  const { error } = await drive("react-tsconfig-extended");
  expect(error).toBe(null);
});

test("an example named after one of the shell's own sessions is refused, naming the shell's", async () => {
  const { error } = await drive("examples-shell-name");
  console.log(`# shell example name: ${error}`);
  expect(error).toBe(
    "alpha: the example session render-rom is the shell's own (cli/sessions/render-rom.ts), which is bundled to the " +
      "same build/cli/render-rom.js",
  );
});

test("an example name that is not a plain file stem is refused", async () => {
  const { error } = await drive("examples-unsafe-name");
  console.log(`# unsafe example name: ${error}`);
  expect(error).toBe(
    "alpha: the example session name ../escape is not a file stem: a name must match ^[A-Za-z0-9_-][A-Za-z0-9._-]*$, " +
      "because it becomes build/cli/<name>.js and an entry of the CMake list",
  );
});

test("two packages declaring the same example session name are refused", async () => {
  const { error } = await drive("examples-duplicate-name");
  console.log(`# duplicate example name: ${error}`);
  expect(error).toBe("the example session same-session is declared twice: alpha and beta");
});

test("a host declaration the shell's list does not have is refused", async () => {
  const { error } = await drive("host-unknown");
  console.log(`# unknown host module: ${error}`);
  expect(error).toBe(
    "alpha: retroplug.host names not-a-host-module, which the shell's host-modules.json does not list",
  );
});

test("a host declaration on the shell's list is accepted", async () => {
  const { error } = await drive("host-known");
  expect(error).toBe(null);
});

test("the generated entry bundles and registers the manifests' nodes in span-label order", async () => {
  const { registered } = await drive("run");
  console.log(`# registered: ${(registered as string[]).join(", ")}`);
  expect(registered).toEqual(["rp.alpha.midi", "rp.fixture.shell"]);
});

test("the generated entry refuses a manifest node its module does not define", async () => {
  const { error } = await drive("missing-node");
  console.log(`# bundled entry: ${error}`);
  expect(error).toBe(
    "the script bundle's nodes are not the manifests': 1 defined, 2 declared; rp.alpha.midi has no definition",
  );
});

test("a product list that puts a package before its dependency is refused, naming both", async () => {
  const { error } = await drive("order");
  console.log(`# order: ${error}`);
  expect(error).toBe(
    "the product lists beta before @retroplug/alpha, which it depends on: a package must follow its dependencies",
  );
});

test("a dependency cycle is refused through the same rule", async () => {
  const { error } = await drive("cycle");
  console.log(`# cycle: ${error}`);
  expect(error).toBe(
    "the product lists alpha before @retroplug/beta, which it depends on: a package must follow its dependencies",
  );
});

test("an unlisted dependency that would contribute to the app is refused", async () => {
  const { error } = await drive("unlisted-contributes");
  console.log(`# unlisted (contributes): ${error}`);
  expect(error).toBe("beta depends on @retroplug/gamma, which contributes to the app and the product does not list");
});

test("an unlisted dependency that contributes nothing is left out of the ordering", async () => {
  const report = await drive("unlisted-plain");
  expect(report.packages).toEqual(["zeta", "alpha", "beta", "retroplug"]);
  expect(await matchesFixture("unlisted-plain", "packages.ts")).toBe(true);
});

test("a dependency on the shell is refused, whatever it contributes", async () => {
  const { error } = await drive("depends-on-shell");
  console.log(`# depends on the shell: ${error}`);
  expect(error).toBe("beta depends on the shell (@retroplug/retroplug); no package may");
});

test("a node key no protocol node reads is refused, naming the key", async () => {
  const { error } = await drive("unknown-key");
  console.log(`# unknown node key: ${error}`);
  expect(error).toBe(
    'alpha: the node rp.alpha.midi has "launchs", which no protocol node reads (a node\'s keys are type, label, ' +
      "midiIn, writes, launches, serialOut, midiOut, capture, out)",
  );
});

test("a node's capture table is checked: what each mode asks the core to do", async () => {
  const { error } = await drive("capture-bad");
  console.log(`# bad capture: ${error}`);
  expect(error).toBe(
    'alpha: the node rp.alpha.midi\'s capture for "midiMap" is "sideways", not "off", "listen" or "drive"',
  );
});

test("a capture is refused on a node that is not wired, or whose out decodes nothing", async () => {
  // A capture arms a core to clock and capture its serial out: what reads those bytes is the serial-out node the
  // capture-carrier names as its `out`. Each misuse here would clock the core under bytes nothing decodes.
  const unwired = await drive("capture-unwired");
  console.log(`# capture on an unwired node: ${unwired.error}`);
  expect(unwired.error).toBe(
    "alpha: the node rp.alpha.midi declares a capture and is not wired (no midiIn and writes), so PROTOCOL_NODES " +
      "would drop it whole and nothing would decode the bytes it clocked",
  );
  const noDecoder = await drive("capture-no-decoder");
  console.log(`# capture with no decoder: ${noDecoder.error}`);
  expect(noDecoder.error).toBe(
    "alpha: the node rp.alpha.midi declares a capture and no out: a capture is for the serial-out node that decodes " +
      "the bytes",
  );
  const undeclared = await drive("capture-out-undeclared");
  console.log(`# capture decoded at no package's node: ${undeclared.error}`);
  expect(undeclared.error).toBe(
    "alpha: the node rp.alpha.midi declares a capture decoded at rp.alpha.nope, which no package declares",
  );
  const notSerial = await drive("capture-out-not-serial");
  console.log(`# capture decoded at a node that is no decoder: ${notSerial.error}`);
  expect(notSerial.error).toBe(
    "alpha: the node rp.alpha.midi declares a capture decoded at rp.alpha.plainer, which declares no serialOut",
  );
});

test("a capture that is not an object of mode: capture is refused", async () => {
  const { error } = await drive("capture-not-object");
  console.log(`# capture not an object: ${error}`);
  expect(error).toBe("alpha: the node rp.alpha.midi's capture is not an object of mode: capture");
});

test("the generated table spells a capture table like the fields around it", async () => {
  // A capture table is data the planner reads, and its emitted form is what a reader of the generated module sees: the
  // fields before and after it are spelled with a space after each colon, so the object is too.
  const files = (await drive("capture-form")).files as Record<string, string>;
  const line = files["protocol.ts"]!.split("\n").find((l) => l.includes('"rp.alpha.midi"'))!;
  console.log(`# emitted capture: ${line}`);
  expect(line).toBe(
    '  "rp.alpha.midi": { midiIn: "midi", writes: ["serialIn"], capture: { "midiMap": "drive", "off": "listen" }, ' +
      'out: "rp.alpha.serial" },',
  );
});

test("a node declaring midiIn and not writes is refused", async () => {
  const { error } = await drive("half-wired");
  console.log(`# half wired: ${error}`);
  expect(error).toBe(
    "alpha: the node rp.alpha.midi declares midiIn without writes: a wired node declares both (an unwired one " +
      "neither)",
  );
});

test("the generated entry refuses a node no manifest declares, naming it", async () => {
  const { error } = await drive("extra-node");
  console.log(`# bundled entry (extra node): ${error}`);
  expect(error).toBe(
    // The label is looked up before the set is compared, since the sort needs it: an undeclared node is named by the
    // refusal that catches it first, which is the more useful one for a pack author.
    "the script node rp.fixture.extra has no span label (a package's retroplug.protocol.nodes)",
  );
});

test("a span label two nodes share is refused, naming both", async () => {
  const { error } = await drive("duplicate");
  console.log(`# duplicate label: ${error}`);
  expect(error).toBe("two script nodes have the span label 16: rp.alpha.midi (alpha) and rp.fixture.shell (retroplug)");
});

test("a node type two packages declare is refused, naming both", async () => {
  const { error } = await drive("duplicate-type");
  console.log(`# duplicate type: ${error}`);
  expect(error).toBe("two packages declare the script node rp.alpha.midi: alpha and retroplug");
});

test("a role kind two packages declare is refused, naming both", async () => {
  const { error } = await drive("duplicate-role");
  console.log(`# duplicate role kind: ${error}`);
  expect(error).toBe("two packages declare the role kind alpha-midi: alpha and retroplug");
});

test("a controller app id two packages declare is refused, naming both", async () => {
  const { error } = await drive("duplicate-app");
  console.log(`# duplicate app id: ${error}`);
  expect(error).toBe("two packages declare the controllerApps item alpha-app: alpha and beta");
});

test("a controller app entry with a key no runtime reads is refused", async () => {
  const { error } = await drive("app-unknown-key");
  console.log(`# unknown app key: ${error}`);
  expect(error).toBe(
    'alpha: the controllerApps item alpha-app has "factorY", which nothing reads (its keys are id, module and the ' +
      "kind's data: syncOverride)",
  );
});

test("the generated module refuses a controller app with only one half", async () => {
  // A manifest's runtime that no app module's control-plane half contributes: the id would reach the script bundle and
  // run an app no menu offers.
  const runtimeOnly = await drive("app-unpaired-runtime");
  console.log(`# app with no control-plane half: ${runtimeOnly.error}`);
  expect(runtimeOnly.error).toBe("the controllerApps item alpha-app has no control-plane half");
  // A control-plane half no manifest declares a runtime for: the menu would offer an app nothing runs.
  const controlOnly = await drive("app-unpaired-control");
  console.log(`# app with no runtime half: ${controlOnly.error}`);
  expect(controlOnly.error).toBe("the controllerApps item orphan-app has no runtime half");
});

test("the generated entry refuses app runtimes that are not the manifests' declarations", async () => {
  const { error } = await drive("app-runtime-mismatch");
  console.log(`# bundled entry (app runtimes): ${error}`);
  expect(error).toBe(
    "the script bundle's controllerApps item item_controllerApps_alpha_alpha_app registers not-alpha-app, but its " +
      "manifest entry declares alpha-app",
  );
});

test("the generated module refuses two roles of one kind", async () => {
  // The registry holds roles in a Map keyed by kind, so the second would replace the first without a word: the kind is
  // looked up, never sequenced, which is why a duplicate - not the order - is what the generated module checks.
  const { error } = await drive("duplicate-role-kind");
  console.log(`# duplicate role kind (app lists): ${error}`);
  expect(error).toBe("the app's roles declare the role kind shared-role twice");
});

test("the generated module refuses two kit formats of one id", async () => {
  // The audio driver dispatches a kit by its format's id, so a second format of one id could never be reached.
  const { error } = await drive("duplicate-kit-format");
  console.log(`# duplicate kit id: ${error}`);
  expect(error).toBe("the app's kit formats declare the id shared-kit twice");
});

test("the generated module refuses two tracker integrations of one marker role", async () => {
  // resolveTracker takes the first integration whose marker a system has, and the markers are exclusive by design, so
  // a duplicate would leave the second unreachable.
  const { error } = await drive("duplicate-marker-role");
  console.log(`# duplicate marker role: ${error}`);
  expect(error).toBe("the app's tracker integrations declare the marker role shared-marker twice");
});

test("a gap in the span labels is refused, naming the missing label", async () => {
  const { error } = await drive("gap");
  console.log(`# gap: ${error}`);
  expect(error).toBe("the script nodes' span labels must run from 16 with no gap: 16, 18 (17 is missing)");
});

test("a span label below 16 is refused", async () => {
  const { error } = await drive("low-label");
  console.log(`# low label: ${error}`);
  expect(error).toBe("the script nodes' span labels must run from 16 with no gap: 15, 16 (15 is below 16)");
});

test("a role naming a type no package declares is refused", async () => {
  const { error } = await drive("role-undeclared");
  console.log(`# role undeclared: ${error}`);
  expect(error).toBe("retroplug: the role shell-role names rp.nope, which no package declares");
});

test("a role naming a type with no wiring is refused", async () => {
  const { error } = await drive("role-unwired");
  console.log(`# role unwired: ${error}`);
  expect(error).toBe(
    "retroplug: the role shell-role names rp.fixture.shell, which declares no midiIn and writes to wire it",
  );
});

test("a contribution its exports do not publish is refused, naming the file and the exports", async () => {
  const { error } = await drive("unexported-app");
  console.log(`# unexported app: ${error}`);
  expect(error).toBe(
    "alpha: its manifest names ts/app.ts, which its exports do not publish: the generated import needs the entry " +
      "whose target is ./ts/app.ts (its exports: ./consoles -> ./ts/consoles.ts, ./nodes -> ./ts/nodes.ts, " +
      "./appRuntime -> ./ts/appRuntime.ts, ./placer -> ./ts/placer.ts, ./ui -> ./ts/ui/index.ts, " +
      "./kind -> ./ts/widgetKind.ts, ./widgets -> ./ts/widgets.ts)",
  );
});

test("a manifest whose name is not its directory's is refused", async () => {
  const { error } = await drive("name-mismatch");
  console.log(`# name mismatch: ${error}`);
  expect((error as string).includes('names the package "@retroplug/alfa", not @retroplug/alpha')).toBe(true);
});

test("a contribution outside the package's ts/ tree is refused", async () => {
  const { error } = await drive("outside-ts");
  console.log(`# outside ts/: ${error}`);
  expect(error).toBe("alpha: app.ts is outside its ts/ tree, so its exports cannot publish it");
});

test("a contributing package the shell does not declare is refused", async () => {
  const { error } = await drive("shell-undeclared");
  console.log(`# shell undeclared: ${error}`);
  expect(error).toBe(
    "retroplug does not declare @retroplug/alpha in its dependencies, which its generated modules import by name: a " +
      "package that contributes to the app is the shell's dependency",
  );
});

test("a manifest naming a module that does not exist is refused, naming the file", async () => {
  const { error } = await drive("missing-module");
  console.log(`# missing module: ${error}`);
  expect((error as string).includes("alpha: retroplug.protocol's module names ts/nope.ts, which does not exist")).toBe(
    true,
  );
});

// ---- the consoles a package contributes, and the real carts ---------------------------------

test("a product's consoles and real carts register in product order, read from the generated lists", async () => {
  const report = await drive("consoles");
  expect(report.consoles).toEqual([
    { id: "alpha.nes", core: "alpha-core", nodeType: "rp.alpha.nes", platforms: ["nes"] },
    { id: "beta.gb", core: "beta-core", nodeType: "rp.beta.gb", platforms: ["gb"] },
  ]);
  expect(report.realCarts).toEqual([{ platform: "nes", cartNode: "rp.alpha.cart" }]);
});

test("a platform id two descriptors declare is refused, naming both", async () => {
  const { error } = await drive("duplicate-console-platform");
  console.log(`# duplicate console platform: ${error}`);
  expect(error).toBe("two console descriptors declare the platform nes: alpha.nes and beta.nes");
});

test("a console whose core has no registered system role is refused", async () => {
  // The core-config role is named after its core, so a descriptor naming a core nothing contributes could be classified
  // and never configured.
  const { error } = await drive("console-core-norole");
  console.log(`# console core with no system role: ${error}`);
  expect(error).toBe("the console alpha.nes names the core alpha-core, whose system role no package contributes");
});

test("a node type two descriptors declare is refused, naming both", async () => {
  const { error } = await drive("duplicate-console-type");
  console.log(`# duplicate console node type: ${error}`);
  expect(error).toBe("two console descriptors declare the node type rp.alpha.nes: alpha.nes and beta.gb");
});

test("a real cart naming a platform no descriptor registers is refused", async () => {
  const { error } = await drive("realcart-unregistered");
  console.log(`# real cart with no platform: ${error}`);
  expect(error).toBe("the real cart rp.alpha.cart names the platform n64, which no console descriptor registers");
});

test("two real carts naming one platform are refused, naming both", async () => {
  // A project drives one cart: with two registered for a platform, which package's cart a NES sent to would be
  // whichever the product listed last, and the store's lookup and buildGraph's placement could each answer
  // differently.
  const { error } = await drive("realcart-platform-twice");
  console.log(`# two carts on one platform: ${error}`);
  expect(error).toBe("two real carts name the platform nes: rp.alpha.cart and rp.beta.cart");
});

test("two real carts naming one cart node are refused, naming both", async () => {
  // The node owns a link: two carts naming it would claim the same node for two platforms' traffic.
  const { error } = await drive("realcart-node-twice");
  console.log(`# two carts on one node: ${error}`);
  expect(error).toBe("two real carts name the cart node rp.alpha.cart: nes and gba");
});

// ---- the embedded ROMs a package declares, and the manifest entry that bakes its bytes in --------------

test("an embedded ROM's declaration and its manifest entry reach the shell together", async () => {
  // The shell loads an embedded ROM by the package's declaration (its id, its label and the console it runs on), and
  // native boots the bytes the manifest bakes in: the generated module pairs them, so the load takes a real platform.
  const report = (await drive("embed-form")) as Record<string, any>;
  console.log(`# embeds: ${JSON.stringify(report.embeds)}`);
  expect(report.embeds).toEqual([{ id: "alpha-rom", label: "Alpha ROM", platform: "nes" }]);
});

test("a manifest embed entry no app module declares is refused", async () => {
  const { error } = await drive("embed-unpaired-manifest");
  console.log(`# embed with no declaration: ${error}`);
  expect(error).toBe(
    "the package alpha bakes in the embedded ROM alpha-rom, which no app module declares (an embeds entry with its " +
      "label and platform)",
  );
});

test("an embed declaration no manifest entry bakes in is refused", async () => {
  const { error } = await drive("embed-unpaired-declaration");
  console.log(`# declaration with no manifest entry: ${error}`);
  expect(error).toBe(
    "the package alpha declares the embedded ROM alpha-rom, which its manifest does not bake in (its retroplug.embed)",
  );
});

test("an embed declaration naming an unregistered platform is refused", async () => {
  const { error } = await drive("embed-unregistered-platform");
  console.log(`# embed on no console: ${error}`);
  expect(error).toBe("the embedded ROM alpha-rom names the platform n64, which no console descriptor registers");
});

test("an embed id two packages declare is refused, naming both", async () => {
  const { error } = await drive("embed-duplicate");
  console.log(`# embed declared twice: ${error}`);
  expect(error).toBe("two packages declare the embedded ROM shared-rom: alpha and beta");
});

test("an embed id one package declares twice is refused, naming the package and the id", async () => {
  // The menu would offer two rows for one ROM and the store would take the first: the id belongs to one declaration.
  const { error } = await drive("embed-declared-twice");
  console.log(`# embed declared twice in one package: ${error}`);
  expect(error).toBe("the package alpha declares the embedded ROM alpha-rom twice: an id is one ROM");
});

test("a manifest embed that is not a list is refused", async () => {
  const { error } = await drive("embed-manifest-not-list");
  console.log(`# embed manifest not a list: ${error}`);
  expect(error).toBe("alpha: retroplug.embed is not a list");
});

test("a manifest embed entry with no id is refused", async () => {
  const { error } = await drive("embed-manifest-no-id");
  console.log(`# embed manifest entry with no id: ${error}`);
  expect(error).toBe('alpha: every retroplug.embed entry needs an "id", the resource\'s name on the wire');
});

test("a manifest embed entry with no file is refused, naming the id", async () => {
  const { error } = await drive("embed-manifest-no-file");
  console.log(`# embed manifest entry with no file: ${error}`);
  expect(error).toBe('alpha: the embedded ROM alpha-rom needs a "file", the bytes to bake in');
});

test("a manifest embed listing one id twice is refused", async () => {
  const { error } = await drive("embed-manifest-id-twice");
  console.log(`# embed manifest id twice: ${error}`);
  expect(error).toBe("alpha: its retroplug.embed lists the id alpha-rom twice");
});

test("an embed declared by one package and baked in by another is refused, naming both", async () => {
  const { error } = await drive("embed-mismatched-package");
  console.log(`# embed halves in two packages: ${error}`);
  expect(error).toBe(
    "the embedded ROM beta-rom is declared by alpha and baked in by beta: the two halves belong to one package",
  );
});

test("an unchanged input leaves the generated files untouched", async () => {
  const report = await drive("twice");
  const [changedFirst, changedSecond] = report.changed as [boolean[], boolean[]];
  const [before, after] = report.mtimes as [number[], number[]];
  expect(changedFirst).toEqual([true, true, true, true, true, true, true, true, true, true]);
  expect(changedSecond).toEqual([false, false, false, false, false, false, false, false, false, false]);
  // The driver aged the files by a minute before the second run, so a rewrite would show: these are the same stamps.
  expect(after).toEqual(before);
  expect(after[0]).toBe(before[0]);
  expect(after[1]).toBe(before[1]);
  expect(after[2]).toBe(before[2]);
  expect(after[3]).toBe(before[3]);
  expect(after[4]).toBe(before[4]);
  expect(after[5]).toBe(before[5]);
  expect(after[6]).toBe(before[6]);
  expect(after[7]).toBe(before[7]);
  expect(after[8]).toBe(before[8]);
  expect(after[9]).toBe(before[9]);
});

test("the generated module refuses two control-plane halves of one id, naming both packages", async () => {
  // A project stores the id alone: one of the two apps would be unreachable without a word, and the menu would offer
  // whichever package came last.
  const { error } = await drive("duplicate-app-control");
  console.log(`# duplicate controller app (control plane): ${error}`);
  expect(error).toBe("two packages contribute the controllerApps item same-app: alpha and beta");
});

test("a nodes module the manifest names with no nodes to export is refused", async () => {
  // The other half of an app bridge's freedom: naming no module is how a package contributes no nodes, while a module
  // that would export an empty list is a leftover.
  const { error } = await drive("app-module-empty");
  console.log(`# empty nodes module: ${error}`);
  expect(error).toBe(
    "alpha: retroplug.protocol names the module \"ts/nodes.ts\" and declares no nodes, so the module would export " +
      "nothing: drop the module",
  );
});

test("a controller app id ONE package declares twice is refused, naming that package once", async () => {
  // The message says which package, not "alpha and alpha": one manifest listed the id twice, and a reader of the
  // refusal should not go looking for a second package.
  const { error } = await drive("duplicate-app-one-package");
  console.log(`# duplicate controller app (one package): ${error}`);
  expect(error).toBe("the package alpha contributes the controllerApps item same-app twice");
});

test("two contributions whose generated identifiers mangle to one name are refused, naming both", async () => {
  // "a-b" with app "c" and "a" with "b-c" both become app_a_b_c: a duplicate import tsc refuses with nothing said
  // about which manifests collided.
  const { error } = await drive("identifier-collision");
  console.log(`# generated identifier collision: ${error}`);
  expect(error).toBe(
    "two contributions take the generated identifier item_controllerApps_a_b_c: the package a-b's controllerApps " +
      "item c and the package a's controllerApps item b-c",
  );
});

test("a package contributing an app and no nodes names no module and still registers its app", async () => {
  // The shape an app bridge has: a protocol member with apps alone, and no nodes.ts to satisfy a check.
  const report = await drive("apps-only");
  const files = report.files as Record<string, string>;
  expect(report.packages).toEqual(["zeta", "alpha", "beta", "retroplug"]);
  expect(files["scriptNodes.ts"].includes("import item_controllerApps_alpha_alpha_app from")).toBe(true);
  expect(files["scriptNodes.ts"].includes("registerControllerApps(kindItems_controllerApps);")).toBe(true);
  expect(files["scriptNodes.ts"].includes("nodes_alpha")).toBe(false); // nothing of alpha's to register
  expect(files["scriptNodes.ts"].includes("registerScriptNodes(nodes);")).toBe(true);
});
