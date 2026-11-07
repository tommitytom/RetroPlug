// The TS package generator (packages/retroplug/scripts/lib/packages.mjs, docs/modular-graph-phase-f1-design.md
// section 3): the product list and the packages' manifests become the three generated modules. Here:
//
// - a fixture product list and fixture manifests give the expected modules byte for byte: packages.ts, protocol.ts
//   and scriptNodes.ts, the shell last;
// - the generated entry, bundled and loaded, registers the manifest's nodes in span-label order, and refuses when what
//   the modules define is not the manifests' declarations;
// - each of the generator's refusals is exercised with its own message: a list that puts a package before a dependency
//   (a cycle is one), an unlisted dependency that would contribute, a dependency on the shell, a node key no protocol
//   node reads, a node wired on one side only, a shared span label, a shared node type, a shared role kind, a label
//   below 16, a gap, a role naming an undeclared or an unwired type, a named module that does not exist, a
//   contribution its exports do not publish, a manifest whose name is not its directory's, a contribution outside ts/
//   and a contributing package the shell does not declare. A dependency that contributes nothing, and one whose
//   manifest has no retroplug member, are left out of the ordering rather than refused;
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
    "the script bundle's nodes are not the manifests': 1 defined, 0 declared; rp.alpha.midi is declared by no manifest",
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
      "whose target is ./ts/app.ts (its exports: ./nodes -> ./ts/nodes.ts)",
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

test("an unchanged input leaves the generated files untouched", async () => {
  const report = await drive("twice");
  const [changedFirst, changedSecond] = report.changed as [boolean[], boolean[]];
  const [before, after] = report.mtimes as [number[], number[]];
  expect(changedFirst).toEqual([true, true, true]);
  expect(changedSecond).toEqual([false, false, false]);
  // The driver aged the files by a minute before the second run, so a rewrite would show: these are the same stamps.
  expect(after).toEqual(before);
  expect(after[0]).toBe(before[0]);
  expect(after[1]).toBe(before[1]);
  expect(after[2]).toBe(before[2]);
});
