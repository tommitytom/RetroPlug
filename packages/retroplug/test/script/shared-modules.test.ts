// The shared module sets (docs/downloadable-packs-m2b-plan.md, Task 2): every contract is a module named by its own
// specifier, nothing imports by a relative path, every import names a module of the set (or a txiki builtin, or what
// the set leaves external), the load order is dependencies first, the build is deterministic, and each host reaches
// only what it did as its own bundle: the control plane no UI, the dispatcher no UI, the UI no CLI tool.
import { test, expect } from "@retroplug/testing";

declare const tjs: {
  env: Record<string, string | undefined>;
  spawn(args: string[], opts: { stdout: "pipe"; stderr: "pipe" }): {
    stdout: { getReader(): { read(): Promise<{ done: boolean; value?: Uint8Array }> } } | null;
    stderr: { getReader(): { read(): Promise<{ done: boolean; value?: Uint8Array }> } } | null;
    wait(): Promise<{ exit_status: number; term_signal: string | null }>;
  };
};

interface Described {
  shared: { name: string; imports: string[]; exports: string[] }[];
  hosts: { name: string; imports: string[]; exports: string[] }[];
  deterministic: boolean;
  closures: Record<string, string[]>;
  /** The outputs the set's first host (rp:cp, rp:cli) reaches that carry esbuild's define-semantics helper. */
  firstHostPublicField: string[];
}

async function text(stream: { getReader(): { read(): Promise<{ done: boolean; value?: Uint8Array }> } } | null) {
  if (!stream) return "";
  const parts: Uint8Array[] = [];
  const reader = stream.getReader();
  for (;;) {
    const { done, value } = await reader.read();
    if (done) break;
    if (value) parts.push(value);
  }
  let n = 0;
  for (const p of parts) n += p.length;
  const all = new Uint8Array(n);
  let at = 0;
  for (const p of parts) (all.set(p, at), (at += p.length));
  return new TextDecoder().decode(all);
}

async function describe(set: string, product?: string): Promise<Described> {
  const child = tjs.spawn([tjs.env.RP_NODE || "node", "test/script/shared-modules.mjs", set, ...(product ? [product] : [])],
    { stdout: "pipe", stderr: "pipe" });
  const [out, err, status] = await Promise.all([text(child.stdout), text(child.stderr), child.wait()]);
  if (status.exit_status !== 0) throw new Error(`the shared-module driver failed: ${err}`);
  return JSON.parse(out) as Described;
}

const ALLOWED_EXTERNAL = new Set(["tjs:path", "retroplug-cli"]);

function holdsTheShape(d: Described): void {
  const names = new Set([...d.shared.map((m) => m.name), ...d.hosts.map((m) => m.name)]);
  const position = new Map(d.shared.map((m, i) => [m.name, i]));
  for (const m of [...d.shared, ...d.hosts]) {
    for (const imp of m.imports) {
      expect(imp.startsWith("."), `${m.name} imports ${imp} by a relative path`).toBe(false);
      expect(names.has(imp) || ALLOWED_EXTERNAL.has(imp), `${m.name} imports ${imp}, which no module of the set is`)
        .toBe(true);
      if (position.has(m.name) && position.has(imp)) {
        expect(position.get(imp)! < position.get(m.name)!, `${m.name} is read before its dependency ${imp}`).toBe(true);
      }
    }
  }
  expect(d.deterministic).toBe(true);
}

test("the app set names every contract by its specifier and holds the shape", async () => {
  const d = await describe("app");
  holdsTheShape(d);
  const names = new Set(d.shared.map((m) => m.name));
  for (const contract of ["@retroplug/core/definePackage", "@retroplug/core/systemRoles", "@retroplug/ui/contribution",
    "@retroplug/ui-react", "react", "react/jsx-runtime", "lvgljs", "lvgljs-ui"]) {
    expect(names.has(contract), `the app set has no module named ${contract}`).toBe(true);
  }
  expect(d.hosts.map((h) => h.name)).toEqual(["rp:cp", "rp:ui"]);
  // The CommonJS host modules link by name through their facades.
  const exportsOf = (name: string) => d.shared.find((m) => m.name === name)?.exports ?? [];
  for (const n of ["useState", "useEffect", "createContext"]) expect(exportsOf("react").includes(n), `react exports no ${n}`).toBe(true);
  for (const n of ["jsx", "jsxs", "Fragment"]) expect(exportsOf("react/jsx-runtime").includes(n), `react/jsx-runtime exports no ${n}`).toBe(true);
});

test("the control plane reaches no UI, and the UI no CLI tool", async () => {
  const d = await describe("app");
  const cp = d.closures["rp:cp"];
  expect(cp.some((p) => p.includes("/packages/retroplug/ui/")), "the control plane reaches the shell's ui/").toBe(false);
  expect(cp.some((p) => /\/node_modules\/react\//.test(p)), "the control plane reaches React").toBe(false);
  expect(cp.some((p) => p.endsWith("src/contributions/registry.ts"))).toBe(true);
  // The control plane's classes compile to assignments, as its pinned options compiled them: the shell's tsconfig
  // (tsconfig.base.json, target ES2020) gives the same semantics, and only lv_binding_js's own classes use define.
  expect(d.firstHostPublicField).toEqual([]);
  const ui = d.closures["rp:ui"];
  expect(ui.some((p) => p.includes("/packages/retroplug/cli/")), "the UI reaches the CLI").toBe(false);
  expect(ui.some((p) => p.endsWith("ui/generated/views.ts"))).toBe(true);
});

test("the plugin's app set is built from its own generated tree", async () => {
  const d = await describe("app", "plugin");
  holdsTheShape(d);
  const cp = d.closures["rp:cp"];
  expect(cp.some((p) => p.includes("/products/plugin/packages/retroplug/src/generated/packages.ts"))).toBe(true);
  expect(cp.some((p) => p.includes("/packages/launchpad/")), "the plugin's control plane reaches the Launchpad").toBe(false);
});

test("the cli set holds the shape and reaches no UI", async () => {
  const d = await describe("cli");
  holdsTheShape(d);
  expect(d.hosts.map((h) => h.name)).toEqual(["rp:cli"]);
  expect(d.closures["rp:cli"].some((p) => p.includes("/packages/retroplug/ui/"))).toBe(false);
});
