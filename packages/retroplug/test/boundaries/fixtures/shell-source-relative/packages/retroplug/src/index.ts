// A relative reach out of the shell's sources: into a package's own code, and into the app's ui/, which still imports
// carts and is no source's to import. Both are refused by rule 9.
import { ALPHA_APP } from "../../alpha/ts/app";
import { APP } from "../../ui/App";

export const app = [ALPHA_APP, APP];
