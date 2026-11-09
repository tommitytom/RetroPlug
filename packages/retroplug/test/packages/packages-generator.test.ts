// The TS package generator (packages/retroplug/scripts/lib/packages.mjs, docs/modular-graph-phase-f1-design.md
// section 3): the product list and the packages' manifests become the three generated modules. Here:
//
// - a fixture product list and fixture manifests give the expected modules byte for byte: packages.ts, protocol.ts
//   and scriptNodes.ts, the shell last;
// - the generated entry, bundled and loaded, registers the manifests' controller app runtimes and their nodes in
//   span-label order, and refuses when what the modules define or register is not the manifests' declarations;
// - each of the generator's refusals is exercised with its own message: a list that puts a package before a
//   dependency (a cycle is one), an unlisted dependency that would contribute, a dependency on the shell, a node key no
//   protocol node reads, a node wired on one side only, a shared span label, a shared node type, a shared role kind, a
//   controller app id two RUNTIMES declare and one two control-plane halves declare (each naming both packages), an app
//   entry with a key no runtime reads, a controller app with only one of its two halves (each way), a nodes module named
//   with no nodes, a sync override whose key, node type or unused off-mode is wrong, two contributions whose generated
//   identifiers mangle to one name, a label below 16, a gap, a role naming an undeclared or an unwired type, a named
//   module that does not exist, a contribution its exports do not publish, a manifest whose name is not its
//   directory's, a contribution outside ts/ and a contributing package the shell does not declare. A dependency that
//   contributes nothing, and one whose manifest has no retroplug member, are left out of the ordering rather than
//   refused; a package contributing an app and no nodes names no nodes module and still registers its app;
// - the generated module itself refuses a duplicate key in the lists the registries look up rather than sequence: a
//   role kind, a kit format's id and a tracker integration's marker role, each with its own message;
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
async function drive(mode: string): Promise<Record<string, any>> {
  const child = tjs.spawn([tjs.env.RP_NODE || "node", "test/packages/run-generator.mjs", mode], {
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
  // The list is the order (alpha, then beta, which depends on it), and the shell closes the app: no package may depend
  // on it.
  expect(report.packages).toEqual(["alpha", "beta", "retroplug"]);
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
  expect(report.packages).toEqual(["alpha", "beta", "retroplug"]);
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
      "midiIn, writes, launches, serialOut, midiOut, out)",
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
  expect(error).toBe("two packages declare the controller app alpha-app: alpha and beta");
});

test("a controller app entry with a key no runtime reads is refused", async () => {
  const { error } = await drive("app-unknown-key");
  console.log(`# unknown app key: ${error}`);
  expect(error).toBe(
    'alpha: the controller app alpha-app has "factorY", which no runtime reads (an app\'s keys are id, module, ' +
      "syncOverride)",
  );
});

test("the generated module refuses a controller app with only one half", async () => {
  // A manifest's runtime that no app module's control-plane half contributes: the id would reach the script bundle and
  // run an app no menu offers.
  const runtimeOnly = await drive("app-unpaired-runtime");
  console.log(`# app with no control-plane half: ${runtimeOnly.error}`);
  expect(runtimeOnly.error).toBe(
    "the controller app alpha-app has no control-plane half: a manifest declares its runtime and no package's app " +
      "contributes it (its label or its config's schema)",
  );
  // A control-plane half no manifest declares a runtime for: the menu would offer an app nothing runs.
  const controlOnly = await drive("app-unpaired-control");
  console.log(`# app with no runtime half: ${controlOnly.error}`);
  expect(controlOnly.error).toBe(
    "the controller app orphan-app has no runtime half: no package's protocol.apps declares it (the app, its playback " +
      "model and its target)",
  );
});

test("the generated entry refuses app runtimes that are not the manifests' declarations", async () => {
  const { error } = await drive("app-runtime-mismatch");
  console.log(`# bundled entry (app runtimes): ${error}`);
  expect(error).toBe(
    "the script bundle's controller app app_alpha_alpha_app registers not-alpha-app, but its manifest entry declares " +
      "alpha-app",
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
      "whose target is ./ts/app.ts (its exports: ./consoles -> ./ts/consoles.ts, ./nodes -> ./ts/nodes.ts, ./appRuntime -> ./ts/appRuntime.ts)",
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
  // A project drives one cart: with two registered for a platform, which package's cart a NES sent to would be whichever
  // the product listed last, and the store's lookup and buildGraph's placement could each answer differently.
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

test("an unchanged input leaves the generated files untouched", async () => {
  const report = await drive("twice");
  const [changedFirst, changedSecond] = report.changed as [boolean[], boolean[]];
  const [before, after] = report.mtimes as [number[], number[]];
  expect(changedFirst).toEqual([true, true, true, true]);
  expect(changedSecond).toEqual([false, false, false, false]);
  // The driver aged the files by a minute before the second run, so a rewrite would show: these are the same stamps.
  expect(after).toEqual(before);
  expect(after[0]).toBe(before[0]);
  expect(after[1]).toBe(before[1]);
  expect(after[2]).toBe(before[2]);
  expect(after[3]).toBe(before[3]);
  expect(after[4]).toBe(before[4]);
});

test("the generated module refuses two control-plane halves of one id, naming both packages", async () => {
  // A project stores the id alone: one of the two apps would be unreachable without a word, and the menu would offer
  // whichever package came last.
  const { error } = await drive("duplicate-app-control");
  console.log(`# duplicate controller app (control plane): ${error}`);
  expect(error).toBe("two packages declare the controller app same-app: alpha and beta");
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

test("a sync override is data the generator checks: its keys, its unused off-mode and its node type", async () => {
  const unknown = await drive("sync-unknown-key");
  console.log(`# sync override key: ${unknown.error}`);
  expect(unknown.error).toBe(
    'alpha: the controller app alpha-app\'s syncOverride has "listing", which nothing reads (its keys are node, ' +
      "mode, listening, off)",
  );
  const unused = await drive("sync-unused-off");
  console.log(`# sync override off without listening: ${unused.error}`);
  expect(unused.error).toBe(
    'alpha: the controller app alpha-app\'s syncOverride names an "off" mode and no "listening" value, so the mode ' +
      "it is for is never used",
  );
  // The mode is set on a node type, so a misspelt one would plan and do nothing (a C++ node ignores a setting its type
  // does not read).
  const undeclared = await drive("sync-node-undeclared");
  console.log(`# sync override node: ${undeclared.error}`);
  expect(undeclared.error).toBe(
    "alpha: the controller app alpha-app's syncOverride names the node rp.alpha.syncx, which no package declares",
  );
});

test("a controller app id ONE package declares twice is refused, naming that package once", async () => {
  // The message says which package, not "alpha and alpha": one manifest listed the id twice, and a reader of the
  // refusal should not go looking for a second package.
  const { error } = await drive("duplicate-app-one-package");
  console.log(`# duplicate controller app (one package): ${error}`);
  expect(error).toBe("the package alpha declares the controller app same-app twice");
});

test("a sync override's shape is checked: an object, its node, its mode, and an off that differs from the mode", async () => {
  // Each of these is data the planner would apply and the cart would never see the effect of, so the generator says
  // which field is wrong rather than emitting an override that does nothing.
  const notObject = await drive("sync-not-object");
  console.log(`# sync override shape: ${notObject.error}`);
  expect(notObject.error).toBe('alpha: the controller app alpha-app\'s syncOverride must be an object, not "MidiMap"');
  const noNode = await drive("sync-no-node");
  console.log(`# sync override node missing: ${noNode.error}`);
  expect(noNode.error).toBe(
    'alpha: the controller app alpha-app\'s syncOverride needs a "node": the cart sync node type its mode is set on',
  );
  const noMode = await drive("sync-no-mode");
  console.log(`# sync override mode missing: ${noMode.error}`);
  expect(noMode.error).toBe(
    'alpha: the controller app alpha-app\'s syncOverride needs a "mode": the mode that node is put in',
  );
  const same = await drive("sync-off-is-mode");
  console.log(`# sync override off is the mode: ${same.error}`);
  expect(same.error).toBe(
    'alpha: the controller app alpha-app\'s syncOverride names "midiMap" as both its mode and the mode a cart that ' +
      "is not listening is put in, so the override changes nothing",
  );
});

test("two contributions whose generated identifiers mangle to one name are refused, naming both", async () => {
  // "a-b" with app "c" and "a" with "b-c" both become app_a_b_c: a duplicate import tsc refuses with nothing said
  // about which manifests collided.
  const { error } = await drive("identifier-collision");
  console.log(`# generated identifier collision: ${error}`);
  expect(error).toBe(
    "two contributions take the generated identifier app_a_b_c: the package a-b's controller app c and the package " +
      "a's controller app b-c",
  );
});

test("a package contributing an app and no nodes names no module and still registers its app", async () => {
  // The shape an app bridge has: a protocol member with apps alone, and no nodes.ts to satisfy a check.
  const report = await drive("apps-only");
  const files = report.files as Record<string, string>;
  expect(report.packages).toEqual(["alpha", "beta", "retroplug"]);
  expect(files["scriptNodes.ts"].includes("import app_alpha_alpha_app from")).toBe(true);
  expect(files["scriptNodes.ts"].includes("registerControllerApps(apps);")).toBe(true);
  expect(files["scriptNodes.ts"].includes("nodes_alpha")).toBe(false); // nothing of alpha's to register
  expect(files["scriptNodes.ts"].includes("registerScriptNodes(nodes);")).toBe(true);
});
