// A package whose own tree does not hold together: the check reports the import esbuild cannot resolve rather than
// failing with a bundler error.
import { nope } from "./nope";

export const kappa = nope;
