// Block partitioning for mgb; the cases are in blocks.ts.
import { runGroup } from "./group";
import { blockCases } from "./blocks";

runGroup("blocks-mgb", blockCases("mgb"));
