// retroplug-modc (docs/downloadable-packs-m2b-plan.md, Task 1): what tjsc writes for a module tjsc accepts, and what tjsc
// refuses (a module importing another module by a name the host registers at run time), and its two C forms. Run as
// child processes, as bundle-protocol.test.ts runs its Node toolchains; the binaries are `pnpm test`'s prebuilt targets.
import { test, expect } from "@retroplug/testing";

declare const tjs: {
  remove(path: string, options?: { recursive?: boolean }): Promise<void>;
  env: Record<string, string | undefined>;
  exePath: string;
  tmpDir: string;
  makeTempDir(template: string): Promise<string>;
  writeFile(path: string, data: Uint8Array | string): Promise<void>;
  readFile(path: string): Promise<Uint8Array>;
  spawn(args: string[], opts: { stdout: "pipe"; stderr: "pipe" }): {
    stdout: { getReader(): { read(): Promise<{ done: boolean; value?: Uint8Array }> } } | null;
    stderr: { getReader(): { read(): Promise<{ done: boolean; value?: Uint8Array }> } } | null;
    wait(): Promise<{ exit_status: number; term_signal: string | null }>;
  };
};

const BIN = "../../build/bin";
const exe = (name: string) => `${name.includes("/") ? "" : `${BIN}/`}${name}${tjs.exePath.endsWith(".exe") ? ".exe" : ""}`;
const TJSC = exe("../../build/dpfjs/deps/lv_binding_js/deps/txiki/tjsc");
/** A scratch directory under the system's temp directory: never under the shell package, which the boundary check walks. */
const scratch = () => tjs.makeTempDir(`${tjs.tmpDir}/rp-modc-XXXXXX`);

async function text(stream: { getReader(): { read(): Promise<{ done: boolean; value?: Uint8Array }> } } | null) {
  if (!stream) return "";
  const parts: Uint8Array[] = [];
  const reader = stream.getReader();
  for (;;) {
    const { done, value } = await reader.read();
    if (done) break;
    if (value) parts.push(value);
  }
  return new TextDecoder().decode(Uint8Array.from(parts.flatMap((p) => [...p])));
}

async function run(args: string[]): Promise<{ status: number; err: string }> {
  const child = tjs.spawn(args, { stdout: "pipe", stderr: "pipe" });
  const [, err, status] = await Promise.all([text(child.stdout), text(child.stderr), child.wait()]);
  return { status: status.exit_status, err };
}

const same = (a: Uint8Array, b: Uint8Array) => a.length === b.length && a.every((x, i) => x === b[i]);

test("a module tjsc accepts compiles to tjsc's bytes", async () => {
  const dir = await scratch();
  try {
    await tjs.writeFile(`${dir}/plain.js`, "export const answer = 6 * 7;\nexport function twice(x) { return x * 2; }\n");
    for (const strip of [[], ["-s"]]) {
      expect((await run([exe("retroplug-modc"), "compile", ...strip, "rp:plain", `${dir}/plain.js`, `${dir}/modc.bin`])).status)
        .toBe(0);
      // tjsc writes C; its `-o` with a .bin name is not a raw form, so the raw bytes are compared through the C array.
      expect((await run([TJSC, "-m", ...strip, "-n", "rp:plain", "-p", "rp_", "-o", `${dir}/tjsc.c`, `${dir}/plain.js`])).status)
        .toBe(0);
      expect((await run([exe("retroplug-modc"), "array", ...strip, "rp:plain", "rp_plain", `${dir}/plain.js`, `${dir}/modc.c`]))
        .status).toBe(0);
      const bytesOf = (c: string) => Uint8Array.from([...c.matchAll(/0x([0-9a-f]{2})/gi)].map((m) => parseInt(m[1], 16)));
      const tjscBytes = bytesOf(new TextDecoder().decode(await tjs.readFile(`${dir}/tjsc.c`)));
      const modcBytes = bytesOf(new TextDecoder().decode(await tjs.readFile(`${dir}/modc.c`)));
      expect(same(modcBytes, tjscBytes)).toBe(true);
      expect(same(await tjs.readFile(`${dir}/modc.bin`), tjscBytes)).toBe(true);
    }
  } finally {
    await tjs.remove(dir, { recursive: true });
  }
});

test("a module importing a registered name compiles, where tjsc refuses it", async () => {
  const dir = await scratch();
  try {
    await tjs.writeFile(`${dir}/host.js`, 'import { box } from "@retroplug/core/registry";\nbox.count++;\n');
    const refused = await run([TJSC, "-m", "-n", "rp:host", "-o", `${dir}/tjsc.c`, `${dir}/host.js`]);
    expect(refused.status === 0).toBe(false);
    expect((await run([exe("retroplug-modc"), "compile", "-s", "rp:host", `${dir}/host.js`, `${dir}/host.bin`])).status)
      .toBe(0);
  } finally {
    await tjs.remove(dir, { recursive: true });
  }
});

test("a table lists its modules in the manifest's order, and a bad name is refused", async () => {
  const dir = await scratch();
  try {
    await tjs.writeFile(`${dir}/a.js`, "export const a = 1;\n");
    await tjs.writeFile(`${dir}/b.js`, 'import { a } from "fx:a";\nexport const b = a + 1;\n');
    await tjs.writeFile(`${dir}/ok.tsv`, `fx:a\t${dir}/a.js\nfx:b\t${dir}/b.js\n`);
    expect((await run([exe("retroplug-modc"), "table", "-s", "rp_fx", `${dir}/ok.tsv`, `${dir}/table.c`])).status).toBe(0);
    const c = new TextDecoder().decode(await tjs.readFile(`${dir}/table.c`));
    expect(c.indexOf('{ "fx:a", rp_fx_0,') < c.indexOf('{ "fx:b", rp_fx_1,')).toBe(true);
    expect(c.includes("const uint32_t rp_fx_count = 2;")).toBe(true);
    for (const bad of ["./relative", "tjs:path", "has space"]) {
      await tjs.writeFile(`${dir}/bad.tsv`, `${bad}\t${dir}/a.js\n`);
      const r = await run([exe("retroplug-modc"), "table", "rp_bad", `${dir}/bad.tsv`, `${dir}/bad.c`]);
      expect(r.status === 0).toBe(false);
      expect(r.err.includes("is not a module name a host can register")).toBe(true);
    }
  } finally {
    await tjs.remove(dir, { recursive: true });
  }
});
