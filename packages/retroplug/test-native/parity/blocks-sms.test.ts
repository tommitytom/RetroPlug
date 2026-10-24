// Block partitioning for sms; the cases are in blocks.ts.
import { runGroup } from "./group";
import { blockCases } from "./blocks";

runGroup("blocks-sms", blockCases("sms"));
