// The shell's sources hold no platform or ROM-extension LITERAL (docs/modular-graph-phase-f4a-design.md section 7.2):
// after F4a a console is known by its descriptor (src/consoles.ts reads the generated registries), a ROM is classified
// by its content through one, and a save path or a file-filter pattern takes its extensions from the same descriptor. A
// literal that spelled "gb" or ".nes" again would be a second copy of a console's knowledge in the shell - exactly what
// the package split removes - so the rule is a test rather than a review note.
//
// The scan walks TypeScript's syntax tree (the pinned `typescript-5` the boundary check loads), not a regular
// expression and not a bare token scan: a comment that says "gb" is trivia, a template's head and each of its spans are
// read, and a template's `${...}` or a regular expression cannot move the walk's place. The guarded ids and extensions
// are the registry's, not a list here: the generated CONSOLES list is read below, so a console a product adds later is
// guarded without an edit to this test. It catches an accident, not a determined author: a computed platform
// (["g", "b"].join("")) is invisible to it, as the boundary check says of itself.
//
// The scanner is Node's, so the test spawns test/source-literals/run-scan.mjs, as the boundary test spawns its runner.
import { test, expect } from "@retroplug/testing";
import { CONSOLES } from "../../src/generated/consoles";

/** The console registry's platform ids and ROM extensions: the descriptors' own, so nothing here is a second copy. */
const PLATFORM_IDS = [
  ...new Set(CONSOLES.flatMap((console) => console.platforms.map((platform) => platform.id))),
].sort();
const ROM_EXTENSIONS = [
  ...new Set(CONSOLES.flatMap((console) => console.platforms.flatMap((platform) => platform.extensions))),
].sort();

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
    ...(root ? ["--root", root] : []),
  ];
  const child = tjs.spawn(args, { stdout: "pipe", stderr: "pipe" });
  const [out, err, status] = await Promise.all([text(child.stdout), text(child.stderr), child.wait()]);
  if (status.term_signal || status.exit_status !== 0) throw new Error(`the literal scan failed: ${err || out}`);
  return (JSON.parse(out) as { literals: { file: string; value: string }[] }).literals;
}

test("the shell's sources spell no platform and no ROM extension", async () => {
  console.log(`# the registry's ids: ${PLATFORM_IDS.join(", ")}; its extensions: ${ROM_EXTENSIONS.join(", ")}`);
  const literals = await scan();
  console.log(`# platform/extension literals: ${JSON.stringify(literals)}`);
  expect(literals).toEqual([]);
});

test("the walk finds a literal after a template and a regex, in a span, not a comment", async () => {
  // The fixture spells platform ids and ROM extensions as string literals in every place a program holds one: a quoted
  // string, a template span, and literals after a substitution template and a quote-holding regular expression. It says
  // "gb" and "sms" in a comment, and names every platform in src/generated: neither the comment nor the generated
  // registry is the shell's to change.
  const literals = await scan("test/source-literals/fixtures/literals");
  expect(literals).toEqual([
    { file: "packages/retroplug/src/render.ts", value: "gb" },
    { file: "packages/retroplug/src/render.ts", value: ".nes" },
    { file: "packages/retroplug/src/render.ts", value: "*.gba" },
    { file: "packages/retroplug/src/render.ts", value: "gb" },
    { file: "packages/retroplug/src/render.ts", value: ".gbc" },
    { file: "packages/retroplug/src/render.ts", value: ".sms" },
  ]);
});
