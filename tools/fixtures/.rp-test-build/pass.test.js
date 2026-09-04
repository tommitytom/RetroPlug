// Exercises the whole path: a `.ts` test that imports a stripped sibling helper, uses erasable syntax
// (annotations, generics, `satisfies`, an inline `type` import) and exits 0.
import { tap,           } from "./helper.js";

const pick =     (v   )    => v;
const n         = pick(2)                 ;
const cases         = [
  { name: "fixture: a stripped test runs", ok: true },
  { name: "fixture: a stripped sibling helper is importable", ok: n === 2 },
];
                                             
tjs.exit(tap(cases));
