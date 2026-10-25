// 1-frame blocks and FL-style ragged splits for nes; the cases are in blocks-tiny.ts.
import { runGroup } from "./group";
import { tinyCases } from "./blocks-tiny";

runGroup("blocks-tiny-nes", tinyCases("nes"));
