// A declared host module imported from the package's plain source: a cli host module is reached only from the files
// its surface owns (examples/ and ts/cli/), and this file is neither.
import { fixture } from "fixture-lib";

export const alpha = fixture;
