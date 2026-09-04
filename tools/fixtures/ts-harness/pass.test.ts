// Exercises the whole path: a `.ts` test that imports a stripped sibling helper, uses erasable syntax
// (annotations, generics, `satisfies`, an inline `type` import) and exits 0.
import { tap, type Case } from "./helper.js";

const pick = <T,>(v: T): T => v;
const n: number = pick(2) satisfies number;
const cases: Case[] = [
  { name: "fixture: a stripped test runs", ok: true },
  { name: "fixture: a stripped sibling helper is importable", ok: n === 2 },
];
declare const tjs: { exit(c: number): void };
tjs.exit(tap(cases));
