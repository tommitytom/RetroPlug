// Block partitioning for nes; the cases are in blocks.ts.
import { runGroup } from "./group";
import { blockCases } from "./blocks";

runGroup("blocks-nes", blockCases("nes"));
