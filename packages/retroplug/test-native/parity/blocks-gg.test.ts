// Block partitioning for gg; the cases are in blocks.ts.
import { runGroup } from "./group";
import { blockCases } from "./blocks";

runGroup("blocks-gg", blockCases("gg"));
