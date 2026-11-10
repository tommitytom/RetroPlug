// A UI file reaching the CLI by PATH: the UI is a surface over the stores src/ owns, not a tree the dispatcher's modules
// live in, so cli/ is outside what ui/ may reach.
import { run } from "../cli/run";

export const main = run;
