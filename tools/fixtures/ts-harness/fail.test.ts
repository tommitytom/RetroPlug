// Must make the whole run fail, so the aggregate exit code is a real gate.
import { tap, type Case } from "./helper.js";
const cases: Case[] = [{ name: "fixture: this one is meant to fail", ok: false }];
declare const tjs: { exit(c: number): void };
tjs.exit(tap(cases));
