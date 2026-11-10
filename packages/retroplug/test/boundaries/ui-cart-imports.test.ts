// The ui/ ratchet (docs/modular-graph-phase-f4b-design.md section 7.3): F4b left five view files allowed to import a
// cart's code, each still required to import one. F4c's moves took all five into their packages, so the list is now
// empty and the test requires an empty hit set; it stays (with its runner and fixtures) until the allow-based rule
// replaces it, rather than being deleted with nothing to show the boundary held.
//
// Under ui/, a cart, a console or a device is reached through the generated registries: ui/generated/menus.ts (not
// judged: it is the one place that names every package), src/generated/* through a store, or - for a menu entry - the
// package's own `ui` module, which the device menus moved into.
//
// The scanner is Node's (the boundary check's), so the test spawns test/boundaries/run-ui-cart-imports.mjs.
import { test, expect } from "@retroplug/testing";

/** The list F4b left, emptied by the moves that took the five files into their packages: the test is kept so the
 *  ratchet's runner and fixtures still hold an empty hit set until the allow-based rule replaces it. */
const ALLOWED: string[] = [];

// The packages whose code a view may not name are DERIVED by the runner from the workspace manifests: any package that
// declares code or baked bytes (an app, ui, cli, examples, protocol, consoles, embed or script member) is guarded, and a
// package that declares none of those is a contract a view may name. So a package added to the product cannot be left
// off a hand-kept list, and a device pack without an app module (the Launchpad) is not invisible to it. A specifier on
// rule 9's SHELL_IMPORTS list is a contract module and is allowed from either side.

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
    wait(): Promise<{ exit_status: number }>;
  };
};

async function read(stream: ByteStream | null): Promise<string> {
  if (!stream) return "";
  const reader = stream.getReader();
  let bytes = new Uint8Array(0);
  for (;;) {
    const { done, value } = await reader.read();
    if (done) break;
    if (!value) continue;
    const next = new Uint8Array(bytes.length + value.length);
    next.set(bytes);
    next.set(value, bytes.length);
    bytes = next;
  }
  return new TextDecoder().decode(bytes);
}

async function scan(root?: string): Promise<{ hits: { file: string; specifier: string }[] }> {
  // The runner resolves the repository from its own path, as the literal scan does; the test's working directory is the
  // shell package, so the script is named relative to it.
  const child = tjs.spawn(
    [tjs.env.RP_NODE ?? "node", "test/boundaries/run-ui-cart-imports.mjs", ...(root ? ["--root", root] : [])],
    { stdout: "pipe", stderr: "pipe" },
  );
  const [out, err, status] = await Promise.all([read(child.stdout), read(child.stderr), child.wait()]);
  if (status.exit_status !== 0) throw new Error(`run-ui-cart-imports failed (${status.exit_status}): ${err || out}`);
  return JSON.parse(out.trim().split("\n").at(-1)!) as { hits: { file: string; specifier: string }[] };
}

test("no ui/ file imports a cart, a console or a device package", async () => {
  const { hits } = await scan();
  const files = [...new Set(hits.map((hit) => hit.file))].sort();
  // A file NOT on the list importing one is the ratchet biting: it would reach a cart's code directly.
  expect(files.filter((file) => !ALLOWED.includes(file))).toEqual([]);
  // And a listed file that stopped importing one shrinks the list with it.
  expect(files).toEqual([...ALLOWED].sort());
});

test("a relative reach from ui/ into a cart's tree is the same reach, and is counted", async () => {
  // A view may reach a console's code by a path as well as by a specifier; the ratchet resolves a relative import to
  // where it lands, so a run around the rule by `../..` fails here.
  const { hits } = await scan("test/boundaries/fixtures/ui-relative");
  expect(hits.map((hit) => hit.file)).toEqual(["packages/retroplug/ui/evil.ts"]);
});

test("a device pack with no app module is guarded too: a view naming its client is a hit", async () => {
  // The Launchpad contributes a UI module and a CLI tool and no control-plane app module, so a derivation keyed on
  // `retroplug.app` alone would leave the very pack a view must not name unguarded.
  const { hits } = await scan("test/boundaries/fixtures/ui-device");
  expect(hits.map((hit) => hit.file)).toEqual(["packages/retroplug/ui/probe.ts"]);
});
