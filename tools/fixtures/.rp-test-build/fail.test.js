// Must make the whole run fail, so the aggregate exit code is a real gate.
import { tap,           } from "./helper.js";
const cases         = [{ name: "fixture: this one is meant to fail", ok: false }];
                                             
tjs.exit(tap(cases));
