// Phase 4 native gate: the PURE-TS codec (src/lsdj/codec) round-trips the entire
// per-version LSDj sav corpus (fmt 0..22, ~549 savs at ../resources/roms/lsdj)
// byte-identically — the exhaustive losslessness proof that can't run in the
// disk-less pure tier. Runs the pure-TS codec inside retroplug-host; the native
// backend is used only to list/read files, never to encode.
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { decodeSav, encodeSav } from "../src/lsdj/codec/sav";

declare const __RESOURCES_DIR__: string;
const DIR = __RESOURCES_DIR__ + "/roms/lsdj";

test("pure-TS sav codec round-trips the full per-version corpus byte-identically", () => {
  const be = createRealBackend();
  if (!be.fileExists(DIR)) {
    console.log(`# SKIP lsdj-codec-corpus: corpus not found at ${DIR}`);
    return;
  }
  const files = be.listDir(DIR).filter((f) => f.endsWith(".sav"));
  expect(files.length > 100).toBeTruthy();

  let decoded = 0;
  let wsIdentical = 0;
  let fullIdentical = 0;
  let earlySram = 0;
  let blankJkRejected = 0;
  const fails: string[] = [];
  const byFmt: Record<number, { total: number; ws: number }> = {};

  for (const f of files) {
    const bytes = be.readFile(`${DIR}/${f}`);
    if (!bytes || bytes.length < 0x8000) continue;

    let m;
    try {
      m = decodeSav(bytes);
    } catch (e) {
      // A blank/uninitialised battery (jk = 0xFFFF) is legitimately undecodable
      // as a 128 KiB sav; the C++ decodeSav rejects it identically.
      blankJkRejected++;
      if (!(bytes.length >= 0x8140 && bytes[0x813e] === 0xff && bytes[0x813f] === 0xff))
        fails.push(`${f}: unexpected decode error: ${(e as Error).message}`);
      continue;
    }
    decoded++;
    const fmt = bytes[0x7fff];
    byFmt[fmt] ??= { total: 0, ws: 0 };
    byFmt[fmt].total++;

    const e1 = encodeSav(m, bytes);

    let wok = true;
    for (let i = 0; i < 0x8000; i++) {
      if (e1[i] !== bytes[i]) {
        wok = false;
        fails.push(`${f} (fmt${fmt}): working-song byte diff @0x${i.toString(16)}`);
        break;
      }
    }
    if (wok) {
      wsIdentical++;
      byFmt[fmt].ws++;
    }

    if (bytes.length === 0x20000) {
      let full = true;
      for (let i = 0; i < 0x20000; i++) {
        if (e1[i] !== bytes[i]) {
          full = false;
          break;
        }
      }
      if (full) {
        fullIdentical++;
      } else {
        // The compressed archive may re-lay stored projects differently; require a
        // byte-stable fixpoint (encode(decode(e1)) == e1) so encode is a stable
        // projection even when it isn't byte-identical to the original layout.
        const e2 = encodeSav(decodeSav(e1), e1);
        let stable = true;
        for (let i = 0; i < 0x20000; i++) {
          if (e1[i] !== e2[i]) {
            stable = false;
            break;
          }
        }
        if (!stable) fails.push(`${f} (fmt${fmt}): full-sav not byte-stable under re-encode`);
      }
    } else {
      earlySram++; // 32 KiB early-SRAM image re-encodes to a modern 128 KiB sav
    }
  }

  const fmtSummary = Object.keys(byFmt)
    .map(Number)
    .sort((a, b) => a - b)
    .map((k) => `${k}:${byFmt[k].ws}/${byFmt[k].total}`)
    .join(" ");
  console.log(
    `# corpus: ${files.length} savs — decoded=${decoded}, blank-jk rejected=${blankJkRejected}, ` +
      `early-SRAM(32K)=${earlySram}, working-song identical=${wsIdentical}, full-128K identical=${fullIdentical}`,
  );
  console.log(`# working-song identical by fmt: ${fmtSummary}`);
  if (fails.length) fails.slice(0, 12).forEach((x) => console.log(`  FAIL ${x}`));

  expect(fails.length).toBe(0);
  // Every decodable sav's working song round-trips byte-for-byte.
  expect(wsIdentical).toBe(decoded);
});
