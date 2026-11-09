// The shell's sources hold no cart or console LITERAL (docs/modular-graph-phase-f4a-design.md section 7.2): after F4a a
// console is known by its descriptor (src/consoles.ts reads the generated registries), a ROM is classified by its
// content through one, and a save path or a file-filter pattern takes its extensions from the same descriptor. A
// literal that spelled "gb" or ".nes" again would be a second copy of a console's knowledge in the shell - exactly what
// the package split removes - so the rule is a test rather than a review note.
//
// The scan walks TypeScript's syntax tree (the pinned `typescript-5` the boundary check loads), not a regular
// expression and not a bare token scan: a comment that says "gb" is trivia, a template's head and each of its spans are
// read, and a template's `${...}` or a regular expression cannot move the walk's place. It reads the three shapes the
// removed tables took: a table's property name (`{ gb: "sameboy" }`, an interface member, an enum member), a pattern
// that reads an extension (`/\.gbc?$/i`) and a string or template text that ends in one (`rom.gb`). The guarded ids,
// extensions, cores and node types are the registry's, not a list here: the generated CONSOLES list is read below, so a
// console a product adds later is guarded without an edit to this test. It catches an accident, not a determined
// author: a computed platform (["g", "b"].join("")) and a dotless pattern (/(gb|gbc)$/) are invisible to it, as the
// boundary check says of itself.
//
// The scanner is Node's, so the test spawns test/source-literals/run-scan.mjs, as the boundary test spawns its runner.
import { test, expect } from "@retroplug/testing";
import { CONSOLES } from "../../src/generated/consoles";

/** The console registry's platform ids, ROM extensions, cores and node types: the descriptors' own, so nothing here is a
 *  second copy. */
const PLATFORM_IDS = [
  ...new Set(CONSOLES.flatMap((console) => console.platforms.map((platform) => platform.id))),
].sort();
const ROM_EXTENSIONS = [
  ...new Set(CONSOLES.flatMap((console) => console.platforms.flatMap((platform) => platform.extensions))),
].sort();
const CORE_IDS = [...new Set(CONSOLES.map((console) => console.core))].sort();
const NODE_TYPES = [...new Set(CONSOLES.map((console) => console.nodeType))].sort();

/** The one deliberate copy of a console's knowledge in the shell, held to being used: the v2 to v3 step's enum tables
 *  are keyed by role kind, and two of those kinds are the consoles' own. The step may not read the registry - it freezes
 *  the v2 writer's lists so a package's rename cannot move what an old file means (design decision 13) - so its keys are
 *  the old file's spellings, not a lookup, and its two console keys stay. */
const FROZEN_MIGRATION_KEYS = [
  { file: "packages/retroplug/src/projectConfig.ts", value: "sameboy" },
  { file: "packages/retroplug/src/projectConfig.ts", value: "mesen" },
];

interface ByteStream {
  getReader(): { read(): Promise<{ done: boolean; value?: Uint8Array }> };
}
declare const tjs: {
  env: Record<string, string | undefined>;
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

/** The scan's findings over a tree: `--root` names a fixture that spells literals, no argument the real tree. */
async function scan(root?: string): Promise<{ file: string; value: string }[]> {
  const args = [
    tjs.env.RP_NODE || "node",
    "test/source-literals/run-scan.mjs",
    "--platforms",
    PLATFORM_IDS.join(","),
    "--extensions",
    ROM_EXTENSIONS.join(","),
    "--cores",
    CORE_IDS.join(","),
    "--node-types",
    NODE_TYPES.join(","),
    ...(root ? ["--root", root] : []),
  ];
  const child = tjs.spawn(args, { stdout: "pipe", stderr: "pipe" });
  const [out, err, status] = await Promise.all([text(child.stdout), text(child.stderr), child.wait()]);
  if (status.term_signal || status.exit_status !== 0) throw new Error(`the literal scan failed: ${err || out}`);
  return (JSON.parse(out) as { literals: { file: string; value: string }[] }).literals;
}

test("the shell's sources spell no platform and no ROM extension", async () => {
  console.log(
    `# the registry's ids: ${PLATFORM_IDS.join(", ")}; extensions: ${ROM_EXTENSIONS.join(", ")}; ` +
      `cores: ${CORE_IDS.join(", ")}; node types: ${NODE_TYPES.join(", ")}`,
  );
  const literals = await scan();
  console.log(`# platform/extension/core/node-type literals: ${JSON.stringify(literals)}`);
  const remaining = literals.filter(
    (hit) => !FROZEN_MIGRATION_KEYS.some((key) => key.file === hit.file && key.value === hit.value),
  );
  expect(remaining).toEqual([]);
  // An exemption that names nothing is stale, and a stale one would hide the next real hit in that file.
  const stale = FROZEN_MIGRATION_KEYS.filter(
    (key) => !literals.some((hit) => hit.file === key.file && hit.value === key.value),
  );
  expect(stale).toEqual([]);
});

test("the walk reads a table's keys, a pattern and a template tail, not a comment or a property access", async () => {
  // The fixture spells platform ids, ROM extensions, a core id and a node type as string literals, as a table's keys and
  // as a pattern, in every place a program holds one. It says "gb" and "sms" in a comment, reads `CORE_BY_PLATFORM.gb`
  // (a property access, which is a read of a table the walk already judged by its declaration), keeps an unrelated key
  // and two patterns with no escaped dot, and names every platform in src/generated: none of those is the shell's to
  // change or a hit.
  const literals = await scan("test/source-literals/fixtures/literals");
  expect(literals).toEqual([
    { file: "packages/retroplug/src/render.ts", value: "gb" },
    { file: "packages/retroplug/src/render.ts", value: ".nes" },
    { file: "packages/retroplug/src/render.ts", value: "*.gba" },
    { file: "packages/retroplug/src/render.ts", value: "gb" },
    { file: "packages/retroplug/src/render.ts", value: ".gbc" },
    { file: "packages/retroplug/src/render.ts", value: ".sms" },
    { file: "packages/retroplug/src/tables.ts", value: "gb" },
    { file: "packages/retroplug/src/tables.ts", value: "sameboy" },
    { file: "packages/retroplug/src/tables.ts", value: "nes" },
    { file: "packages/retroplug/src/tables.ts", value: "mesen" },
    { file: "packages/retroplug/src/tables.ts", value: "gb" },
    { file: "packages/retroplug/src/tables.ts", value: "/\\.(gb|gbc)$/i" },
    { file: "packages/retroplug/src/tables.ts", value: "rp.sameboy.gb" },
    { file: "packages/retroplug/src/tables.ts", value: "sameboy" },
    { file: "packages/retroplug/src/tables.ts", value: "rom.gb" },
  ]);
});
